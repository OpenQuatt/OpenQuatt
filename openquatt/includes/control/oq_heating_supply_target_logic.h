#pragma once

#include <math.h>

namespace oq_heating_supply {

// Outcome of choosing the effective heating supply target for one evaluation.
struct EffectiveTarget {
  float supply_target_c = NAN;
  bool external = false;
};

inline float clamp_target(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

// Choose the effective supply target. A valid external target replaces the
// locally computed heating-curve target entirely: the curve math including
// room-trim must not run again on top of the external value, otherwise the
// correction applies twice. Anything else falls back to the local curve, so a
// stale or absent external link returns the installation to its normal
// behaviour. Only the target moves here: water-temperature limits, trips, the
// PID, demand logic and compressor/Duo dispatch stay firmware-owned
// downstream of this call.
inline EffectiveTarget select_effective_target(float local_curve_c, float external_c, bool external_valid, float min_c,
                                               float max_c) {
  if (external_valid && isfinite(external_c) && isfinite(min_c) && isfinite(max_c) && max_c > min_c) {
    return {clamp_target(external_c, min_c, max_c), true};
  }
  return {local_curve_c, false};
}

}  // namespace oq_heating_supply
