#pragma once

#include <algorithm>
#include <math.h>
#include <stdint.h>
#include <string>

namespace oq_request {

enum StrategyCode : int {
  STRATEGY_INACTIVE = 0,
  STRATEGY_COOLING = 1,
  STRATEGY_CURVE = 2,
  STRATEGY_POWER_HOUSE = 3,
  STRATEGY_MANUAL_HP = 4,
};

struct ModeContext {
  bool cooling;
  bool curve;
  bool power_house;
  bool cm_allows_hp;
  int control_flavor_code;
  int thermal_mode_code;
  int strategy_code;
};

struct StrategyRequestInput {
  ModeContext mode;
  bool duo;
  int cooling_hp1;
  int cooling_hp2;
  int cooling_owner;
  int cooling_reason;
  int curve_hp1;
  int curve_hp2;
  int curve_owner;
  int curve_capacity_mode;
  int power_house_hp1;
  int power_house_hp2;
  int power_house_owner;
};

struct StrategyRequest {
  int hp1_level;
  int hp2_level;
  int owner_hp;
  const char* reason;
};

enum ManualReason : int {
  MANUAL_ALLOWED = 0,
  MANUAL_SAFETY_STOP = 1,
  MANUAL_STARTUP_INHIBIT = 2,
  MANUAL_MODE_CONFLICT = 3,
};

struct ManualRequestInput {
  bool duo;
  int hp1_mode_code;
  int hp2_mode_code;
  int hp1_requested_level;
  int hp2_requested_level;
  int hp1_max_level;
  int hp2_max_level;
  int hp1_hold_level;
  int hp2_hold_level;
  bool hp1_cooling_hold;
  bool hp2_cooling_hold;
  bool stop_requested;
  bool safety_stop;
  bool startup_inhibit;
};

struct ManualRequest {
  int mode_code;
  int hp1_level;
  int hp2_level;
  int desired_hp1_level;
  int desired_hp2_level;
  bool mode_allowed;
  bool mode_conflict;
  ManualReason reason;
};

struct CadenceDecision {
  bool due;
  uint32_t dt_ms;
};

struct PublishedRequest {
  int mode_code;
  int hp1_level;
  int hp2_level;
  int owner_hp;
  int topology_code;
  int strategy_code;
};

inline int whole_minutes_floor(float elapsed_min) { return (int)floorf(elapsed_min + 1e-3f); }

inline void reset_dual_hold_state(bool& dual_enabled, float& dual_enable_hold_elapsed_accum_min,
                                  float& dual_disable_hold_elapsed_accum_min,
                                  float& dual_emergency_hold_elapsed_accum_min) {
  dual_enabled = false;
  dual_enable_hold_elapsed_accum_min = 0.0f;
  dual_disable_hold_elapsed_accum_min = 0.0f;
  dual_emergency_hold_elapsed_accum_min = 0.0f;
}

inline void reset_dual_runtime_state(bool& dual_enabled, float& dual_enable_hold_elapsed_accum_min,
                                     float& dual_disable_hold_elapsed_accum_min,
                                     float& dual_emergency_hold_elapsed_accum_min, int& single_owner_hp,
                                     uint32_t& duo_request_hold_until_ms, int owner_hp) {
  reset_dual_hold_state(dual_enabled, dual_enable_hold_elapsed_accum_min, dual_disable_hold_elapsed_accum_min,
                        dual_emergency_hold_elapsed_accum_min);
  single_owner_hp = owner_hp;
  duo_request_hold_until_ms = 0;
}

inline int clamp_level(int level, int min_level, int max_level) {
  return std::max(min_level, std::min(max_level, level));
}

inline bool finite_value_at_least(bool has_state, float value, float minimum) {
  return has_state && isfinite(value) && value >= minimum;
}

inline int minimum_runtime_seconds(bool has_state, float value, int floor_s) {
  constexpr int MAX_SAFE_SECONDS = static_cast<int>(UINT32_MAX / 1000UL);
  const int safe_floor = clamp_level(floor_s, 0, MAX_SAFE_SECONDS);
  if (!has_state || !isfinite(value) || value < static_cast<float>(safe_floor)) return safe_floor;
  return static_cast<int>(roundf(std::min(value, static_cast<float>(MAX_SAFE_SECONDS))));
}

inline ModeContext resolve_mode_context(int control_mode_code, int heat_mode_code) {
  const bool cooling = control_mode_code == 5;
  const bool curve = heat_mode_code == 1 || cooling;
  return {
      cooling,
      curve,
      !cooling && heat_mode_code != 1,
      control_mode_code == 2 || control_mode_code == 3 || cooling,
      cooling ? 2 : heat_mode_code,
      cooling ? 1 : 2,
      cooling ? STRATEGY_COOLING : (heat_mode_code == 1 ? STRATEGY_CURVE : STRATEGY_POWER_HOUSE),
  };
}

inline StrategyRequest select_strategy_request(const StrategyRequestInput& input) {
  if (input.mode.cooling) {
    const int owner = input.cooling_owner;
    const char* reason = "cooling_idle";
    if (input.duo && input.cooling_reason == 1)
      reason = "cooling_owner_hp1";
    else if (input.duo && input.cooling_reason == 2)
      reason = "cooling_owner_hp2";
    else if (!input.duo && owner > 0)
      reason = "cooling_owner_hp1";
    return {input.cooling_hp1, input.duo ? input.cooling_hp2 : 0, owner, reason};
  }
  if (input.mode.power_house) {
    return {input.power_house_hp1, input.duo ? input.power_house_hp2 : 0, input.power_house_owner, nullptr};
  }
  const int hp2_level = input.duo ? input.curve_hp2 : 0;
  const int owner = input.curve_owner;
  const char* reason = "curve_idle";
  if (input.curve_hp1 + hp2_level > 0) {
    if (!input.duo)
      reason = "curve_single_hp1";
    else if (input.curve_capacity_mode == 2)
      reason = "curve_dual";
    else if (owner == 1)
      reason = "curve_single_hp1";
    else if (owner == 2)
      reason = "curve_single_hp2";
    else
      reason = "curve_single";
  }
  return {input.curve_hp1, hp2_level, owner, reason};
}

inline int request_topology_code(int hp1_level, int hp2_level) {
  if (hp1_level > 0 && hp2_level > 0) return 3;
  if (hp1_level > 0) return 1;
  if (hp2_level > 0) return 2;
  return 0;
}

inline int request_owner_from_topology_code(int topology_code) {
  if (topology_code == 1) return 1;
  if (topology_code == 2) return 2;
  return 0;
}

inline int sanitize_request_mode_code(int mode_code) { return (mode_code >= 0 && mode_code <= 2) ? mode_code : 0; }

inline int sanitize_request_strategy_code(int strategy_code) {
  return (strategy_code >= 0 && strategy_code <= 4) ? strategy_code : 0;
}

inline PublishedRequest make_published_request(int mode_code, int hp1_level, int hp2_level, int strategy_code,
                                               int hp1_max_level = 10, int hp2_max_level = 10) {
  hp1_level = clamp_level(hp1_level, 0, std::max(0, hp1_max_level));
  hp2_level = clamp_level(hp2_level, 0, std::max(0, hp2_max_level));
  const int topology_code = request_topology_code(hp1_level, hp2_level);
  return PublishedRequest{
      sanitize_request_mode_code(mode_code),           hp1_level,     hp2_level,
      request_owner_from_topology_code(topology_code), topology_code, sanitize_request_strategy_code(strategy_code),
  };
}

inline bool thermal_mode_matches(float mode_raw, int mode_code) {
  return isfinite(mode_raw) && ((int)roundf(mode_raw) == mode_code);
}

inline bool target_option_matches_mode(bool has_state, const std::string& option, int mode_code) {
  if (!has_state) return false;
  if (mode_code == 1) return option == "Cooling";
  if (mode_code == 2) return option == "Heating";
  if (mode_code == 0) return option == "Standby";
  return false;
}

inline const char* request_mode_option(int request_mode_code) {
  return (request_mode_code == 1) ? "Cooling" : (request_mode_code == 2) ? "Heating" : "Standby";
}

inline const char* retained_mode_name(bool cooling_active, bool heating_active, const char* fallback_mode_name) {
  if (cooling_active) return "Cooling";
  if (heating_active) return "Heating";
  return fallback_mode_name;
}

inline int hold_request_mode_code(int hold1, int hold2, bool hp1_cooling_hold, bool hp2_cooling_hold) {
  if (hold1 <= 0 && hold2 <= 0) return 0;
  return (hp1_cooling_hold || hp2_cooling_hold) ? 1 : 2;
}

inline ManualRequest arbitrate_manual_request(const ManualRequestInput& input) {
  const int hp1_mode = clamp_level(input.hp1_mode_code, 0, 2);
  const int hp2_mode = input.duo ? clamp_level(input.hp2_mode_code, 0, 2) : 0;
  int hp1_level = hp1_mode > 0 ? clamp_level(input.hp1_requested_level, 0, std::max(0, input.hp1_max_level)) : 0;
  int hp2_level = hp2_mode > 0 ? clamp_level(input.hp2_requested_level, 0, std::max(0, input.hp2_max_level)) : 0;
  const bool conflict = hp1_mode > 0 && hp2_mode > 0 && hp1_mode != hp2_mode;
  const int desired_hp1 = (!input.stop_requested && !input.safety_stop && !conflict) ? hp1_level : 0;
  const int desired_hp2 = (!input.stop_requested && !input.safety_stop && !conflict) ? hp2_level : 0;

  if (input.stop_requested) {
    hp1_level = input.hp1_hold_level;
    hp2_level = input.duo ? input.hp2_hold_level : 0;
  }
  if (input.safety_stop || input.startup_inhibit || conflict) {
    hp1_level = 0;
    hp2_level = 0;
  } else if (!input.stop_requested) {
    if (hp1_level == 0) hp1_level = input.hp1_hold_level;
    if (input.duo && hp2_level == 0) hp2_level = input.hp2_hold_level;
  }

  int mode_code = 0;
  if (hp1_level > 0 && hp1_mode > 0) mode_code = hp1_mode;
  if (hp2_level > 0 && hp2_mode > 0) mode_code = hp2_mode;
  if (mode_code == 0 && (hp1_level > 0 || hp2_level > 0)) {
    mode_code = hold_request_mode_code(hp1_level, hp2_level, input.hp1_cooling_hold, input.hp2_cooling_hold);
  }
  const ManualReason reason = input.safety_stop       ? MANUAL_SAFETY_STOP
                              : input.startup_inhibit ? MANUAL_STARTUP_INHIBIT
                              : conflict              ? MANUAL_MODE_CONFLICT
                                                      : MANUAL_ALLOWED;
  return {mode_code,   hp1_level,   hp2_level,
          desired_hp1, desired_hp2, !input.stop_requested && !input.safety_stop && !input.startup_inhibit && !conflict,
          conflict,    reason};
}

inline int defrost_hold_level(bool defrost_active, bool cooling_mode_active, int selected_level,
                              int previous_applied_level) {
  if (!defrost_active) return 0;
  if (cooling_mode_active) return 0;
  if (selected_level > 0) return selected_level;
  if (previous_applied_level > 0) return previous_applied_level;
  return 0;
}

inline uint32_t capped_loop_dt_ms(uint32_t now_ms, uint32_t last_loop_ms, uint32_t base_tick_ms) {
  if (base_tick_ms == 0) return 0;
  if (last_loop_ms == 0) return base_tick_ms;
  const uint32_t dt_cap_ms = base_tick_ms * 2UL;
  const uint32_t dt_ms = now_ms - last_loop_ms;
  return (dt_cap_ms > 0 && dt_ms > dt_cap_ms) ? dt_cap_ms : dt_ms;
}

inline CadenceDecision cadence_decision(uint32_t now_ms, uint32_t last_loop_ms, uint32_t target_ms) {
  if (target_ms == 0 || last_loop_ms == 0) return {true, target_ms};
  const uint32_t elapsed_ms = now_ms - last_loop_ms;
  if (elapsed_ms < target_ms) return {false, 0};
  return {true, capped_loop_dt_ms(now_ms, last_loop_ms, target_ms)};
}

inline bool deadline_pending(uint32_t now_ms, uint32_t deadline_ms) {
  return deadline_ms != 0 && static_cast<int32_t>(deadline_ms - now_ms) > 0;
}

inline int limit_level_slew(int request_level, int previous_level, bool cooling, uint32_t now_ms,
                            uint32_t last_change_ms, uint32_t up_hold_ms, uint32_t down_hold_ms) {
  if (request_level == previous_level) return request_level;
  int limited = request_level;
  if (limited > previous_level + 1) limited = previous_level + 1;
  if (!cooling && limited < previous_level - 1) limited = previous_level - 1;
  if (limited == previous_level || cooling) return limited;
  const uint32_t hold_ms = limited > previous_level ? up_hold_ms : down_hold_ms;
  if (last_change_ms > 0 && static_cast<uint32_t>(now_ms - last_change_ms) < hold_ms) return previous_level;
  return limited;
}

inline void limit_duo_to_one_change(int& hp1_level, int& hp2_level, int hp1_previous, int hp2_previous,
                                    bool lead_is_hp1) {
  const int hp1_delta = hp1_level - hp1_previous;
  const int hp2_delta = hp2_level - hp2_previous;
  if (hp1_delta == 0 || hp2_delta == 0) return;
  bool apply_hp1 = true;
  if (hp1_delta > 0 && hp2_delta > 0)
    apply_hp1 = lead_is_hp1;
  else if (hp1_delta < 0 && hp2_delta < 0)
    apply_hp1 = !lead_is_hp1;
  else
    apply_hp1 = hp1_delta < 0;
  if (apply_hp1)
    hp2_level = hp2_previous;
  else
    hp1_level = hp1_previous;
}

inline bool min_runtime_window_active(uint32_t now_ms, uint32_t last_real_start_ms, uint32_t min_runtime_ms) {
  return last_real_start_ms > 0 && (uint32_t)(now_ms - last_real_start_ms) < min_runtime_ms;
}

inline bool min_runtime_hold_required(int requested_level, bool runtime_hold_blocked, bool runtime_window_active,
                                      int previous_applied_level, bool measured_thermal) {
  return requested_level == 0 && !runtime_hold_blocked && runtime_window_active &&
         (previous_applied_level > 0 || measured_thermal);
}

}  // namespace oq_request
