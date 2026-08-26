#pragma once

#include <algorithm>
#include <math.h>
#include <stdint.h>

namespace oq_cooling {

enum ReasonCode {
  REASON_INACTIVE = 0,
  REASON_FULL = 1,
  REASON_PROJECTED_FLOOR = 2,
  REASON_SIMMER = 3,
  REASON_FALLING_GAP = 4,
  REASON_BUFFER_STOP = 5,
  REASON_DEW_STOP = 6,
  REASON_FALLBACK_FLOOR = 7,
  REASON_RESTART_WAIT = 8,
  REASON_ROOM_CAP = 9,
  REASON_FALLBACK_CAP1 = 10,
  REASON_LEVEL1_HOLD = 11,
  REASON_OIL_RETURN_HOLD = 12,
  REASON_OIL_RETURN_RECOVERY = 13,
  REASON_CAPACITY_CAP = 14,
};

enum WaterStopReasonCode {
  WATER_STOP_NONE = 0,
  WATER_STOP_LIMITER = 1,
  WATER_STOP_DEW = 2,
  WATER_STOP_FALLBACK = 3,
  WATER_STOP_PROJECTED_FLOOR = 4,
  WATER_STOP_REQUEST_CLEARED = 5,
  WATER_STOP_FLOW_PERMISSION_LOST = 6,
  WATER_STOP_CORE_PERMISSION_LOST = 7,
};

struct WaterCycleState {
  bool active = false;
  float stop_buffer_gap_c = 0.0f;
  int stop_reason_code = WATER_STOP_NONE;
};

inline bool record_pi_zero_stop(int previous_demand, int demand, float filtered_gap_c, WaterCycleState& state) {
  if (!state.active || previous_demand <= 0 || demand > 0) return false;

  state.active = false;
  state.stop_buffer_gap_c = filtered_gap_c;
  if (state.stop_reason_code == WATER_STOP_NONE) state.stop_reason_code = WATER_STOP_LIMITER;
  return true;
}

inline bool water_restart_gap_recovered(const WaterCycleState& state, float filtered_gap_c, float restart_delta_c) {
  if (state.stop_reason_code == WATER_STOP_NONE || state.stop_reason_code == WATER_STOP_PROJECTED_FLOOR) return true;
  return filtered_gap_c >= state.stop_buffer_gap_c + restart_delta_c;
}

inline uint32_t global_minimum_off_time_remaining_ms(bool enabled, uint32_t now_ms, bool stop_seen,
                                                     uint32_t last_stop_ms, bool boot_hold_elapsed,
                                                     uint32_t minimum_off_ms) {
  if (!enabled || minimum_off_ms == 0) return 0;

  if (stop_seen) {
    const uint32_t elapsed_ms = now_ms - last_stop_ms;
    return elapsed_ms >= minimum_off_ms ? 0 : minimum_off_ms - elapsed_ms;
  }

  if (boot_hold_elapsed) return 0;
  return now_ms >= minimum_off_ms ? 0 : minimum_off_ms - now_ms;
}

inline bool cooling_stop_is_planned(bool was_cooling, int previous_applied_level, int requested_level) {
  return was_cooling && previous_applied_level > 0 && requested_level <= 0;
}

inline bool apply_hp2_before_hp1_for_cooling_handover(bool hp1_was_cooling, bool hp2_was_cooling) {
  return !hp1_was_cooling && hp2_was_cooling;
}

inline bool record_confirmed_cooling_stop(bool confirmation_pending, bool stop_confirmed, uint32_t now_ms,
                                          uint32_t& last_confirmed_stop_ms, bool& confirmed_stop_seen) {
  if (!confirmation_pending || !stop_confirmed) return false;
  last_confirmed_stop_ms = now_ms;
  confirmed_stop_seen = true;
  return true;
}

inline bool global_minimum_off_time_blocks_start(uint32_t remaining_ms, bool stop_confirmation_pending,
                                                 bool stop_planned_this_tick, int previous_applied_level) {
  return previous_applied_level <= 0 && (remaining_ms > 0 || stop_confirmation_pending || stop_planned_this_tick);
}

inline bool cooling_minimum_off_stop_is_pending(bool enabled, bool water_cycle_active, int stop_reason_code,
                                                bool cooling_stop_or_wait_active) {
  return enabled && !water_cycle_active && cooling_stop_or_wait_active && stop_reason_code != WATER_STOP_NONE &&
         stop_reason_code != WATER_STOP_REQUEST_CLEARED;
}

struct LimiterTuning {
  float hard_dew_stop_base_gap_c = 0.15f;
  float hard_dew_stop_margin_gain_c = 0.30f;
  float hard_dew_restart_hyst_c = 0.20f;
  float projection_horizon_min = 3.0f;
  float projected_floor_brake_gap_c = 0.25f;
  float projected_floor_release_gap_c = 0.45f;
  float projected_floor_fast_fall_rate_c_per_min = 0.20f;
  float projected_floor_fast_fall_brake_gain = 1.0f;
  float projected_floor_fast_fall_brake_max_c = 0.15f;
  float simmer_enable_gap_c = -0.20f;
  float simmer_disable_gap_c = -0.30f;
  float buffer_hard_stop_gap_c = -0.70f;
  float limiter_soft_stop_gap_c = -0.10f;
  float fallback_full_gap_c = 1.00f;
  float fallback_cap1_gap_c = 0.30f;
  float fallback_stop_gap_c = 0.00f;
  uint32_t limiter_stop_confirm_ms = 30000UL;
  uint32_t capacity_min_hold_ms = 180000UL;
  uint32_t capacity_recovery_stable_ms = 120000UL;
  float capacity_fast_fall_rate_c_per_min = -0.15f;
  float capacity_slow_fall_rate_c_per_min = -0.05f;
  float capacity_recovery_min_rate_c_per_min = -0.02f;
};

struct LimiterState {
  bool simmer_allowed = false;
  bool projection_brake_active = false;
  uint32_t stop_candidate_since_ms = 0;
  uint32_t dew_stop_candidate_since_ms = 0;
  int capacity_cap = 10;
  uint32_t capacity_last_change_ms = 0;
  uint32_t capacity_recovery_since_ms = 0;
  uint32_t capacity_full_recovery_since_ms = 0;
};

struct LimiterInput {
  uint32_t now_ms = 0;
  int demand_max = 1;
  int capacity_demand_max = 1;
  int previous_limited_demand = 0;
  bool room_cap_active = false;
  bool dew_mode = false;
  bool fallback_mode = false;
  bool oil_return_mask_active = false;
  bool oil_return_recovery_cap_active = false;
  float buffer_gap_c = NAN;
  float filtered_gap_c = NAN;
  float gap_rate_c_per_min = 0.0f;
  float dew_gap_c = NAN;
  float safety_margin_c = 0.0f;
};

struct LimiterOutput {
  int allowed_max = 0;
  int reason_code = REASON_INACTIVE;
  float projected_gap_c = NAN;
  bool reset_gap_rate_reference = false;
  bool clamp_integral_to_zero = false;
};

inline const char* reason_name(int reason) {
  static const char* const names[] = {"inactive",        "full",
                                      "projected_floor", "simmer",
                                      "falling_gap",     "buffer_stop",
                                      "dew_stop",        "fallback_floor",
                                      "restart_wait",    "room_cap",
                                      "fallback_cap1",   "level1_hold",
                                      "oil_return_hold", "oil_return_recovery",
                                      "capacity_cap"};
  return reason >= 0 && reason < (int)(sizeof(names) / sizeof(names[0])) ? names[reason] : names[0];
}

inline int clamp_level(int value, int min_value, int max_value) {
  return std::max(min_value, std::min(max_value, value));
}

inline bool finite_float(float value) { return !isnan(value); }

inline bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t duration_ms) {
  return since_ms > 0 && now_ms > since_ms && (uint32_t)(now_ms - since_ms) >= duration_ms;
}

inline void apply_cap(int cap, int reason, int& allowed_cap, int& reason_code) {
  cap = std::max(0, cap);
  if (cap < allowed_cap ||
      (cap == allowed_cap && reason != REASON_FULL && (reason_code == REASON_FULL || reason_code == REASON_ROOM_CAP))) {
    allowed_cap = cap;
    reason_code = reason;
  }
}

inline int dew_capacity_risk_cap(const LimiterInput& in, const LimiterTuning& tuning, float hard_dew_restart_gap_c) {
  int risk_cap = std::max(1, in.capacity_demand_max);
  if (!in.dew_mode || !finite_float(in.dew_gap_c)) return risk_cap;

  const float dew_cap1_gap_c = fmaxf(0.70f, hard_dew_restart_gap_c + 0.10f);
  const float dew_cap2_gap_c = fmaxf(0.95f, hard_dew_restart_gap_c + 0.30f);
  const float dew_cap3_gap_c = fmaxf(1.20f, hard_dew_restart_gap_c + 0.55f);

  if (in.dew_gap_c <= dew_cap1_gap_c)
    risk_cap = std::min(risk_cap, 1);
  else if (in.dew_gap_c <= dew_cap2_gap_c)
    risk_cap = std::min(risk_cap, 2);
  else if (in.dew_gap_c <= dew_cap3_gap_c)
    risk_cap = std::min(risk_cap, 3);

  if (in.gap_rate_c_per_min <= tuning.capacity_fast_fall_rate_c_per_min) {
    if (in.dew_gap_c <= dew_cap2_gap_c)
      risk_cap = std::min(risk_cap, 1);
    else if (in.dew_gap_c <= dew_cap3_gap_c)
      risk_cap = std::min(risk_cap, 2);
  } else if (in.gap_rate_c_per_min <= tuning.capacity_slow_fall_rate_c_per_min && in.dew_gap_c <= dew_cap2_gap_c) {
    risk_cap = std::min(risk_cap, 2);
  }

  return risk_cap;
}

inline void update_hysteretic_capacity_cap(const LimiterInput& in, const LimiterTuning& tuning,
                                           float hard_dew_restart_gap_c, LimiterState& state, LimiterOutput& out) {
  if (in.oil_return_mask_active) {
    state.capacity_recovery_since_ms = 0;
    state.capacity_full_recovery_since_ms = 0;
    return;
  }

  const int capacity_demand_max = std::max(1, in.capacity_demand_max);
  if (!in.dew_mode || !finite_float(in.dew_gap_c)) {
    state.capacity_cap = capacity_demand_max;
    state.capacity_last_change_ms = 0;
    state.capacity_recovery_since_ms = 0;
    state.capacity_full_recovery_since_ms = 0;
    return;
  }

  const int risk_cap = dew_capacity_risk_cap(in, tuning, hard_dew_restart_gap_c);
  int current_cap = state.capacity_cap;
  if (current_cap < 1 || current_cap > capacity_demand_max) current_cap = capacity_demand_max;

  if (risk_cap < current_cap) {
    current_cap = risk_cap;
    state.capacity_last_change_ms = in.now_ms;
    state.capacity_recovery_since_ms = 0;
    state.capacity_full_recovery_since_ms = 0;
    out.clamp_integral_to_zero = true;
  } else if (risk_cap > current_cap && current_cap < capacity_demand_max) {
    const float dew_release_to2_gap_c = fmaxf(1.05f, hard_dew_restart_gap_c + 0.45f);
    const float dew_release_to3_gap_c = fmaxf(1.25f, hard_dew_restart_gap_c + 0.65f);
    const float dew_release_full_gap_c = fmaxf(1.50f, hard_dew_restart_gap_c + 0.90f);
    const float required_dew_gap_c = current_cap <= 1   ? dew_release_to2_gap_c
                                     : current_cap == 2 ? dew_release_to3_gap_c
                                                        : dew_release_full_gap_c;
    const bool staged_recovery_ready =
        in.dew_gap_c >= required_dew_gap_c && in.gap_rate_c_per_min >= tuning.capacity_recovery_min_rate_c_per_min;
    const bool capacity_bound = current_cap < in.demand_max && in.previous_limited_demand >= current_cap;
    const bool adaptive_recovery_ready =
        current_cap >= 3 && capacity_bound && in.gap_rate_c_per_min >= tuning.capacity_recovery_min_rate_c_per_min;
    const bool recovery_ready = staged_recovery_ready || adaptive_recovery_ready;
    const bool full_recovery_ready = staged_recovery_ready && risk_cap >= capacity_demand_max && current_cap >= 3;

    if (recovery_ready) {
      if (state.capacity_recovery_since_ms == 0 || in.now_ms <= state.capacity_recovery_since_ms) {
        state.capacity_recovery_since_ms = in.now_ms;
      }
      if (full_recovery_ready) {
        if (state.capacity_full_recovery_since_ms == 0 || in.now_ms <= state.capacity_full_recovery_since_ms) {
          state.capacity_full_recovery_since_ms = in.now_ms;
        }
      } else {
        state.capacity_full_recovery_since_ms = 0;
      }
      const bool hold_elapsed = state.capacity_last_change_ms == 0 || in.now_ms <= state.capacity_last_change_ms ||
                                (uint32_t)(in.now_ms - state.capacity_last_change_ms) >= tuning.capacity_min_hold_ms;
      if (hold_elapsed && elapsed(in.now_ms, state.capacity_recovery_since_ms, tuning.capacity_recovery_stable_ms)) {
        const bool full_capacity_recovered =
            full_recovery_ready &&
            elapsed(in.now_ms, state.capacity_full_recovery_since_ms, tuning.capacity_recovery_stable_ms);
        const int non_full_capacity_max = current_cap >= 3 ? capacity_demand_max - 1 : capacity_demand_max;
        const int next_cap = full_capacity_recovered
                                 ? capacity_demand_max
                                 : std::min(non_full_capacity_max, std::min(risk_cap, current_cap + 1));
        if (next_cap > current_cap) {
          current_cap = next_cap;
          state.capacity_last_change_ms = in.now_ms;
          state.capacity_recovery_since_ms = 0;
          state.capacity_full_recovery_since_ms = 0;
        }
      }
    } else {
      state.capacity_recovery_since_ms = 0;
      state.capacity_full_recovery_since_ms = 0;
    }
  } else if (current_cap >= capacity_demand_max) {
    state.capacity_recovery_since_ms = 0;
    state.capacity_full_recovery_since_ms = 0;
  } else {
    state.capacity_recovery_since_ms = 0;
    state.capacity_full_recovery_since_ms = 0;
  }

  state.capacity_cap = current_cap;
  if (current_cap < in.demand_max) {
    apply_cap(current_cap, REASON_CAPACITY_CAP, out.allowed_max, out.reason_code);
  }
}

inline LimiterOutput update_limiter(const LimiterInput& in, const LimiterTuning& tuning, LimiterState& state) {
  LimiterOutput out;
  out.allowed_max = std::max(0, in.demand_max);
  out.reason_code = in.room_cap_active ? REASON_ROOM_CAP : REASON_FULL;
  const float hard_dew_stop_gap_c =
      tuning.hard_dew_stop_base_gap_c + (tuning.hard_dew_stop_margin_gain_c * in.safety_margin_c);
  const float hard_dew_restart_gap_c = hard_dew_stop_gap_c + tuning.hard_dew_restart_hyst_c;
  out.projected_gap_c = in.filtered_gap_c + (in.gap_rate_c_per_min * tuning.projection_horizon_min);

  float projected_floor_dynamic_brake_gap_c = tuning.projected_floor_brake_gap_c;
  if (in.gap_rate_c_per_min < -tuning.projected_floor_fast_fall_rate_c_per_min) {
    const float fast_fall_extra_c = ((-in.gap_rate_c_per_min) - tuning.projected_floor_fast_fall_rate_c_per_min) *
                                    tuning.projected_floor_fast_fall_brake_gain;
    projected_floor_dynamic_brake_gap_c +=
        fminf(tuning.projected_floor_fast_fall_brake_max_c, fmaxf(0.0f, fast_fall_extra_c));
  }
  if (in.gap_rate_c_per_min >= 0.0f || out.projected_gap_c >= tuning.projected_floor_release_gap_c) {
    state.projection_brake_active = false;
  } else if (out.projected_gap_c <= projected_floor_dynamic_brake_gap_c) {
    state.projection_brake_active = true;
  }

  if (in.filtered_gap_c > tuning.simmer_enable_gap_c)
    state.simmer_allowed = true;
  else if (in.filtered_gap_c < tuning.simmer_disable_gap_c)
    state.simmer_allowed = false;

  const bool dew_hard_stop_candidate =
      !in.oil_return_mask_active && in.dew_mode && finite_float(in.dew_gap_c) && in.dew_gap_c <= hard_dew_stop_gap_c;
  bool dew_hard_stop = false;
  if (dew_hard_stop_candidate) {
    if (state.dew_stop_candidate_since_ms == 0 || in.now_ms <= state.dew_stop_candidate_since_ms) {
      state.dew_stop_candidate_since_ms = in.now_ms;
    }
    dew_hard_stop = elapsed(in.now_ms, state.dew_stop_candidate_since_ms, tuning.limiter_stop_confirm_ms);
  } else {
    state.dew_stop_candidate_since_ms = 0;
  }

  if (in.fallback_mode) {
    if (in.filtered_gap_c <= tuning.fallback_stop_gap_c) {
      apply_cap(0, REASON_FALLBACK_FLOOR, out.allowed_max, out.reason_code);
    } else if (in.filtered_gap_c <= tuning.fallback_full_gap_c) {
      apply_cap(1, REASON_FALLBACK_CAP1, out.allowed_max, out.reason_code);
    }
  } else {
    const bool dew_gap_allows_simmer =
        !in.dew_mode || !finite_float(in.dew_gap_c) || in.dew_gap_c > hard_dew_restart_gap_c;
    if (dew_hard_stop) {
      apply_cap(0, REASON_DEW_STOP, out.allowed_max, out.reason_code);
    } else if (in.filtered_gap_c < tuning.buffer_hard_stop_gap_c ||
               in.filtered_gap_c <= tuning.limiter_soft_stop_gap_c) {
      apply_cap(0, REASON_BUFFER_STOP, out.allowed_max, out.reason_code);
    } else if (in.filtered_gap_c <= 0.0f) {
      apply_cap((dew_gap_allows_simmer && state.simmer_allowed) ? 1 : 0,
                (dew_gap_allows_simmer && state.simmer_allowed) ? REASON_SIMMER : REASON_BUFFER_STOP, out.allowed_max,
                out.reason_code);
    }
  }

  const bool projection_caution_active = !state.projection_brake_active && in.gap_rate_c_per_min < 0.0f &&
                                         out.projected_gap_c <= tuning.projected_floor_release_gap_c;
  if ((state.projection_brake_active || projection_caution_active) && out.allowed_max > 0) {
    if (in.previous_limited_demand > 1) out.reset_gap_rate_reference = true;
    apply_cap(1, REASON_PROJECTED_FLOOR, out.allowed_max, out.reason_code);
  }

  update_hysteretic_capacity_cap(in, tuning, hard_dew_restart_gap_c, state, out);

  if (in.oil_return_mask_active) {
    out.allowed_max = std::min(out.allowed_max, 1);
    if (out.allowed_max <= 0) {
      out.allowed_max = 1;
      state.stop_candidate_since_ms = 0;
    }
    out.reason_code = REASON_OIL_RETURN_HOLD;
  } else if (in.oil_return_recovery_cap_active && out.allowed_max > 0) {
    out.allowed_max = std::min(out.allowed_max, 1);
    out.reason_code = REASON_OIL_RETURN_RECOVERY;
  }

  const bool immediate_stop = out.reason_code == REASON_DEW_STOP || out.reason_code == REASON_FALLBACK_FLOOR;
  if (!immediate_stop && out.allowed_max <= 0) {
    if (in.buffer_gap_c > 0.0f) {
      out.allowed_max = 1;
      out.reason_code = REASON_LEVEL1_HOLD;
      state.stop_candidate_since_ms = 0;
    } else if (in.filtered_gap_c > tuning.limiter_soft_stop_gap_c) {
      if (state.stop_candidate_since_ms == 0 || in.now_ms <= state.stop_candidate_since_ms) {
        state.stop_candidate_since_ms = in.now_ms;
      }
      if (!elapsed(in.now_ms, state.stop_candidate_since_ms, tuning.limiter_stop_confirm_ms)) {
        out.allowed_max = 1;
        out.reason_code = REASON_LEVEL1_HOLD;
      }
    } else {
      state.stop_candidate_since_ms = 0;
    }
  } else {
    state.stop_candidate_since_ms = 0;
  }

  out.allowed_max = std::max(0, out.allowed_max);
  return out;
}

}  // namespace oq_cooling
