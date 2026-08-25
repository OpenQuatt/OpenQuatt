#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string>

#include "../../boiler/oq_boiler_commissioning_logic.h"
#include "../../boiler/oq_boiler_logic.h"
#include "../oq_service_runtime.h"

namespace oq_boiler_task {

using oq_boiler_commissioning::boiler_test_dhw_interferes;
using oq_boiler_commissioning::compute_opentherm_operating_point;
using oq_boiler_commissioning::normalize_max_water_temperature_c;
using oq_boiler_commissioning::result_apply_allowed;
using oq_boiler_commissioning::select_initial_test_flow_lph;

static constexpr int TASK_NONE = oq_commissioning::TASK_NONE;
static constexpr int TASK_BOILER_POWER_TEST = oq_commissioning::TASK_BOILER_POWER_TEST;
static constexpr int TASK_FLOW_AUTOTUNE = oq_commissioning::TASK_FLOW_AUTOTUNE;
static constexpr int TASK_AIR_PURGE = oq_commissioning::TASK_AIR_PURGE;

static constexpr int STATE_IDLE = oq_commissioning::TASK_STATE_IDLE;
static constexpr int STATE_FLOW_SETTLE = oq_commissioning::TASK_STATE_FLOW_SETTLE;
static constexpr int STATE_BOILER_SETTLE = oq_commissioning::TASK_STATE_BOILER_SETTLE;
static constexpr int STATE_MEASURE = oq_commissioning::TASK_STATE_MEASURE;
static constexpr int STATE_COOLDOWN = oq_commissioning::TASK_STATE_COOLDOWN;
static constexpr int STATE_DONE = oq_commissioning::TASK_STATE_DONE;
static constexpr int STATE_ABORT = oq_commissioning::TASK_STATE_ABORT;
static constexpr int STATE_FAILED = oq_commissioning::TASK_STATE_FAILED;

struct RuntimeConfig {
  uint32_t max_runtime_ms;
  uint32_t flow_settle_min_ms;
  uint32_t boiler_settle_min_ms;
  uint32_t measure_min_ms;
  uint32_t cooldown_ms;
  float target_flow_lph;
  float flow_band_lph;
  int stable_flow_samples;
  int measure_min_samples;
  float plateau_ratio;
  int plateau_confirm_samples;
};

inline RuntimeConfig default_config() {
  return RuntimeConfig{
      .max_runtime_ms = 15UL * 60UL * 1000UL,
      .flow_settle_min_ms = 2UL * 60UL * 1000UL,
      .boiler_settle_min_ms = 30UL * 1000UL,
      .measure_min_ms = 3UL * 60UL * 1000UL,
      .cooldown_ms = 15UL * 1000UL,
      .target_flow_lph = 800.0f,
      .flow_band_lph = 40.0f,
      .stable_flow_samples = 4,
      .measure_min_samples = 8,
      .plateau_ratio = 0.95f,
      .plateau_confirm_samples = 4,
  };
}

class BoilerPowerTestRuntime {
 public:
  void start(const RuntimeConfig& cfg, uint32_t now_ms) {
    const int cm_code = id(oq_control_mode_code);
    const bool task_running = id(oq_commissioning_active) && id(oq_commissioning_task_code) != TASK_NONE;
    if (!id(oq_aux_heat_source_present).state) {
      oq_service_status::set_boiler_power_test("REFUSED: auxiliary heat source not connected");
      return;
    }
    if (task_running || id(oq_commissioning_request_pending)) {
      oq_service_status::set_boiler_power_test("REFUSED: BUSY");
      return;
    }
    if (!id(oq_flow_control_mode).has_state() || id(oq_flow_control_mode).current_option() != "Flow Setpoint") {
      oq_service_status::set_boiler_power_test("REFUSED: Flow mode must be Flow Setpoint");
      return;
    }
    if (!id(oq_flow_setpoint_lph).has_state()) {
      oq_service_status::set_boiler_power_test("REFUSED: flow setpoint unavailable");
      return;
    }
    if (!id(oq_boiler_power_test_flow_lph).has_state()) {
      oq_service_status::set_boiler_power_test("REFUSED: boiler test flow unavailable");
      return;
    }
    if (cm_code != 100) {
      oq_service_status::set_boiler_power_test("REFUSED: not CM100");
      return;
    }

    reset_test_state();
    const float initial_test_flow_lph =
        select_initial_test_flow_lph(id(oq_boiler_power_test_flow_lph).state, cfg.target_flow_lph);

#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    active_test_opentherm_ =
        id(oq_boiler_connection).has_state() && id(oq_boiler_connection).current_option() == "OpenTherm";
    if (active_test_opentherm_) {
      if (boiler_test_dhw_interferes(true, id(otb_dhw_active).has_state(), id(otb_dhw_active).state)) {
        oq_service_status::set_boiler_power_test("REFUSED: DHW active; retry without hot water or tap comfort");
        reset_test_state();
        return;
      }
      const float max_c = normalize_max_water_temperature_c(id(max_water_temp_limit_c).state);
      float inlet_c = NAN;
      if (id(otb_return_water_temp).has_state() && !isnan(id(otb_return_water_temp).state)) {
        inlet_c = id(otb_return_water_temp).state;
      } else {
        inlet_c = id(water_supply_temp_selected).state;
      }
      if (id(otb_max_capacity).has_state() && !isnan(id(otb_max_capacity).state) && id(otb_max_capacity).state > 0.0f) {
        active_test_capacity_w_ = id(otb_max_capacity).state * 1000.0f;
        active_test_capacity_verified_ = true;
      }
      const float rated_w = id(oq_boiler_rated_heat_power).state;
      const auto op = compute_opentherm_operating_point(true, active_test_capacity_w_, rated_w, inlet_c, max_c,
                                                        initial_test_flow_lph);
      if (!op.feasible) {
        oq_service_status::set_boiler_power_test("REFUSED: insufficient thermal headroom for boiler power test");
        ESP_LOGW("quatt.cm100.boiler", "Boiler test refused: %s (inlet=%.1fC max=%.1fC headroom=%.1fC)",
                 op.reason ? op.reason : "unknown", inlet_c, max_c, op.headroom_c);
        reset_test_state();
        return;
      }
    } else {
      active_test_capacity_w_ = id(oq_boiler_rated_heat_power).state;
      active_test_capacity_verified_ = true;
    }
#else
    active_test_capacity_w_ = id(oq_boiler_rated_heat_power).state;
    active_test_capacity_verified_ = true;
#endif

    ESP_LOGI("quatt.cm100.boiler",
             "Boiler power test requested (cm=%d flow_mode=%s normal_flow=%.0fL/h test_flow=%.0fL/h "
             "current_task=%d active=%d)",
             cm_code, id(oq_flow_control_mode).current_option().c_str(), id(oq_flow_setpoint_lph).state,
             initial_test_flow_lph, id(oq_commissioning_task_code), (int)id(oq_commissioning_active));

    reset_measurement_accumulators();
    prev_flow_setpoint_lph_ = id(oq_flow_setpoint_lph).state;
    flow_setpoint_saved_ = true;
    active_test_flow_target_lph_ = initial_test_flow_lph;

    id(oq_commissioning_task_code) = TASK_BOILER_POWER_TEST;
    id(oq_commissioning_request_pending) = false;
    id(oq_commissioning_active) = true;
    id(oq_commissioning_abort_requested) = false;
    id(oq_commissioning_state_code) = STATE_FLOW_SETTLE;
    id(oq_commissioning_started_ms) = now_ms;
    id(oq_commissioning_state_since_ms) = now_ms;
    id(oq_commissioning_boiler_request) = false;
    id(oq_commissioning_result_w) = NAN;
    id(oq_commissioning_result_confidence) = 0.0f;

    ESP_LOGI("quatt.cm100.boiler", "Boiler test armed: initial target_flow=%.0fL/h saved_flow=%.0fL/h state=%d",
             active_test_flow_target_lph_, prev_flow_setpoint_lph_, id(oq_commissioning_state_code));
    publish_transient_number_value(id(oq_flow_setpoint_lph), active_test_flow_target_lph_);

    oq_service_status::set_commissioning("BOILER TEST STARTED");
    publish_status("FLOW_SETTLING");
  }

  void apply_result() {
    const float result = id(oq_commissioning_result_w);
    if (isnan(result) || result <= 0.0f) {
      publish_status("APPLY_FAILED: invalid result");
      return;
    }
    if (!active_test_result_apply_allowed_) {
      if (active_test_flow_limited_) {
        publish_status("APPLY_REFUSED: flow limited");
      } else {
        publish_status("APPLY_REFUSED: capacity unverified");
      }
      return;
    }
    const int rounded_result = (int)roundf(result / 100.0f) * 100;
    set_number_value(id(oq_boiler_rated_heat_power), (float)rounded_result);
    char msg[64];
    snprintf(msg, sizeof(msg), "APPLIED: %dW", rounded_result);
    publish_status(msg);
  }

  void abort_or_clear() {
    const int task_code = id(oq_commissioning_task_code);
    const bool task_running = id(oq_commissioning_active) && task_code != TASK_NONE;
    if (task_running) {
      if (task_code == TASK_FLOW_AUTOTUNE) {
        id(oq_flow_autotune_abort) = true;
      } else {
        id(oq_commissioning_abort_requested) = true;
      }
      oq_service_status::set_commissioning("ABORT REQUESTED");
      return;
    }
    if (task_code == TASK_FLOW_AUTOTUNE || id(oq_flow_autotune_req)) {
      id(oq_flow_autotune_abort) = true;
      id(oq_commissioning_abort_requested) = true;
      oq_service_status::set_commissioning("ABORT REQUESTED");
      return;
    }
    clear_container();
  }

  void tick(const RuntimeConfig& cfg, uint32_t now_ms) {
    const int cm_code = id(oq_control_mode_code);
    const bool in_cm100 = cm_code == 100;
    const int task_code = id(oq_commissioning_task_code);
    const bool task_is_none = task_code == TASK_NONE;
    const bool task_is_boiler = task_code == TASK_BOILER_POWER_TEST;
    const bool task_is_flow_autotune = task_code == TASK_FLOW_AUTOTUNE;
    const bool task_is_air_purge = task_code == TASK_AIR_PURGE;
    const bool task_is_manual_flow = task_code == oq_commissioning::TASK_MANUAL_FLOW;
    const bool task_is_manual_hp = task_code == oq_commissioning::TASK_MANUAL_HP;
    const bool boiler_test_running = id(oq_commissioning_active) && task_is_boiler;
    const float flow_lph = id(flow_rate_selected).state;
    const bool flow_stable_now = flow_on_target(flow_lph, cfg.flow_band_lph);
    const float heat_w = id(boiler_heat_power).state;
    const bool heat_valid = !isnan(heat_w) && heat_w >= 0.0f;
    log_heartbeat(task_is_boiler, cm_code, flow_lph, heat_w, now_ms, cfg);

    if (task_is_air_purge || task_is_manual_flow || task_is_manual_hp) return;

    if (id(oq_commissioning_abort_requested)) {
      ESP_LOGW("quatt.cm100.boiler", "Boiler test abort requested (state=%d cm=%d active=%d pending=%d)",
               id(oq_commissioning_state_code), cm_code, (int)id(oq_commissioning_active),
               (int)id(oq_commissioning_request_pending));
      finish_task("ABORTED", STATE_ABORT, true, true);
      return;
    }

    if (task_is_flow_autotune) {
      if (id(oq_commissioning_request_pending) && in_cm100) {
        ESP_LOGI("quatt.cm100.autotune", "Autotune request accepted in CM100; activating commissioning container");
        id(oq_commissioning_active) = true;
        id(oq_commissioning_request_pending) = false;
        id(oq_commissioning_started_ms) = now_ms;
        id(oq_commissioning_state_since_ms) = now_ms;
      }
      return;
    }

    if (task_is_none) {
      accept_neutral_cm100_if_ready(in_cm100, now_ms);
      return;
    }

    if (!boiler_test_running) {
      accept_boiler_if_ready(in_cm100, now_ms);
      return;
    }

    if (!in_cm100) {
      finish_task("ABORTED: CM100 exited", STATE_ABORT, true, false);
      return;
    }

    if (id(oq_commissioning_started_ms) == 0) {
      id(oq_commissioning_started_ms) = now_ms;
      id(oq_commissioning_state_since_ms) = now_ms;
    }
    if ((uint32_t)(now_ms - id(oq_commissioning_started_ms)) >= cfg.max_runtime_ms) {
      finish_task("FAILED: timeout", STATE_FAILED, false, true);
      return;
    }
    if (!guards_ok()) return;

#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    const int state_code = id(oq_commissioning_state_code);
    const bool dhw_can_interfere =
        state_code == STATE_FLOW_SETTLE || state_code == STATE_BOILER_SETTLE || state_code == STATE_MEASURE;
    if (dhw_can_interfere &&
        boiler_test_dhw_interferes(active_test_opentherm_, id(otb_dhw_active).has_state(), id(otb_dhw_active).state)) {
      ESP_LOGW("quatt.cm100.boiler", "Boiler test failed because DHW became active (state=%d)", state_code);
      finish_task("FAILED: DHW active; retry without hot water or tap comfort", STATE_FAILED, false, true);
      return;
    }
#endif

    if (id(oq_commissioning_boiler_request)) id(oq_commissioning_boiler_request_updated_ms) = now_ms;

    switch (id(oq_commissioning_state_code)) {
      case STATE_FLOW_SETTLE:
        run_flow_settle(cfg, now_ms, flow_lph, flow_stable_now);
        return;
      case STATE_BOILER_SETTLE:
        run_boiler_settle(cfg, now_ms, flow_lph, heat_w, flow_stable_now);
        return;
      case STATE_MEASURE:
        run_measure(cfg, now_ms, flow_stable_now, heat_valid, heat_w);
        return;
      case STATE_COOLDOWN:
        run_cooldown(cfg, now_ms);
        return;
      case STATE_DONE:
        publish_done_status();
        return;
      default:
        return;
    }
  }

 private:
  bool flow_setpoint_saved_{false};
  float prev_flow_setpoint_lph_{NAN};
  float active_test_flow_target_lph_{NAN};
  float active_test_capacity_w_{NAN};
  float active_test_theoretical_flow_lph_{NAN};
  bool active_test_opentherm_{false};
  bool active_test_capacity_verified_{false};
  bool active_test_flow_limited_{false};
  bool active_test_result_apply_allowed_{false};
  oq_boiler_commissioning::FlowReachabilityMonitor flow_reachability_{};
  int stable_flow_count_{0};
  int sample_count_{0};
  float sum_w_{0.0f};
  float min_w_{NAN};
  float max_w_{NAN};
  float peak_w_{NAN};
  int plateau_count_{0};
  int last_state_logged_{-1};
  uint32_t last_heartbeat_ms_{0};
  std::string last_status_{};

  template <typename NumberEntity>
  void set_number_value(NumberEntity& number_entity, float value) {
    auto call = number_entity.make_call();
    call.set_value(value);
    call.perform();
  }

  template <typename NumberEntity>
  void publish_transient_number_value(NumberEntity& number_entity, float value) {
    // Do not overwrite the restore_value preference with a temporary service target.
    number_entity.publish_state(value);
  }

  bool flow_on_target(float flow_lph, float flow_band_lph) const {
    return !isnan(flow_lph) && flow_lph > 0.0f && !isnan(active_test_flow_target_lph_) &&
           fabsf(flow_lph - active_test_flow_target_lph_) <= flow_band_lph;
  }

  void publish_status(const char* status) {
    if (last_status_ != status) {
      oq_service_status::set_boiler_power_test(status);
      last_status_ = status;
    }
  }

  void restore_flow_setpoint() {
    if (!flow_setpoint_saved_) return;
    publish_transient_number_value(id(oq_flow_setpoint_lph), prev_flow_setpoint_lph_);
    flow_setpoint_saved_ = false;
  }

  void reset_measurement_accumulators() {
    stable_flow_count_ = 0;
    sample_count_ = 0;
    sum_w_ = 0.0f;
    min_w_ = NAN;
    max_w_ = NAN;
    peak_w_ = NAN;
    plateau_count_ = 0;
  }

  void reset_test_state() {
    reset_measurement_accumulators();
    flow_reachability_.reset();
    active_test_flow_target_lph_ = NAN;
    active_test_capacity_w_ = NAN;
    active_test_theoretical_flow_lph_ = NAN;
    active_test_opentherm_ = false;
    active_test_capacity_verified_ = false;
    active_test_flow_limited_ = false;
    active_test_result_apply_allowed_ = false;
  }

  void clear_container() {
    restore_flow_setpoint();
    reset_test_state();
    oq_commissioning::clear_container(false);
    id(oq_commissioning_boiler_request) = false;
    id(oq_flow_autotune_req) = false;
    id(oq_flow_autotune_abort) = false;
    id(oq_flow_autotune_active) = false;
    id(oq_flow_autotune_state) = 0;
    oq_service_status::set_commissioning("IDLE");
  }

  void finish_task(const char* status, int next_state, bool keep_result, bool keep_cm100) {
    id(oq_commissioning_boiler_request) = false;
    restore_flow_setpoint();
    oq_commissioning::clear_container(keep_cm100, next_state);
    if (!keep_result) {
      id(oq_commissioning_result_w) = NAN;
      id(oq_commissioning_result_confidence) = 0.0f;
      reset_test_state();
    }
    publish_status(status);
  }

  void accept_neutral_cm100_if_ready(bool in_cm100, uint32_t now_ms) {
    if (!id(oq_commissioning_request_pending)) return;
    if (!in_cm100) {
      publish_status("WAITING_FOR_CM100");
      oq_service_status::set_commissioning("WAITING_FOR_CM100");
      ESP_LOGI("quatt.cm100", "CM100 request pending while not in CM100; waiting");
      return;
    }
    ESP_LOGI("quatt.cm100", "CM100 request accepted; entering neutral commissioning container");
    id(oq_commissioning_active) = true;
    id(oq_commissioning_request_pending) = false;
    id(oq_commissioning_started_ms) = now_ms;
    id(oq_commissioning_state_since_ms) = now_ms;
    publish_status("CM100 READY");
    oq_service_status::set_commissioning("CM100 READY");
  }

  void accept_boiler_if_ready(bool in_cm100, uint32_t now_ms) {
    if (!id(oq_commissioning_request_pending)) return;
    if (!in_cm100) {
      publish_status("WAITING_FOR_CM100");
      ESP_LOGI("quatt.cm100.boiler", "Boiler test waiting for CM100");
      return;
    }
    ESP_LOGI("quatt.cm100.boiler", "Boiler test request accepted in CM100; entering FLOW_SETTLING");
    id(oq_commissioning_active) = true;
    id(oq_commissioning_request_pending) = false;
    id(oq_commissioning_started_ms) = now_ms;
    id(oq_commissioning_state_since_ms) = now_ms;
    id(oq_commissioning_state_code) = STATE_FLOW_SETTLE;
    flow_reachability_.reset();
    publish_status("FLOW_SETTLING");
  }

  bool guards_ok() {
    if (id(oq_water_temp_hard_trip_active)) {
      ESP_LOGW("quatt.cm100.boiler", "Boiler test failed: hard trip active");
      finish_task("FAILED: hard trip active", STATE_FAILED, false, true);
      return false;
    }
    if (id(oq_water_temp_boiler_inhibit_active)) {
      ESP_LOGW("quatt.cm100.boiler", "Boiler test failed: boiler inhibit active");
      finish_task("FAILED: boiler inhibit active", STATE_FAILED, false, true);
      return false;
    }
    if (!id(oq_aux_heat_source_present).state) {
      ESP_LOGW("quatt.cm100.boiler", "Boiler test failed: auxiliary heat source not connected");
      finish_task("FAILED: auxiliary heat source not connected", STATE_FAILED, false, true);
      return false;
    }
    return true;
  }

  bool flow_reachable(const RuntimeConfig& cfg, uint32_t now_ms, float flow_lph) {
    const uint32_t state_age_ms = now_ms - id(oq_commissioning_state_since_ms);
    if (state_age_ms < cfg.flow_settle_min_ms) {
      flow_reachability_.reset();
      return true;
    }
    const float output_ipwm = id(oq_flow_output_ipwm).has_state() ? id(oq_flow_output_ipwm).state : NAN;
    if (!flow_reachability_.update(now_ms, flow_lph, active_test_flow_target_lph_, cfg.flow_band_lph, output_ipwm)) {
      return true;
    }

    ESP_LOGW("quatt.cm100.boiler",
             "Boiler test flow unreachable (target=%.0fL/h flow=%.0fL/h best=%.0fL/h iPWM=%.0f saturated=%lus)",
             active_test_flow_target_lph_, flow_lph, flow_reachability_.best_flow_lph(), output_ipwm,
             (unsigned long)(flow_reachability_.saturated_duration_ms(now_ms) / 1000UL));
    finish_task("FAILED: required boiler test flow cannot be reached", STATE_FAILED, false, true);
    return false;
  }

  void run_flow_settle(const RuntimeConfig& cfg, uint32_t now_ms, float flow_lph, bool flow_stable_now) {
    if (!flow_reachable(cfg, now_ms, flow_lph)) return;

    stable_flow_count_ = flow_stable_now ? stable_flow_count_ + 1 : 0;
    if (stable_flow_count_ < cfg.stable_flow_samples ||
        (uint32_t)(now_ms - id(oq_commissioning_state_since_ms)) < cfg.flow_settle_min_ms) {
      publish_status("FLOW_SETTLING");
      return;
    }

#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    if (active_test_opentherm_) {
      const float max_c = normalize_max_water_temperature_c(id(max_water_temp_limit_c).state);
      float inlet_c = NAN;
      if (id(otb_return_water_temp).has_state() && !isnan(id(otb_return_water_temp).state)) {
        inlet_c = id(otb_return_water_temp).state;
      } else {
        inlet_c = id(water_supply_temp_selected).state;
      }
      const float rated_w = id(oq_boiler_rated_heat_power).state;
      const auto op = compute_opentherm_operating_point(true, active_test_capacity_w_, rated_w, inlet_c, max_c,
                                                        active_test_flow_target_lph_);
      if (!op.feasible) {
        finish_task("FAILED: insufficient thermal headroom for boiler power test", STATE_FAILED, false, true);
        return;
      }
      active_test_theoretical_flow_lph_ = op.theoretical_flow_lph;
      active_test_flow_limited_ = op.flow_limited;
      if (op.target_flow_lph > active_test_flow_target_lph_ + 10.0f) {
        ESP_LOGI("quatt.cm100.boiler",
                 "Preflow settled at %.0f L/h; theoretical flow %.0f L/h, selecting %.0f L/h%s and re-settling",
                 flow_lph, op.theoretical_flow_lph, op.target_flow_lph, op.flow_limited ? " (flow limited)" : "");
        active_test_flow_target_lph_ = op.target_flow_lph;
        publish_transient_number_value(id(oq_flow_setpoint_lph), active_test_flow_target_lph_);
        stable_flow_count_ = 0;
        flow_reachability_.reset();
        id(oq_commissioning_state_since_ms) = now_ms;
        publish_status("FLOW_SETTLING");
        return;
      }
    }
#endif

    flow_reachability_.reset();
    id(oq_commissioning_boiler_request_updated_ms) = now_ms;
    id(oq_commissioning_boiler_request) = true;
    id(oq_commissioning_state_code) = STATE_BOILER_SETTLE;
    id(oq_commissioning_state_since_ms) = now_ms;
    stable_flow_count_ = 0;
    ESP_LOGI("quatt.cm100.boiler", "Flow settled at %.0fL/h after %lus; requesting boiler relay", flow_lph,
             (unsigned long)((now_ms - id(oq_commissioning_started_ms)) / 1000UL));
    publish_status("BOILER_SETTLING");
  }

  void run_boiler_settle(const RuntimeConfig& cfg, uint32_t now_ms, float flow_lph, float heat_w,
                         bool flow_stable_now) {
    stable_flow_count_ = flow_stable_now ? stable_flow_count_ + 1 : 0;
    const uint32_t state_age_ms = now_ms - id(oq_commissioning_state_since_ms);
    if (!id(boiler_active).state) {
      if (state_age_ms >= cfg.boiler_settle_min_ms) {
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
        const bool opentherm_selected = active_test_opentherm_;
#else
        const bool opentherm_selected = false;
#endif
        const char* failure_reason = oq_boiler::commissioning_start_failure_reason(
            id(oq_boiler_block_reason_code), opentherm_selected, id(oq_boiler_output_request),
            id(oq_otb_link_available_state));
        char failure_status[96];
        snprintf(failure_status, sizeof(failure_status), "FAILED: %s", failure_reason);
        ESP_LOGW("quatt.cm100.boiler",
                 "Boiler did not start in time (flow=%.0fL/h boiler_req=%d output_req=%d block=%s otb_link=%d "
                 "elapsed=%lus)",
                 flow_lph, (int)id(oq_commissioning_boiler_request), (int)id(oq_boiler_output_request), failure_reason,
                 (int)id(oq_otb_link_available_state), (unsigned long)(state_age_ms / 1000UL));
        finish_task(failure_status, STATE_FAILED, false, true);
      } else {
        publish_status("BOILER_SETTLING");
      }
      return;
    }
    if (stable_flow_count_ >= cfg.stable_flow_samples && state_age_ms >= cfg.boiler_settle_min_ms) {
      id(oq_commissioning_state_code) = STATE_MEASURE;
      id(oq_commissioning_state_since_ms) = now_ms;
      reset_measurement_accumulators();
      ESP_LOGI("quatt.cm100.boiler",
               "Boiler settled; starting measurement window (flow=%.0fL/h heat=%.0fW boiler_active=%d)", flow_lph,
               heat_w, (int)id(boiler_active).state);
      publish_status("MEASURING");
    } else {
      publish_status("BOILER_SETTLING");
    }
  }

  void run_measure(const RuntimeConfig& cfg, uint32_t now_ms, bool flow_stable_now, bool heat_valid, float heat_w) {
    if (flow_stable_now && heat_valid && heat_w > 0.0f) {
      if (isnan(peak_w_) || heat_w > peak_w_) {
        peak_w_ = heat_w;
        plateau_count_ = 0;
      }
      const float plateau_floor = isnan(peak_w_) ? heat_w : peak_w_ * cfg.plateau_ratio;
      if (heat_w >= plateau_floor) {
        if (plateau_count_ < 1000) plateau_count_++;
      } else {
        plateau_count_ = 0;
      }
      if (plateau_count_ >= cfg.plateau_confirm_samples) {
        sample_count_++;
        sum_w_ += heat_w;
        if (isnan(min_w_) || heat_w < min_w_) min_w_ = heat_w;
        if (isnan(max_w_) || heat_w > max_w_) max_w_ = heat_w;
      }
    }

    const uint32_t measure_age_ms = now_ms - id(oq_commissioning_state_since_ms);
    if (sample_count_ < cfg.measure_min_samples || measure_age_ms < cfg.measure_min_ms) {
      publish_status("MEASURING");
      return;
    }
    if (sample_count_ <= 0 || isnan(min_w_) || isnan(max_w_)) {
      finish_task("FAILED: invalid measurement", STATE_FAILED, false, true);
      return;
    }

    const float sample_count_f = (float)sample_count_;
    const float avg_w = sum_w_ / sample_count_f;
    const float spread_w = max_w_ - min_w_;
    float confidence = 100.0f;
    if (sample_count_f < 10.0f) confidence -= (10.0f - sample_count_f) * 4.0f;
    if (spread_w > avg_w * 0.05f) confidence -= 15.0f;
    if (spread_w > avg_w * 0.10f) confidence -= 20.0f;
    if (confidence < 0.0f) confidence = 0.0f;
    if (confidence > 100.0f) confidence = 100.0f;

    active_test_result_apply_allowed_ =
        result_apply_allowed(active_test_opentherm_, active_test_capacity_verified_, active_test_flow_limited_);
    if (!active_test_result_apply_allowed_) {
      confidence = fminf(confidence, 70.0f);
      ESP_LOGI("quatt.cm100.boiler", "Measurement result is informational only: %s; avg=%.0fW conf=%.0f%%",
               active_test_flow_limited_ ? "flow/headroom limited" : "capacity unverified", avg_w, confidence);
    }

    id(oq_commissioning_result_w) = avg_w;
    id(oq_commissioning_result_confidence) = confidence;
    id(oq_commissioning_state_code) = STATE_COOLDOWN;
    id(oq_commissioning_state_since_ms) = now_ms;
    id(oq_commissioning_boiler_request) = false;
    ESP_LOGI("quatt.cm100.boiler",
             "Measurement complete: avg=%.0fW min=%.0fW max=%.0fW samples=%u "
             "conf=%.0f%%",
             avg_w, min_w_, max_w_, (unsigned int)sample_count_, confidence);
    restore_flow_setpoint();
    publish_status("COOLDOWN");
  }

  const char* result_quality_suffix() const {
    if (active_test_flow_limited_) return "flow limited";
    if (active_test_opentherm_ && !active_test_capacity_verified_) return "capacity unverified";
    return nullptr;
  }

  void build_done_status(char* msg, size_t size) const {
    const char* suffix = result_quality_suffix();
    if (suffix != nullptr) {
      snprintf(msg, size, "DONE: %.0fW (conf %.0f%%) - %s", id(oq_commissioning_result_w),
               id(oq_commissioning_result_confidence), suffix);
    } else {
      snprintf(msg, size, "DONE: %.0fW (conf %.0f%%)", id(oq_commissioning_result_w),
               id(oq_commissioning_result_confidence));
    }
  }

  void run_cooldown(const RuntimeConfig& cfg, uint32_t now_ms) {
    if ((uint32_t)(now_ms - id(oq_commissioning_state_since_ms)) < cfg.cooldown_ms) {
      publish_status("COOLDOWN");
      return;
    }
    char msg[128];
    build_done_status(msg, sizeof(msg));
    ESP_LOGI("quatt.cm100.boiler", "Cooldown complete; CM100 idle after boiler test (flow restored, boiler off, %s)",
             msg);
    finish_task(msg, STATE_DONE, true, true);
  }

  void publish_done_status() {
    char msg[128];
    build_done_status(msg, sizeof(msg));
    publish_status(msg);
  }

  void log_heartbeat(bool task_is_boiler, int cm_code, float flow_lph, float heat_w, uint32_t now_ms,
                     const RuntimeConfig& cfg) {
    const uint32_t elapsed_ms = (id(oq_commissioning_started_ms) == 0) ? 0 : (now_ms - id(oq_commissioning_started_ms));
    if (task_is_boiler && id(oq_commissioning_state_code) != last_state_logged_) {
      ESP_LOGI("quatt.cm100.boiler",
               "state=%d cm=%d active=%d pending=%d flow=%.0fL/h target=%.0fL/h stable=%d/%d boiler_req=%d "
               "boiler_active=%d heat=%.0fW elapsed=%lus",
               id(oq_commissioning_state_code), cm_code, (int)id(oq_commissioning_active),
               (int)id(oq_commissioning_request_pending), flow_lph, active_test_flow_target_lph_, stable_flow_count_,
               cfg.stable_flow_samples, (int)id(oq_commissioning_boiler_request), (int)id(boiler_active).state, heat_w,
               (unsigned long)(elapsed_ms / 1000UL));
      last_state_logged_ = id(oq_commissioning_state_code);
    }
    if (task_is_boiler && id(oq_commissioning_state_code) >= STATE_FLOW_SETTLE &&
        id(oq_commissioning_state_code) <= STATE_COOLDOWN) {
      if (last_heartbeat_ms_ == 0 || (uint32_t)(now_ms - last_heartbeat_ms_) >= 30000UL) {
        last_state_logged_ = -1;
        last_heartbeat_ms_ = now_ms;
      }
    } else {
      last_heartbeat_ms_ = 0;
    }
  }
};

inline BoilerPowerTestRuntime& runtime() {
  static BoilerPowerTestRuntime instance;
  return instance;
}

}  // namespace oq_boiler_task
