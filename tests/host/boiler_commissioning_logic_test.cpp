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

void test_dhw_only_interferes_with_opentherm_boiler_test() {
  assert(boiler_test_dhw_interferes(true, true, true));
  assert(!boiler_test_dhw_interferes(true, true, false));
  assert(!boiler_test_dhw_interferes(true, false, true));
  assert(!boiler_test_dhw_interferes(false, true, true));
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

MeasurementQualityEvidence valid_measurement_evidence() {
  return MeasurementQualityEvidence{
      .completed = true,
      .opentherm_selected = true,
      .capacity_verified = true,
      .flow_limited = false,
      .transport_clean = true,
      .boiler_active_throughout = true,
      .thermal_safe = true,
      .dhw_clear = true,
      .measurement_ticks = 100,
      .stable_flow_ticks = 90,
      .valid_power_samples = 12,
      .result_w = 8400.0f,
      .confidence_percent = 92.0f,
  };
}

void test_result_quality_separates_provenance_from_confidence() {
  auto evidence = valid_measurement_evidence();
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_OPENTHERM_ID15_AVAILABLE);
  assert(result_apply_mode(RESULT_QUALITY_OPENTHERM_ID15_AVAILABLE) == RESULT_APPLY_DIRECT);

  evidence.capacity_verified = false;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_EMPIRICAL_UNVERIFIED);
  assert(result_apply_mode(RESULT_QUALITY_EMPIRICAL_UNVERIFIED) == RESULT_APPLY_CONFIRMATION_REQUIRED);

  evidence.opentherm_selected = false;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_RELAY_EMPIRICAL);
  assert(result_apply_mode(RESULT_QUALITY_RELAY_EMPIRICAL) == RESULT_APPLY_DIRECT);

  evidence.opentherm_selected = true;
  evidence.capacity_verified = true;
  evidence.flow_limited = true;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_FLOW_LIMITED);
  assert(result_apply_mode(RESULT_QUALITY_FLOW_LIMITED) == RESULT_APPLY_DENIED);
}

void test_result_quality_rejects_invalid_measurement_evidence() {
  auto evidence = valid_measurement_evidence();
  evidence.completed = false;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_INVALID);

  evidence = valid_measurement_evidence();
  evidence.stable_flow_ticks = 89;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_INVALID);

  evidence = valid_measurement_evidence();
  evidence.stable_flow_ticks = 101;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_INVALID);

  evidence = valid_measurement_evidence();
  evidence.valid_power_samples = 7;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_INVALID);

  evidence = valid_measurement_evidence();
  evidence.transport_clean = false;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_INVALID);

  evidence = valid_measurement_evidence();
  evidence.boiler_active_throughout = false;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_INVALID);

  evidence = valid_measurement_evidence();
  evidence.thermal_safe = false;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_INVALID);

  evidence = valid_measurement_evidence();
  evidence.dhw_clear = false;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_INVALID);

  evidence = valid_measurement_evidence();
  evidence.result_w = 999.0f;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_INVALID);
  evidence.result_w = 50001.0f;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_INVALID);

  evidence = valid_measurement_evidence();
  evidence.confidence_percent = 79.0f;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_INVALID);
}

void test_result_quality_accepts_exact_contract_boundaries() {
  auto evidence = valid_measurement_evidence();
  evidence.measurement_ticks = 10;
  evidence.stable_flow_ticks = 9;
  evidence.valid_power_samples = 8;
  evidence.result_w = 1000.0f;
  evidence.confidence_percent = 80.0f;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_OPENTHERM_ID15_AVAILABLE);

  evidence.result_w = 50000.0f;
  assert(evaluate_result_quality(evidence) == RESULT_QUALITY_OPENTHERM_ID15_AVAILABLE);
}

void test_empirical_apply_confirmation_is_explicit_bounded_and_wrap_safe() {
  ApplyConfirmationWindow confirmation;
  assert(confirmation.confirm_or_arm(1000) == APPLY_CONFIRMATION_ARMED);
  assert(confirmation.active(31000));
  assert(confirmation.confirm_or_arm(31000) == APPLY_CONFIRMATION_CONFIRMED);
  assert(!confirmation.active(31000));

  assert(confirmation.confirm_or_arm(50000) == APPLY_CONFIRMATION_ARMED);
  assert(!confirmation.active(80001));
  assert(confirmation.expire(80001));
  assert(confirmation.confirm_or_arm(80002) == APPLY_CONFIRMATION_ARMED);

  confirmation.reset();
  assert(confirmation.confirm_or_arm(UINT32_MAX - 10, 20) == APPLY_CONFIRMATION_ARMED);
  assert(confirmation.confirm_or_arm(5, 20) == APPLY_CONFIRMATION_CONFIRMED);
}

void test_power_plateau_rebases_after_xtreme_startup_transient() {
  PowerPlateauMonitor monitor;
  const float power_w[] = {8754.0f, 8000.0f, 7361.0f, 6999.0f, 6776.0f, 6144.0f, 5988.0f, 5718.0f, 5550.0f,
                           5347.0f, 5283.0f, 5463.0f, 5170.0f, 5149.0f, 5154.0f, 5164.0f, 5167.0f, 5189.0f,
                           5166.0f, 5094.0f, 5260.0f, 5244.0f, 5194.0f, 5372.0f, 5235.0f, 5155.0f};
  int stable_samples = 0;
  float stable_sum_w = 0.0f;
  for (float sample_w : power_w) {
    const auto update = monitor.update(sample_w, 0.95f, 4);
    if (update == POWER_PLATEAU_STABLE) {
      stable_samples++;
      stable_sum_w += sample_w;
    }
  }

  assert(stable_samples >= 8);
  const float average_w = stable_sum_w / (float)stable_samples;
  assert(average_w > 5100.0f);
  assert(average_w < 5300.0f);
  assert(monitor.stable());
  assert(monitor.reference_w() > 5100.0f);
  assert(monitor.reference_w() < 5200.0f);
}

void test_power_plateau_loses_old_result_and_rebases() {
  PowerPlateauMonitor monitor;
  assert(monitor.update(5000.0f, 0.95f, 4) == POWER_PLATEAU_WAITING);
  assert(monitor.update(5010.0f, 0.95f, 4) == POWER_PLATEAU_WAITING);
  assert(monitor.update(4990.0f, 0.95f, 4) == POWER_PLATEAU_WAITING);
  assert(monitor.update(5005.0f, 0.95f, 4) == POWER_PLATEAU_STABLE);
  assert(monitor.update(6000.0f, 0.95f, 4) == POWER_PLATEAU_LOST);
  assert(!monitor.stable());
  assert(monitor.update(6010.0f, 0.95f, 4) == POWER_PLATEAU_WAITING);
  assert(monitor.update(5990.0f, 0.95f, 4) == POWER_PLATEAU_WAITING);
  assert(monitor.update(6005.0f, 0.95f, 4) == POWER_PLATEAU_STABLE);
  assert(monitor.reference_w() > 5990.0f);
  assert(monitor.reference_w() < 6010.0f);
}

void test_power_plateau_rejects_invalid_configuration_and_samples() {
  PowerPlateauMonitor monitor;
  assert(monitor.update(5000.0f, 0.95f, 1) == POWER_PLATEAU_WAITING);
  assert(monitor.update(5000.0f, 1.0f, 4) == POWER_PLATEAU_WAITING);
  assert(monitor.update(NAN, 0.95f, 4) == POWER_PLATEAU_WAITING);
  assert(!monitor.stable());
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

void test_boiler_settle_starts_at_confirmed_activation() {
  BoilerActivationSettleMonitor monitor;
  assert(!monitor.update(1000, false, 30000));
  assert(!monitor.update(81000, true, 30000));
  assert(!monitor.update(110999, true, 30000));
  assert(monitor.update(111000, true, 30000));
}

void test_boiler_settle_restarts_after_activation_drops() {
  BoilerActivationSettleMonitor monitor;
  assert(!monitor.update(1000, true, 30000));
  assert(!monitor.update(25000, false, 30000));
  assert(!monitor.update(30000, true, 30000));
  assert(!monitor.update(59999, true, 30000));
  assert(monitor.update(60000, true, 30000));
}

}  // namespace

int main() {
  test_commissioning_temperature_policy();
  test_dhw_only_interferes_with_opentherm_boiler_test();
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
  test_power_plateau_rebases_after_xtreme_startup_transient();
  test_power_plateau_loses_old_result_and_rebases();
  test_power_plateau_rejects_invalid_configuration_and_samples();
  test_result_quality_separates_provenance_from_confidence();
  test_result_quality_rejects_invalid_measurement_evidence();
  test_result_quality_accepts_exact_contract_boundaries();
  test_empirical_apply_confirmation_is_explicit_bounded_and_wrap_safe();
  test_unreachable_flow_after_sustained_saturation();
  test_reachability_monitor_allows_meaningful_progress();
  test_reachability_monitor_resets_when_actuator_not_saturated();
  test_reachability_monitor_resets_when_target_band_reached();
  test_reachability_monitor_rejects_invalid_flow();
  test_boiler_settle_starts_at_confirmed_activation();
  test_boiler_settle_restarts_after_activation_drops();
  return 0;
}
