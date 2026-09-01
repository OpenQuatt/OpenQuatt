#pragma once

#ifndef OPENQUATT_OQ_FLOW_CONTROL_LOGIC_H_
#define OPENQUATT_OQ_FLOW_CONTROL_LOGIC_H_

#include <math.h>
#include <stdint.h>

#include "oq_flow_pump_logic.h"

namespace oq_flow_control {

constexpr int kAutoStartFallbackIpwm = 440;

// iPWM limits shared with YAML.
inline int clamp_ipwm(int value) {
  constexpr int kMin = 50;
  constexpr int kMax = 850;
  if (value < kMin) return kMin;
  if (value > kMax) return kMax;
  return value;
}

inline int compute_start_pwm(bool commissioning_start, int commissioning_start_pwm, int last_good_pwm,
                             bool cooling_target, int last_good_pwm_cooling) {
  if (commissioning_start) return clamp_ipwm(commissioning_start_pwm);
  const int candidate = cooling_target ? last_good_pwm_cooling : last_good_pwm;
  if (candidate >= 50 && candidate <= 850) return clamp_ipwm(candidate);
  return clamp_ipwm(kAutoStartFallbackIpwm);
}

inline float select_local_flow(bool secondary_enabled, float hp1_flow_lph, float hp2_flow_lph,
                               float mismatch_threshold_lph) {
  const bool hp1_valid = !isnan(hp1_flow_lph);
  const bool hp2_valid = !isnan(hp2_flow_lph);
  if (!secondary_enabled) return hp1_valid ? hp1_flow_lph : NAN;
  if (hp1_valid && hp2_valid) {
    if (fabsf(hp1_flow_lph - hp2_flow_lph) > mismatch_threshold_lph) {
      return fmaxf(hp1_flow_lph, hp2_flow_lph);
    }
    return (hp1_flow_lph + hp2_flow_lph) / 2.0f;
  }
  if (hp1_valid) return hp1_flow_lph;
  if (hp2_valid) return hp2_flow_lph;
  return NAN;
}

inline bool uses_cooling_setpoint(int control_mode, bool manual_hp_owner, int manual_hp1_mode, int manual_hp2_mode) {
  return control_mode == 5 || (manual_hp_owner && (manual_hp1_mode == 1 || manual_hp2_mode == 1));
}

inline float select_flow_setpoint(bool manual_flow_owner, float manual_setpoint_lph, bool cooling_target,
                                  float cooling_setpoint_lph, float normal_setpoint_lph) {
  if (manual_flow_owner) return manual_setpoint_lph;
  return cooling_target ? cooling_setpoint_lph : normal_setpoint_lph;
}

struct MismatchState {
  bool active = false;
  bool timer_running = false;
  uint32_t started_ms = 0;
};

struct MismatchInputs {
  bool secondary_enabled = false;
  oq_flow::PumpRelayState hp1_pump{};
  oq_flow::PumpRelayState hp2_pump{};
  float hp1_flow_lph = NAN;
  float hp2_flow_lph = NAN;
  float threshold_lph = 0.0f;
  float hysteresis_lph = 0.0f;
  uint32_t now_ms = 0;
  uint32_t hold_ms = 30000;
};

inline bool update_mismatch(MismatchState& state, const MismatchInputs& in) {
  const bool pumps_stopped = oq_flow::all_relevant_pumps_stopped(in.secondary_enabled, in.hp1_pump, in.hp2_pump);
  if (!in.secondary_enabled || pumps_stopped) {
    state = {};
    return false;
  }
  if (isnan(in.hp1_flow_lph) || isnan(in.hp2_flow_lph)) {
    state.active = false;
    state.timer_running = false;
    return false;
  }

  const float difference_lph = fabsf(in.hp1_flow_lph - in.hp2_flow_lph);
  const float off_threshold_lph = fmaxf(0.0f, in.threshold_lph - in.hysteresis_lph);
  if (!state.active) {
    if (difference_lph > in.threshold_lph) {
      if (!state.timer_running) {
        state.timer_running = true;
        state.started_ms = in.now_ms;
      }
      if (static_cast<uint32_t>(in.now_ms - state.started_ms) >= in.hold_ms) {
        state.active = true;
        state.timer_running = false;
      }
    } else {
      state.timer_running = false;
    }
  } else if (difference_lph < off_threshold_lph) {
    state.active = false;
  }
  return state.active;
}

enum class ExecutionMode : uint8_t {
  AUTO,
  AUTO_STARTING,
  AUTO_FAILSAFE,
  MANUAL,
  FROST,
  AUTOTUNE,
  AIR_PURGE,
  QUICK_FLOW_TEST,
  MANUAL_FLOW,
  HP_WATER_CALIBRATION,
  CM0,
  CM100_IDLE,
};

inline const char* execution_mode_text(ExecutionMode mode) {
  switch (mode) {
    case ExecutionMode::AUTO_STARTING:
      return "AUTO (starting)";
    case ExecutionMode::AUTO_FAILSAFE:
      return "AUTO (failsafe)";
    case ExecutionMode::MANUAL:
      return "MANUAL";
    case ExecutionMode::FROST:
      return "CM98";
    case ExecutionMode::AUTOTUNE:
      return "AUTOTUNE";
    case ExecutionMode::AIR_PURGE:
      return "AIR PURGE";
    case ExecutionMode::QUICK_FLOW_TEST:
      return "QUICK FLOW TEST";
    case ExecutionMode::MANUAL_FLOW:
      return "MANUAL FLOW";
    case ExecutionMode::HP_WATER_CALIBRATION:
      return "HP WATER CAL";
    case ExecutionMode::CM0:
      return "CM0";
    case ExecutionMode::CM100_IDLE:
      return "CM100 idle";
    default:
      return "AUTO";
  }
}

struct State {
  float sp_f = NAN;       // filtered setpoint, NAN means not yet seeded
  float integral = 0.0f;  // oq_flow_i
  float last_e = 0.0f;    // last_pi_e
  int startup_hold = 0;   // ticks remaining in AUTO(starting)
  int stable_cnt = 0;
  bool pi_failsafe = false;
};

struct PiInputs {
  float pv = NAN;
  float sp_target = NAN;
  int pwm_seed = 400;
  float kp = 0.03f;
  float ki = 0.0008f;
  float dt = 10.0f;
};

struct PiResult {
  int pwm = 400;
  bool in_startup_hold = false;
  bool failsafe = false;
  bool stable_ready = false;
  float sp_f = NAN;
  float error = 0.0f;
  float integral = 0.0f;
};

// Pure helper that mirrors openquatt/oq_flow_control.yaml:run_auto_pi
// but is fully host-testable. The fix for #464 is that sp_f is kept NAN
// during startup_hold instead of being seeded with a stale/zero pv each
// tick, so the first real PI step seeds sp_f from the actual pv.
inline PiResult update_pi(State& state, const PiInputs& in) {
  PiResult res{};
  res.sp_f = state.sp_f;
  res.integral = state.integral;
  int pwm_local = clamp_ipwm(in.pwm_seed);

  const bool flow_signal_valid = !isnan(in.pv);
  const bool failsafe = (isnan(in.sp_target) || in.sp_target <= 0.0f || !flow_signal_valid);
  state.pi_failsafe = failsafe;
  res.failsafe = failsafe;

  if (failsafe) {
    constexpr int kFailsafeIpwm = 850;
    pwm_local = kFailsafeIpwm;
    state.integral = 0.0f;
    state.last_e = 0.0f;
    state.stable_cnt = 0;
    state.sp_f = NAN;
    res.pwm = pwm_local;
    res.sp_f = NAN;
    res.error = 0.0f;
    res.integral = state.integral;
    return res;
  }

  const bool in_startup_hold = (state.startup_hold > 0);
  res.in_startup_hold = in_startup_hold;
  if (in_startup_hold) {
    // Fix #464: do NOT seed sp_f with a possibly stale pv (e.g. 0 at
    // hydraulic start). Keep NAN so the first post-hold cycle seeds from
    // the actual flow.
    state.startup_hold--;
    state.stable_cnt = 0;
    res.pwm = clamp_ipwm(pwm_local);
    res.sp_f = state.sp_f;  // remains NAN
    return res;
  }

  // Setpoint ramp
  constexpr float sp_ramp_up = 25.0f;
  constexpr float sp_ramp_dn = 15.0f;
  if (isnan(state.sp_f)) {
    state.sp_f = flow_signal_valid ? in.pv : in.sp_target;
  }
  float d = in.sp_target - state.sp_f;
  const float max_step = ((d >= 0.0f) ? sp_ramp_up : sp_ramp_dn) * in.dt;
  if (d > max_step) d = max_step;
  if (d < -max_step) d = -max_step;
  state.sp_f += d;
  res.sp_f = state.sp_f;

  float e = state.sp_f - in.pv;
  constexpr float deadband_lph = 10.0f;
  if (fabsf(e) < deadband_lph) e = 0.0f;
  res.error = e;

  // Anti-windup (mirrors YAML)
  constexpr float i_active_band_lph = 60.0f;
  constexpr float i_cross_keep = 0.25f;
  constexpr float i_deadband_keep = 0.60f;
  constexpr float i_large_err_keep = 0.35f;
  const bool have_e = (e > 0.0f) || (e < 0.0f);
  const bool have_last_e = (state.last_e > 0.0f) || (state.last_e < 0.0f);
  const bool sign_flip = have_e && have_last_e && ((e > 0.0f) != (state.last_e > 0.0f));
  if (sign_flip) {
    state.integral *= i_cross_keep;
  }
  if (!have_e) {
    state.integral *= i_deadband_keep;
  } else if (fabsf(e) <= i_active_band_lph) {
    state.integral += e * in.dt;
  } else {
    state.integral *= i_large_err_keep;
  }
  state.integral = fmaxf(-4000.0f, fminf(4000.0f, state.integral));
  state.last_e = e;
  res.integral = state.integral;

  float u = in.kp * e + in.ki * state.integral;
  constexpr float u_up_s = 12.0f;
  constexpr float u_down_s = 8.0f;
  const float u_up = u_up_s * in.dt;
  const float u_down = u_down_s * in.dt;
  const float u_lim = (e >= 0.0f) ? u_up : u_down;
  if (u > u_lim) u = u_lim;
  if (u < -u_lim) u = -u_lim;

  pwm_local = (int)roundf((float)pwm_local - u);
  pwm_local = clamp_ipwm(pwm_local);
  res.pwm = pwm_local;

  // Stability counter - single source for last_good tracking
  constexpr float stable_err_lph = 15.0f;
  constexpr int stable_time_s = 60;
  const int stable_time_ticks = (stable_time_s <= 0) ? 0 : (int)roundf((float)stable_time_s / in.dt);
  if (state.startup_hold <= 0 && !state.pi_failsafe) {
    const float e_target = in.sp_target - in.pv;
    if (fabsf(e_target) < stable_err_lph) {
      state.stable_cnt++;
    } else {
      state.stable_cnt = 0;
    }
    if (state.stable_cnt >= stable_time_ticks) {
      state.stable_cnt = stable_time_ticks;
    }
  } else {
    state.stable_cnt = 0;
  }
  res.stable_ready = (state.stable_cnt >= stable_time_ticks);

  return res;
}

}  // namespace oq_flow_control

#endif  // OPENQUATT_OQ_FLOW_CONTROL_LOGIC_H_
