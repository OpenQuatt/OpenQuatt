#pragma once

#include <algorithm>
#include <math.h>
#include <stdint.h>
#include <string>

namespace oq_request {

constexpr int NORMAL_MAX_COMPRESSOR_LEVEL = 10;
constexpr int MANUAL_HP_MAX_COMPRESSOR_LEVEL = 20;

inline int manual_hp_max_compressor_level(bool is_v2) {
  return is_v2 ? MANUAL_HP_MAX_COMPRESSOR_LEVEL : NORMAL_MAX_COMPRESSOR_LEVEL;
}

struct ExcludedLevels {
  std::string a;
  std::string b;
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
                                               int manual_hp_max_level = NORMAL_MAX_COMPRESSOR_LEVEL) {
  const int sanitized_strategy_code = sanitize_request_strategy_code(strategy_code);
  const int request_max_level =
      sanitized_strategy_code == 4
          ? clamp_level(manual_hp_max_level, NORMAL_MAX_COMPRESSOR_LEVEL, MANUAL_HP_MAX_COMPRESSOR_LEVEL)
          : NORMAL_MAX_COMPRESSOR_LEVEL;
  hp1_level = clamp_level(hp1_level, 0, request_max_level);
  hp2_level = clamp_level(hp2_level, 0, request_max_level);
  const int topology_code = request_topology_code(hp1_level, hp2_level);
  return PublishedRequest{
      sanitize_request_mode_code(mode_code),           hp1_level,     hp2_level,
      request_owner_from_topology_code(topology_code), topology_code, sanitized_strategy_code,
  };
}

inline bool excluded_option_matches_level(const std::string& opt, int level) {
  switch (level) {
    case 1:
      return opt == "L1 (H30/C30)";
    case 2:
      return opt == "L2 (H39/C36)";
    case 3:
      return opt == "L3 (H49/C42)";
    case 4:
      return opt == "L4 (H55/C47)";
    case 5:
      return opt == "L5 (H61/C52)";
    case 6:
      return opt == "L6 (H67/C56)";
    case 7:
      return opt == "L7 (H72/C61)";
    case 8:
      return opt == "L8 (H79/C66)";
    case 9:
      return opt == "L9 (H85/C71)";
    case 10:
      return opt == "L10 (H90/C74)";
    default:
      return false;
  }
}

inline bool level_allowed_for_excluded_levels(const ExcludedLevels& excluded, int level) {
  if (level <= 0 || level > 10) return true;
  return !excluded_option_matches_level(excluded.a, level) && !excluded_option_matches_level(excluded.b, level);
}

inline int pick_allowed_level(int req, int min_level, int max_level, const ExcludedLevels& excluded) {
  if (req <= 0) return 0;
  req = clamp_level(req, min_level, max_level);
  if (req <= 0) return 0;
  if (level_allowed_for_excluded_levels(excluded, req)) return req;

  for (int level = req - 1; level >= min_level; --level) {
    if (level > 0 && level_allowed_for_excluded_levels(excluded, level)) return level;
  }
  for (int level = req + 1; level <= max_level; ++level) {
    if (level > 0 && level_allowed_for_excluded_levels(excluded, level)) return level;
  }
  return 0;
}

inline int pick_allowed_capped_level(int req, int min_level, int max_level, int cap_level,
                                     const ExcludedLevels& excluded) {
  if (req <= 0) return 0;
  cap_level = clamp_level(cap_level, min_level, max_level);
  if (cap_level <= 0) return 0;
  req = clamp_level(req, min_level, cap_level);
  if (req <= 0) return 0;
  if (level_allowed_for_excluded_levels(excluded, req)) return req;

  for (int level = req - 1; level >= min_level; --level) {
    if (level > 0 && level_allowed_for_excluded_levels(excluded, level)) return level;
  }
  for (int level = req + 1; level <= cap_level; ++level) {
    if (level > 0 && level_allowed_for_excluded_levels(excluded, level)) return level;
  }
  return 0;
}

inline bool thermal_mode_matches(float mode_raw, int mode_code) {
  return !isnan(mode_raw) && ((int)roundf(mode_raw) == mode_code);
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
  if (last_loop_ms == 0 || now_ms <= last_loop_ms) return base_tick_ms;
  const uint32_t dt_cap_ms = base_tick_ms * 2UL;
  const uint32_t dt_ms = now_ms - last_loop_ms;
  return (dt_cap_ms > 0 && dt_ms > dt_cap_ms) ? dt_cap_ms : dt_ms;
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
