#pragma once

#include <algorithm>
#include <array>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string>

namespace oq_curve {

struct ControlProfileTuning {
  float start_delta_c = 0.45f;
  float stop_delta_c = 0.90f;
  int off_pid_max_f = 1;
  uint32_t off_confirm_ms = 360000UL;
  float room_overheat_off_c = 0.30f;
  float room_resume_heat_c = 0.05f;
  float restart_delta_c = 0.80f;
  float restart_bypass_extra_c = 0.45f;
  uint32_t off_reentry_min_ms = 480000UL;
  float recovery_enter_c = 0.75f;
  float recovery_exit_c = 0.25f;
  float outside_tau_s = 1800.0f;
  float trim_start_c = 0.10f;
  float trim_gain = 1.50f;
  float trim_max_c = 2.00f;
  float quant_step_c = 0.5f;
  int steady_up_hold_s = 180;
  int steady_down_hold_s = 15;
  int recovery_up_hold_s = 45;
  int dual_startup_grace_s = 480;
  int dual_emergency_hold_min = 6;
  float dual_emergency_temp_err_c = 1.50f;
  float dual_disable_temp_err_max_c = 0.50f;
};

enum RestartReason : uint8_t {
  RESTART_NONE = 0,
  RESTART_WATER_BAND = 1,
  RESTART_ROOM_DEMAND = 2,
};

struct RestartDecision {
  bool restart = false;
  bool blocked_by_room = false;
  bool blocked_by_off_lock = false;
  RestartReason reason = RESTART_NONE;
};

enum StopReason : uint8_t {
  STOP_NONE = 0,
  STOP_NORMAL = 1,
  STOP_LOW_LOAD = 2,
};
struct OilReturnDecision {
  uint32_t hold_until_ms = 0;
  bool mask_active = false;
};
struct DemandState {
  bool heat_request_active = false;
  uint32_t stop_arm_ms = 0;
  uint32_t off_since_ms = 0;
  bool restart_inhibit_active = false;
  bool restart_blocked_by_room = false;
  int regime_code = 0;
};
struct DemandInput {
  uint32_t now_ms = 0;
  float pid_output = NAN, supply_target_c = NAN, supply_c = NAN;
  float room_c = NAN, room_setpoint_c = NAN;
  bool room_data_fresh = false;
  bool oil_return_mask_active = false;
  int applied_total_level = 0;
  int demand_max = 0;
};
struct DemandDecision {
  DemandState next;
  float demand_continuous = NAN;
  int demand = 0, demand_pre_guardrail = 0;
  bool valid = false;
  StopReason stop_reason = STOP_NONE;
  RestartReason restart_reason = RESTART_NONE;
};

struct DispatchCandidate {
  bool valid = false;
  int hp1_level = 0;
  int hp2_level = 0;
  float power_w = 0.0f;
  float error_w = 1.0e9f;
  int active_hp_count = 0;
  int balance_gap = 0;
};

struct OutsideEmaState {
  float value_c = NAN;
  bool initialized = false;
  uint32_t last_ms = 0;
};

struct OutsideEmaDecision {
  OutsideEmaState next;
  float value_c = NAN;
};

struct CurvePoint {
  float outside_c = NAN;
  float target_c = NAN;
};

inline ControlProfileTuning control_profile(const std::string& profile_option) {
  ControlProfileTuning tuning;
  if (profile_option == "Comfort") {
    tuning.start_delta_c = 0.30f;
    tuning.stop_delta_c = 0.70f;
    tuning.off_confirm_ms = 240000UL;
    tuning.room_overheat_off_c = 0.20f;
    tuning.room_resume_heat_c = 0.03f;
    tuning.restart_delta_c = 0.60f;
    tuning.restart_bypass_extra_c = 0.35f;
    tuning.off_reentry_min_ms = 300000UL;
    tuning.recovery_enter_c = 0.55f;
    tuning.recovery_exit_c = 0.15f;
    tuning.outside_tau_s = 900.0f;
    tuning.trim_start_c = 0.05f;
    tuning.trim_gain = 2.00f;
    tuning.quant_step_c = 0.25f;
    tuning.steady_up_hold_s = 90;
    tuning.steady_down_hold_s = 10;
    tuning.recovery_up_hold_s = 30;
    tuning.dual_startup_grace_s = 300;
    tuning.dual_emergency_hold_min = 4;
    tuning.dual_emergency_temp_err_c = 1.20f;
    tuning.dual_disable_temp_err_max_c = 0.70f;
  } else if (profile_option == "Stable") {
    tuning.start_delta_c = 0.65f;
    tuning.stop_delta_c = 1.10f;
    tuning.off_confirm_ms = 420000UL;
    tuning.room_overheat_off_c = 0.35f;
    tuning.room_resume_heat_c = 0.08f;
    tuning.restart_delta_c = 1.00f;
    tuning.restart_bypass_extra_c = 0.55f;
    tuning.off_reentry_min_ms = 600000UL;
    tuning.recovery_enter_c = 1.00f;
    tuning.recovery_exit_c = 0.35f;
    tuning.outside_tau_s = 3600.0f;
    tuning.trim_start_c = 0.15f;
    tuning.trim_gain = 1.00f;
    tuning.trim_max_c = 2.50f;
    tuning.quant_step_c = 1.0f;
    tuning.steady_up_hold_s = 300;
    tuning.steady_down_hold_s = 30;
    tuning.recovery_up_hold_s = 75;
    tuning.dual_startup_grace_s = 600;
    tuning.dual_emergency_hold_min = 8;
    tuning.dual_emergency_temp_err_c = 1.80f;
    tuning.dual_disable_temp_err_max_c = 0.40f;
  }

  return tuning;
}

inline RestartDecision evaluate_restart(bool below_restart_band, bool deep_undershoot_restart, bool off_lock_active,
                                        bool room_data_fresh, float room_c, float room_sp_c, float room_overheat_off_c,
                                        float room_resume_heat_c) {
  RestartDecision decision;
  const bool room_values_valid = room_data_fresh && isfinite(room_c) && isfinite(room_sp_c);
  const bool room_requests_heat = room_values_valid && room_c <= (room_sp_c - room_resume_heat_c);
  if (room_requests_heat) {
    decision.restart = true;
    decision.reason = RESTART_ROOM_DEMAND;
    return decision;
  }

  if (!below_restart_band) return decision;

  const bool room_blocks_water_restart = room_values_valid && room_c >= (room_sp_c + room_overheat_off_c);
  if (room_blocks_water_restart) {
    decision.blocked_by_room = true;
    return decision;
  }

  if (off_lock_active && !deep_undershoot_restart) {
    decision.blocked_by_off_lock = true;
    return decision;
  }

  decision.restart = true;
  decision.reason = RESTART_WATER_BAND;
  return decision;
}

inline uint32_t timestamp_ms(uint32_t now_ms) { return now_ms == 0 ? UINT32_MAX : now_ms; }

inline OilReturnDecision update_oil_return_hold(uint32_t now_ms, bool oil_return_active, uint32_t hold_until_ms,
                                                uint32_t hold_ms) {
  if (oil_return_active) {
    uint32_t deadline_ms = now_ms + hold_ms;
    if (deadline_ms == 0) deadline_ms = 1;
    return {deadline_ms, true};
  }
  if (hold_until_ms != 0 && static_cast<int32_t>(hold_until_ms - now_ms) > 0) return {hold_until_ms, true};
  return {};
}

inline OutsideEmaDecision update_outside_ema(uint32_t now_ms, float outside_c, float tau_s,
                                             const OutsideEmaState& state) {
  OutsideEmaDecision out;
  if (!isfinite(outside_c)) return out;
  if (!state.initialized || state.last_ms == 0 || !isfinite(state.value_c) || !isfinite(tau_s) || tau_s <= 0.0f) {
    out.next = {outside_c, true, timestamp_ms(now_ms)};
    out.value_c = outside_c;
    return out;
  }
  const float dt_s = static_cast<float>(static_cast<uint32_t>(now_ms - state.last_ms)) / 1000.0f;
  const float alpha = std::max(0.0f, std::min(1.0f, dt_s / (tau_s + dt_s)));
  out.value_c = state.value_c + alpha * (outside_c - state.value_c);
  out.next = {out.value_c, true, timestamp_ms(now_ms)};
  return out;
}

inline float supply_target(float outside_c, float fallback_c, const std::array<CurvePoint, 6>& points, float room_c,
                           float room_setpoint_c, const ControlProfileTuning& tuning, float max_water_c) {
  float target_c = fallback_c;
  if (isfinite(outside_c)) {
    if (outside_c <= points.front().outside_c)
      target_c = points.front().target_c;
    else if (outside_c >= points.back().outside_c)
      target_c = points.back().target_c;
    else
      for (size_t i = 0; i + 1 < points.size(); ++i)
        if (outside_c >= points[i].outside_c && outside_c <= points[i + 1].outside_c) {
          const float fraction = (outside_c - points[i].outside_c) / (points[i + 1].outside_c - points[i].outside_c);
          target_c = points[i].target_c + fraction * (points[i + 1].target_c - points[i].target_c);
          break;
        }
  }
  if (isfinite(target_c) && isfinite(room_c) && isfinite(room_setpoint_c)) {
    const float warm_error_c = room_c - room_setpoint_c;
    if (warm_error_c > tuning.trim_start_c) {
      const float trim_c =
          std::max(0.0f, std::min(tuning.trim_max_c, (warm_error_c - tuning.trim_start_c) * tuning.trim_gain));
      target_c -= trim_c;
    }
  }
  if (isfinite(target_c) && tuning.quant_step_c > 0.0f)
    target_c = roundf(target_c / tuning.quant_step_c) * tuning.quant_step_c;
  if (isfinite(target_c) && isfinite(max_water_c)) target_c = fminf(target_c, max_water_c);
  return target_c;
}

inline bool cadence_due(uint32_t now_ms, uint32_t last_ms, uint32_t target_ms) {
  return target_ms > 0 && (last_ms == 0 || static_cast<uint32_t>(now_ms - last_ms) >= target_ms);
}

inline bool elapsed_window_active(uint32_t now_ms, uint32_t started_ms, uint32_t duration_ms) {
  return started_ms != 0 && duration_ms > 0 && static_cast<uint32_t>(now_ms - started_ms) < duration_ms;
}

inline DemandDecision decide_demand(const DemandInput& in, const ControlProfileTuning& requested_tuning,
                                    const DemandState& state) {
  DemandDecision out;
  const float tuning_values[] = {
      requested_tuning.start_delta_c,      requested_tuning.stop_delta_c,    requested_tuning.room_overheat_off_c,
      requested_tuning.room_resume_heat_c, requested_tuning.restart_delta_c, requested_tuning.restart_bypass_extra_c,
      requested_tuning.recovery_enter_c,   requested_tuning.recovery_exit_c, requested_tuning.trim_start_c};
  bool tuning_valid = true;
  for (float value : tuning_values) tuning_valid = tuning_valid && isfinite(value);
  if (in.demand_max <= 0 || !isfinite(in.pid_output) || !isfinite(in.supply_target_c) || !isfinite(in.supply_c) ||
      !tuning_valid) {
    return out;
  }

  ControlProfileTuning tuning = requested_tuning;
  tuning.stop_delta_c = std::max(tuning.stop_delta_c, tuning.start_delta_c + 0.10f);
  tuning.restart_delta_c = std::max(tuning.restart_delta_c, tuning.start_delta_c + 0.05f);
  tuning.restart_bypass_extra_c = std::max(tuning.restart_bypass_extra_c, 0.20f);
  tuning.off_pid_max_f = std::max(0, std::min(in.demand_max, tuning.off_pid_max_f));

  float demand_continuous = in.pid_output <= 0.0f   ? 0.0f
                            : in.pid_output >= 1.0f ? static_cast<float>(in.demand_max)
                                                    : in.pid_output * static_cast<float>(in.demand_max);
  int demand = std::max(0, std::min(in.demand_max, static_cast<int>(lroundf(demand_continuous))));
  out.demand_pre_guardrail = demand;
  out.next = state;
  out.next.restart_inhibit_active = out.next.restart_blocked_by_room = false;

  const bool room_valid = in.room_data_fresh && isfinite(in.room_c) && isfinite(in.room_setpoint_c);
  const bool room_warm_for_off = room_valid && in.room_c >= in.room_setpoint_c + tuning.room_overheat_off_c;
  const bool room_allows_off = !room_valid || room_warm_for_off;
  const bool normal_stop = !in.oil_return_mask_active && in.supply_c >= in.supply_target_c + tuning.stop_delta_c &&
                           demand <= tuning.off_pid_max_f && room_allows_off;
  const bool low_load_stop = !in.oil_return_mask_active && state.heat_request_active && state.regime_code == 2 &&
                             out.demand_pre_guardrail <= 0 &&
                             (!room_valid || in.room_c >= in.room_setpoint_c - tuning.room_resume_heat_c) &&
                             in.supply_c >= in.supply_target_c + 0.25f;

  if (out.next.heat_request_active) {
    if (normal_stop || low_load_stop) {
      const uint32_t confirm_ms =
          low_load_stop ? std::min<uint32_t>(tuning.off_confirm_ms, 90000UL) : tuning.off_confirm_ms;
      if (out.next.stop_arm_ms == 0) {
        out.next.stop_arm_ms = timestamp_ms(in.now_ms);
      } else if (static_cast<uint32_t>(in.now_ms - out.next.stop_arm_ms) >= confirm_ms) {
        out.next.heat_request_active = false;
        out.next.stop_arm_ms = 0;
        out.next.off_since_ms = timestamp_ms(in.now_ms);
        out.stop_reason = low_load_stop ? STOP_LOW_LOAD : STOP_NORMAL;
      }
    } else {
      out.next.stop_arm_ms = 0;
    }
  } else {
    out.next.stop_arm_ms = 0;
    const bool off_age_known = out.next.off_since_ms != 0;
    if (!off_age_known) out.next.off_since_ms = timestamp_ms(in.now_ms);
    const bool below_restart_band = in.supply_c <= in.supply_target_c - tuning.restart_delta_c;
    const bool deep_undershoot =
        in.supply_c <= in.supply_target_c - (tuning.restart_delta_c + tuning.restart_bypass_extra_c);
    const bool off_lock_active = off_age_known && tuning.off_reentry_min_ms > 0 &&
                                 static_cast<uint32_t>(in.now_ms - out.next.off_since_ms) < tuning.off_reentry_min_ms;
    const auto restart = evaluate_restart(below_restart_band, deep_undershoot, off_lock_active, room_valid, in.room_c,
                                          in.room_setpoint_c, tuning.room_overheat_off_c, tuning.room_resume_heat_c);
    out.next.restart_inhibit_active = restart.blocked_by_off_lock;
    out.next.restart_blocked_by_room = restart.blocked_by_room;
    out.restart_reason = restart.reason;
    if (restart.restart) {
      out.next.heat_request_active = true;
      out.next.off_since_ms = 0;
    }
  }

  if (!out.next.heat_request_active) {
    demand = 0;
    out.next.regime_code = 0;
  } else {
    out.next.restart_inhibit_active = false;
    out.next.restart_blocked_by_room = false;
    out.next.off_since_ms = 0;
    demand = std::max(1, demand);
    const float supply_error_c = in.supply_target_c - in.supply_c;
    int regime = out.next.regime_code;
    if (regime != 1 && regime != 2) regime = supply_error_c >= tuning.recovery_enter_c ? 1 : 2;
    if (regime == 1 && supply_error_c <= tuning.recovery_exit_c) {
      regime = 2;
    } else if (regime == 2 && !in.oil_return_mask_active && supply_error_c >= tuning.recovery_enter_c && demand >= 8 &&
               (!room_valid || in.room_c <= in.room_setpoint_c + tuning.trim_start_c)) {
      regime = 1;
    }
    out.next.regime_code = regime;
    if (regime == 1) {
      demand = std::max(demand, std::min(in.demand_max, std::max(2, in.applied_total_level + 1)));
    } else {
      static constexpr float thresholds[] = {0.00f, 0.10f, 0.20f, 0.35f, 0.50f, 0.70f, 0.90f};
      static constexpr int caps[] = {1, 2, 3, 4, 5, 6, 8};
      for (int i = 0; i < 7; ++i)
        if (supply_error_c <= thresholds[i]) {
          demand = std::min(demand, caps[i]);
          break;
        }
    }
  }

  out.demand_continuous = !out.next.heat_request_active ? 0.0f
                          : out.next.regime_code == 1   ? std::max(demand_continuous, static_cast<float>(demand))
                                                        : std::min(demand_continuous, static_cast<float>(demand));
  out.demand = demand;
  out.valid = true;
  return out;
}

inline void reset_control_state(float& demand_continuous, int& demand_curve, int& demand_pre_guardrail,
                                bool& heat_request_active, uint32_t& stop_arm_ms, uint32_t& off_since_ms,
                                bool& restart_inhibit_active, bool& restart_blocked_by_room, int& regime_code) {
  demand_continuous = NAN;
  demand_curve = 0;
  demand_pre_guardrail = 0;
  heat_request_active = false;
  stop_arm_ms = 0;
  off_since_ms = 0;
  restart_inhibit_active = false;
  restart_blocked_by_room = false;
  regime_code = 0;
}

inline void reset_outside_ema_state(float& outside_ema_c, bool& outside_ema_initialized,
                                    uint32_t& outside_ema_last_ms) {
  outside_ema_c = 0.0f;
  outside_ema_initialized = false;
  outside_ema_last_ms = 0;
}

inline void reset_request_state(uint32_t& request_last_loop_ms, int& request_total_level, int& request_owner_hp,
                                int& dispatch_hp1_level, int& dispatch_hp2_level, int& capacity_mode_code) {
  request_last_loop_ms = 0;
  request_total_level = 0;
  request_owner_hp = 0;
  dispatch_hp1_level = 0;
  dispatch_hp2_level = 0;
  capacity_mode_code = 0;
}

inline DispatchCandidate invalid_dispatch_candidate() { return DispatchCandidate{}; }

inline float power_capped_demand_u(float demand, int cap_f, int max_f) {
  if (max_f <= 0 || cap_f <= 0 || !isfinite(demand)) return 0.0f;  // Invalid continuous demand fails closed.
  return std::max(0.0f, std::min({demand, (float)cap_f, (float)max_f})) / (float)max_f;
}

inline float phase_target_power_w(bool heat_phase, float capped_demand_u, float single_cap_w, float duo_cap_w) {
  if (!isfinite(capped_demand_u) || capped_demand_u <= 0.0f) return 0.0f;
  const float single_target_w = isfinite(single_cap_w) ? (single_cap_w * capped_demand_u) : 0.0f;
  const float duo_target_w = isfinite(duo_cap_w) ? (duo_cap_w * capped_demand_u) : single_target_w;
  return heat_phase ? single_target_w : duo_target_w;
}

inline int pick_single_owner(bool demand_active, int stored_owner_hp, bool prev_hp1_on, bool prev_hp2_on,
                             bool lead_is_hp1) {
  if (!demand_active) return 0;
  if (prev_hp1_on != prev_hp2_on) return prev_hp1_on ? 1 : 2;
  if (stored_owner_hp == 1 || stored_owner_hp == 2) return stored_owner_hp;
  return lead_is_hp1 ? 1 : 2;
}

inline bool better_dispatch_candidate(const DispatchCandidate& candidate, const DispatchCandidate& best,
                                      int prev_hp1_level, int prev_hp2_level) {
  if (!candidate.valid) return false;
  if (!best.valid) return true;
  if (fabsf(candidate.error_w - best.error_w) > 50.0f) return candidate.error_w < best.error_w;

  const int candidate_starts = ((prev_hp1_level == 0) && (candidate.hp1_level > 0) ? 1 : 0) +
                               ((prev_hp2_level == 0) && (candidate.hp2_level > 0) ? 1 : 0);
  const int best_starts =
      ((prev_hp1_level == 0) && (best.hp1_level > 0) ? 1 : 0) + ((prev_hp2_level == 0) && (best.hp2_level > 0) ? 1 : 0);
  if (candidate_starts != best_starts) return candidate_starts < best_starts;

  const int candidate_moves = abs(candidate.hp1_level - prev_hp1_level) + abs(candidate.hp2_level - prev_hp2_level);
  const int best_moves = abs(best.hp1_level - prev_hp1_level) + abs(best.hp2_level - prev_hp2_level);
  if (candidate_moves != best_moves) return candidate_moves < best_moves;

  if (candidate.active_hp_count != best.active_hp_count) {
    return candidate.active_hp_count < best.active_hp_count;
  }
  if (candidate.balance_gap != best.balance_gap) return candidate.balance_gap < best.balance_gap;
  return candidate.power_w < best.power_w;
}

inline const char* regime_name(int regime_code) {
  switch (regime_code) {
    case 1:
      return "recovery";
    case 2:
      return "maintain";
    default:
      return "off";
  }
}

inline const char* capacity_mode_name(int capacity_mode_code) {
  switch (capacity_mode_code) {
    case 1:
      return "single";
    case 2:
      return "duo";
    default:
      return "off";
  }
}

}  // namespace oq_curve
