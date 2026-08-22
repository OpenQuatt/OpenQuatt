#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/boiler/oq_boiler_commissioning_logic.h"

namespace {

using namespace oq_boiler_commissioning;

void test_commissioning_temperature_policy() {
  assert(normalize_max_water_temperature_c(NAN) == 60.0f);
  assert(normalize_max_water_temperature_c(10.0f) == 25.0f);
  assert(normalize_max_water_temperature_c(90.0f) == 75.0f);
  assert(commissioning_target_temperature_c(50.0f) == 45.0f);
  assert(commissioning_target_temperature_c(NAN) == 55.0f);
  assert(isnan(commissioning_target_temperature_c(50.0f, -1.0f)));
}

void test_sufficient_headroom() {
  auto op = compute_operating_point(6000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op.feasible);
  assert(op.target_temperature_c == 45.0f);
  assert(op.theoretical_flow_lph < 800.0f);
  assert(op.target_flow_lph == op.theoretical_flow_lph);
}

void test_insufficient_headroom_inlet_high() {
  auto op = compute_operating_point(6000.0f, 45.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(!op.feasible);
  assert(op.reason != nullptr);
}

void test_generic_helper_preserves_theoretical_flow() {
  auto op = compute_operating_point(50000.0f, 20.0f, 50.0f, 800.0f, 4180.0f, 5.0f);
  assert(op.feasible);
  assert(op.theoretical_flow_lph > 1500.0f);
  assert(op.target_flow_lph == op.theoretical_flow_lph);
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

void test_opentherm_known_capacity_selects_max_1000() {
  auto op = compute_opentherm_operating_point(true, 30000.0f, 6000.0f, 20.0f, 50.0f, 800.0f);
  assert(op.feasible);
  assert(op.flow_limited);
  assert(op.theoretical_flow_lph > 1000.0f);
  assert(op.target_flow_lph == 1000.0f);
  assert(op.target_temperature_c == 45.0f);
}

void test_opentherm_missing_max_capacity_keeps_base_flow() {
  auto op = compute_opentherm_operating_point(true, NAN, 6000.0f, 20.0f, 50.0f, 800.0f);
  assert(op.feasible);
  assert(!op.flow_limited);
  assert(isnan(op.theoretical_flow_lph));
  assert(op.target_flow_lph == 800.0f);
  assert(op.target_temperature_c == 45.0f);

  auto op_zero = compute_opentherm_operating_point(true, 0.0f, 6000.0f, 20.0f, 50.0f, 800.0f);
  assert(op_zero.feasible);
  assert(op_zero.target_flow_lph == 800.0f);
}

void test_missing_max_capacity_still_requires_thermal_headroom() {
  auto op = compute_opentherm_operating_point(true, NAN, 6000.0f, 45.0f, 50.0f, 800.0f);
  assert(!op.feasible);
}

void test_r1_keeps_configured_flow() {
  auto op = compute_opentherm_operating_point(false, 30000.0f, 6000.0f, 20.0f, 50.0f, 800.0f);
  assert(op.feasible);
  assert(!op.flow_limited);
  assert(op.target_flow_lph == 800.0f);
  assert(op.target_temperature_c == 45.0f);
}

void test_dynamic_flow_after_initial_800_preflow() {
  auto op1 = compute_opentherm_operating_point(true, 25000.0f, 6000.0f, 20.0f, 50.0f, 800.0f);
  assert(op1.feasible);
  assert(!op1.flow_limited);
  assert(op1.target_flow_lph > 800.0f);
  assert(op1.target_flow_lph < 1000.0f);
  assert(op1.theoretical_flow_lph == op1.target_flow_lph);

  auto op2 = compute_opentherm_operating_point(true, 25000.0f, 6000.0f, 30.0f, 50.0f, 800.0f);
  assert(op2.feasible);
  assert(op2.flow_limited);
  assert(op2.theoretical_flow_lph > 1000.0f);
  assert(op2.target_flow_lph == 1000.0f);
}

void test_very_high_theoretical_flow_is_limited_not_refused() {
  auto op = compute_opentherm_operating_point(true, 30000.0f, 6000.0f, 30.0f, 50.0f, 800.0f);
  assert(op.feasible);
  assert(op.flow_limited);
  assert(op.theoretical_flow_lph > 1500.0f);
  assert(op.target_flow_lph == 1000.0f);
}

void test_apply_policy() {
  assert(result_apply_allowed(false, true, false));
  assert(result_apply_allowed(true, true, false));
  assert(!result_apply_allowed(true, false, false));
  assert(!result_apply_allowed(true, true, true));
  assert(!result_apply_allowed(true, false, true));
}

void test_unreachable_flow_after_sustained_saturation() {
  FlowReachabilityMonitor monitor;
  assert(!monitor.update(1000, 700.0f, 800.0f, 40.0f, 50.0f));
  assert(!monitor.update(31000, 704.0f, 800.0f, 40.0f, 50.0f));
  assert(monitor.update(61000, 703.0f, 800.0f, 40.0f, 50.0f));
  assert(monitor.best_flow_lph() == 704.0f);
}

void test_reachability_monitor_allows_meaningful_progress() {
  FlowReachabilityMonitor monitor;
  assert(!monitor.update(1000, 680.0f, 800.0f, 40.0f, 55.0f));
  assert(!monitor.update(31000, 690.0f, 800.0f, 40.0f, 55.0f));
  assert(!monitor.update(61000, 701.0f, 800.0f, 40.0f, 55.0f));
  assert(!monitor.update(91000, 715.0f, 800.0f, 40.0f, 55.0f));
  assert(!monitor.update(121000, 725.0f, 800.0f, 40.0f, 55.0f));
}

void test_reachability_monitor_resets_when_actuator_not_saturated() {
  FlowReachabilityMonitor monitor;
  assert(!monitor.update(1000, 700.0f, 800.0f, 40.0f, 50.0f));
  assert(!monitor.update(31000, 704.0f, 800.0f, 40.0f, 100.0f));
  assert(!monitor.update(91000, 704.0f, 800.0f, 40.0f, 50.0f));
}

void test_reachability_monitor_resets_when_target_band_reached() {
  FlowReachabilityMonitor monitor;
  assert(!monitor.update(1000, 700.0f, 800.0f, 40.0f, 50.0f));
  assert(!monitor.update(31000, 765.0f, 800.0f, 40.0f, 50.0f));
  assert(!monitor.update(91000, 700.0f, 800.0f, 40.0f, 50.0f));
}

void test_reachability_monitor_rejects_invalid_flow() {
  FlowReachabilityMonitor monitor;
  assert(!monitor.update(1000, NAN, 800.0f, 40.0f, 50.0f));
  assert(!monitor.update(61000, NAN, 800.0f, 40.0f, 50.0f));
}

}  // namespace

int main() {
  test_commissioning_temperature_policy();
  test_sufficient_headroom();
  test_insufficient_headroom_inlet_high();
  test_generic_helper_preserves_theoretical_flow();
  test_hard_trip_preserved();
  test_opentherm_known_capacity_selects_max_1000();
  test_opentherm_missing_max_capacity_keeps_base_flow();
  test_missing_max_capacity_still_requires_thermal_headroom();
  test_r1_keeps_configured_flow();
  test_dynamic_flow_after_initial_800_preflow();
  test_very_high_theoretical_flow_is_limited_not_refused();
  test_apply_policy();
  test_unreachable_flow_after_sustained_saturation();
  test_reachability_monitor_allows_meaningful_progress();
  test_reachability_monitor_resets_when_actuator_not_saturated();
  test_reachability_monitor_resets_when_target_band_reached();
  test_reachability_monitor_rejects_invalid_flow();
  return 0;
}
