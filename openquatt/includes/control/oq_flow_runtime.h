#pragma once

#include <math.h>
#include <stdint.h>

#include "oq_flow_control_logic.h"

namespace oq_flow_runtime {

struct TickConfig {
  float dt_s = 10.0f;
  int startup_hold_s = 20;
  int frost_ipwm = 800;
};

class Runtime {
 public:
  float local_flow(float mismatch_threshold_lph) const {
    float hp1_flow_lph = id(hp1_flow).state;
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    if (id(hp_generation).has_state() && id(hp_generation).current_option() == "V1") {
      hp1_flow_lph = id(flow_rate_controller).state;
    }
#endif
#if OQ_TOPOLOGY_DUO
    const float hp2_flow_lph = id(hp2_flow).state;
    return oq_flow_control::select_local_flow(true, hp1_flow_lph, hp2_flow_lph, mismatch_threshold_lph);
#else
    return oq_flow_control::select_local_flow(false, hp1_flow_lph, NAN, mismatch_threshold_lph);
#endif
  }

  bool flow_mismatch(float threshold_lph, float hysteresis_lph) {
    const oq_flow::PumpRelayState hp1_pump{id(hp1_is_online) && id(hp1_pump_relay).has_state(),
                                           id(hp1_pump_relay).state};
#if OQ_TOPOLOGY_DUO
    const oq_flow::PumpRelayState hp2_pump{id(hp2_is_online) && id(hp2_pump_relay).has_state(),
                                           id(hp2_pump_relay).state};
    const bool secondary_enabled = true;
    const float hp2_flow_lph = id(hp2_flow).state;
#else
    const oq_flow::PumpRelayState hp2_pump{};
    const bool secondary_enabled = false;
    const float hp2_flow_lph = NAN;
#endif
    return oq_flow_control::update_mismatch(
        mismatch_, {secondary_enabled, hp1_pump, hp2_pump, id(hp1_flow).state, hp2_flow_lph, threshold_lph,
                    hysteresis_lph, (uint32_t)millis(), 30000UL});
  }

  void service_tick() {
    if (id(oq_control_mode_code) != 100) return;
    const int task_code = id(oq_commissioning_task_code);
    const bool air_purge = id(oq_air_purge_active) && task_code == oq_commissioning::TASK_AIR_PURGE;
    const bool quick_flow_test = id(oq_quick_flow_test_active) && task_code == oq_commissioning::TASK_MANUAL_FLOW;
    if (!air_purge && !quick_flow_test) return;

    const int pwm = oq_flow_control::clamp_ipwm(air_purge ? (int)id(oq_air_purge_target_ipwm)
                                                          : (int)id(oq_quick_flow_test_target_ipwm));
    set_output_pwm_(pwm);
    write_pumps_(pwm);
  }

  void tick(const TickConfig& config) {
    migrate_cooling_settings_();

    const int cm_code = id(oq_control_mode_code);
    const int task_code = id(oq_commissioning_task_code);
    const bool want_manual = id(oq_flow_control_mode).active_index().value_or(0) == 1;
    const bool cm100_idle = cm_code == 100 && task_code == oq_commissioning::TASK_NONE;
    const bool flow_idle = cm_code == 0 || cm100_idle;
    const bool cm100_task_started =
        cm_code == 100 && task_code != oq_commissioning::TASK_NONE &&
        (last_cm_code_ != 100 || last_task_code_ == oq_commissioning::TASK_NONE || last_task_code_ != task_code);

    if ((was_flow_idle_ && !flow_idle) || cm100_task_started) {
      start_auto_(config, cm100_task_started ? "CM100-task" : "idle->*");
    }
    was_flow_idle_ = flow_idle;
    last_cm_code_ = cm_code;
    last_task_code_ = task_code;

    const bool auto_target_is_cooling = cm_code == 5;
    if (flow_idle) {
      was_manual_ = want_manual;
      last_auto_target_is_cooling_ = auto_target_is_cooling;
      pi_.stable_cnt = 0;
      pi_.startup_hold = 0;
      pi_.sp_f = NAN;
      pi_.last_e = 0.0f;
      publish_mode_(cm100_idle ? oq_flow_control::ExecutionMode::CM100_IDLE : oq_flow_control::ExecutionMode::CM0);
      return;
    }

    const bool frost = cm_code == 98;
    const int fixed_mode = frost ? 2 : 0;
    if (fixed_mode != last_fixed_mode_) {
      reset_pi_();
    }
    if (!want_manual && !frost && auto_target_is_cooling != last_auto_target_is_cooling_) {
      ESP_LOGI("flow", "AUTO target switched to %s flow setpoint", auto_target_is_cooling ? "cooling" : "normal");
      start_auto_(config, auto_target_is_cooling ? "AUTO->cooling" : "AUTO->normal");
    }
    last_auto_target_is_cooling_ = auto_target_is_cooling;
    last_fixed_mode_ = fixed_mode;

    if (id(oq_flow_autotune_active) && cm_code == 100 && task_code == oq_commissioning::TASK_FLOW_AUTOTUNE && !frost) {
      const int pwm = oq_flow_control::clamp_ipwm((int)id(oq_flow_autotune_pwm));
      set_output_pwm_(pwm);
      pi_.stable_cnt = 0;
      pi_.pi_failsafe = false;
      write_pumps_(pwm);
      publish_mode_(oq_flow_control::ExecutionMode::AUTOTUNE);
      return;
    }
    if (id(oq_air_purge_active) && cm_code == 100 && task_code == oq_commissioning::TASK_AIR_PURGE && !frost) {
      set_output_pwm_(oq_flow_control::clamp_ipwm((int)id(oq_air_purge_target_ipwm)));
      pi_.stable_cnt = 0;
      pi_.pi_failsafe = false;
      publish_mode_(oq_flow_control::ExecutionMode::AIR_PURGE);
      return;
    }
    if (id(oq_quick_flow_test_active) && cm_code == 100 && task_code == oq_commissioning::TASK_MANUAL_FLOW && !frost) {
      set_output_pwm_(oq_flow_control::clamp_ipwm((int)id(oq_quick_flow_test_target_ipwm)));
      pi_.stable_cnt = 0;
      pi_.pi_failsafe = false;
      publish_mode_(oq_flow_control::ExecutionMode::QUICK_FLOW_TEST);
      return;
    }

    if (was_manual_ && !want_manual) start_auto_(config, "MANUAL->AUTO");
    was_manual_ = want_manual;

    const bool manual_flow_pi =
        id(oq_manual_flow_active) && cm_code == 100 && task_code == oq_commissioning::TASK_MANUAL_FLOW;
    const bool hp_water_calibration_pi = id(oq_hp_water_calibration_active) && cm_code == 100 &&
                                         task_code == oq_commissioning::TASK_HP_WATER_CALIBRATION;
    int pwm = oq_flow_control::clamp_ipwm((int)id(oq_flow_last_pwm));
    oq_flow_control::ExecutionMode mode = oq_flow_control::ExecutionMode::AUTO;

    if (want_manual && !manual_flow_pi && !hp_water_calibration_pi && !frost) {
      pwm = oq_flow_control::clamp_ipwm((int)roundf(id(oq_flow_manual_pwm).state));
      reset_pi_();
      pi_.pi_failsafe = false;
      mode = oq_flow_control::ExecutionMode::MANUAL;
    } else if (frost) {
      pwm = oq_flow_control::clamp_ipwm(config.frost_ipwm);
      pi_.stable_cnt = 0;
      pi_.pi_failsafe = false;
      pi_.last_e = 0.0f;
      mode = oq_flow_control::ExecutionMode::FROST;
    } else {
      const bool cooling_target = oq_flow_control::uses_cooling_setpoint(
          cm_code, oq_manual_hp::owns_control(), id(oq_manual_hp1_mode_code), id(oq_manual_hp2_mode_code));
      const float setpoint_lph =
          oq_flow_control::select_flow_setpoint(manual_flow_pi, id(oq_manual_flow_setpoint_lph).state, cooling_target,
                                                id(oq_cooling_flow_setpoint_lph).state, id(oq_flow_setpoint_lph).state);
      oq_flow_control::PiInputs inputs{id(flow_rate_selected).state, setpoint_lph,         pwm,
                                       id(oq_flow_kp).state,         id(oq_flow_ki).state, config.dt_s};
      const auto result = oq_flow_control::update_pi(pi_, inputs);
      pwm = result.pwm;
      if (result.failsafe) {
        ESP_LOGW("flow", "Flow PI failsafe engaged: sp=%0.1f pv=%0.1f -> forcing iPWM=%d", setpoint_lph, inputs.pv,
                 pwm);
      }
      track_last_good_(cooling_target, pwm, result.stable_ready);
      if (pi_.pi_failsafe) {
        mode = oq_flow_control::ExecutionMode::AUTO_FAILSAFE;
      } else if (pi_.startup_hold > 0) {
        mode = oq_flow_control::ExecutionMode::AUTO_STARTING;
      } else if (manual_flow_pi) {
        mode = oq_flow_control::ExecutionMode::MANUAL_FLOW;
      } else if (hp_water_calibration_pi) {
        mode = oq_flow_control::ExecutionMode::HP_WATER_CALIBRATION;
      }
    }

    pwm = oq_flow_control::clamp_ipwm(pwm);
    set_output_pwm_(pwm);
    write_pumps_(pwm);
    publish_mode_(mode, true, cm_code, want_manual, pi_.pi_failsafe, pi_.startup_hold, pwm);
  }

 private:
  void migrate_cooling_settings_() {
    if (id(oq_flow_cooling_settings_migrated) || isnan(id(oq_flow_setpoint_lph).state)) return;
    const float setpoint_lph = id(oq_flow_setpoint_lph).state;
    auto call = id(oq_cooling_flow_setpoint_lph).make_call();
    call.set_value(setpoint_lph);
    call.perform();
    id(oq_flow_last_good_pwm_cooling) = id(oq_flow_last_good_pwm);
    id(oq_flow_cooling_settings_migrated) = true;
    ESP_LOGI("flow", "Initialized cooling flow settings from current flow setpoint: sp=%.0f L/h last_good=%d",
             setpoint_lph, (int)id(oq_flow_last_good_pwm_cooling));
  }

  void start_auto_(const TickConfig& config, const char* reason) {
    const int cm_code = id(oq_control_mode_code);
    const bool cooling_target = cm_code == 5;
    const bool commissioning_start = cm_code == 100 && id(oq_commissioning_task_code) != oq_commissioning::TASK_NONE;
    const int last_good = cooling_target ? id(oq_flow_last_good_pwm_cooling) : id(oq_flow_last_good_pwm);
    const int pwm = oq_flow_control::compute_start_pwm(commissioning_start, 400, id(oq_flow_last_good_pwm),
                                                       cooling_target, id(oq_flow_last_good_pwm_cooling));
    ESP_LOGI("flow", "AUTO start(%s): iPWM=%d (last_good=%d bank=%s fallback=%d commissioning=%s hold %ds, fixed iPWM)",
             reason, pwm, last_good, cooling_target ? "cooling" : "normal", oq_flow_control::kAutoStartFallbackIpwm,
             commissioning_start ? "yes" : "no", config.startup_hold_s);
    set_output_pwm_(pwm);
    pi_ = {};
    pi_.startup_hold = config.startup_hold_s <= 0 ? 0 : (int)roundf((float)config.startup_hold_s / config.dt_s);
  }

  void reset_pi_() {
    pi_.integral = 0.0f;
    pi_.sp_f = NAN;
    pi_.last_e = 0.0f;
    pi_.stable_cnt = 0;
  }

  void track_last_good_(bool cooling_target, int pwm, bool stable_ready) {
    if (!stable_ready) return;
    int& last_good = cooling_target ? id(oq_flow_last_good_pwm_cooling) : id(oq_flow_last_good_pwm);
    if (abs(last_good - pwm) >= 10) last_good = pwm;
  }

  static void set_output_pwm_(int pwm) {
    const bool changed = !id(oq_flow_output_ipwm).has_state() || id(oq_flow_last_pwm) != pwm;
    id(oq_flow_last_pwm) = pwm;
    if (changed) id(oq_flow_output_ipwm).publish_state((float)pwm);
  }

  template <typename T>
  static void write_pump_if_changed_(T& number, int pwm) {
    const float current = number.state;
    if (!isnan(current) && fabsf(current - pwm) <= 1.0f) return;
    auto call = number.make_call();
    call.set_value(pwm);
    call.perform();
  }

  static void write_pumps_(int pwm) {
    write_pump_if_changed_(id(hp1_pump_speed), pwm);
#if OQ_TOPOLOGY_DUO
    write_pump_if_changed_(id(hp2_pump_speed), pwm);
#endif
  }

  void publish_mode_(oq_flow_control::ExecutionMode mode, bool log = false, int cm_code = 0, bool manual = false,
                     bool failsafe = false, int hold = 0, int pwm = 0) {
    if (have_published_mode_ && mode == published_mode_) return;
    const char* text = oq_flow_control::execution_mode_text(mode);
    id(oq_flow_mode).publish_state(text);
    if (log) {
      ESP_LOGI("flow", "Flow mode changed: %s (cm=%s manual=%d failsafe=%d hold=%d pwm=%d)", text,
               id(oq_control_mode).state.c_str(), (int)manual, (int)failsafe, hold, pwm);
    }
    published_mode_ = mode;
    have_published_mode_ = true;
  }

  oq_flow_control::State pi_{};
  oq_flow_control::MismatchState mismatch_{};
  bool was_flow_idle_ = true;
  bool was_manual_ = false;
  bool last_auto_target_is_cooling_ = false;
  int last_cm_code_ = 0;
  int last_task_code_ = 0;
  int last_fixed_mode_ = 0;
  bool have_published_mode_ = false;
  oq_flow_control::ExecutionMode published_mode_ = oq_flow_control::ExecutionMode::CM0;
};

inline Runtime& runtime() {
  static Runtime value;
  return value;
}

}  // namespace oq_flow_runtime
