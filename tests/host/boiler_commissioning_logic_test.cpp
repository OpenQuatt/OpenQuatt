#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/boiler/oq_boiler_commissioning_logic.h"

namespace {

using namespace oq_boiler_commissioning;

void test_sufficient_headroom() {
  auto op = compute_operating_point(6000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op.feasible);
  assert(op.target_temperature_c == 45.0f);
  assert(op.required_flow_lph < 800.0f);
}

void test_insufficient_headroom_inlet_high() {
  auto op = compute_operating_point(6000.0f, 45.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(!op.feasible);
  assert(op.reason != nullptr);
}

void test_generic_helper_reports_large_required_flow() {
  // The pure helper reports the theoretical requirement; policy caps belong in the OT wrapper.
  auto op = compute_operating_point(50000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op.feasible);
  assert(op.required_flow_lph > 1500.0f);
  assert(!op.flow_limited);
}

void test_hard_trip_preserved() {
  float max_c = 60.0f;
  float trip_c = max_c + 5.0f;
  assert(trip_c == 65.0f);
  auto op = compute_operating_point(6000.0f, 20.0f, max_c, 800.0f, 4180.0f, 5.0f);
  assert(op.feasible);
  assert(op.target_temperature_c == 55.0f);
  assert(op.target_temperature_c < max_c);
  assert(op.target_temperature_c < trip_c);
}

void test_opentherm_uses_ot_max_capacity() {
  // 30 kW at 20C return theoretically needs >1000 L/h, so OT policy caps at 1000 and marks limited.
  auto op_ot = compute_opentherm_operating_point(true, 30000.0f, 6000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op_ot.feasible);
  assert(op_ot.flow_limited);
  assert(op_ot.required_flow_lph == 1000.0f);
  assert(op_ot.target_temperature_c == 45.0f);
}

void test_opentherm_missing_max_capacity_keeps_base_flow() {
  auto op = compute_opentherm_operating_point(true, NAN, 6000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op.feasible);
  assert(!op.flow_limited);
  assert(op.required_flow_lph == 800.0f);
  assert(op.target_temperature_c == 45.0f);

  auto op_zero = compute_opentherm_operating_point(true, 0.0f, 6000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op_zero.feasible);
  assert(op_zero.required_flow_lph == 800.0f);
}

void test_missing_max_capacity_still_requires_thermal_headroom() {
  auto op = compute_opentherm_operating_point(true, NAN, 6000.0f, 45.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(!op.feasible);
}

void test_r1_keeps_configured_flow() {
  auto op = compute_opentherm_operating_point(false, 30000.0f, 6000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op.feasible);
  assert(!op.flow_limited);
  assert(op.required_flow_lph == 800.0f);
  assert(op.target_temperature_c == 45.0f);
}

void test_dynamic_flow_after_preflow() {
  // 25 kW at 20C fits below 1000 L/h.
  auto op1 = compute_opentherm_operating_point(true, 25000.0f, 6000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op1.feasible);
  assert(!op1.flow_limited);
  assert(op1.required_flow_lph > 800.0f);
  assert(op1.required_flow_lph < 1000.0f);

  // After preflow the return rises. The same boiler now needs >1000, but remains a valid limited test.
  auto op2 = compute_opentherm_operating_point(true, 25000.0f, 6000.0f, 30.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op2.feasible);
  assert(op2.flow_limited);
  assert(op2.required_flow_lph == 1000.0f);
}

void test_very_high_theoretical_flow_is_limited_not_refused() {
  // 30 kW at 30C would theoretically require ~1720 L/h. Policy keeps the test at 1000 instead of refusing.
  auto op = compute_opentherm_operating_point(true, 30000.0f, 6000.0f, 30.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op.feasible);
  assert(op.flow_limited);
  assert(op.required_flow_lph == 1000.0f);
  assert(op.target_temperature_c == 45.0f);
}

}  // namespace

int main() {
  test_sufficient_headroom();
  test_insufficient_headroom_inlet_high();
  test_generic_helper_reports_large_required_flow();
  test_hard_trip_preserved();
  test_opentherm_uses_ot_max_capacity();
  test_opentherm_missing_max_capacity_keeps_base_flow();
  test_missing_max_capacity_still_requires_thermal_headroom();
  test_r1_keeps_configured_flow();
  test_dynamic_flow_after_preflow();
  test_very_high_theoretical_flow_is_limited_not_refused();
  return 0;
}
