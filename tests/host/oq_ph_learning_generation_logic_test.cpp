#include <assert.h>
#include <stdint.h>

#include "../../openquatt/includes/learning/oq_ph_learning_generation_logic.h"
#include "../../openquatt/includes/learning/oq_ph_learning_live_logic.h"

namespace {
using namespace oq_power_house::learning;

constexpr uint64_t kOwnerA = 1001;
constexpr uint64_t kOwnerB = 1002;
constexpr uint64_t kNowMs = 1000000;
constexpr uint32_t kEpochS = 1800000000;

template <typename T>
PhysicalMeasurement<T> measurement(T value, uint32_t id, PhysicalUnit unit = PhysicalUnit::SYSTEM) {
  PhysicalMeasurement<T> result;
  result.value = value;
  result.valid = true;
  result.source = {PhysicalSourceKind::MODBUS_REGISTER, id, unit};
  result.source_generation = 1000U + id;
  result.received_monotonic_ms = kNowMs - 1;
  result.timing = {5000, 1000};
  result.provenance = MeasurementProvenance::PHYSICAL_RECEIPT;
  return result;
}

HeatPumpRawMeasurements heat_pump(PhysicalUnit unit, uint32_t id_base, float water_in_c, float water_out_c) {
  HeatPumpRawMeasurements result;
  result.present = true;
  result.water_in_c = measurement(water_in_c, id_base + 1, unit);
  result.water_out_c = measurement(water_out_c, id_base + 2, unit);
  result.mode = measurement(HeatPumpMode::HEATING, id_base + 3, unit);
  result.compressor_active = measurement(true, id_base + 4, unit);
  result.defrost_active = measurement(false, id_base + 5, unit);
  result.valve_transition_active = measurement(false, id_base + 6, unit);
  result.oil_return_active = measurement(false, id_base + 7, unit);
  return result;
}

LearningSourceInput valid_input() {
  LearningSourceInput input;
  input.monotonic_ms = kNowMs;
  input.epoch_s = kEpochS;
  input.topology = HydronicTopology::SINGLE;
  input.calorimetry.meter_boundary = MeterBoundary::SINGLE_HEAT_PUMP_CIRCUIT;
  input.calorimetry.fluid_model = FluidHeatCapacityModel::WATER_CP_4180;
  input.calorimetry.calorimetry_generation = 41;
  input.calorimetry.uncertainty_proven = true;
  input.calorimetry.heat_uncertainty_w = 100.0f;
  input.calorimetry.max_flow_lph = 2000.0f;
  input.room_c = measurement(20.0f, 1);
  input.setpoint_c = measurement(20.0f, 2);
  input.outside_c = measurement(5.0f, 3, PhysicalUnit::HP1);
  input.flow_lph = measurement(1000.0f, 4, PhysicalUnit::HP1);
  input.hp1 = heat_pump(PhysicalUnit::HP1, 100, 30.0f, 32.0f);
  input.boiler_heat = measurement(BoilerHeatState::NO_HEAT, 5);
  input.operation.captured_monotonic_ms = kNowMs - 1;
  input.operation.control_mode_valid = true;
  input.operation.control_mode = LearningControlMode::HEATING;
  input.operation.active_limit_valid = true;
  input.operation.service_or_ota_valid = true;
  input.operation.setpoint_recovery_valid = true;
  input.operation.comfort_acceptable_valid = true;
  input.operation.comfort_acceptable = true;
  return input;
}

PhysicalContextTokens physical_tokens() { return {11, 12, 13}; }

ControlContextTokens control_tokens() { return {21, 22, 23, 24}; }

template <typename T>
void retime(PhysicalMeasurement<T>& value, uint64_t received_ms) {
  value.received_monotonic_ms = received_ms;
}

void retime(HeatPumpRawMeasurements& heat_pump, uint64_t received_ms) {
  retime(heat_pump.water_in_c, received_ms);
  retime(heat_pump.water_out_c, received_ms);
  retime(heat_pump.mode, received_ms);
  retime(heat_pump.compressor_active, received_ms);
  retime(heat_pump.defrost_active, received_ms);
  retime(heat_pump.valve_transition_active, received_ms);
  retime(heat_pump.oil_return_active, received_ms);
}

void advance(LearningSourceInput& input, uint64_t delta_ms) {
  input.monotonic_ms += delta_ms;
  input.epoch_s += static_cast<uint32_t>(delta_ms / 1000ULL);
  const uint64_t received_ms = input.monotonic_ms - 1;
  retime(input.room_c, received_ms);
  retime(input.setpoint_c, received_ms);
  retime(input.outside_c, received_ms);
  retime(input.flow_lph, received_ms);
  retime(input.hp1, received_ms);
  if (input.hp2.present) retime(input.hp2, received_ms);
  retime(input.boiler_heat, received_ms);
  input.operation.captured_monotonic_ms = received_ms;
}

struct Harness {
  GenerationOwnerState owner;
  SegmentAccumulator accumulator;
  SegmentRecord storage[4]{};
  RecordBuffer records{storage, 0, 4};
  QualityConfig quality;
};

OwnedSourceObserveResult start(Harness& harness, uint64_t owner_token, const LearningSourceInput& input,
                               PhysicalContextTokens physical = physical_tokens(),
                               ControlContextTokens control = control_tokens()) {
  return start_owned_source_input(harness.owner, owner_token, physical, control, harness.accumulator, harness.records,
                                  input, harness.quality);
}

OwnedSourceObserveResult observe(Harness& harness, uint64_t owner_token, const LearningSourceInput& input,
                                 PhysicalContextTokens physical = physical_tokens(),
                                 ControlContextTokens control = control_tokens()) {
  return observe_owned_source_input(harness.owner, owner_token, physical, control, harness.accumulator, harness.records,
                                    input, harness.quality);
}

void assert_generations(const GenerationDecision& decision, uint32_t source, uint32_t physical, uint32_t control) {
  assert(decision.source_generation == source);
  assert(decision.physical_generation == physical);
  assert(decision.control_generation == control);
}

void test_explicit_start_clears_scope_and_stamps_snapshot() {
  Harness harness;
  harness.records.count = 1;
  harness.accumulator.active = true;
  const LearningSourceInput input = valid_input();

  const GenerationDecision no_reset = start_generation_owner(harness.owner, kOwnerA, DatasetScopeReset::NOT_CLEARED,
                                                             input, physical_tokens(), control_tokens());
  assert(no_reset.status == GenerationStatus::DATASET_SCOPE_NOT_CLEARED);
  assert(!harness.owner.initialized);

  const OwnedSourceObserveResult result = start(harness, kOwnerA, input);
  assert(result.generation.status == GenerationStatus::INITIALIZED);
  assert(result.generation.accepted);
  assert(result.generation.invalidate_dataset);
  assert(result.generation.invalidated_contexts ==
         (GENERATION_CHANGE_SOURCE | GENERATION_CHANGE_PHYSICAL | GENERATION_CHANGE_CONTROL));
  assert_generations(result.generation, 1, 1, 1);
  assert(harness.records.count == 0);
  assert(harness.accumulator.active);
  assert(harness.accumulator.source_generation == 1);
  assert(harness.accumulator.physical_context_generation == 1);
  assert(harness.accumulator.control_generation == 1);
  assert(result.observation.source.measurement_valid);
}

void test_observe_requires_current_owner_and_late_callback_cannot_take_ownership() {
  Harness harness;
  LearningSourceInput input = valid_input();
  assert(start(harness, kOwnerA, input).generation.accepted);

  advance(input, 1000);
  const GenerationDecision before_start = start_generation_owner(harness.owner, kOwnerB, DatasetScopeReset::NOT_CLEARED,
                                                                 input, physical_tokens(), control_tokens());
  assert(before_start.status == GenerationStatus::DATASET_SCOPE_NOT_CLEARED);
  assert(harness.owner.owner_token == kOwnerA);

  const OwnedSourceObserveResult switched = start(harness, kOwnerB, input);
  assert(switched.generation.status == GenerationStatus::OWNER_REINITIALIZED);
  assert(switched.generation.accepted);
  assert_generations(switched.generation, 2, 2, 2);
  assert(harness.owner.owner_token == kOwnerB);

  advance(input, 1000);
  harness.records.count = 1;
  const OwnedSourceObserveResult late_a = observe(harness, kOwnerA, input);
  assert(late_a.generation.status == GenerationStatus::OWNER_MISMATCH);
  assert(!late_a.generation.accepted);
  assert(harness.owner.owner_token == kOwnerB);
  assert_generations(late_a.generation, 2, 2, 2);
  assert(harness.records.count == 1);
  assert(!harness.accumulator.active);

  const GenerationDecision reused_a = start_generation_owner(harness.owner, kOwnerA, DatasetScopeReset::CLEARED, input,
                                                             physical_tokens(), control_tokens());
  assert(reused_a.status == GenerationStatus::NON_MONOTONIC_OWNER);
  assert(harness.owner.owner_token == kOwnerB);

  GenerationOwnerState never_started;
  const GenerationDecision missing_owner =
      observe_generation(never_started, kOwnerA, input, physical_tokens(), control_tokens());
  assert(missing_owner.status == GenerationStatus::OWNER_NOT_INITIALIZED);
}

void test_unchanged_configuration_preserves_segment_and_dataset() {
  Harness harness;
  LearningSourceInput input = valid_input();
  assert(start(harness, kOwnerA, input).generation.accepted);
  harness.records.count = 1;
  advance(input, 1000);

  const OwnedSourceObserveResult result = observe(harness, kOwnerA, input);
  assert(result.generation.status == GenerationStatus::READY);
  assert(result.generation.accepted);
  assert(result.generation.changes == GENERATION_CHANGE_NONE);
  assert(result.generation.invalidated_contexts == GENERATION_CHANGE_NONE);
  assert(!result.generation.reset_segment);
  assert(!result.generation.invalidate_dataset);
  assert_generations(result.generation, 1, 1, 1);
  assert(harness.records.count == 1);
  assert(harness.accumulator.active);
  assert(harness.accumulator.integrated_duration_s == 1.0);
}

void test_exact_source_policy_changes_and_a_b_a_are_new_generations() {
  Harness harness;
  LearningSourceInput input = valid_input();
  assert(start(harness, kOwnerA, input).generation.accepted);

  const uint32_t original_id = input.outside_c.source.id;
  advance(input, 1000);
  input.outside_c.source.id = 9001;
  input.outside_c.source_generation = 9002;
  const auto route_b = observe(harness, kOwnerA, input).generation;
  assert(route_b.changes == GENERATION_CHANGE_SOURCE);
  assert(route_b.invalidate_dataset && route_b.reset_segment);
  assert_generations(route_b, 2, 1, 1);

  advance(input, 1000);
  input.outside_c.source.id = original_id;
  input.outside_c.source_generation = 1003;
  const auto route_a_again = observe(harness, kOwnerA, input).generation;
  assert(route_a_again.changes == GENERATION_CHANGE_SOURCE);
  assert_generations(route_a_again, 3, 1, 1);

  advance(input, 1000);
  ++input.flow_lph.timing.max_age_ms;
  const auto timing = observe(harness, kOwnerA, input).generation;
  assert(timing.changes == GENERATION_CHANGE_SOURCE);
  assert_generations(timing, 4, 1, 1);

  advance(input, 1000);
  input.flow_lph.provenance = MeasurementProvenance::HELD;
  const OwnedSourceObserveResult provenance = observe(harness, kOwnerA, input);
  assert(provenance.generation.changes == GENERATION_CHANGE_SOURCE);
  assert_generations(provenance.generation, 5, 1, 1);
  assert(!provenance.observation.source.measurement_valid);
}

void test_physical_and_control_changes_have_distinct_invalidation_scope() {
  Harness harness;
  LearningSourceInput input = valid_input();
  assert(start(harness, kOwnerA, input).generation.accepted);
  harness.records.count = 1;

  advance(input, 1000);
  PhysicalContextTokens physical = physical_tokens();
  ++physical.sensor_calibration;
  auto result = observe(harness, kOwnerA, input, physical);
  assert(result.generation.changes == GENERATION_CHANGE_PHYSICAL);
  assert(result.generation.invalidate_dataset);
  assert(harness.records.count == 0);
  assert_generations(result.generation, 1, 2, 1);

  harness.records.count = 1;
  advance(input, 1000);
  input.calorimetry.heat_uncertainty_w += 1.0f;
  result = observe(harness, kOwnerA, input, physical);
  assert(result.generation.changes == GENERATION_CHANGE_PHYSICAL);
  assert(result.generation.invalidate_dataset);
  assert(harness.records.count == 0);
  assert_generations(result.generation, 1, 3, 1);

  harness.records.count = 1;
  advance(input, 1000);
  ControlContextTokens control = control_tokens();
  ++control.active_model;
  result = observe(harness, kOwnerA, input, physical, control);
  assert(result.generation.changes == GENERATION_CHANGE_CONTROL);
  assert(result.generation.invalidated_contexts == GENERATION_CHANGE_CONTROL);
  assert(!result.generation.invalidate_dataset);
  assert(result.generation.reset_segment);
  assert(harness.records.count == 1);
  assert_generations(result.generation, 1, 3, 2);

  advance(input, 1000);
  ++input.calorimetry.calorimetry_generation;
  ++input.calorimetry.max_flow_lph;
  ++physical.installation;
  ++control.comfort_policy;
  ++input.room_c.source_generation;
  result = observe(harness, kOwnerA, input, physical, control);
  assert(result.generation.changes ==
         (GENERATION_CHANGE_SOURCE | GENERATION_CHANGE_PHYSICAL | GENERATION_CHANGE_CONTROL));
  assert_generations(result.generation, 2, 4, 3);
}

void test_unknown_configuration_invalidates_then_recovery_gets_new_generations() {
  Harness harness;
  LearningSourceInput input = valid_input();
  assert(start(harness, kOwnerA, input).generation.accepted);
  harness.records.count = 1;

  advance(input, 1000);
  input.flow_lph.source_generation = 0;
  const OwnedSourceObserveResult invalid = observe(harness, kOwnerA, input);
  assert(invalid.generation.status == GenerationStatus::INVALID_CONFIGURATION);
  assert(!invalid.generation.accepted);
  assert(invalid.generation.invalidate_dataset);
  assert(invalid.generation.invalidated_contexts ==
         (GENERATION_CHANGE_SOURCE | GENERATION_CHANGE_PHYSICAL | GENERATION_CHANGE_CONTROL));
  assert_generations(invalid.generation, 2, 2, 2);
  assert(harness.records.count == 0);
  assert(!harness.accumulator.active);

  advance(input, 1000);
  input.flow_lph.source_generation = 1004;
  const OwnedSourceObserveResult recovered = observe(harness, kOwnerA, input);
  assert(recovered.generation.accepted);
  assert(recovered.generation.changes ==
         (GENERATION_CHANGE_SOURCE | GENERATION_CHANGE_PHYSICAL | GENERATION_CHANGE_CONTROL));
  assert_generations(recovered.generation, 3, 3, 3);
}

void test_generation_overflow_blocks_owner_without_partial_increment() {
  Harness harness;
  LearningSourceInput input = valid_input();
  assert(start(harness, kOwnerA, input).generation.accepted);
  harness.owner.source_generation = UINT32_MAX;
  harness.owner.physical_generation = 9;
  harness.owner.control_generation = 11;
  harness.records.count = 1;

  advance(input, 1000);
  ++input.room_c.source_generation;
  PhysicalContextTokens physical = physical_tokens();
  ++physical.installation;
  const OwnedSourceObserveResult overflow = observe(harness, kOwnerA, input, physical);
  assert(overflow.generation.status == GenerationStatus::GENERATION_OVERFLOW);
  assert(!overflow.generation.accepted);
  assert(harness.owner.blocked);
  assert_generations(overflow.generation, UINT32_MAX, 9, 11);
  assert(harness.records.count == 0);

  const auto blocked = observe(harness, kOwnerA, input, physical).generation;
  assert(blocked.status == GenerationStatus::OWNER_BLOCKED);
  assert(!blocked.accepted);
  const GenerationDecision cannot_reset_at_new_owner =
      start_generation_owner(harness.owner, kOwnerB, DatasetScopeReset::CLEARED, input, physical, control_tokens());
  assert(cannot_reset_at_new_owner.status == GenerationStatus::OWNER_BLOCKED);
  assert(harness.owner.owner_token == kOwnerA);
}

void test_reboot_state_requires_explicit_unique_owner_start() {
  LearningSourceInput input = valid_input();
  Harness rebooted;
  rebooted.records.count = 1;
  assert(observe(rebooted, kOwnerA, input).generation.status == GenerationStatus::OWNER_NOT_INITIALIZED);
  assert(rebooted.records.count == 0);

  const auto started = start(rebooted, kOwnerB, input);
  assert(started.generation.status == GenerationStatus::INITIALIZED);
  assert(started.generation.accepted);
  assert(rebooted.records.count == 0);
  assert(rebooted.owner.owner_token == kOwnerB);
  assert_generations(started.generation, 1, 1, 1);
}

void test_malformed_caller_owned_state_fails_closed() {
  LearningSourceInput input = valid_input();
  Harness harness;
  harness.owner.initialized = true;
  harness.owner.owner_token = kOwnerA;
  harness.owner.source_generation = 0;
  harness.owner.physical_generation = 1;
  harness.owner.control_generation = 1;
  harness.records.count = 1;

  const OwnedSourceObserveResult result = observe(harness, kOwnerA, input);
  assert(result.generation.status == GenerationStatus::INVALID_OWNER_STATE);
  assert(!result.generation.accepted);
  assert(result.generation.invalidate_dataset);
  assert(result.generation.invalidated_contexts ==
         (GENERATION_CHANGE_SOURCE | GENERATION_CHANGE_PHYSICAL | GENERATION_CHANGE_CONTROL));
  assert(harness.records.count == 0);
  assert(!harness.accumulator.active);
}

void test_cm2_contract_keeps_source_generation_outside_cm2() {
  Harness harness;
  LearningSourceInput input = valid_input();
  input.boiler_heat = learning_cm2_boiler_contract(
      true, 2,
      {true, false, false, NAN, NAN, oq_boiler::COMMAND_SOURCE_NONE, static_cast<uint32_t>(input.monotonic_ms)}, false,
      false, input.monotonic_ms, false);
  const auto started = start(harness, kOwnerA, input);
  assert(started.generation.accepted && started.observation.source.measurement_valid);
  harness.records.count = 1;

  advance(input, 1000);
  input.boiler_heat = learning_cm2_boiler_contract(
      true, 0,
      {true, false, false, NAN, NAN, oq_boiler::COMMAND_SOURCE_NONE, static_cast<uint32_t>(input.monotonic_ms)}, false,
      false, input.monotonic_ms, false);
  const auto outside_cm2 = observe(harness, kOwnerA, input);
  assert(outside_cm2.generation.accepted);
  assert(outside_cm2.generation.changes == GENERATION_CHANGE_NONE);
  assert(!outside_cm2.generation.invalidate_dataset);
  assert(!outside_cm2.observation.source.measurement_valid);
  assert(harness.records.count == 1);
  assert_generations(outside_cm2.generation, 1, 1, 1);

  advance(input, 1000);
  input.boiler_heat = learning_cm2_boiler_contract(
      true, 2,
      {true, false, false, NAN, NAN, oq_boiler::COMMAND_SOURCE_NONE, static_cast<uint32_t>(input.monotonic_ms)}, false,
      false, input.monotonic_ms, false);
  const auto returned_cm2 = observe(harness, kOwnerA, input);
  assert(returned_cm2.generation.accepted);
  assert(returned_cm2.generation.changes == GENERATION_CHANGE_NONE);
  assert(!returned_cm2.generation.invalidate_dataset);
  assert(returned_cm2.observation.source.measurement_valid);
  assert(harness.records.count == 1);
  assert_generations(returned_cm2.generation, 1, 1, 1);

  // Optional OT activity veto breaks the sample/window, not source ownership
  // or retained historical records. Loss of the optional receipt is not a veto.
  const auto contract = input.boiler_heat;
  input.boiler_heat = learning_boiler_heat(true, contract, learning_boiler_status(8U, input.monotonic_ms, true, true));
  const auto veto = observe(harness, kOwnerA, input);
  assert(veto.generation.accepted && veto.generation.changes == GENERATION_CHANGE_NONE);
  assert(!veto.observation.source.measurement_valid);
  assert(!veto.generation.invalidate_dataset && harness.records.count == 1);
  input.boiler_heat = learning_boiler_heat(true, contract, {});
  const auto resumed = observe(harness, kOwnerA, input);
  assert(resumed.generation.accepted && resumed.generation.changes == GENERATION_CHANGE_NONE);
  assert(resumed.observation.source.measurement_valid);
  assert(!resumed.generation.invalidate_dataset && harness.records.count == 1);
}

}  // namespace

int main() {
  test_explicit_start_clears_scope_and_stamps_snapshot();
  test_observe_requires_current_owner_and_late_callback_cannot_take_ownership();
  test_unchanged_configuration_preserves_segment_and_dataset();
  test_exact_source_policy_changes_and_a_b_a_are_new_generations();
  test_physical_and_control_changes_have_distinct_invalidation_scope();
  test_unknown_configuration_invalidates_then_recovery_gets_new_generations();
  test_generation_overflow_blocks_owner_without_partial_increment();
  test_reboot_state_requires_explicit_unique_owner_start();
  test_malformed_caller_owned_state_fails_closed();
  test_cm2_contract_keeps_source_generation_outside_cm2();
  return 0;
}
