#include <assert.h>

#include "../../openquatt/includes/control/oq_control_mode_log_logic.h"
#include "../../openquatt/includes/control/oq_hp_supervisory_logic.h"
#include "../../openquatt/includes/control/oq_power_house_demand_logic.h"
#include "../../openquatt/includes/control/oq_thermal_limits_logic.h"

namespace {

void test_heating_enable_gate() {
  using oq_hp_supervisory::apply_heating_enable_gate;

  assert(apply_heating_enable_gate(true, true, true));
  assert(!apply_heating_enable_gate(true, true, false));
  assert(!apply_heating_enable_gate(true, false, false));
  assert(!apply_heating_enable_gate(false, true, true));
}

void test_frost_control_mode_remains_independent_of_heating_request() {
  using oq_hp_supervisory::base_control_mode;

  assert(base_control_mode(false, false, true) == 98);
  assert(base_control_mode(false, true, true) == 2);
  assert(base_control_mode(true, true, true) == 5);
  assert(base_control_mode(false, false, false) == 0);
}

void test_cold_start_temperature_bands() {
  using oq_hp_supervisory::ColdStartWaterSample;
  using oq_hp_supervisory::evaluate_cold_start;

  const uint32_t sample_after_ms = 1000;
  ColdStartWaterSample hp1{true, 4.9f, 1001};
  ColdStartWaterSample hp2{false, NAN, 0};

  auto decision = evaluate_cold_start(sample_after_ms, hp1, hp2, 5.0f, 12.0f);
  assert(decision.samples_ready);
  assert(!decision.hp_start_allowed);
  assert(!decision.auxiliary_assist_recommended);
  assert(!decision.released);

  hp1.temperature_c = 5.0f;
  decision = evaluate_cold_start(sample_after_ms, hp1, hp2, 5.0f, 12.0f);
  assert(decision.hp_start_allowed);
  assert(decision.auxiliary_assist_recommended);
  assert(!decision.released);

  hp1.temperature_c = 12.0f;
  decision = evaluate_cold_start(sample_after_ms, hp1, hp2, 5.0f, 12.0f);
  assert(decision.hp_start_allowed);
  assert(!decision.auxiliary_assist_recommended);
  assert(decision.released);

  hp1.updated_at_ms = sample_after_ms;
  decision = evaluate_cold_start(sample_after_ms, hp1, hp2, 5.0f, 12.0f);
  assert(!decision.samples_ready);
  assert(!decision.hp_start_allowed);

  hp1 = ColdStartWaterSample{true, 10.0f, 1002};
  hp2 = ColdStartWaterSample{true, 4.0f, 1003};
  decision = evaluate_cold_start(sample_after_ms, hp1, hp2, 5.0f, 12.0f);
  assert(decision.samples_ready);
  assert(decision.minimum_temperature_c == 4.0f);
  assert(!decision.hp_start_allowed);

  hp2.temperature_c = NAN;
  decision = evaluate_cold_start(sample_after_ms, hp1, hp2, 5.0f, 12.0f);
  assert(!decision.samples_ready);
}

void test_cold_start_availability_matrix() {
  using oq_hp_supervisory::cold_start_required_added;
  using oq_hp_supervisory::cold_start_required_set;
  using oq_hp_supervisory::cold_start_sample_is_new;
  using oq_hp_supervisory::ColdStartWaterSample;
  using oq_hp_supervisory::evaluate_cold_start;

  uint32_t sample_after_ms = 1000;

  // A. HP1 unavailable, HP2 available and warm: healthy HP2 alone releases.
  auto required = cold_start_required_set(false, true);
  assert(!required.hp1);
  assert(required.hp2);
  ColdStartWaterSample hp1{required.hp1, NAN, 0};
  ColdStartWaterSample hp2{required.hp2, 15.0f, 1001};
  auto decision = evaluate_cold_start(sample_after_ms, hp1, hp2, 5.0f, 12.0f);
  assert(decision.samples_ready);
  assert(decision.hp_start_allowed);
  assert(decision.released);
  assert(decision.minimum_temperature_c == 15.0f);

  // B. Mirror: only HP1 available.
  required = cold_start_required_set(true, false);
  assert(required.hp1);
  assert(!required.hp2);
  hp1 = ColdStartWaterSample{required.hp1, 15.0f, 1001};
  hp2 = ColdStartWaterSample{required.hp2, NAN, 0};
  decision = evaluate_cold_start(sample_after_ms, hp1, hp2, 5.0f, 12.0f);
  assert(decision.samples_ready);
  assert(decision.hp_start_allowed);
  assert(decision.released);
  assert(decision.minimum_temperature_c == 15.0f);

  // C. Available HP below 5 C blocks the start.
  required = cold_start_required_set(false, true);
  hp1 = ColdStartWaterSample{required.hp1, NAN, 0};
  hp2 = ColdStartWaterSample{required.hp2, 4.9f, 1001};
  decision = evaluate_cold_start(sample_after_ms, hp1, hp2, 5.0f, 12.0f);
  assert(decision.samples_ready);
  assert(!decision.hp_start_allowed);
  assert(!decision.released);

  // D. Available HP at 5..12 C allows start with assist, without release.
  hp2 = ColdStartWaterSample{required.hp2, 8.0f, 1001};
  decision = evaluate_cold_start(sample_after_ms, hp1, hp2, 5.0f, 12.0f);
  assert(decision.samples_ready);
  assert(decision.hp_start_allowed);
  assert(decision.auxiliary_assist_recommended);
  assert(!decision.released);

  // E. Two available HP's: both samples required, coldest outlet decides.
  required = cold_start_required_set(true, true);
  hp1 = ColdStartWaterSample{required.hp1, 15.0f, 1002};
  hp2 = ColdStartWaterSample{required.hp2, 4.0f, 1003};
  decision = evaluate_cold_start(sample_after_ms, hp1, hp2, 5.0f, 12.0f);
  assert(decision.samples_ready);
  assert(decision.minimum_temperature_c == 4.0f);
  assert(!decision.hp_start_allowed);
  assert(!decision.released);

  // F. No available HP must never fake a release; fallback logic stays owner.
  required = cold_start_required_set(false, false);
  hp1 = ColdStartWaterSample{required.hp1, NAN, 0};
  hp2 = ColdStartWaterSample{required.hp2, NAN, 0};
  decision = evaluate_cold_start(sample_after_ms, hp1, hp2, 5.0f, 12.0f);
  assert(!decision.samples_ready);
  assert(!decision.hp_start_allowed);
  assert(!decision.released);

  // Recovery before any compressor start is a false->true edge in the
  // required set, whatever the release history was.
  const auto hp2_only = cold_start_required_set(false, true);
  const auto both = cold_start_required_set(true, true);
  const auto none = cold_start_required_set(false, false);
  assert(!cold_start_required_added(hp2_only, hp2_only));
  assert(cold_start_required_added(hp2_only, both));
  assert(cold_start_required_added(none, cold_start_required_set(true, false)));
  assert(!cold_start_required_added(hp2_only, none));
  assert(!cold_start_required_added(both, both));
  assert(!cold_start_required_added(both, hp2_only));

  // Recovery must start a new freshness epoch: a measurement taken before
  // the edge is stale even though it is newer than the original preflow.
  // t=1000 flow OK, t=1500 HP1 sample, t=2000 HP1 offline, t=5000 HP2-only
  // release, t=8000 HP1 recovers.
  sample_after_ms = 1000;
  hp1 = ColdStartWaterSample{false, 20.0f, 1500};
  hp2 = ColdStartWaterSample{true, 21.0f, 4000};
  decision = evaluate_cold_start(sample_after_ms, hp1, hp2, 5.0f, 12.0f);
  assert(decision.released);
  assert(cold_start_required_added(hp2_only, both));
  // Without the epoch reset, the pre-recovery HP1 sample would wrongly
  // count as fresh (1500 > 1000) and release immediately: the reported bug.
  assert(cold_start_sample_is_new(ColdStartWaterSample{true, 20.0f, 1500}, 1000));
  assert(evaluate_cold_start(1000, ColdStartWaterSample{true, 20.0f, 1500}, ColdStartWaterSample{true, 21.0f, 4000},
                             5.0f, 12.0f)
             .released);
  sample_after_ms = 8000;  // runtime resets the epoch on the edge
  assert(!cold_start_sample_is_new(ColdStartWaterSample{true, 20.0f, 1500}, sample_after_ms));
  assert(!cold_start_sample_is_new(ColdStartWaterSample{true, 21.0f, 4000}, sample_after_ms));
  decision = evaluate_cold_start(sample_after_ms, ColdStartWaterSample{true, 20.0f, 1500},
                                 ColdStartWaterSample{true, 21.0f, 4000}, 5.0f, 12.0f);
  assert(!decision.samples_ready);
  assert(!decision.hp_start_allowed);
  assert(!decision.released);
  decision = evaluate_cold_start(sample_after_ms, ColdStartWaterSample{true, 20.5f, 8100},
                                 ColdStartWaterSample{true, 21.0f, 8200}, 5.0f, 12.0f);
  assert(decision.released);

  // A validated HP that drops out and recovers also edges false->true, so
  // it cannot restart on its pre-loss measurement either.
  assert(!cold_start_required_added(hp2_only, none));
  assert(cold_start_required_added(none, hp2_only));
  sample_after_ms = 9000;  // runtime resets the epoch on the recovery edge
  assert(!cold_start_sample_is_new(ColdStartWaterSample{true, 21.0f, 8200}, sample_after_ms));
}

oq_hp_supervisory::FallbackEvaluationInputs eligible_fallback_evaluation_inputs() {
  oq_hp_supervisory::FallbackEvaluationInputs inputs;
  inputs.current_mode = 3;
  inputs.heating_demand = true;
  inputs.fallback_enabled = true;
  inputs.available_hp_count = 0;
  inputs.raw_availability_complete = true;
  inputs.every_unavailable_hp_has_fallback_cause = true;
  inputs.all_hp_outputs_safe = true;
  inputs.flow_valid = true;
  inputs.flow_sufficient = true;
  inputs.supply_temperature_valid = true;
  inputs.boiler_guards_clear = true;
  return inputs;
}

void test_post_dhw_odu_failure_keeps_the_compressor_stopped_and_releases_safe_boiler_fallback() {
  oq_thermal_limits::State thermal;
  auto fallback = eligible_fallback_evaluation_inputs();
  oq_power_house::DemandInput demand{1000, 5.0f, -10.0f, 16.0f, 8000.0f, 17.0f, 20.0f, 4200.0f, 1.0f, true};
  const oq_power_house::DemandTuning tuning{3.0f, 400.0f, 0.3f, 0.3f, 5.0f, 2.0f, 0.2f};
  const auto cm4_allowed = [&]() {
    demand.water_limit_factor = thermal.limit_factor;
    const auto house_demand = oq_power_house::decide_demand(demand, tuning, {});
    assert(house_demand.valid);
    fallback.heating_demand = house_demand.requested_w > 0.0f;
    fallback.boiler_guards_clear = !thermal.boiler_inhibit && !thermal.hard_trip;
    return oq_hp_supervisory::evaluate_fallback(fallback).decision.cm4_allowed;
  };

  oq_thermal_limits::update(thermal, {1000, 65.6f, 46.0f, 3, 200, 34.0f});
  assert(thermal.post_dhw_active);
  oq_thermal_limits::update(thermal, {2000, 65.6f, 46.0f, 3, 0, NAN, true, false, false});
  assert(thermal.heat_pump_hard_trip() && thermal.hard_trip && !cm4_allowed());

  oq_thermal_limits::update(thermal, {3000, 35.0f, 46.0f, 1, 0, NAN, false, false, false});
  assert(thermal.heat_pump_hard_trip() && !cm4_allowed());
  oq_thermal_limits::update(thermal, {4000, 35.0f, 46.0f, 1, 0, NAN, true, false, false});
  assert(thermal.heat_pump_hard_trip() && thermal.limit_factor == 1.0f && cm4_allowed());

  oq_thermal_limits::update(thermal, {5000, 35.0f, 46.0f, 4, 0, NAN, false, false, false});
  assert(thermal.heat_pump_hard_trip() && !cm4_allowed());
}

void test_fallback_evaluation_and_recovering_handover() {
  using oq_hp_fallback::FallbackBlockReason;
  using oq_hp_supervisory::evaluate_fallback;

  auto inputs = eligible_fallback_evaluation_inputs();
  auto evaluation = evaluate_fallback(inputs);
  assert(evaluation.availability_complete);
  assert(evaluation.no_hp_available_confirmed);
  assert(evaluation.fallback_requested);
  assert(evaluation.decision.cm4_allowed);
  assert(!evaluation.cm3_handover_wait);

  // After LOST, the first complete reconnect round may be RECOVERING with
  // only one fresh stopped sample. Preserve CM3 heat output while the second
  // sample is pending, but do not permit CM4 entry yet.
  inputs.raw_availability_complete = false;
  inputs.all_hp_outputs_safe = false;
  evaluation = evaluate_fallback(inputs);
  assert(!evaluation.availability_complete);
  assert(!evaluation.no_hp_available_confirmed);
  assert(evaluation.fallback_requested);
  assert(!evaluation.decision.cm4_allowed);
  assert(evaluation.decision.block_reason == FallbackBlockReason::HP_AVAILABILITY_UNKNOWN);
  assert(evaluation.cm3_handover_wait);

  oq_hp_supervisory::Cm4ResumeTracker recovering_resume;
  recovering_resume.observe_fallback_request(evaluation.fallback_requested, 3);
  assert(recovering_resume.resume_mode() == 3);
  recovering_resume.observe_fallback_request(evaluation.fallback_requested, 1);
  assert(recovering_resume.resume_mode() == 3);

  // The prospective handover check releases only the two coupled stop gates.
  // Every real independent guard still prevents the CM3 hold.
  const auto recovering_inputs = inputs;
  auto assert_no_handover_hold = [&](const oq_hp_supervisory::FallbackEvaluationInputs& candidate) {
    const auto blocked = evaluate_fallback(candidate);
    assert(blocked.fallback_requested);
    assert(!blocked.cm3_handover_wait);
  };

  inputs = recovering_inputs;
  inputs.fallback_enabled = false;
  assert_no_handover_hold(inputs);
  inputs = recovering_inputs;
  inputs.flow_valid = false;
  assert_no_handover_hold(inputs);
  inputs = recovering_inputs;
  inputs.flow_sufficient = false;
  assert_no_handover_hold(inputs);
  inputs = recovering_inputs;
  inputs.supply_temperature_valid = false;
  assert_no_handover_hold(inputs);
  inputs = recovering_inputs;
  inputs.boiler_guards_clear = false;
  assert_no_handover_hold(inputs);
  inputs = recovering_inputs;
  inputs.cooling_active = true;
  assert_no_handover_hold(inputs);
  inputs = recovering_inputs;
  inputs.frost_active = true;
  assert_no_handover_hold(inputs);
  inputs = recovering_inputs;
  inputs.commissioning_active = true;
  assert_no_handover_hold(inputs);
  inputs = recovering_inputs;
  inputs.override_active = true;
  assert_no_handover_hold(inputs);

  // BOOTSTRAP/SUSPECT have no explicit fallback cause and stay fail-closed.
  inputs = eligible_fallback_evaluation_inputs();
  inputs.raw_availability_complete = false;
  inputs.every_unavailable_hp_has_fallback_cause = false;
  inputs.all_hp_outputs_safe = false;
  evaluation = evaluate_fallback(inputs);
  assert(!evaluation.fallback_requested);
  assert(!evaluation.cm3_handover_wait);

  // A confirmed sub-5°C cold start is a temporary HP-unavailable cause.
  // It may enter CM4 only when the normal output and boiler guards pass.
  inputs = eligible_fallback_evaluation_inputs();
  inputs.available_hp_count = 2;
  inputs.every_unavailable_hp_has_fallback_cause = false;
  inputs.cold_start_blocked = true;
  evaluation = evaluate_fallback(inputs);
  assert(evaluation.no_hp_available_confirmed);
  assert(evaluation.fallback_requested);
  assert(evaluation.decision.cm4_allowed);

  inputs.all_hp_outputs_safe = false;
  evaluation = evaluate_fallback(inputs);
  assert(!evaluation.no_hp_available_confirmed);
  assert(evaluation.fallback_requested);
  assert(!evaluation.decision.cm4_allowed);
}

void test_degraded_link_loss_cm4_evaluation() {
  using oq_hp_fallback::FallbackBlockReason;
  using oq_hp_supervisory::evaluate_fallback;

  // Single HP, confirmed link loss, stop confirmation still pending: the
  // fallback is requested but CM4 stays blocked on the HP output state.
  auto inputs = eligible_fallback_evaluation_inputs();
  inputs.all_hp_outputs_safe = false;
  auto evaluation = evaluate_fallback(inputs);
  assert(evaluation.fallback_requested);
  assert(!evaluation.decision.cm4_allowed);
  assert(evaluation.decision.block_reason == FallbackBlockReason::HP_OUTPUT_STATE_UNSAFE);

  // After the stop-confirmation timeout the degraded output gate clears and
  // CM4 is allowed while every other guard stays green.
  inputs.all_hp_outputs_safe = true;
  evaluation = evaluate_fallback(inputs);
  assert(evaluation.fallback_requested);
  assert(evaluation.decision.cm4_allowed);

  // RECOVERING after a confirmed loss keeps CM4: raw availability is still
  // incomplete, but the degraded gate completes it.
  inputs.raw_availability_complete = false;
  evaluation = evaluate_fallback(inputs);
  assert(evaluation.availability_complete);
  assert(evaluation.no_hp_available_confirmed);
  assert(evaluation.decision.cm4_allowed);

  // Every independent CM4 guard still blocks with a clear degraded gate.
  const auto degraded = inputs;
  const auto assert_still_blocked = [&](const oq_hp_supervisory::FallbackEvaluationInputs& candidate,
                                        FallbackBlockReason expected) {
    const auto blocked = evaluate_fallback(candidate);
    assert(!blocked.decision.cm4_allowed);
    assert(blocked.decision.block_reason == expected);
  };
  auto candidate = degraded;
  candidate.flow_valid = false;
  assert_still_blocked(candidate, FallbackBlockReason::FLOW_UNAVAILABLE);
  candidate = degraded;
  candidate.flow_sufficient = false;
  assert_still_blocked(candidate, FallbackBlockReason::FLOW_INSUFFICIENT);
  candidate = degraded;
  candidate.supply_temperature_valid = false;
  assert_still_blocked(candidate, FallbackBlockReason::SUPPLY_TEMPERATURE_UNAVAILABLE);
  candidate = degraded;
  candidate.boiler_guards_clear = false;
  assert_still_blocked(candidate, FallbackBlockReason::BOILER_GUARD_BLOCKED);
  candidate = degraded;
  candidate.cooling_active = true;
  assert_still_blocked(candidate, FallbackBlockReason::COOLING_ACTIVE);
  candidate = degraded;
  candidate.frost_active = true;
  assert_still_blocked(candidate, FallbackBlockReason::FROST_ACTIVE);
  candidate = degraded;
  candidate.commissioning_active = true;
  assert_still_blocked(candidate, FallbackBlockReason::COMMISSIONING_ACTIVE);
  candidate = degraded;
  candidate.override_active = true;
  assert_still_blocked(candidate, FallbackBlockReason::OVERRIDE_ACTIVE);
  candidate = degraded;
  candidate.heating_demand = false;
  assert_still_blocked(candidate, FallbackBlockReason::NO_HEATING_DEMAND);
  candidate = degraded;
  candidate.fallback_enabled = false;
  assert_still_blocked(candidate, FallbackBlockReason::FALLBACK_DISABLED);
  candidate = degraded;
  candidate.available_hp_count = 1;
  assert_still_blocked(candidate, FallbackBlockReason::HP_AVAILABLE);
}

void test_heating_mode_decisions() {
  using oq_hp_supervisory::decide_heating_mode;
  using oq_hp_supervisory::HeatingModeInputs;

  HeatingModeInputs inputs;
  inputs.current_mode = 3;
  inputs.base_target = 2;
  inputs.cm3_fallback_handover_wait = true;
  auto decision = decide_heating_mode(inputs);
  assert(decision.desired_mode == 3);
  assert(decision.start_cm1_for_mode == -1);

  inputs.cm3_fallback_handover_wait = false;
  inputs.base_target = 4;
  decision = decide_heating_mode(inputs);
  assert(decision.desired_mode == 4);
  assert(decision.start_cm1_for_mode == -1);

  inputs.current_mode = 2;
  decision = decide_heating_mode(inputs);
  assert(decision.desired_mode == 1);
  assert(decision.start_cm1_for_mode == 4);

  inputs.current_mode = 4;
  inputs.base_target = 2;
  inputs.cm4_resume_mode = 3;
  inputs.hp_available = true;
  inputs.power_house_active = true;
  inputs.boiler_assist_enabled = true;
  decision = decide_heating_mode(inputs);
  assert(decision.desired_mode == 3);
  assert(decision.start_cm1_for_mode == -1);

  inputs.power_house_active = false;
  decision = decide_heating_mode(inputs);
  assert(decision.desired_mode == 2);

  inputs.base_target = 1;
  decision = decide_heating_mode(inputs);
  assert(decision.desired_mode == 1);
  assert(decision.flow_interlock_hold);
}

void test_control_mode_log_classification() {
  using oq_hp_supervisory::classify_control_mode_transition;
  using oq_hp_supervisory::ControlModeLogCodes;

  const ControlModeLogCodes codes{
      {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21},
      {30, 31, 32},
      {40, 41, 42, 43},
  };

  auto decision = classify_control_mode_transition({3, 4, true, false, 10}, codes);
  assert(decision.reason == 11);
  assert(decision.severity == 32);
  assert(decision.from_state == 42);
  assert(decision.to_state == 43);

  decision = classify_control_mode_transition({4, 3, false, false, 10}, codes);
  assert(decision.reason == 12);
  assert(decision.severity == 30);
  assert(decision.from_state == 43);
  assert(decision.to_state == 42);

  decision = classify_control_mode_transition({4, 1, true, false, 10}, codes);
  assert(decision.reason == 13);
  assert(decision.severity == 31);

  decision = classify_control_mode_transition({4, 1, false, false, 77}, codes);
  assert(decision.reason == 77);
  assert(decision.severity == 30);

  decision = classify_control_mode_transition({4, 1, false, false, 10}, codes);
  assert(decision.reason == 21);
  assert(decision.severity == 31);

  decision = classify_control_mode_transition({4, 0, false, false, 10}, codes);
  assert(decision.reason == 20);
  assert(decision.severity == 30);

  decision = classify_control_mode_transition({4, 100, false, false, 10}, codes);
  assert(decision.reason == 14);
  assert(decision.severity == 30);

  decision = classify_control_mode_transition({0, 1, true, false, 10}, codes);
  assert(decision.reason == 13);
  assert(decision.severity == 31);

  decision = classify_control_mode_transition({2, 98, false, true, 10}, codes);
  // Override classification retains its priority over the target mode.
  assert(decision.reason == 15);

  decision = classify_control_mode_transition({0, 1, false, false, 77}, codes);
  assert(decision.reason == 77);

  decision = classify_control_mode_transition({5, 0, false, false, 10}, codes);
  assert(decision.reason == 19);
  assert(decision.to_state == 40);
}

}  // namespace

int main() {
  test_heating_enable_gate();
  test_frost_control_mode_remains_independent_of_heating_request();
  test_cold_start_temperature_bands();
  test_cold_start_availability_matrix();
  using oq_hp_supervisory::Cm4ResumeTracker;
  using oq_hp_supervisory::fallback_availability_is_confirmed;
  using oq_hp_supervisory::recovered_heating_mode;

  assert(fallback_availability_is_confirmed(true, false, false));
  // LOST -> RECOVERING may enter CM4 only after every unavailable HP has an
  // explicit fallback cause and fresh stopped-output confirmation.
  assert(fallback_availability_is_confirmed(false, true, true));
  assert(!fallback_availability_is_confirmed(false, true, false));
  assert(!fallback_availability_is_confirmed(false, false, true));

  Cm4ResumeTracker resume;
  resume.observe_fallback_request(true, 2);
  assert(resume.origin_valid());
  assert(resume.resume_mode() == 2);
  // Moving through CM1 while waiting for a physical stop must not replace
  // the mode captured at the first fallback-request edge.
  resume.observe_fallback_request(true, 1);
  assert(resume.resume_mode() == 2);
  resume.finish_after_decision(true, false, false, true);
  assert(!resume.origin_valid());

  // An earlier CM3 episode must not leak into a later CM2/CM1 cycle.
  resume.observe_fallback_request(true, 3);
  assert(resume.resume_mode() == 3);
  resume.finish_after_decision(false, false, false, false);
  assert(!resume.origin_valid());
  resume.observe_fallback_request(true, 1);
  assert(resume.resume_mode() == 2);

  // Temporary fallback guard loss keeps provenance for the same unresolved
  // incident; an external owner explicitly ends it.
  resume.observe_fallback_request(false, 1);
  resume.finish_after_decision(true, false, false, false);
  assert(resume.origin_valid());
  assert(resume.resume_mode() == 2);
  resume.finish_after_decision(true, true, false, false);
  assert(!resume.origin_valid());

  assert(recovered_heating_mode(3, true, true, true) == 3);
  assert(recovered_heating_mode(3, true, false, true) == 2);
  assert(recovered_heating_mode(3, true, true, false) == 2);
  assert(recovered_heating_mode(2, true, true, true) == 2);
  assert(recovered_heating_mode(3, false, true, true) == 1);

  test_fallback_evaluation_and_recovering_handover();
  test_post_dhw_odu_failure_keeps_the_compressor_stopped_and_releases_safe_boiler_fallback();
  test_degraded_link_loss_cm4_evaluation();
  test_heating_mode_decisions();
  test_control_mode_log_classification();
  return 0;
}
