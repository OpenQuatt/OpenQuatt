#pragma once

#include <math.h>

namespace oq_power_house {

// The reference coordinate (Tc, Pr) describes a line; it is not a third
// independent property of the house. These values never own control limits.
struct HouseLine {
  float heat_loss_w_per_k = NAN;
  float zero_power_temp_c = NAN;
};

struct PowerHouseEnvelope {
  float request_max_w = NAN;
  float demand_scale_w = NAN;
  float slew_scale_w = NAN;
};

inline bool valid_house_line(const HouseLine& line) {
  return isfinite(line.heat_loss_w_per_k) && line.heat_loss_w_per_k > 0.0f && isfinite(line.zero_power_temp_c);
}

inline bool valid_house_envelope(const PowerHouseEnvelope& envelope) {
  return isfinite(envelope.request_max_w) && envelope.request_max_w > 0.0f && isfinite(envelope.demand_scale_w) &&
         envelope.demand_scale_w > 0.0f && isfinite(envelope.slew_scale_w) && envelope.slew_scale_w > 0.0f;
}

inline HouseLine house_line_from_legacy(float cold_c, float zero_power_c, float rated_w, float guard_c = 0.0f) {
  if (!isfinite(cold_c) || !isfinite(zero_power_c) || !isfinite(rated_w) || rated_w <= 0.0f || !isfinite(guard_c) ||
      guard_c < 0.0f || !(zero_power_c > cold_c + guard_c))
    return {};
  const HouseLine line{rated_w / (zero_power_c - cold_c), zero_power_c};
  return valid_house_line(line) ? line : HouseLine{};
}

// A manual legacy edit retains its existing scale/cap semantics. A fitted line
// must never be passed here to derive a new control envelope.
inline PowerHouseEnvelope house_envelope_from_legacy(float rated_w) { return {rated_w, rated_w, rated_w}; }

inline float house_line_power_w(const HouseLine& line, float outside_c) {
  if (!valid_house_line(line) || !isfinite(outside_c)) return NAN;
  const float watts = line.heat_loss_w_per_k * (line.zero_power_temp_c - outside_c);
  if (!isfinite(watts)) return NAN;
  return fmaxf(0.0f, watts);
}

inline float house_line_reference_power_w(const HouseLine& line, float cold_c) {
  if (!isfinite(cold_c) || !(line.zero_power_temp_c > cold_c)) return NAN;
  return house_line_power_w(line, cold_c);
}

}  // namespace oq_power_house
