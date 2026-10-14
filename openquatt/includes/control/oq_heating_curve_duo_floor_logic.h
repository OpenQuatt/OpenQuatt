#pragma once

#include <math.h>

namespace oq_curve {

// The single owner is the HP that runs alone before the second HP is added.
struct DuoFloorInput {
  int owner_level = 0;             // applied level of the single owner (0 = not running)
  int owner_request = 0;           // single level requested for the owner by dispatch
  bool owner_may_step_up = false;  // incidents and defrost allow a higher compressor request
  float owner_power_w = NAN;       // predicted output of the owner at its applied level
  float duo_floor_power_w = NAN;   // lowest predicted output of any valid duo combination
};

// While the owner is still ramping towards its request, adding the second HP below the duo floor overshoots the
// demand and the second HP is shed again after the disable hold, causing cycling. When the owner cannot step up
// (not running, request reached, incident or defrost hold) or the model is unavailable, the previous behaviour applies.
inline bool duo_floor_reached(const DuoFloorInput& in) {
  const bool ramping = in.owner_level > 0 && in.owner_request > in.owner_level && in.owner_may_step_up;
  if (!ramping || !isfinite(in.owner_power_w) || !isfinite(in.duo_floor_power_w) || in.duo_floor_power_w <= 0.0f) {
    return true;
  }
  return in.owner_power_w >= in.duo_floor_power_w;
}

}  // namespace oq_curve
