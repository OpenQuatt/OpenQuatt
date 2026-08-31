#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace oq_strategy {

struct LocalOutsideInput {
  uint32_t now_ms = 0;
  uint32_t stale_ms = 0;
  bool dual = false;
  float hp1_outside_c = NAN;
  float hp2_outside_c = NAN;
  float hp1_mode = NAN;
  float hp2_mode = NAN;
  uint32_t hp1_last_change_ms = 0;
  uint32_t hp2_last_change_ms = 0;
  uint32_t hp1_activity_ms = 0;
  uint32_t hp2_activity_ms = 0;
};

inline bool thermal_working_mode(float mode) {
  if (!std::isfinite(mode)) return false;
  const int rounded = static_cast<int>(std::lround(mode));
  return rounded == 1 || rounded == 2;
}

inline bool trusted_local_outside(float outside_c, uint32_t now_ms, uint32_t stale_ms, uint32_t last_change_ms,
                                  uint32_t activity_ms, bool dual) {
  if (!std::isfinite(outside_c)) return false;
  if (!dual || stale_ms == 0 || last_change_ms == 0 || activity_ms == 0) return true;
  const uint32_t outside_age_ms = static_cast<uint32_t>(now_ms - last_change_ms);
  const uint32_t activity_age_ms = static_cast<uint32_t>(now_ms - activity_ms);
  const bool activity_after_change = activity_age_ms < outside_age_ms;
  return !(activity_after_change && outside_age_ms >= stale_ms && activity_age_ms < stale_ms);
}

inline float aggregate_local_outside(const LocalOutsideInput& in) {
  const bool hp1_valid = trusted_local_outside(in.hp1_outside_c, in.now_ms, in.stale_ms, in.hp1_last_change_ms,
                                               in.hp1_activity_ms, in.dual);
  const bool hp2_valid = in.dual && trusted_local_outside(in.hp2_outside_c, in.now_ms, in.stale_ms,
                                                          in.hp2_last_change_ms, in.hp2_activity_ms, true);
  const bool hp1_active = hp1_valid && thermal_working_mode(in.hp1_mode);
  const bool hp2_active = hp2_valid && thermal_working_mode(in.hp2_mode);
  if (hp1_active && hp2_active) return std::min(in.hp1_outside_c, in.hp2_outside_c);
  if (hp1_active) return in.hp1_outside_c;
  if (hp2_active) return in.hp2_outside_c;
  if (hp1_valid && hp2_valid) return 0.5f * (in.hp1_outside_c + in.hp2_outside_c);
  if (hp1_valid) return in.hp1_outside_c;
  if (hp2_valid) return in.hp2_outside_c;
  return NAN;
}

}  // namespace oq_strategy
