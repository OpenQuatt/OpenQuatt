#pragma once

#include <algorithm>
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

struct DispatchCandidate {
  bool valid = false;
  int hp1_level = 0;
  int hp2_level = 0;
  float power_w = 0.0f;
  float error_w = 1.0e9f;
  int active_hp_count = 0;
  int balance_gap = 0;
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
  const bool room_values_valid = room_data_fresh && !isnan(room_c) && !isnan(room_sp_c);
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
