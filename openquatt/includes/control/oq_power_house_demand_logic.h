#pragma once

#include <math.h>

namespace oq_power_house {

// Outcome of choosing the Power House feedforward term for one control tick.
struct Feedforward {
  float house_power_w = 0.0f;
  bool external = false;
};

inline float clamp_power(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

// Modelled house power: linear between the zero-power outdoor temperature and
// the full-load point, saturated at both ends. Returns NAN when the house model
// itself is unusable, which the strategy lambda already guards upstream.
inline float modelled_house_power_w(float zero_power_temp_c, float cold_temp_c, float outside_temp_c,
                                    float rated_power_w) {
  if (!isfinite(zero_power_temp_c) || !isfinite(cold_temp_c) || !isfinite(outside_temp_c) || !isfinite(rated_power_w) ||
      !(zero_power_temp_c > cold_temp_c)) {
    return NAN;
  }
  const float load = clamp_power((zero_power_temp_c - outside_temp_c) / (zero_power_temp_c - cold_temp_c), 0.0f, 1.0f);
  return rated_power_w * load;
}

// Choose the feedforward term. A valid external demand replaces the modelled
// value; anything else degrades to the model rather than to no heat at all, so
// a stale or absent planner link returns the installation to its normal
// behaviour. Only the feedforward moves here: the comfort trim, the saturation
// clamp, the slew limiter and the water-temperature limiter stay firmware-owned
// downstream of this call.
inline Feedforward select_feedforward(float modelled_power_w, float external_power_w, bool external_valid,
                                      float rated_power_w) {
  Feedforward result;
  result.house_power_w = modelled_power_w;
  result.external = false;

  // Without a usable rating there is no scale to clamp an external demand
  // against, and the demand mapping downstream divides by it.
  if (!external_valid || !isfinite(external_power_w) || !isfinite(rated_power_w) || rated_power_w <= 0.0f) {
    return result;
  }

  result.house_power_w = clamp_power(external_power_w, 0.0f, rated_power_w);
  result.external = true;
  return result;
}

}  // namespace oq_power_house
