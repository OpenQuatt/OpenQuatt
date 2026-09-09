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

// Heating-range gate for the external paths without their own ingress range
// enforcement (OT TSet, HA proxy; API and MQTT already clamp at ingress).
// TSet=0 (thermostat without heat demand) or any out-of-range value counts as
// unusable and falls back to the local curve instead of being clamped onto an
// external minimum target.
inline bool external_target_in_range(float value, float min_c = 20.0f, float max_c = 70.0f) {
  return isfinite(value) && isfinite(min_c) && isfinite(max_c) && max_c > min_c && value >= min_c && value <= max_c;
}

// An explicitly switched-off HA validity flag revokes the cached target at
// once instead of bridging it for the hold window. A missing flag (for
// example during an HA reload) keeps bridging: only an affirmative off
// revokes, so short dropouts without an explicit off still bridge.
inline bool ha_hold_revoked(bool valid_has_state, bool valid_state) { return valid_has_state && !valid_state; }

}  // namespace oq_heating_supply
