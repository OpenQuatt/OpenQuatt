#pragma once
#include <algorithm>
#include <math.h>
#include <stdint.h>
#include "oq_cooling_limiter_logic.h"
namespace oq_cooling {
enum GuardMode { GUARD_NONE = 0, GUARD_DEW = 1, GUARD_FALLBACK = 2, GUARD_USER_RESPONSIBILITY = 3 };
struct DemandTuning {
  float filter_tau_s = 60.0f;
  uint32_t rate_window_ms = 60000UL, upscale_dwell_ms = 90000UL, downscale_dwell_ms = 30000UL;
  float simmer_room_error_min_c = 0.20f;
  LimiterTuning limiter;
  OilReturnTuning oil_return;
};
struct DemandInput {
  uint32_t now_ms = 0;
  bool control_active = false, cycle_was_active = false, request_cleared = false, sensor_valid = false;
  bool flow_permission_lost = false, core_permission_lost = false, oil_return_active = false;
  bool cooling_hp_applied = false, restart_by_minimum_off_time = false, minimum_off_stop_pending = false;
  bool minimum_off_wait_active = false;
  GuardMode guard_mode = GUARD_NONE;
  bool dew_mode = false, fallback_mode = false, room_error_valid = false;
  float supply_c = NAN, target_c = NAN, dew_point_c = NAN, safety_margin_c = 0.0f, room_error_c = NAN;
  float restart_delta_c = 1.0f, demand_max = 1.0f, kp = 0.0f, ki = 0.0f, kd = 0.0f;
};
struct GapFilterState {
  bool ready = false, rate_reference_seen = false;
  float filtered_gap_c = 0.0f, rate_reference_c = 0.0f, rate_c_per_min = 0.0f;
  uint32_t rate_reference_ms = 0;
};
struct DemandState {
  bool loop_seen = false, demand_change_seen = false, guard_seen = false;
  uint32_t last_loop_ms = 0, last_demand_change_ms = 0;
  GuardMode last_guard_mode = GUARD_NONE;
  float integral = 0.0f, last_error_c = 0.0f, dew_gap_c = NAN, projected_gap_c = NAN;
  int base_demand = 0, limited_demand = 0, allowed_max = 0, limiter_reason_code = REASON_INACTIVE;
  GapFilterState filter;
  LimiterState limiter;
  OilReturnState oil_return;
  WaterCycleState water_cycle;
};
struct DemandOutput {
  bool control_active = false, guard_changed = false, arm_minimum_off_stop = false, pi_zero_stop = false;
  bool event_limiter_active = false, event_hard_pause = false;
};
inline bool demand_finite(float value) { return isfinite(value); }
inline int sanitize_demand_max(float value) {
  return demand_finite(value) ? (int)lroundf(std::max(1.0f, std::min(10.0f, value))) : 1;
}
inline float sanitize_gain(float value, float maximum) {
  return demand_finite(value) ? std::max(0.0f, std::min(maximum, value)) : 0.0f;
}
inline float sanitize_restart_delta(float value) {
  return !demand_finite(value) || value < 0.0f ? 1.0f : std::min(5.0f, value);
}
inline float demand_loop_dt_s(uint32_t now_ms, DemandState& state) {
  float dt_s = 5.0f;
  if (state.loop_seen) {
    const uint32_t elapsed_ms = now_ms - state.last_loop_ms;
    if (elapsed_ms > 0) dt_s = (float)elapsed_ms / 1000.0f;
  }
  state.loop_seen = true;
  state.last_loop_ms = now_ms;
  return std::max(0.1f, std::min(15.0f, dt_s));
}
inline void reset_limiter_state(int capacity_cap, DemandState& state) {
  state.limiter = {};
  state.limiter.capacity_cap = capacity_cap;
}
inline DemandOutput reset_inactive_demand(const DemandInput& in, const DemandTuning& tuning, bool inputs_valid,
                                          DemandState& state, bool arm_minimum_off_stop = false) {
  state.integral = state.last_error_c = 0.0f;
  state.base_demand = state.limited_demand = state.allowed_max = 0;
  state.limiter_reason_code = REASON_INACTIVE;
  state.water_cycle.active = false;
  state.water_cycle.stop_reason_code = inactive_stop_reason(in.cycle_was_active, in.request_cleared, !inputs_valid,
                                                            in.flow_permission_lost, in.core_permission_lost);
  state.filter = {};
  state.dew_gap_c = state.projected_gap_c = NAN;
  state.demand_change_seen = false;
  update_oil_return({in.now_ms, false, in.oil_return_active}, {}, tuning.oil_return, tuning.limiter, state.oil_return);
  reset_limiter_state(sanitize_demand_max(in.demand_max), state);
  return {false, false, arm_minimum_off_stop};
}
inline void reset_for_guard_change(float buffer_gap_c, DemandState& state) {
  state.water_cycle = {};
  state.filter.ready = state.filter.rate_reference_seen = false;
  state.projected_gap_c = NAN;
  state.demand_change_seen = false;
  reset_oil_return_recovery(state.oil_return);
  reset_limiter_state(10, state);
  state.integral = 0.0f;
  state.last_error_c = buffer_gap_c;
}
inline bool update_gap_filter(float buffer_gap_c, float dt_s, uint32_t now_ms, const DemandTuning& tuning,
                              DemandState& state) {
  auto& filter = state.filter;
  if (!filter.ready || !demand_finite(filter.filtered_gap_c)) {
    filter = {true, true, buffer_gap_c, buffer_gap_c, 0.0f, now_ms};
    return true;
  }
  const float tau_s = demand_finite(tuning.filter_tau_s) && tuning.filter_tau_s > 0.0f ? tuning.filter_tau_s : 60.0f;
  const float alpha = dt_s / (tau_s + dt_s);
  filter.filtered_gap_c += alpha * (buffer_gap_c - filter.filtered_gap_c);
  if (!demand_finite(filter.filtered_gap_c)) return false;
  if (!filter.rate_reference_seen || !demand_finite(filter.rate_reference_c) || !demand_finite(filter.rate_c_per_min)) {
    filter.rate_reference_seen = true;
    filter.rate_reference_c = filter.filtered_gap_c;
    filter.rate_reference_ms = now_ms;
    filter.rate_c_per_min = 0.0f;
    return true;
  }
  const uint32_t window_ms = tuning.rate_window_ms > 0 ? tuning.rate_window_ms : 60000UL;
  const uint32_t elapsed_ms = now_ms - filter.rate_reference_ms;
  if (elapsed_ms < window_ms) return true;
  const float elapsed_min = (float)elapsed_ms / 60000.0f;
  filter.rate_c_per_min = (filter.filtered_gap_c - filter.rate_reference_c) / elapsed_min;
  filter.rate_reference_c = filter.filtered_gap_c;
  filter.rate_reference_ms = now_ms;
  return demand_finite(filter.rate_c_per_min);
}
inline int apply_demand_dwell(int candidate, int allowed_max, uint32_t now_ms, const DemandTuning& tuning,
                              DemandState& state) {
  const int previous = state.limited_demand;
  const uint32_t elapsed_ms = state.demand_change_seen ? now_ms - state.last_demand_change_ms : UINT32_MAX;
  int demand = candidate;
  if (previous > 0 && allowed_max < previous) {
    demand = std::min(candidate, allowed_max);
  } else if (candidate > previous) {
    if (previous == 0)
      demand = std::min(candidate, 1);
    else if (!state.demand_change_seen || elapsed_ms >= tuning.upscale_dwell_ms)
      demand = std::min(candidate, previous + 1);
    else
      demand = previous;
  } else if (candidate < previous) {
    demand = !state.demand_change_seen || elapsed_ms >= tuning.downscale_dwell_ms ? candidate : previous;
  }
  demand = std::max(0, std::min(allowed_max, demand));
  if (demand != previous) {
    state.demand_change_seen = true;
    state.last_demand_change_ms = now_ms;
  }
  return demand;
}
inline DemandOutput update_demand(const DemandInput& in, const DemandTuning& tuning, DemandState& state) {
  const float dt_s = demand_loop_dt_s(in.now_ms, state);
  const bool valid_guard = (in.guard_mode == GUARD_DEW && in.dew_mode && !in.fallback_mode) ||
                           (in.guard_mode == GUARD_FALLBACK && !in.dew_mode && in.fallback_mode) ||
                           (in.guard_mode == GUARD_USER_RESPONSIBILITY && !in.dew_mode && !in.fallback_mode);
  const bool valid_config = demand_finite(in.restart_delta_c) && demand_finite(in.demand_max) && demand_finite(in.kp) &&
                            demand_finite(in.ki) && demand_finite(in.kd) &&
                            (!in.dew_mode || demand_finite(in.safety_margin_c));
  const bool finite_sensors = in.sensor_valid && demand_finite(in.supply_c) && demand_finite(in.target_c) &&
                              (!in.dew_mode || demand_finite(in.dew_point_c)) &&
                              (!in.room_error_valid || demand_finite(in.room_error_c));
  const float buffer_gap_c = finite_sensors ? in.supply_c - in.target_c : NAN;
  const float dew_gap_c = finite_sensors && in.dew_mode ? in.supply_c - in.dew_point_c : NAN;
  const bool inputs_valid = valid_guard && valid_config && finite_sensors && demand_finite(buffer_gap_c) &&
                            (!in.dew_mode || demand_finite(dew_gap_c));
  if (!in.control_active || !inputs_valid) return reset_inactive_demand(in, tuning, inputs_valid, state);
  bool arm_minimum_off_stop =
      cooling_minimum_off_stop_is_pending(in.restart_by_minimum_off_time, state.water_cycle.active,
                                          state.water_cycle.stop_reason_code, in.minimum_off_wait_active);
  bool guard_changed = false;
  if (state.guard_seen && in.guard_mode != state.last_guard_mode) {
    reset_for_guard_change(buffer_gap_c, state);
    guard_changed = true;
  }
  state.guard_seen = true;
  state.last_guard_mode = in.guard_mode;
  if (!update_gap_filter(buffer_gap_c, dt_s, in.now_ms, tuning, state)) {
    return reset_inactive_demand(in, tuning, false, state, arm_minimum_off_stop);
  }
  const float filtered_gap_c = state.filter.filtered_gap_c;
  const float gap_rate_c_per_min = state.filter.rate_c_per_min;
  const float projected_gap_c = filtered_gap_c + gap_rate_c_per_min * tuning.limiter.projection_horizon_min;
  if (!demand_finite(projected_gap_c)) return reset_inactive_demand(in, tuning, false, state, arm_minimum_off_stop);
  state.dew_gap_c = dew_gap_c;
  const float safety_margin_c =
      demand_finite(in.safety_margin_c) ? std::max(0.0f, std::min(2.0f, in.safety_margin_c)) : 0.0f;
  const float kp = sanitize_gain(in.kp, 10.0f);
  const float ki = sanitize_gain(in.ki, 2.0f);
  const float kd = sanitize_gain(in.kd, 2.0f);
  if (!demand_finite(state.integral)) state.integral = 0.0f;
  float derivative = 0.0f;
  if (demand_finite(state.last_error_c)) derivative = (buffer_gap_c - state.last_error_c) / dt_s;
  if (!demand_finite(derivative)) return reset_inactive_demand(in, tuning, false, state, arm_minimum_off_stop);
  int demand_max = sanitize_demand_max(in.demand_max);
  const int capacity_demand_max = demand_max;
  const bool room_error_valid = in.room_error_valid && demand_finite(in.room_error_c);
  bool room_cap_active = false;
  if (room_error_valid) {
    const int before_room_cap = demand_max;
    if (in.room_error_c < 0.4f)
      demand_max = std::min(demand_max, 1);
    else if (in.room_error_c < 0.8f)
      demand_max = std::min(demand_max, 2);
    else if (in.room_error_c < 1.2f)
      demand_max = std::min(demand_max, 3);
    room_cap_active = demand_max < before_room_cap;
  }
  LimiterInput limiter_input{in.now_ms,       demand_max,     capacity_demand_max, state.limited_demand,
                             room_cap_active, in.dew_mode,    in.fallback_mode,    false,
                             false,           buffer_gap_c,   filtered_gap_c,      gap_rate_c_per_min,
                             state.dew_gap_c, safety_margin_c};
  const auto oil_return = update_oil_return({in.now_ms, true, in.oil_return_active}, limiter_input, tuning.oil_return,
                                            tuning.limiter, state.oil_return);
  limiter_input.oil_return_mask_active = oil_return.mask_active;
  limiter_input.oil_return_recovery_cap_active = oil_return.recovery_cap_active;
  if (oil_return.reset_integral_and_dwell) {
    state.integral = 0.0f;
    state.demand_change_seen = true;
    state.last_demand_change_ms = in.now_ms;
  } else if (oil_return.mask_active || oil_return.recovery_cap_active) {
    state.integral = fminf(state.integral, 0.0f);
  }
  const auto limiter = update_limiter(limiter_input, tuning.limiter, state.limiter);
  if (limiter.reset_gap_rate_reference) {
    state.filter.rate_reference_seen = true;
    state.filter.rate_reference_c = filtered_gap_c;
    state.filter.rate_reference_ms = in.now_ms;
  }
  if (limiter.clamp_integral_to_zero) state.integral = fminf(state.integral, 0.0f);
  state.projected_gap_c = limiter.projected_gap_c;
  state.allowed_max = limiter.allowed_max;
  state.limiter_reason_code = limiter.reason_code;
  arm_minimum_off_stop |=
      guard_changed && in.restart_by_minimum_off_time && in.cooling_hp_applied && state.allowed_max <= 0;
  const bool effective_minimum_off_stop_pending = in.minimum_off_stop_pending || arm_minimum_off_stop;
  if (!state.water_cycle.active) {
    const auto restart = evaluate_water_restart(in.restart_by_minimum_off_time, effective_minimum_off_stop_pending,
                                                sanitize_restart_delta(in.restart_delta_c), limiter_input, limiter,
                                                tuning.limiter, state.limiter, state.water_cycle);
    state.water_cycle = restart.state;
    if (!state.water_cycle.active) {
      state.base_demand = state.limited_demand = 0;
      state.limiter_reason_code = effective_minimum_off_stop_pending && limiter.allowed_max > 0
                                      ? REASON_RESTART_WAIT
                                      : restart.limiter_reason_code;
      state.last_error_c = buffer_gap_c;
      if (restart.reset_integral) state.integral = 0.0f;
      return {true, guard_changed, arm_minimum_off_stop, false, true, true};
    }
  }
  if (state.allowed_max <= 0) {
    arm_minimum_off_stop |= in.restart_by_minimum_off_time && state.water_cycle.active && in.cooling_hp_applied;
    state.base_demand = state.limited_demand = 0;
    state.water_cycle.active = false;
    state.water_cycle.stop_buffer_gap_c = filtered_gap_c;
    state.water_cycle.stop_reason_code = state.limiter_reason_code == REASON_DEW_STOP         ? WATER_STOP_DEW
                                         : state.limiter_reason_code == REASON_FALLBACK_FLOOR ? WATER_STOP_FALLBACK
                                                                                              : WATER_STOP_LIMITER;
    state.last_error_c = buffer_gap_c;
    if (state.limiter_reason_code == REASON_DEW_STOP || state.limiter_reason_code == REASON_FALLBACK_FLOOR)
      state.integral = 0.0f;
    else if (state.limiter_reason_code != REASON_PROJECTED_FLOOR)
      state.integral = fminf(state.integral, 0.0f);
    return {true, guard_changed, arm_minimum_off_stop, false, true, true};
  }
  const float before_integral = kp * buffer_gap_c + ki * state.integral + kd * derivative;
  if (!demand_finite(before_integral)) return reset_inactive_demand(in, tuning, false, state, arm_minimum_off_stop);
  if (!(before_integral > (float)state.allowed_max && buffer_gap_c > 0.0f)) state.integral += buffer_gap_c * dt_s;
  state.integral = demand_finite(state.integral) ? std::max(-20.0f, std::min(20.0f, state.integral)) : 0.0f;
  const float controller_output = kp * buffer_gap_c + ki * state.integral + kd * derivative;
  if (!demand_finite(controller_output)) return reset_inactive_demand(in, tuning, false, state, arm_minimum_off_stop);
  state.base_demand = controller_output <= 0.0f                ? 0
                      : controller_output >= (float)demand_max ? demand_max
                                                               : (int)lroundf(controller_output);
  int candidate = std::min(state.base_demand, state.allowed_max);
  const bool oil_level1_hold = (state.limiter_reason_code == REASON_OIL_RETURN_HOLD ||
                                state.limiter_reason_code == REASON_OIL_RETURN_RECOVERY) &&
                               state.allowed_max >= 1;
  const bool can_simmer = oil_level1_hold || ((!room_error_valid || in.room_error_c > tuning.simmer_room_error_min_c) &&
                                              state.allowed_max >= 1 &&
                                              (state.limiter_reason_code == REASON_SIMMER ||
                                               state.limiter_reason_code == REASON_FALLING_GAP || in.fallback_mode));
  if (can_simmer && candidate < 1) {
    candidate = 1;
    if (state.limiter_reason_code != REASON_FALLING_GAP && state.limiter_reason_code != REASON_FALLBACK_CAP1 &&
        state.limiter_reason_code != REASON_OIL_RETURN_HOLD && state.limiter_reason_code != REASON_OIL_RETURN_RECOVERY)
      state.limiter_reason_code = REASON_SIMMER;
  }
  const int previous = state.limited_demand;
  state.limited_demand = apply_demand_dwell(candidate, state.allowed_max, in.now_ms, tuning, state);
  const bool pi_zero_stop = record_pi_zero_stop(previous, state.limited_demand, filtered_gap_c, state.water_cycle);
  if (pi_zero_stop) {
    arm_minimum_off_stop |= in.restart_by_minimum_off_time && in.cooling_hp_applied;
    state.limiter_reason_code = REASON_BUFFER_STOP;
    state.integral = fminf(state.integral, 0.0f);
  }
  state.last_error_c = buffer_gap_c;
  return {true,        guard_changed, arm_minimum_off_stop, pi_zero_stop, state.limiter_reason_code != REASON_FULL,
          pi_zero_stop};
}
class DemandRuntime {
 public:
  DemandOutput tick(const DemandInput& input, const DemandTuning& tuning) {
    return update_demand(input, tuning, state_);
  }
  const DemandState& state() const { return state_; }

 private:
  DemandState state_;
};
inline DemandRuntime& demand_runtime() {
  static DemandRuntime runtime;
  return runtime;
}
}  // namespace oq_cooling
