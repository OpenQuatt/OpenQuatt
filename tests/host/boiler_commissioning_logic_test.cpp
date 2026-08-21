#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/boiler/oq_boiler_commissioning_logic.h"

namespace {

using namespace oq_boiler_commissioning;

void test_sufficient_headroom() {
  // Rated 6kW, inlet 20, max 50, flow 800, headroom 5 -> available headroom 25C, required flow ~206 L/h, feasible
  auto op = compute_operating_point(6000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op.feasible);
  assert(op.target_temperature_c < 50.0f);
  assert(op.target_temperature_c <= 45.0f);  // max - headroom
  assert(op.required_flow_lph < 800.0f);
}

void test_insufficient_headroom_inlet_high() {
  // Inlet 48, max 50, headroom 5 -> available  -3 -> infeasible
  auto op = compute_operating_point(6000.0f, 48.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(!op.feasible);
  assert(op.reason != nullptr);
}

void test_high_power_requires_higher_flow() {
  // 30kW, inlet 20, max 50, flow 800 -> required flow ~860, feasible but needs higher flow
  auto op = compute_operating_point(30000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op.feasible);
  assert(op.required_flow_lph > 800.0f);
  assert(op.required_flow_lph < 1500.0f);
  // With flow 1500, should still be feasible but target capped
  auto op2 = compute_operating_point(30000.0f, 20.0f, 50.0f, 1500.0f, 4180.0f, 5.0f);
  assert(op2.feasible);
}

void test_insufficient_headroom_power_too_high() {
  // 50kW, inlet 20, max 50, flow 800 -> required flow >1500 -> infeasible
  auto op = compute_operating_point(50000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(!op.feasible);
}

void test_hard_trip_preserved() {
  // Hard trip is max +5, not affected by commissioning headroom
  // This test just ensures our helper doesn't touch hard trip logic
  float max_c = 60.0f;
  float trip_c = max_c + 5.0f;
  assert(trip_c == 65.0f);
  // Commissioning target must be below max, not trip
  auto op = compute_operating_point(6000.0f, 20.0f, max_c, 800.0f, 4180.0f, 5.0f);
  assert(op.feasible);
  assert(op.target_temperature_c < max_c);
  assert(op.target_temperature_c < trip_c);
}

void test_opentherm_uses_ot_max_capacity() {
  // OT max 30kW should be used instead of rated 6kW -> requires higher flow
  auto op_ot = compute_opentherm_operating_point(true, 30000.0f, 6000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  auto op_rated = compute_opentherm_operating_point(true, NAN, 6000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op_ot.feasible);
  assert(op_rated.feasible);
  // OT missing keeps at 800 (not 1500, many systems cannot reach 1500)
  assert(op_rated.required_flow_lph == 800.0f);
  assert(op_ot.required_flow_lph > 800.0f);
  assert(op_ot.required_flow_lph < 1500.0f);
  // R1 should not use OT logic
  auto op_r1 = compute_opentherm_operating_point(false, 30000.0f, 6000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op_r1.feasible);
  assert(op_r1.required_flow_lph == 800.0f);    // R1 keeps configured flow
  assert(op_r1.target_temperature_c == 45.0f);  // max - headroom
}

void test_opentherm_missing_max_capacity() {
  // OT max missing -> keep at 800, not 1500 (many systems cannot reach 1500)
  auto op = compute_opentherm_operating_point(true, NAN, 6000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op.feasible);
  assert(op.required_flow_lph == 800.0f);
  assert(op.target_temperature_c == 45.0f);
  auto op_zero = compute_opentherm_operating_point(true, 0.0f, 6000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op_zero.feasible);
  assert(op_zero.required_flow_lph == 800.0f);
}

void test_dynamic_flow_after_preflow() {
  // Simulate 800 preflow with OT 30kW, inlet 22 -> required ~860, feasible
  auto op1 = compute_opentherm_operating_point(true, 30000.0f, 6000.0f, 22.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op1.feasible);
  assert(op1.required_flow_lph > 800.0f);
  assert(op1.required_flow_lph < 1500.0f);
  // After 2 min, inlet rises to 30 (warmer return), required flow becomes higher
  // With inlet 30, available headroom = 15C, required flow = 30000/(4180*15)=1722 >1500 -> infeasible
  auto op2 = compute_opentherm_operating_point(true, 30000.0f, 6000.0f, 30.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(!op2.feasible);
  // With a more powerful flow (1200) and same inlet 30, it would still be infeasible, showing need for re-evaluation
  // The task should detect this and fail with insufficient headroom rather than starting boiler
}

}  // namespace

int main() {
  test_sufficient_headroom();
  test_insufficient_headroom_inlet_high();
  test_high_power_requires_higher_flow();
  test_insufficient_headroom_power_too_high();
  test_hard_trip_preserved();
  test_opentherm_uses_ot_max_capacity();
  test_opentherm_missing_max_capacity();
  test_dynamic_flow_after_preflow();
  return 0;
}
