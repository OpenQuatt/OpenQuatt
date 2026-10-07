#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string>

#include "PsramBuffer.h"
#include "../../control/oq_supply_calibration_logic.h"
#include "../oq_service_runtime.h"

namespace oq_hp_water_calibration {

static constexpr int TASK_HP_WATER_CALIBRATION = oq_commissioning::TASK_HP_WATER_CALIBRATION;

static constexpr int STATE_FLOW_SETTLE = oq_commissioning::TASK_STATE_FLOW_SETTLE;
static constexpr int STATE_MEASURE = oq_commissioning::TASK_STATE_MEASURE;
static constexpr int STATE_DONE = oq_commissioning::TASK_STATE_DONE;
static constexpr int STATE_ABORT = oq_commissioning::TASK_STATE_ABORT;
static constexpr int STATE_FAILED = oq_commissioning::TASK_STATE_FAILED;

static constexpr int MAX_WINDOW_SAMPLES = 120;

enum PhaseCode {
  PHASE_IDLE = 0,
  PHASE_MIXING = 1,
  PHASE_MEASURING = 2,
  PHASE_DONE = 3,
};

struct RuntimeConfig {
  int sample_time_s;
  int min_mixing_s;
  int stable_window_s;
  int max_duration_s;
  int no_flow_fail_s;
  int target_ipwm;
  int pwm_min;
  int pwm_max;
  float min_flow_lph;
  float stable_spread_c;
  float stable_drift_c_per_min;
  float max_offset_c;
};

struct SampleSet {
  float hp1_in;
  float hp1_out;
  float hp2_in;
  float hp2_out;
  float supply;
};

struct WindowStats {
  int count;
  int span_s;
  float hp1_in_avg;
  float hp1_out_avg;
  float hp2_in_avg;
  float hp2_out_avg;
  float supply_avg;
  float reference;
  float spread;
  float drift;
};

inline RuntimeConfig make_runtime_config(int sample_time_s) {
  return RuntimeConfig{
      sample_time_s <= 0 ? 1 : sample_time_s, 180, 60, 300, 120, 650, 50, 850, 20.0f, 0.35f, 0.20f, 2.0f};
}

inline int clamp_ipwm(int value, int min_ipwm, int max_ipwm) {
  if (value < min_ipwm) return min_ipwm;
  if (value > max_ipwm) return max_ipwm;
  return value;
}

class HpWaterCalibrationRuntime {
 public:
  void reset() {
    phase_ = PHASE_IDLE;
    supply_source_code_ = oq_supply_calibration::SOURCE_NONE;
    supply_source_fingerprint_ = 0;
    last_status_.clear();
    release_samples();
    reset_result_globals();
    id(oq_hp_water_calibration_active) = false;
    id(oq_hp_water_calibration_abort) = false;
    id(oq_hp_water_calibration_phase) = PHASE_IDLE;
    id(oq_hp_water_calibration_target_ipwm) = 1000;
    id(oq_hp_water_calibration_started_ms) = 0;
    id(oq_hp_water_calibration_remaining_s) = 0;
    id(oq_hp_water_calibration_spread_c) = NAN;
    id(oq_hp_water_calibration_supply_delta_c) = NAN;
    id(oq_hp_water_calibration_stable_progress_s) = 0;
    id(oq_hp_water_calibration_stable_required_s) = 0;
  }

  void start(const RuntimeConfig& cfg, uint32_t now_ms) {
    const bool task_running =
        oq_commissioning::task_running(id(oq_commissioning_active), id(oq_commissioning_task_code));

    if (id(oq_hp_water_calibration_active) || task_running || id(oq_commissioning_request_pending)) {
      publish("REFUSED: BUSY");
      return;
    }
    if (!oq_commissioning::is_cm100(id(oq_control_mode_code))) {
      publish("REFUSED: CM100 required");
      return;
    }
    if (id(oq_water_temp_hard_trip_active)) {
      publish("REFUSED: water temperature hard trip");
      return;
    }
    if (id(boiler_active).state) {
      publish("REFUSED: boiler active");
      return;
    }
    if (!heat_pumps_idle()) {
      publish("REFUSED: heat pump active");
      return;
    }

    const auto supply_source = current_supply_source();
    float supply_c = NAN;
    if (!supply_source.ready || !read_supply_temperature(supply_source, supply_c)) {
      publish("REFUSED: supply source unavailable");
      return;
    }
    if (!allocate_samples()) {
      ESP_LOGE("quatt.cm100.hpcal", "Strict PSRAM allocation failed for calibration samples");
      publish("REFUSED: calibration memory unavailable");
      return;
    }

    ESP_LOGI("quatt.cm100.hpcal",
             "HP water sensor calibration requested (min_mix=%ds stable_window=%ds max=%ds flow_setpoint=normal "
             "spread<=%.2fC drift<=%.2fC/min max_offset=%.2fC)",
             cfg.min_mixing_s, cfg.stable_window_s, cfg.max_duration_s, cfg.stable_spread_c, cfg.stable_drift_c_per_min,
             cfg.max_offset_c);

    reset_result_globals();
    supply_source_code_ = supply_source.code;
    supply_source_fingerprint_ = supply_source.fingerprint;

    id(oq_commissioning_task_code) = TASK_HP_WATER_CALIBRATION;
    id(oq_commissioning_request_pending) = false;
    id(oq_commissioning_active) = true;
    id(oq_commissioning_abort_requested) = false;
    id(oq_commissioning_state_code) = STATE_FLOW_SETTLE;
    id(oq_commissioning_started_ms) = now_ms;
    id(oq_commissioning_state_since_ms) = now_ms;

    id(oq_hp_water_calibration_active) = true;
    id(oq_hp_water_calibration_abort) = false;
    id(oq_hp_water_calibration_phase) = PHASE_MIXING;
    id(oq_hp_water_calibration_target_ipwm) = clamp_ipwm(cfg.target_ipwm, cfg.pwm_min, cfg.pwm_max);
    id(oq_hp_water_calibration_started_ms) = now_ms;
    id(oq_hp_water_calibration_remaining_s) = cfg.max_duration_s;
    id(oq_hp_water_calibration_spread_c) = NAN;
    id(oq_hp_water_calibration_supply_delta_c) = NAN;
    id(oq_hp_water_calibration_stable_progress_s) = 0;
    id(oq_hp_water_calibration_stable_required_s) = cfg.stable_window_s;

    phase_ = PHASE_MIXING;
    oq_service_status::set_commissioning("HP WATER CALIBRATION STARTED");
    publish("MIXING");
  }

  void abort_or_clear() {
    if (id(oq_hp_water_calibration_active) ||
        oq_commissioning::task_selected(id(oq_commissioning_task_code), oq_commissioning::TASK_HP_WATER_CALIBRATION)) {
      id(oq_hp_water_calibration_abort) = true;
      id(oq_commissioning_abort_requested) = true;
      publish("ABORT REQUESTED");
      oq_service_status::set_commissioning("ABORT REQUESTED");
      return;
    }
    reset();
    publish("IDLE");
  }

  void apply(const RuntimeConfig& cfg) {
    const float hp1_in = id(oq_hp_calibration_hp1_water_in_offset_suggested).state;
    const float hp1_out = id(oq_hp_calibration_hp1_water_out_offset_suggested).state;
    const float hp2_in = id(oq_hp_calibration_hp2_water_in_offset_suggested).state;
    const float hp2_out = id(oq_hp_calibration_hp2_water_out_offset_suggested).state;
    const float supply_offset = id(oq_hp_calibration_supply_offset_suggested).state;

    if (!result_ready()) {
      publish("APPLY_FAILED: NO_RESULT");
      return;
    }
    if (!valid_offset(hp1_in, cfg.max_offset_c) || !valid_offset(hp1_out, cfg.max_offset_c)) {
      publish("APPLY_FAILED");
      return;
    }
    if (!valid_offset(supply_offset, cfg.max_offset_c)) {
      publish("APPLY_FAILED: SUPPLY_OFFSET");
      return;
    }
#if OQ_TOPOLOGY_DUO
    if (!valid_offset(hp2_in, cfg.max_offset_c) || !valid_offset(hp2_out, cfg.max_offset_c)) {
      publish("APPLY_FAILED");
      return;
    }
#endif

    if (!oq_commissioning::is_cm100(id(oq_control_mode_code)) || id(oq_water_temp_hard_trip_active) ||
        id(boiler_active).state || !heat_pumps_idle()) {
      publish("APPLY_FAILED: SAFETY_STATE");
      return;
    }
    const auto supply_source = current_supply_source();
    const int32_t result_source = id(oq_hp_water_calibration_result_supply_source_code);
    float supply_c = NAN;
    if (!supply_source.ready || result_source <= 0 || result_source != static_cast<int32_t>(supply_source.code) ||
        id(oq_hp_water_calibration_result_supply_source_fingerprint) != supply_source.fingerprint ||
        !read_supply_temperature(supply_source, supply_c)) {
      publish("APPLY_FAILED: SUPPLY_SOURCE_CHANGED");
      return;
    }

    if (!oq_supply_calibration::record_input_valid(supply_source, supply_offset, cfg.max_offset_c)) {
      publish("APPLY_FAILED: SUPPLY_RECORD");
      return;
    }

    set_number_value(id(hp1_water_in_temp_offset), hp1_in);
    set_number_value(id(hp1_water_out_temp_offset), hp1_out);

#if OQ_TOPOLOGY_DUO
    set_number_value(id(hp2_water_in_temp_offset), hp2_in);
    set_number_value(id(hp2_water_out_temp_offset), hp2_out);
#endif

    set_number_value(id(water_supply_temp_calibration_offset), supply_offset);

    bool supply_record_stored = false;
    switch (supply_source.code) {
      case oq_supply_calibration::SOURCE_LOCAL_PT1000:
        supply_record_stored = oq_supply_calibration::store_record(id(oq_water_supply_temp_calibration_pt1000_record),
                                                                   supply_source, supply_offset, cfg.max_offset_c);
        break;
      case oq_supply_calibration::SOURCE_LOCAL_DS18B20:
        supply_record_stored = oq_supply_calibration::store_record(id(oq_water_supply_temp_calibration_ds18b20_record),
                                                                   supply_source, supply_offset, cfg.max_offset_c);
        break;
      case oq_supply_calibration::SOURCE_CIC:
        supply_record_stored = oq_supply_calibration::store_record(id(oq_water_supply_temp_calibration_cic_record),
                                                                   supply_source, supply_offset, cfg.max_offset_c);
        break;
      case oq_supply_calibration::SOURCE_HA_INPUT:
        supply_record_stored = oq_supply_calibration::store_record(id(oq_water_supply_temp_calibration_ha_input_record),
                                                                   supply_source, supply_offset, cfg.max_offset_c);
        break;
      default:
        break;
    }
    if (!supply_record_stored) {
      publish("APPLY_FAILED: SUPPLY_RECORD");
      return;
    }
    switch (supply_source.code) {
      case oq_supply_calibration::SOURCE_LOCAL_PT1000:
        id(oq_water_supply_temp_calibration_pt1000_backup).update();
        break;
      case oq_supply_calibration::SOURCE_LOCAL_DS18B20:
        id(oq_water_supply_temp_calibration_ds18b20_backup).update();
        break;
      case oq_supply_calibration::SOURCE_CIC:
        id(oq_water_supply_temp_calibration_cic_backup).update();
        break;
      case oq_supply_calibration::SOURCE_HA_INPUT:
        id(oq_water_supply_temp_calibration_ha_input_backup).update();
        {
          char fingerprint[9];
          snprintf(fingerprint, sizeof(fingerprint), "%08lx", static_cast<unsigned long>(supply_source.fingerprint));
          id(oq_water_supply_temp_calibration_ha_input_identity_backup).publish_state(fingerprint);
        }
        break;
      default:
        break;
    }
    id(water_supply_temp_selected).update();

    publish("APPLIED");
  }

  void tick(const RuntimeConfig& cfg, uint32_t now_ms) {
    if (!id(oq_hp_water_calibration_active)) return;

    if (!oq_commissioning::hp_water_calibration_mode_valid(id(oq_control_mode_code), id(oq_commissioning_task_code))) {
      finish("ABORT: not CM100", STATE_ABORT);
      return;
    }
    if (id(oq_hp_water_calibration_abort) || id(oq_commissioning_abort_requested)) {
      finish("ABORTED", STATE_ABORT);
      return;
    }
    if (id(oq_water_temp_hard_trip_active)) {
      finish("FAILED: water temperature hard trip", STATE_FAILED);
      return;
    }
    if (id(boiler_active).state) {
      finish("FAILED: boiler active", STATE_FAILED);
      return;
    }
    if (!heat_pumps_idle()) {
      finish("FAILED: heat pump active", STATE_FAILED);
      return;
    }
    const auto supply_source = current_supply_source();
    if (!supply_source.ready || supply_source.code != supply_source_code_ ||
        supply_source.fingerprint != supply_source_fingerprint_) {
      finish("FAILED: supply source changed", STATE_FAILED);
      return;
    }

    const uint32_t started_ms = id(oq_hp_water_calibration_started_ms);
    if (started_ms == 0) {
      finish("FAILED: missing start time", STATE_FAILED);
      return;
    }

    const int elapsed_s = (int)((uint32_t)(now_ms - started_ms) / 1000UL);
    id(oq_hp_water_calibration_remaining_s) = (elapsed_s >= cfg.max_duration_s) ? 0 : (cfg.max_duration_s - elapsed_s);

    const float flow_lph = id(flow_rate_selected).state;
    const bool flow_seen = !isnan(flow_lph) && flow_lph >= cfg.min_flow_lph;
    if (elapsed_s >= cfg.no_flow_fail_s && !flow_seen) {
      finish("FAILED: no flow detected", STATE_FAILED);
      return;
    }

    if (flow_seen) {
      SampleSet sample;
      if (!read_raw_sample(sample)) {
        finish("FAILED: invalid temperature sample", STATE_FAILED);
        return;
      }
      if (!push_sample(sample)) {
        finish("FAILED: calibration memory unavailable", STATE_FAILED);
        return;
      }
    }

    if (elapsed_s < cfg.min_mixing_s) {
      update_window_diagnostics(cfg);
      set_phase(PHASE_MIXING, STATE_FLOW_SETTLE, now_ms);
      publish("MIXING");
      return;
    }

    set_phase(PHASE_MEASURING, STATE_MEASURE, now_ms);
    WindowStats stats;
    const bool have_stats = compute_window_stats(cfg, stats);
    const bool stable = have_stats && window_is_stable(cfg, stats);
    id(oq_hp_water_calibration_stable_progress_s) = stable ? stats.span_s : 0;
    if (stable) {
      compute_result(cfg, stats);
      return;
    }

    publish("MEASURING");

    if (elapsed_s >= cfg.max_duration_s) {
      char status[96];
      if (have_stats) {
        snprintf(status, sizeof(status), "FAILED: not stable spread %.2fC drift %.2fC", stats.spread, stats.drift);
        finish(status, STATE_FAILED);
      } else {
        finish("FAILED: not enough samples", STATE_FAILED);
      }
    }
  }

 private:
  int phase_ = PHASE_IDLE;
  oq_supply_calibration::SourceCode supply_source_code_ = oq_supply_calibration::SOURCE_NONE;
  uint32_t supply_source_fingerprint_ = 0;
  std::string last_status_;
  int sample_count_ = 0;
  int sample_next_ = 0;
  esphome::openquatt_common::PsramBuffer<SampleSet> samples_{};

  template <typename NumberEntity>
  void set_number_value(NumberEntity& number_entity, float value) {
    auto call = number_entity.make_call();
    call.set_value(value);
    call.perform();
  }

  bool allocate_samples() {
    if (!samples_.allocate_external(MAX_WINDOW_SAMPLES)) return false;
    clear_samples();
    return true;
  }

  void release_samples() {
    samples_.release();
    sample_count_ = 0;
    sample_next_ = 0;
  }

  void clear_samples() {
    sample_count_ = 0;
    sample_next_ = 0;
    if (!samples_) return;
    for (int i = 0; i < MAX_WINDOW_SAMPLES; i++) {
      samples_[i] = SampleSet{NAN, NAN, NAN, NAN, NAN};
    }
  }

  void reset_result_globals() {
    id(oq_hp_water_calibration_result_reference_c) = NAN;
    id(oq_hp_water_calibration_result_spread_before_c) = NAN;
    id(oq_hp_water_calibration_result_expected_spread_c) = NAN;
    id(oq_hp_water_calibration_result_hp1_in_raw_avg_c) = NAN;
    id(oq_hp_water_calibration_result_hp1_out_raw_avg_c) = NAN;
    id(oq_hp_water_calibration_result_hp2_in_raw_avg_c) = NAN;
    id(oq_hp_water_calibration_result_hp2_out_raw_avg_c) = NAN;
    id(oq_hp_water_calibration_result_supply_raw_avg_c) = NAN;
    id(oq_hp_water_calibration_result_supply_offset_c) = NAN;
    id(oq_hp_water_calibration_result_supply_source_code) = 0;
    id(oq_hp_water_calibration_result_supply_source_fingerprint) = 0;
    id(oq_hp_water_calibration_result_supply_source).clear();
  }

  bool valid_offset(float value, float max_offset_c) const { return !isnan(value) && fabsf(value) <= max_offset_c; }

  bool result_ready() const {
    return !id(oq_hp_water_calibration_active) && id(oq_hp_water_calibration_phase) == PHASE_DONE &&
           id(oq_commissioning_state_code) == STATE_DONE && !isnan(id(oq_hp_water_calibration_result_reference_c)) &&
           !isnan(id(oq_hp_water_calibration_result_spread_before_c)) &&
           !isnan(id(oq_hp_water_calibration_result_expected_spread_c)) &&
           !isnan(id(oq_hp_water_calibration_result_supply_raw_avg_c)) &&
           !isnan(id(oq_hp_water_calibration_result_supply_offset_c)) &&
           id(oq_hp_water_calibration_result_supply_source_code) > 0;
  }

  void set_phase(int phase, int state_code, uint32_t now_ms) {
    if (phase != phase_) {
      id(oq_commissioning_state_since_ms) = now_ms;
      phase_ = phase;
    }
    id(oq_hp_water_calibration_phase) = phase;
    id(oq_commissioning_state_code) = state_code;
  }

  bool read_raw_sample(SampleSet& sample) {
    sample.hp1_in = id(hp1_water_in_temp_raw).state;
    sample.hp1_out = id(hp1_water_out_temp_raw).state;
    sample.hp2_in = NAN;
    sample.hp2_out = NAN;
    if (isnan(sample.hp1_in) || isnan(sample.hp1_out)) return false;

#if OQ_TOPOLOGY_DUO
    sample.hp2_in = id(hp2_water_in_temp_raw).state;
    sample.hp2_out = id(hp2_water_out_temp_raw).state;
    if (isnan(sample.hp2_in) || isnan(sample.hp2_out)) return false;
#endif
    const auto supply_source = current_supply_source();
    if (!supply_source.ready || supply_source.code != supply_source_code_ ||
        supply_source.fingerprint != supply_source_fingerprint_) {
      return false;
    }
    return read_supply_temperature(supply_source, sample.supply);
  }

  oq_supply_calibration::SourceIdentity current_supply_source() const {
    return {static_cast<oq_supply_calibration::SourceCode>(id(oq_water_supply_temp_current_source_code)),
            id(oq_water_supply_temp_current_source_fingerprint), id(oq_water_supply_temp_current_source_ready)};
  }

  bool read_supply_temperature(const oq_supply_calibration::SourceIdentity& source, float& value) const {
    value = NAN;
    if (!source.ready || id(oq_water_supply_temp_fallback_runtime_active) ||
        id(oq_water_supply_temp_selected_hold_active)) {
      return false;
    }
    switch (source.code) {
      case oq_supply_calibration::SOURCE_LOCAL_PT1000:
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
        if (id(water_supply_temp_pt1000).has_state()) value = id(water_supply_temp_pt1000).state;
#endif
        break;
      case oq_supply_calibration::SOURCE_LOCAL_DS18B20:
        if (id(water_supply_temp_ds18b20).has_state()) value = id(water_supply_temp_ds18b20).state;
        break;
      case oq_supply_calibration::SOURCE_CIC:
        if (id(feed_ok).has_state() && id(feed_ok).state && id(cic_data_stale).has_state() &&
            !id(cic_data_stale).state && id(water_supply_temp_cic).has_state()) {
          value = id(water_supply_temp_cic).state;
        }
        break;
      case oq_supply_calibration::SOURCE_HA_INPUT:
        if (id(water_supply_temp_valid_ha).has_state() && id(water_supply_temp_valid_ha).state &&
            id(water_supply_temp_ha).has_state()) {
          value = id(water_supply_temp_ha).state;
        }
        break;
      default:
        break;
    }
    return isfinite(value);
  }

  bool push_sample(const SampleSet& sample) {
    if (!samples_ || samples_.size() != static_cast<size_t>(MAX_WINDOW_SAMPLES)) return false;
    samples_[sample_next_] = sample;
    sample_next_ = (sample_next_ + 1) % MAX_WINDOW_SAMPLES;
    if (sample_count_ < MAX_WINDOW_SAMPLES) sample_count_++;
    return true;
  }

  int required_window_samples(const RuntimeConfig& cfg) const {
    int samples = (cfg.stable_window_s + cfg.sample_time_s - 1) / cfg.sample_time_s;
    if (samples < 2) samples = 2;
    if (samples > MAX_WINDOW_SAMPLES) samples = MAX_WINDOW_SAMPLES;
    return samples;
  }

  int sample_index_from_oldest(int oldest_offset) const {
    const int oldest = (sample_next_ - sample_count_ + MAX_WINDOW_SAMPLES) % MAX_WINDOW_SAMPLES;
    return (oldest + oldest_offset) % MAX_WINDOW_SAMPLES;
  }

  bool compute_window_stats(const RuntimeConfig& cfg, WindowStats& stats) {
    if (!samples_ || samples_.size() != static_cast<size_t>(MAX_WINDOW_SAMPLES)) return false;
    const int required = required_window_samples(cfg);
    id(oq_hp_water_calibration_stable_required_s) = cfg.stable_window_s;
    id(oq_hp_water_calibration_stable_progress_s) =
        sample_count_ >= required ? cfg.stable_window_s : sample_count_ * cfg.sample_time_s;

    if (sample_count_ < required) return false;

    const int start_offset = sample_count_ - required;
    SampleSet first = samples_[sample_index_from_oldest(start_offset)];
    SampleSet last = samples_[sample_index_from_oldest(sample_count_ - 1)];
    float hp1_in_sum = 0.0f;
    float hp1_out_sum = 0.0f;
    float hp2_in_sum = 0.0f;
    float hp2_out_sum = 0.0f;
    float supply_sum = 0.0f;

    for (int offset = start_offset; offset < sample_count_; offset++) {
      const SampleSet sample = samples_[sample_index_from_oldest(offset)];
      hp1_in_sum += sample.hp1_in;
      hp1_out_sum += sample.hp1_out;
      supply_sum += sample.supply;
#if OQ_TOPOLOGY_DUO
      hp2_in_sum += sample.hp2_in;
      hp2_out_sum += sample.hp2_out;
#endif
    }

    stats.count = required;
    stats.span_s = required * cfg.sample_time_s;
    stats.hp1_in_avg = hp1_in_sum / required;
    stats.hp1_out_avg = hp1_out_sum / required;
    stats.supply_avg = supply_sum / required;
    stats.reference = (stats.hp1_in_avg + stats.hp1_out_avg) * 0.5f;
    float min_avg = fminf(stats.hp1_in_avg, stats.hp1_out_avg);
    float max_avg = fmaxf(stats.hp1_in_avg, stats.hp1_out_avg);
    stats.drift = fmaxf(fabsf(last.hp1_in - first.hp1_in), fabsf(last.hp1_out - first.hp1_out));
    stats.drift = fmaxf(stats.drift, fabsf(last.supply - first.supply));

    stats.hp2_in_avg = NAN;
    stats.hp2_out_avg = NAN;
#if OQ_TOPOLOGY_DUO
    stats.hp2_in_avg = hp2_in_sum / required;
    stats.hp2_out_avg = hp2_out_sum / required;
    stats.reference = (stats.hp1_in_avg + stats.hp1_out_avg + stats.hp2_in_avg + stats.hp2_out_avg) * 0.25f;
    min_avg = fminf(min_avg, fminf(stats.hp2_in_avg, stats.hp2_out_avg));
    max_avg = fmaxf(max_avg, fmaxf(stats.hp2_in_avg, stats.hp2_out_avg));
    stats.drift = fmaxf(stats.drift, fmaxf(fabsf(last.hp2_in - first.hp2_in), fabsf(last.hp2_out - first.hp2_out)));
#endif

    stats.spread = max_avg - min_avg;
    id(oq_hp_water_calibration_spread_c) = stats.spread;
    id(oq_hp_water_calibration_supply_delta_c) = stats.reference - stats.supply_avg;
    return true;
  }

  void update_window_diagnostics(const RuntimeConfig& cfg) {
    WindowStats stats;
    compute_window_stats(cfg, stats);
  }

  bool window_is_stable(const RuntimeConfig& cfg, const WindowStats& stats) const {
    const float allowed_drift = cfg.stable_drift_c_per_min * ((float)stats.span_s / 60.0f);
    return stats.spread <= cfg.stable_spread_c && stats.drift <= allowed_drift;
  }

  void compute_result(const RuntimeConfig& cfg, const WindowStats& stats) {
    const float hp1_in_offset = oq_supply_calibration::rounded_offset(stats.reference - stats.hp1_in_avg);
    const float hp1_out_offset = oq_supply_calibration::rounded_offset(stats.reference - stats.hp1_out_avg);
    const float supply_offset = oq_supply_calibration::rounded_offset(stats.reference - stats.supply_avg);
    if (fabsf(hp1_in_offset) > cfg.max_offset_c || fabsf(hp1_out_offset) > cfg.max_offset_c) {
      finish("FAILED: offset too large", STATE_FAILED);
      return;
    }
    if (fabsf(supply_offset) > cfg.max_offset_c) {
      finish("FAILED: supply offset too large", STATE_FAILED);
      return;
    }

    set_number_value(id(oq_hp_calibration_hp1_water_in_offset_suggested), hp1_in_offset);
    set_number_value(id(oq_hp_calibration_hp1_water_out_offset_suggested), hp1_out_offset);
    set_number_value(id(oq_hp_calibration_supply_offset_suggested), supply_offset);

#if OQ_TOPOLOGY_DUO
    const float hp2_in_offset = oq_supply_calibration::rounded_offset(stats.reference - stats.hp2_in_avg);
    const float hp2_out_offset = oq_supply_calibration::rounded_offset(stats.reference - stats.hp2_out_avg);
    if (fabsf(hp2_in_offset) > cfg.max_offset_c || fabsf(hp2_out_offset) > cfg.max_offset_c) {
      finish("FAILED: offset too large", STATE_FAILED);
      return;
    }
    set_number_value(id(oq_hp_calibration_hp2_water_in_offset_suggested), hp2_in_offset);
    set_number_value(id(oq_hp_calibration_hp2_water_out_offset_suggested), hp2_out_offset);
#endif

    id(oq_hp_water_calibration_result_reference_c) = stats.reference;
    id(oq_hp_water_calibration_result_spread_before_c) = stats.spread;
    id(oq_hp_water_calibration_result_expected_spread_c) = 0.0f;
    id(oq_hp_water_calibration_result_hp1_in_raw_avg_c) = stats.hp1_in_avg;
    id(oq_hp_water_calibration_result_hp1_out_raw_avg_c) = stats.hp1_out_avg;
    id(oq_hp_water_calibration_result_hp2_in_raw_avg_c) = stats.hp2_in_avg;
    id(oq_hp_water_calibration_result_hp2_out_raw_avg_c) = stats.hp2_out_avg;
    id(oq_hp_water_calibration_result_supply_raw_avg_c) = stats.supply_avg;
    id(oq_hp_water_calibration_result_supply_offset_c) = supply_offset;
    id(oq_hp_water_calibration_result_supply_source_code) = static_cast<int32_t>(supply_source_code_);
    id(oq_hp_water_calibration_result_supply_source_fingerprint) = supply_source_fingerprint_;
    id(oq_hp_water_calibration_result_supply_source) = oq_supply_calibration::source_label(supply_source_code_);

    char status[96];
    snprintf(status, sizeof(status), "DONE: stable spread %.2fC", stats.spread);
    finish(status, STATE_DONE);
  }

  bool hp_active(bool hp1) {
    int level = 0;
    int last_applied = 0;
    float working_mode = NAN;
    float freq = NAN;
    bool defrost = false;
    bool fault = false;

    if (hp1) {
      if (id(hp1_compressor_level).has_state()) {
        auto idx = id(hp1_compressor_level).active_index();
        level = idx.has_value() ? (int)idx.value() : 0;
      }
      last_applied = id(hp1_last_applied_level);
      working_mode = id(hp1_working_mode).state;
      freq = id(hp1_compressor_frequency).state;
      defrost = id(hp1_defrost).state;
      fault = id(hp1_ot_fault_active).state;
    } else {
#if OQ_TOPOLOGY_DUO
      if (id(hp2_compressor_level).has_state()) {
        auto idx = id(hp2_compressor_level).active_index();
        level = idx.has_value() ? (int)idx.value() : 0;
      }
      last_applied = id(hp2_last_applied_level);
      working_mode = id(hp2_working_mode).state;
      freq = id(hp2_compressor_frequency).state;
      defrost = id(hp2_defrost).state;
      fault = id(hp2_ot_fault_active).state;
#endif
    }

    const int mode = isnan(working_mode) ? 0 : (int)roundf(working_mode);
    const bool thermal_mode = (mode == 1 || mode == 2);
    const bool compressor_running = !isnan(freq) && freq > 0.5f;
    return level > 0 || last_applied > 0 || thermal_mode || compressor_running || defrost || fault;
  }

  bool heat_pumps_idle() {
    if (hp_active(true)) return false;
#if OQ_TOPOLOGY_DUO
    if (hp_active(false)) return false;
#endif
    return true;
  }

  void finish(const char* status, int next_state) {
    id(oq_hp_water_calibration_active) = false;
    id(oq_hp_water_calibration_abort) = false;
    id(oq_hp_water_calibration_phase) = next_state == STATE_DONE ? PHASE_DONE : PHASE_IDLE;
    id(oq_hp_water_calibration_target_ipwm) = 1000;
    id(oq_hp_water_calibration_remaining_s) = 0;

    const bool still_cm100 = oq_commissioning::is_cm100(id(oq_control_mode_code));
    oq_commissioning::clear_container(still_cm100, next_state);

    publish(status);
    phase_ = PHASE_IDLE;
    release_samples();
  }

  void publish(const char* status) {
    const std::string s(status ? status : "");
    if (s != last_status_) {
      oq_service_status::set_hp_water_calibration(s);
      ESP_LOGI("quatt.cm100.hpcal", "status=%s phase=%d remaining=%ds flow=%.0fL/h spread=%.2fC stable=%ds/%ds",
               s.c_str(), id(oq_hp_water_calibration_phase), id(oq_hp_water_calibration_remaining_s),
               id(flow_rate_selected).state, id(oq_hp_water_calibration_spread_c),
               id(oq_hp_water_calibration_stable_progress_s), id(oq_hp_water_calibration_stable_required_s));
      last_status_ = s;
    }
  }
};

inline HpWaterCalibrationRuntime& runtime() {
  static HpWaterCalibrationRuntime instance;
  return instance;
}

}  // namespace oq_hp_water_calibration
