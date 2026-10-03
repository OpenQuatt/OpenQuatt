#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string>

#include "../oq_service_runtime.h"

namespace oq_air_purge {

static constexpr int TASK_AIR_PURGE = oq_commissioning::TASK_AIR_PURGE;

static constexpr int STATE_IDLE = oq_commissioning::TASK_STATE_IDLE;
static constexpr int STATE_DONE = oq_commissioning::TASK_STATE_DONE;
static constexpr int STATE_ABORT = oq_commissioning::TASK_STATE_ABORT;
static constexpr int STATE_FAILED = oq_commissioning::TASK_STATE_FAILED;

enum PhaseCode {
  PHASE_IDLE = 0,
  PHASE_STEADY = 1,
  PHASE_PULSE = 2,
  PHASE_STABILIZE = 3,
};

struct RuntimeConfig {
  int sample_time_s;
  int duration_s;
  int steady_s;
  int pulse_s;
  int no_flow_fail_s;
  int steady_ipwm;
  int hard_ipwm;
  int rest_ipwm;
  int stabilize_ipwm;
  int pulse_hard_s;
  int pulse_rest_s;
  int pwm_min;
  int pwm_max;
};

inline RuntimeConfig make_runtime_config(int sample_time_s) {
  return RuntimeConfig{sample_time_s,
                       300,  // total duration
                       60,   // quiet start
                       180,  // pulse window
                       120,  // maximum continuous time without flow
                       800,  // quiet start iPWM
                       300,  // hard pulse iPWM
                       800,  // rest iPWM between pulses
                       650,  // stabilization iPWM
                       25,
                       15,
                       50,
                       850};
}

inline int clamp_ipwm(int value, int min_ipwm, int max_ipwm) {
  if (value < min_ipwm) return min_ipwm;
  if (value > max_ipwm) return max_ipwm;
  return value;
}

class AirPurgeRuntime {
 public:
  void reset() {
    state_ = STATE_IDLE;
    last_flow_ms_ = 0;
    last_status_.clear();
    id(oq_air_purge_active) = false;
    id(oq_air_purge_abort) = false;
    id(oq_air_purge_state) = STATE_IDLE;
    id(oq_air_purge_phase) = PHASE_IDLE;
    id(oq_air_purge_target_ipwm) = 1000;
    id(oq_air_purge_started_ms) = 0;
    id(oq_air_purge_remaining_s) = 0;
  }

  void start(const RuntimeConfig& cfg, uint32_t now_ms) {
    const int cm_code = id(oq_control_mode_code);
    const bool cm100_ready = oq_commissioning::is_cm100(cm_code);
    const bool task_running =
        oq_commissioning::task_running(id(oq_commissioning_active), id(oq_commissioning_task_code));

    if (id(oq_air_purge_active) || task_running || id(oq_commissioning_request_pending)) {
      publish("REFUSED: BUSY");
      return;
    }
    if (!cm100_ready) {
      publish("REFUSED: not CM100");
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

    ESP_LOGI("quatt.cm100.purge",
             "Air purge requested (duration=%ds steady=%d pulse=%d hard_iPWM=%d rest_iPWM=%d stabilize_iPWM=%d "
             "return_auto=%d)",
             cfg.duration_s, cfg.steady_ipwm, cfg.pulse_s, cfg.hard_ipwm, cfg.rest_ipwm, cfg.stabilize_ipwm,
             (int)id(oq_air_purge_return_to_auto).state);

    state_ = PHASE_STEADY;
    last_flow_ms_ = now_ms;
    id(oq_commissioning_task_code) = TASK_AIR_PURGE;
    id(oq_commissioning_request_pending) = false;
    id(oq_commissioning_active) = true;
    id(oq_commissioning_abort_requested) = false;
    id(oq_commissioning_state_code) = oq_commissioning::TASK_STATE_REQUESTED;
    id(oq_commissioning_started_ms) = now_ms;
    id(oq_commissioning_state_since_ms) = now_ms;

    id(oq_air_purge_active) = true;
    id(oq_air_purge_abort) = false;
    id(oq_air_purge_state) = PHASE_STEADY;
    id(oq_air_purge_phase) = PHASE_STEADY;
    id(oq_air_purge_target_ipwm) = clamp_ipwm(cfg.steady_ipwm, cfg.pwm_min, cfg.pwm_max);
    id(oq_air_purge_started_ms) = now_ms;
    id(oq_air_purge_remaining_s) = cfg.duration_s;

    oq_service_status::set_commissioning("AIR PURGE STARTED");
    publish("PHASE1_STEADY");
  }

  void abort_or_clear() {
    if (id(oq_air_purge_active) ||
        oq_commissioning::task_selected(id(oq_commissioning_task_code), oq_commissioning::TASK_AIR_PURGE)) {
      id(oq_air_purge_abort) = true;
      id(oq_commissioning_abort_requested) = true;
      publish("ABORT REQUESTED");
      oq_service_status::set_commissioning("ABORT REQUESTED");
      return;
    }
    reset();
    publish("IDLE");
  }

  void tick(const RuntimeConfig& cfg, uint32_t now_ms) {
    if (!id(oq_air_purge_active)) return;

    if (!oq_commissioning::air_purge_mode_valid(id(oq_control_mode_code), id(oq_commissioning_task_code))) {
      finish("ABORT: not CM100", STATE_ABORT, false);
      return;
    }
    if (id(oq_air_purge_abort) || id(oq_commissioning_abort_requested)) {
      finish("ABORTED", STATE_ABORT, false);
      return;
    }
    if (id(oq_water_temp_hard_trip_active)) {
      finish("FAILED: water temperature hard trip", STATE_FAILED, false);
      return;
    }
    if (id(boiler_active).state) {
      finish("FAILED: boiler active", STATE_FAILED, false);
      return;
    }

    const uint32_t started_ms = id(oq_air_purge_started_ms);
    if (started_ms == 0) {
      finish("FAILED: missing start time", STATE_FAILED, false);
      return;
    }

    const int elapsed_s = (int)((uint32_t)(now_ms - started_ms) / 1000UL);
    const int remaining_s = (elapsed_s >= cfg.duration_s) ? 0 : (cfg.duration_s - elapsed_s);
    id(oq_air_purge_remaining_s) = remaining_s;

    // Quiet/rest phases can legitimately have no flow. Keep the guard active,
    // but measure a continuous no-flow interval instead of testing one sample
    // after the first two minutes. A later loss of flow must still fail closed.
    const float flow_lph = id(flow_rate_selected).state;
    const bool flow_seen = isfinite(flow_lph) && flow_lph >= 20.0f;
    if (flow_seen) last_flow_ms_ = now_ms;
    if ((uint32_t)(now_ms - last_flow_ms_) >= (uint32_t)cfg.no_flow_fail_s * 1000UL) {
      finish("FAILED: no flow detected", STATE_FAILED, false);
      return;
    }

    // Check the no-flow guard before declaring completion, also after a delayed tick.
    if (elapsed_s >= cfg.duration_s) {
      finish("DONE", STATE_DONE, id(oq_air_purge_return_to_auto).state);
      return;
    }

    int phase = PHASE_STEADY;
    int target_ipwm = cfg.steady_ipwm;
    const int pulse_start_s = cfg.steady_s;
    const int stabilize_start_s = cfg.steady_s + cfg.pulse_s;

    if (elapsed_s < pulse_start_s) {
      phase = PHASE_STEADY;
      target_ipwm = cfg.steady_ipwm;
    } else if (elapsed_s < stabilize_start_s) {
      phase = PHASE_PULSE;
      const int pulse_elapsed_s = elapsed_s - pulse_start_s;
      const int cycle_s = cfg.pulse_hard_s + cfg.pulse_rest_s;
      const int cycle_pos_s = cycle_s > 0 ? (pulse_elapsed_s % cycle_s) : 0;
      target_ipwm = (cycle_pos_s < cfg.pulse_hard_s) ? cfg.hard_ipwm : cfg.rest_ipwm;
    } else {
      phase = PHASE_STABILIZE;
      target_ipwm = cfg.stabilize_ipwm;
    }

    target_ipwm = clamp_ipwm(target_ipwm, cfg.pwm_min, cfg.pwm_max);
    id(oq_air_purge_phase) = phase;
    id(oq_air_purge_state) = phase;
    id(oq_air_purge_target_ipwm) = target_ipwm;

    if (phase != state_) {
      id(oq_commissioning_state_since_ms) = now_ms;
      state_ = phase;
    }

    switch (phase) {
      case PHASE_STEADY:
        publish("PHASE1_STEADY");
        break;
      case PHASE_PULSE:
        publish(target_ipwm == cfg.hard_ipwm ? "PHASE2_PULSE_HARD" : "PHASE2_PULSE_REST");
        break;
      case PHASE_STABILIZE:
        publish("PHASE3_STABILIZE");
        break;
      default:
        publish("RUNNING");
        break;
    }
  }

 private:
  int state_ = STATE_IDLE;
  uint32_t last_flow_ms_ = 0;
  std::string last_status_;

  void publish(const char* status) {
    const std::string s(status ? status : "");
    if (s != last_status_) {
      oq_service_status::set_air_purge(s);
      ESP_LOGI("quatt.cm100.purge", "status=%s phase=%d target_iPWM=%d remaining=%ds flow=%.0fL/h", s.c_str(),
               id(oq_air_purge_phase), id(oq_air_purge_target_ipwm), id(oq_air_purge_remaining_s),
               id(flow_rate_selected).state);
      last_status_ = s;
    }
  }

  bool hp_active(bool hp1) {
    int level = 0;
    int last_applied = 0;
    float working_mode = NAN;
    if (hp1) {
      if (id(hp1_compressor_level).has_state()) {
        auto idx = id(hp1_compressor_level).active_index();
        level = idx.has_value() ? (int)idx.value() : 0;
      }
      last_applied = id(hp1_last_applied_level);
      working_mode = id(hp1_working_mode).state;
    } else {
#if OQ_TOPOLOGY_DUO
      if (id(hp2_compressor_level).has_state()) {
        auto idx = id(hp2_compressor_level).active_index();
        level = idx.has_value() ? (int)idx.value() : 0;
      }
      last_applied = id(hp2_last_applied_level);
      working_mode = id(hp2_working_mode).state;
#endif
    }

    const int mode = isnan(working_mode) ? 0 : (int)roundf(working_mode);
    const bool thermal_mode = (mode == 1 || mode == 2);
    return level > 0 || last_applied > 0 || thermal_mode;
  }

  bool heat_pumps_idle() {
    if (hp_active(true)) return false;
#if OQ_TOPOLOGY_DUO
    if (hp_active(false)) return false;
#endif
    return true;
  }

  void finish(const char* status, int next_state, bool return_to_auto) {
    id(oq_air_purge_active) = false;
    id(oq_air_purge_abort) = false;
    id(oq_air_purge_state) = next_state;
    id(oq_air_purge_phase) = PHASE_IDLE;
    id(oq_air_purge_target_ipwm) = 1000;
    id(oq_air_purge_remaining_s) = 0;

    if (return_to_auto) {
      oq_commissioning::clear_container(false, next_state);
      if (id(oq_cm_override).has_state() && id(oq_cm_override).current_option() != "Auto") {
        auto call = id(oq_cm_override).make_call();
        call.set_option("Auto");
        call.perform();
      }
      oq_service_status::set_commissioning("IDLE");
    } else {
      const bool still_cm100 = oq_commissioning::is_cm100(id(oq_control_mode_code));
      oq_commissioning::clear_container(still_cm100, next_state);
    }

    publish(status);
    state_ = STATE_IDLE;
  }
};

inline AirPurgeRuntime& runtime() {
  static AirPurgeRuntime instance;
  return instance;
}

}  // namespace oq_air_purge
