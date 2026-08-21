#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string>

#include "../../boiler/oq_boiler_commissioning_logic.h"
#include "../../boiler/oq_boiler_logic.h"
#include "../oq_service_runtime.h"

namespace oq_boiler_task {

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
    if (!id(oq_boiler_assist_enabled).state) {
      oq_service_status::set_boiler_power_test("REFUSED: boiler/CV assist disabled");
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
    if (cm_code != 100) {
      oq_service_status::set_boiler_power_test("REFUSED: not CM100");
      return;
    }
    // Thermal headroom check - OpenTherm only; R1 keeps existing 800 L/h behavior
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    const bool opentherm_selected_headroom =
        id(oq_boiler_connection).has_state() && id(oq_boiler_connection).current_option() == "OpenTherm";
    if (opentherm_selected_headroom) {
      float max_c = id(max_water_temp_limit_c).state;
      if (isnan(max_c)) max_c = 60.0f;
      max_c = fmaxf(25.0f, fminf(max_c, 75.0f));
      // Prefer OTB return water temp as boiler inlet if available, fallback to supply
      float inlet_c = NAN;
      if (id(otb_return_water_temp).has_state() && !isnan(id(otb_return_water_temp).state)) {
        inlet_c = id(otb_return_water_temp).state;
      } else {
        inlet_c = id(water_supply_temp_selected).state;
      }
      const float flow_lph = id(flow_rate_selected).state;
      float rated_w = id(oq_boiler_rated_heat_power).state;
      float otb_max_w = NAN;
      if (id(otb_max_capacity).has_state() && !isnan(id(otb_max_capacity).state) && id(otb_max_capacity).state > 0.0f) {
        otb_max_w = id(otb_max_capacity).state * 1000.0f;
      }
      const float cp = 4180.0f;
      const float flow_for_check = (!isnan(flow_lph) && flow_lph > 0.0f) ? flow_lph : cfg.target_flow_lph;
      auto op = oq_boiler_commissioning::compute_opentherm_operating_point(true, otb_max_w, rated_w, inlet_c, max_c,
                                                                           flow_for_check, cp, 5.0f);
      if (!op.feasible) {
        oq_service_status::set_boiler_power_test("REFUSED: insufficient thermal headroom for boiler power test");
        ESP_LOGW("quatt.cm100.boiler",
                 "Boiler test refused: %s (inlet=%.1fC max=%.1fC flow=%.0fL/h rated=%.0fW headroom=%.1fC)",
                 op.reason ? op.reason : "unknown", inlet_c, max_c, flow_for_check, rated_w, op.headroom_c);
        return;
      }
      if (op.required_flow_lph > flow_for_check + 10.0f) {
        ESP_LOGI("quatt.cm100.boiler",
                 "Boiler test headroom requires higher flow: need %.0f L/h vs current %.0f L/h, target remains %.0f",
                 op.required_flow_lph, flow_for_check, cfg.target_flow_lph);
      }
      active_test_capacity_w_ = rated_w;
    } else {
      // R1 on Q: store rated power as capacity for later consistent use
      active_test_capacity_w_ = id(oq_boiler_rated_heat_power).state;
    }
#else
    // Non-Q: no OT, store rated power
    active_test_capacity_w_ = id(oq_boiler_rated_heat_power).state;
#endif

    ESP_LOGI("quatt.cm100.boiler",
             "Boiler power test requested (cm=%d flow_mode=%s flow_sp=%.0fL/h current_task=%d active=%d)", cm_code,
             id(oq_flow_control_mode).current_option().c_str(), id(oq_flow_setpoint_lph).state,
             id(oq_commissioning_task_code), (int)id(oq_commissioning_active));

    reset_measurement();
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

    prev_flow_setpoint_lph_ = id(oq_flow_setpoint_lph).state;
    flow_setpoint_saved_ = true;
    // Use headroom-aware flow if required flow exceeds default 800 - reuse stored capacity from start()
    float target_flow_to_use = cfg.target_flow_lph;
    float rated_w_for_calc = active_test_capacity_w_;
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    const bool opentherm_selected =
        id(oq_boiler_connection).has_state() && id(oq_boiler_connection).current_option() == "OpenTherm";
    if (opentherm_selected) {
      float max_c = id(max_water_temp_limit_c).state;
      if (isnan(max_c)) max_c = 60.0f;
      max_c = fmaxf(25.0f, fminf(max_c, 75.0f));
      float inlet_c2 = NAN;
      if (id(otb_return_water_temp).has_state() && !isnan(id(otb_return_water_temp).state)) {
        inlet_c2 = id(otb_return_water_temp).state;
      } else {
        inlet_c2 = id(water_supply_temp_selected).state;
      }
      float otb_max_w2 = NAN;
      if (id(otb_max_capacity).has_state() && !isnan(id(otb_max_capacity).state) && id(otb_max_capacity).state > 0.0f) {
        otb_max_w2 = id(otb_max_capacity).state * 1000.0f;
      }
      auto op2 = oq_boiler_commissioning::compute_opentherm_operating_point(
          true, otb_max_w2, rated_w_for_calc, inlet_c2, max_c, cfg.target_flow_lph, 4180.0f, 5.0f);
      if (!op2.feasible) {
        // Still infeasible even with OT logic (e.g., >1500) - keep at 800 and let guard handle, but mark limited
        active_test_flow_limited_ = op2.flow_limited;
      } else if (op2.flow_limited) {
        target_flow_to_use = 1000.0f;
        active_test_flow_limited_ = true;
        ESP_LOGI("quatt.cm100.boiler",
                 "Boiler test flow limited to 1000 L/h (required %.0f) - result may be headroom limited",
                 op2.required_flow_lph);
      } else if (op2.required_flow_lph > cfg.target_flow_lph) {
        target_flow_to_use = fminf(op2.required_flow_lph, 1000.0f);
        if (op2.required_flow_lph > 1000.0f) active_test_flow_limited_ = true;
        ESP_LOGI("quatt.cm100.boiler", "Boiler test using headroom flow %.0f L/h (required %.0f)", target_flow_to_use,
                 op2.required_flow_lph);
      }
    }
#else
    (void)rated_w_for_calc;
#endif
    active_test_flow_target_lph_ = target_flow_to_use;
    ESP_LOGI("quatt.cm100.boiler", "Boiler test armed: target_flow=%.0fL/h saved_flow=%.0fL/h state=%d",
             target_flow_to_use, prev_flow_setpoint_lph_, id(oq_commissioning_state_code));
    set_number_value(id(oq_flow_setpoint_lph), target_flow_to_use);

    oq_service_status::set_commissioning("BOILER TEST STARTED");
    publish_status("FLOW_SETTLING");
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

    if (task_is_air_purge || task_is_manual_flow || task_is_manual_hp) {
      return;
    }

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

    // Renew the authorization only from this guarded originating state machine.
    // The dispatcher deliberately cannot make a commissioning command fresh.
    if (id(oq_commissioning_boiler_request)) {
      id(oq_commissioning_boiler_request_updated_ms) = now_ms;
    }

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
  bool active_test_flow_limited_{false};
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

  bool flow_on_target(float flow_lph, float flow_band_lph) const {
    return !isnan(flow_lph) && flow_lph > 0.0f && fabsf(flow_lph - active_test_flow_target_lph_) <= flow_band_lph;
  }

  void publish_status(const char* status) {
    if (last_status_ != status) {
      oq_service_status::set_boiler_power_test(status);
      last_status_ = status;
    }
  }

  void restore_flow_setpoint() {
    if (!flow_setpoint_saved_) return;
    set_number_value(id(oq_flow_setpoint_lph), prev_flow_setpoint_lph_);
    flow_setpoint_saved_ = false;
  }

  void reset_measurement() {
    stable_flow_count_ = 0;
    sample_count_ = 0;
    sum_w_ = 0.0f;
    min_w_ = NAN;
    max_w_ = NAN;
    peak_w_ = NAN;
    plateau_count_ = 0;
    active_test_flow_target_lph_ = NAN;
    active_test_capacity_w_ = NAN;
    active_test_flow_limited_ = false;
  }

  void clear_container() {
    restore_flow_setpoint();
    reset_measurement();
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
      reset_measurement();
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
    if (!id(oq_boiler_assist_enabled).state) {
      ESP_LOGW("quatt.cm100.boiler", "Boiler test failed: boiler/CV assist disabled");
      finish_task("FAILED: boiler/CV assist disabled", STATE_FAILED, false, true);
      return false;
    }
    return true;
  }

  void run_flow_settle(const RuntimeConfig& cfg, uint32_t now_ms, float flow_lph, bool flow_stable_now) {
    stable_flow_count_ = flow_stable_now ? stable_flow_count_ + 1 : 0;
    if (stable_flow_count_ >= cfg.stable_flow_samples &&
        (uint32_t)(now_ms - id(oq_commissioning_state_since_ms)) >= cfg.flow_settle_min_ms) {
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
      // Re-evaluate headroom with fresh inlet after preflow (OT return temp may have risen from 22 to 30°C)
      const bool opentherm_selected =
          id(oq_boiler_connection).has_state() && id(oq_boiler_connection).current_option() == "OpenTherm";
      if (opentherm_selected) {
        float max_c = id(max_water_temp_limit_c).state;
        if (isnan(max_c)) max_c = 60.0f;
        max_c = fmaxf(25.0f, fminf(max_c, 75.0f));
        float inlet_c = NAN;
        if (id(otb_return_water_temp).has_state() && !isnan(id(otb_return_water_temp).state)) {
          inlet_c = id(otb_return_water_temp).state;
        } else {
          inlet_c = id(water_supply_temp_selected).state;
        }
        float rated_w = active_test_capacity_w_;
        auto op = oq_boiler_commissioning::compute_operating_point(rated_w, inlet_c, max_c, flow_lph, 4180.0f, 5.0f);
        if (!op.feasible) {
          finish_task("FAILED: insufficient thermal headroom for boiler power test", STATE_FAILED, false, true);
          return;
        }
        if (op.required_flow_lph > active_test_flow_target_lph_ + 10.0f) {
          float new_flow = fminf(op.required_flow_lph, 1000.0f);
          bool limited = op.flow_limited || op.required_flow_lph > 1000.0f;
          ESP_LOGI("quatt.cm100.boiler",
                   "Flow settled at %.0f but headroom now requires %.0f L/h (inlet %.1fC); updating target %sand "
                   "re-settling",
                   flow_lph, op.required_flow_lph, inlet_c, limited ? "(limited to 1000) " : "");
          active_test_flow_target_lph_ = new_flow;
          if (limited) active_test_flow_limited_ = true;
          set_number_value(id(oq_flow_setpoint_lph), new_flow);
          stable_flow_count_ = 0;
          id(oq_commissioning_state_since_ms) = now_ms;
          publish_status("FLOW_SETTLING");
          return;
        }
      }
#endif
      id(oq_commissioning_boiler_request_updated_ms) = now_ms;
      id(oq_commissioning_boiler_request) = true;
      id(oq_commissioning_state_code) = STATE_BOILER_SETTLE;
      id(oq_commissioning_state_since_ms) = now_ms;
      stable_flow_count_ = 0;
      ESP_LOGI("quatt.cm100.boiler", "Flow settled at %.0fL/h after %lus; requesting boiler relay", flow_lph,
               (unsigned long)((now_ms - id(oq_commissioning_started_ms)) / 1000UL));
      publish_status("BOILER_SETTLING");
    } else {
      publish_status("FLOW_SETTLING");
    }
  }

  void run_boiler_settle(const RuntimeConfig& cfg, uint32_t now_ms, float flow_lph, float heat_w,
                         bool flow_stable_now) {
    stable_flow_count_ = flow_stable_now ? stable_flow_count_ + 1 : 0;
    const uint32_t state_age_ms = now_ms - id(oq_commissioning_state_since_ms);
    if (!id(boiler_active).state) {
      if (state_age_ms >= cfg.boiler_settle_min_ms) {
        const bool opentherm_selected =
            id(oq_boiler_connection).has_state() && id(oq_boiler_connection).current_option() == "OpenTherm";
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
      reset_measurement();
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

    // If flow was limited to 1000 due to headroom, mark result as headroom-limited
    if (active_test_flow_limited_) {
      confidence = fminf(confidence, 70.0f);
      ESP_LOGI("quatt.cm100.boiler",
               "Measurement headroom limited (flow 1000, capacity %.0fW): avg=%.0fW conf=%.0f%% (penalized)",
               active_test_capacity_w_, avg_w, confidence);
    }

    id(oq_commissioning_result_w) = avg_w;
    id(oq_commissioning_result_confidence) = confidence;
    id(oq_commissioning_state_code) = STATE_COOLDOWN;
    id(oq_commissioning_state_since_ms) = now_ms;
    id(oq_commissioning_boiler_request) = false;
    ESP_LOGI("quatt.cm100.boiler", "Measurement complete: avg=%.0fW min=%.0fW max=%.0fW samples=%u conf=%.0f%% %s",
             avg_w, min_w_, max_w_, (unsigned int)sample_count_, confidence,
             active_test_flow_limited_ ? "(flow limited)" : "");
    restore_flow_setpoint();
    publish_status("COOLDOWN");
  }

  void run_cooldown(const RuntimeConfig& cfg, uint32_t now_ms) {
    if ((uint32_t)(now_ms - id(oq_commissioning_state_since_ms)) < cfg.cooldown_ms) {
      publish_status("COOLDOWN");
      return;
    }
    char msg[128];
    if (active_test_flow_limited_) {
      snprintf(msg, sizeof(msg), "DONE: %.0fW (conf %.0f%%) - flow limited", id(oq_commissioning_result_w),
               id(oq_commissioning_result_confidence));
    } else {
      snprintf(msg, sizeof(msg), "DONE: %.0fW (conf %.0f%%)", id(oq_commissioning_result_w),
               id(oq_commissioning_result_confidence));
    }
    ESP_LOGI("quatt.cm100.boiler", "Cooldown complete; CM100 idle after boiler test (flow restored, boiler off, %s)",
             msg);
    finish_task(msg, STATE_DONE, true, true);
  }

  void publish_done_status() {
    char msg[128];
    if (active_test_flow_limited_) {
      snprintf(msg, sizeof(msg), "DONE: %.0fW (conf %.0f%%) - flow limited", id(oq_commissioning_result_w),
               id(oq_commissioning_result_confidence));
    } else {
      snprintf(msg, sizeof(msg), "DONE: %.0fW (conf %.0f%%)", id(oq_commissioning_result_w),
               id(oq_commissioning_result_confidence));
    }
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
