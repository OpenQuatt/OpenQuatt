#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/control/oq_heating_curve_duo_floor_logic.h"
namespace {
using namespace oq_curve;

// Maintain regime at full demand: the single owner runs at level 2 (~2.6 kW) while dispatch requests level 6, and
// the lowest duo combination would deliver ~3.3 kW.
DuoFloorInput ramping_below_floor() {
  DuoFloorInput in;
  in.owner_level = 2;
  in.owner_request = 6;
  in.owner_may_step_up = true;
  in.owner_power_w = 2600.0f;
  in.duo_floor_power_w = 3300.0f;
  return in;
}

// Regression: the second HP was added while the owner was still ramping below the duo floor, overshot the demand and
// was shed again after the disable hold, restarting ~13 minutes later.
void test_ramping_owner_below_floor_waits() {
  assert(!duo_floor_reached(ramping_below_floor()));
  auto reached = ramping_below_floor();
  reached.owner_power_w = 3300.0f;
  assert(duo_floor_reached(reached));
}

// An owner that cannot step up must never block the second HP.
void test_owner_that_cannot_step_up_keeps_duo_available() {
  auto held = ramping_below_floor();
  held.owner_may_step_up = false;  // incident blocks a higher request, or the owner is defrosting
  assert(duo_floor_reached(held));
  auto at_request = ramping_below_floor();
  at_request.owner_request = at_request.owner_level;  // frequency cap, envelope or demand already reached
  assert(duo_floor_reached(at_request));
  at_request.owner_request = 1;
  assert(duo_floor_reached(at_request));
  auto idle = ramping_below_floor();
  idle.owner_level = 0;
  idle.owner_power_w = 0.0f;
  assert(duo_floor_reached(idle));
}

void test_unknown_model_keeps_previous_behaviour() {
  auto in = ramping_below_floor();
  in.owner_power_w = NAN;
  assert(duo_floor_reached(in));
  in = ramping_below_floor();
  in.duo_floor_power_w = NAN;
  assert(duo_floor_reached(in));
  in.duo_floor_power_w = 0.0f;
  assert(duo_floor_reached(in));
}
}  // namespace
int main() {
  test_ramping_owner_below_floor_waits();
  test_owner_that_cannot_step_up_keeps_duo_available();
  test_unknown_model_keeps_previous_behaviour();
  return 0;
}
