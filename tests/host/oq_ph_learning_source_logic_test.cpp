#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/learning/oq_ph_learning_source_logic.h"

namespace {
using namespace oq_power_house::learning;

constexpr uint64_t kNowMs = 1000000;
constexpr uint32_t kEpochS = 1800000000;

template <typename T>
PhysicalMeasurement<T> measurement(T value, uint32_t id, PhysicalUnit unit = PhysicalUnit::SYSTEM,
                                   uint64_t received_ms = kNowMs - 100) {
  PhysicalMeasurement<T> result;
  result.value = value;
  result.valid = true;
  result.source = {PhysicalSourceKind::MODBUS_REGISTER, id, unit};
  result.source_generation = 1000U + id;
  result.received_monotonic_ms = received_ms;
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

LearningSourceInput valid_input(HydronicTopology topology = HydronicTopology::SINGLE) {
  LearningSourceInput input;
  input.monotonic_ms = kNowMs;
  input.epoch_s = kEpochS;
  input.context_revision = 17;
  input.topology = topology;
  input.room_c = measurement(20.0f, 1);
  input.setpoint_c = measurement(20.0f, 2);
  input.outside_c = measurement(5.0f, 3, PhysicalUnit::HP1);
  input.flow_lph = measurement(1000.0f, 4, PhysicalUnit::HP1);
  input.hp1 = heat_pump(PhysicalUnit::HP1, 100, 30.0f, 32.0f);
  if (topology == HydronicTopology::DUO_SERIES) input.hp2 = heat_pump(PhysicalUnit::HP2, 200, 32.0f, 35.0f);
  input.boiler_heat = measurement(BoilerHeatState::NO_HEAT, 5);
  input.operation.captured_monotonic_ms = kNowMs - 100;
  input.operation.captured_context_revision = input.context_revision;
  input.operation.control_mode_valid = true;
  input.operation.control_mode = LearningControlMode::HEATING;
  input.operation.active_limit_valid = true;
  input.operation.service_or_ota_valid = true;
  input.operation.setpoint_recovery_valid = true;
  input.operation.comfort_acceptable_valid = true;
  input.operation.comfort_acceptable = true;
  return input;
}

SnapshotBuildResult build(const LearningSourceInput& input) {
  QualityConfig quality;
  return build_learning_snapshot(input, quality);
}

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

void retime(LearningSourceInput& input, uint64_t now_ms) {
  input.monotonic_ms = now_ms;
  const uint64_t received_ms = now_ms - 1;
  retime(input.room_c, received_ms);
  retime(input.setpoint_c, received_ms);
  retime(input.outside_c, received_ms);
  retime(input.flow_lph, received_ms);
  retime(input.hp1, received_ms);
  if (input.hp2.present) retime(input.hp2, received_ms);
  retime(input.boiler_heat, received_ms);
  input.operation.captured_monotonic_ms = received_ms;
  input.operation.captured_context_revision = input.context_revision;
}

void assert_failed(const SnapshotBuildResult& result, SnapshotSourceStatus status) {
  assert(result.status == status);
  assert(result.has_snapshot);
  assert(!result.measurement_valid);
  assert(result.snapshot.monotonic_ms == kNowMs);
  assert(result.snapshot.epoch_s == kEpochS);
  assert(isnan(result.snapshot.heat_to_water_w));
  assert(result.invalid_reasons != INVALID_NONE);
  assert(result.snapshot.invalid_reasons == result.invalid_reasons);
}

void test_single_and_series_calorimetry() {
  const auto single_input = valid_input();
  const auto single_calorimetry = evaluate_calorimetry(single_input, QualityConfig{});
  const auto single =
      build_learning_snapshot(single_input, QualityConfig{}, SnapshotPurpose::STRUCTURAL_BATCH, &single_calorimetry);
  assert(single.status == SnapshotSourceStatus::OK && single.has_snapshot);
  assert(single_calorimetry.valid);
  assert(single.snapshot.heat_to_water_w == single_calorimetry.heat_to_water_w);
  assert(single.snapshot.mean_water_c == single_calorimetry.mean_water_c);
  assert(fabsf(single.snapshot.heat_to_water_w - (1000.0f / 3600.0f * 4180.0f * 2.0f)) < 0.01f);
  assert(fabsf(single.snapshot.mean_water_c - 31.0f) < 0.001f);

  auto duo_input = valid_input(HydronicTopology::DUO_SERIES);
  auto duo = build(duo_input);
  assert(duo.status == SnapshotSourceStatus::OK && duo.has_snapshot);
  const float expected_w = 1000.0f / 3600.0f * 4180.0f * (2.0f + 3.0f);
  assert(fabsf(duo.snapshot.heat_to_water_w - expected_w) < 0.01f);
  assert(fabsf(duo.snapshot.mean_water_c - 32.25f) < 0.001f);
}

void test_missing_and_unsupported_topology_fail_closed() {
  auto missing_duo = valid_input(HydronicTopology::DUO_SERIES);
  missing_duo.hp2.present = false;
  assert_failed(build(missing_duo), SnapshotSourceStatus::MISSING_REQUIRED_UNIT);

  auto parallel = valid_input(HydronicTopology::DUO_PARALLEL);
  assert_failed(build(parallel), SnapshotSourceStatus::INVALID_TOPOLOGY);
  auto unknown = valid_input();
  unknown.topology = HydronicTopology::UNKNOWN;
  assert_failed(build(unknown), SnapshotSourceStatus::INVALID_TOPOLOGY);
}

void test_freshness_and_skew_are_per_field() {
  auto stale = valid_input();
  stale.room_c.received_monotonic_ms = kNowMs - 5001;
  assert_failed(build(stale), SnapshotSourceStatus::SOURCE_STALE);

  auto skewed = valid_input();
  skewed.room_c.received_monotonic_ms = kNowMs - 2000;
  skewed.room_c.timing.max_age_ms = 10000;
  skewed.room_c.timing.max_skew_ms = 500;
  assert_failed(build(skewed), SnapshotSourceStatus::TIME_SKEW);

  auto no_contract = valid_input();
  no_contract.flow_lph.timing.max_skew_ms = 0;
  assert_failed(build(no_contract), SnapshotSourceStatus::INVALID_TIMING_CONTRACT);

  auto unbounded = valid_input();
  unbounded.room_c.timing.max_age_ms = UINT32_MAX;
  assert_failed(build(unbounded), SnapshotSourceStatus::INVALID_TIMING_CONTRACT);
  unbounded = valid_input();
  unbounded.room_c.timing.max_skew_ms = UINT32_MAX;
  assert_failed(build(unbounded), SnapshotSourceStatus::INVALID_TIMING_CONTRACT);
}

void test_context_revision_binds_each_snapshot() {
  const auto baseline = build(valid_input());
  assert(baseline.has_snapshot);
  auto changed_context_input = valid_input();
  ++changed_context_input.context_revision;
  changed_context_input.operation.captured_context_revision = changed_context_input.context_revision;
  const auto changed_context = build(changed_context_input);
  assert(changed_context.has_snapshot);
  assert(changed_context.snapshot.context_revision != baseline.snapshot.context_revision);
}

void test_unit_identity_and_distinct_temperature_paths() {
  auto duplicate = valid_input(HydronicTopology::DUO_SERIES);
  duplicate.hp2.water_in_c.source = duplicate.hp1.water_in_c.source;
  duplicate.hp2.water_in_c.source.unit = PhysicalUnit::HP2;
  duplicate.hp2.water_out_c.source = duplicate.hp1.water_out_c.source;
  duplicate.hp2.water_out_c.source.unit = PhysicalUnit::HP2;
  // Same numeric endpoint on a different physical unit is a distinct identity.
  assert(build(duplicate).measurement_valid);

  duplicate = valid_input(HydronicTopology::DUO_SERIES);
  duplicate.hp2.water_in_c.source = duplicate.hp2.water_out_c.source;
  assert_failed(build(duplicate), SnapshotSourceStatus::DUPLICATE_TEMPERATURE_SOURCE);

  auto mismatched = valid_input(HydronicTopology::DUO_SERIES);
  mismatched.hp2.water_out_c.source.unit = PhysicalUnit::HP1;
  assert_failed(build(mismatched), SnapshotSourceStatus::SOURCE_UNIT_MISMATCH);
}

void test_off_periods_keep_signed_heat_and_allow_zero_flow() {
  auto off = valid_input();
  off.hp1.mode.value = HeatPumpMode::OFF;
  off.hp1.compressor_active.value = false;
  off.hp1.water_out_c.value = 29.0f;
  const auto signed_heat = build(off);
  assert(signed_heat.has_snapshot);
  assert(signed_heat.snapshot.heat_to_water_w < 0.0f);
  assert(fabsf(signed_heat.snapshot.heat_to_water_w + 1000.0f / 3600.0f * 4180.0f) < 0.01f);

  auto zero = valid_input();
  zero.hp1.mode.value = HeatPumpMode::OFF;
  zero.hp1.compressor_active.value = false;
  zero.flow_lph.value = 0.0f;
  const auto zero_heat = build(zero);
  assert(zero_heat.has_snapshot && zero_heat.snapshot.heat_to_water_w == 0.0f);

  // Zero-flow proof does not make missing water temperatures safe: mean-water storage remains required.
  zero.hp1.water_in_c.valid = false;
  assert_failed(build(zero), SnapshotSourceStatus::MISSING_MEASUREMENT);
}

void test_invalid_values_contracts_and_unknowns_never_become_zero() {
  auto fixed_limits = valid_input(HydronicTopology::DUO_SERIES);
  fixed_limits.flow_lph.value = kPassiveMaximumFlowLph;
  fixed_limits.hp2.water_in_c.value = fixed_limits.hp1.water_out_c.value + kPassiveSeriesJunctionToleranceC;
  assert(build(fixed_limits).measurement_valid);

  auto nan_value = valid_input();
  nan_value.hp1.water_out_c.value = NAN;
  assert_failed(build(nan_value), SnapshotSourceStatus::INVALID_VALUE);

  auto cancelling_extremes = valid_input();
  cancelling_extremes.hp1.water_in_c.value = -1000.0f;
  cancelling_extremes.hp1.water_out_c.value = 1000.0f;
  assert_failed(build(cancelling_extremes), SnapshotSourceStatus::INVALID_VALUE);

  auto excessive_flow = valid_input();
  excessive_flow.flow_lph.value = kPassiveMaximumFlowLph + 1.0f;
  assert_failed(build(excessive_flow), SnapshotSourceStatus::FLOW_OUT_OF_RANGE);

  auto broken_series_junction = valid_input(HydronicTopology::DUO_SERIES);
  broken_series_junction.hp2.water_in_c.value += 2.0f;
  assert_failed(build(broken_series_junction), SnapshotSourceStatus::SERIES_JUNCTION_MISMATCH);

  auto missing_flow = valid_input();
  missing_flow.flow_lph.value = 0.0f;
  missing_flow.flow_lph.valid = false;
  assert_failed(build(missing_flow), SnapshotSourceStatus::MISSING_MEASUREMENT);

  auto boiler_unknown = valid_input();
  boiler_unknown.boiler_heat.value = BoilerHeatState::UNKNOWN;
  assert_failed(build(boiler_unknown), SnapshotSourceStatus::BOILER_UNKNOWN);

  auto invalid_boiler = valid_input();
  invalid_boiler.boiler_heat.value = static_cast<BoilerHeatState>(99);
  assert_failed(build(invalid_boiler), SnapshotSourceStatus::BOILER_UNKNOWN);

  auto missing_protection = valid_input();
  missing_protection.hp1.defrost_active.valid = false;
  assert_failed(build(missing_protection), SnapshotSourceStatus::MISSING_MEASUREMENT);
}

void test_operational_context_is_explicit_and_fail_closed() {
  auto unknown = valid_input();
  unknown.operation.active_limit_valid = false;
  assert_failed(build(unknown), SnapshotSourceStatus::OPERATIONAL_CONTEXT_UNKNOWN);

  auto wrong_mode = valid_input();
  wrong_mode.operation.control_mode = LearningControlMode::OTHER;
  assert_failed(build(wrong_mode), SnapshotSourceStatus::CONTROL_MODE_BLOCKED);

  auto limited = valid_input();
  limited.operation.active_limit = true;
  assert_failed(build(limited), SnapshotSourceStatus::ACTIVE_LIMIT);

  auto maintenance = valid_input();
  maintenance.operation.service_or_ota = true;
  assert_failed(build(maintenance), SnapshotSourceStatus::SERVICE_OR_OTA_ACTIVE);

  auto recovery = valid_input();
  recovery.operation.setpoint_recovery = true;
  assert_failed(build(recovery), SnapshotSourceStatus::SETPOINT_RECOVERY_ACTIVE);

  auto comfort_unknown = valid_input();
  comfort_unknown.operation.comfort_acceptable_valid = false;
  assert_failed(build(comfort_unknown), SnapshotSourceStatus::OPERATIONAL_CONTEXT_UNKNOWN);

  auto uncomfortable = valid_input();
  uncomfortable.operation.comfort_acceptable = false;
  assert_failed(build(uncomfortable), SnapshotSourceStatus::COMFORT_UNACCEPTABLE);

  auto stale = valid_input();
  stale.operation.captured_monotonic_ms = kNowMs - QualityConfig{}.max_interval_ms - 1U;
  assert_failed(build(stale), SnapshotSourceStatus::OPERATIONAL_CONTEXT_STALE);

  auto future = valid_input();
  future.operation.captured_monotonic_ms = kNowMs + 1U;
  assert_failed(build(future), SnapshotSourceStatus::OPERATIONAL_CONTEXT_STALE);

  auto generation_mismatch = valid_input();
  ++generation_mismatch.operation.captured_context_revision;
  assert_failed(build(generation_mismatch), SnapshotSourceStatus::CONTEXT_REVISION_MISMATCH);
}

void test_power_cap_only_excludes_a_constrained_request() {
  assert(!power_cap_binds_filtered_demand(0, 19));
  assert(!power_cap_binds_filtered_demand(19, 19));
  assert(power_cap_binds_filtered_demand(20, 19));
  assert(power_cap_binds_filtered_demand(1, 0));
}

void test_invalid_raw_event_poisoning_reaches_aggregate() {
  QualityConfig quality;
  quality.max_interval_ms = 60000;
  SegmentAccumulator accumulator;
  const uint64_t start_ms = 1000;
  const uint32_t start_epoch_s = 1800000000;
  SourceObserveResult result;
  bool saw_timestamped_invalid_event = false;
  for (uint32_t step = 0; step <= 1440; ++step) {
    auto input = valid_input(HydronicTopology::DUO_SERIES);
    retime(input, start_ms + static_cast<uint64_t>(step) * 10000ULL);
    input.epoch_s = start_epoch_s + step * 10U;
    if (step == 720) input.hp2.water_out_c.valid = false;
    result = observe_source_input(accumulator, input, quality);
    if (step == 720) {
      saw_timestamped_invalid_event = result.source.has_snapshot && !result.source.measurement_valid &&
                                      result.source.snapshot.invalid_reasons != INVALID_NONE &&
                                      result.aggregate.status == LearningStatus::INVALID_MEASUREMENT;
    }
  }
  assert(saw_timestamped_invalid_event);
  assert(result.aggregate.status == LearningStatus::INVALID_MEASUREMENT);
  assert(!result.aggregate.has_record);
  assert(!accumulator.active);
}

void test_provenance_and_active_exclusions() {
  auto republished = valid_input();
  republished.room_c.provenance = MeasurementProvenance::REPUBLISHED;
  assert_failed(build(republished), SnapshotSourceStatus::UNTRUSTED_PROVENANCE);
  auto held = valid_input();
  held.outside_c.provenance = MeasurementProvenance::HELD;
  assert_failed(build(held), SnapshotSourceStatus::UNTRUSTED_PROVENANCE);

  auto protection = valid_input();
  protection.hp1.oil_return_active.value = true;
  assert_failed(build(protection), SnapshotSourceStatus::PROTECTION_ACTIVE);
  auto boiler = valid_input();
  boiler.boiler_heat.value = BoilerHeatState::HEAT_ACTIVE;
  assert_failed(build(boiler), SnapshotSourceStatus::BOILER_ACTIVE);

  auto cooling = valid_input();
  cooling.hp1.mode.value = HeatPumpMode::COOLING;
  assert_failed(build(cooling), SnapshotSourceStatus::COOLING_ACTIVE);
}

void test_dynamic_learning_keeps_temperature_response_but_not_hidden_heat() {
  auto input = valid_input();
  input.operation.setpoint_recovery = true;
  input.operation.comfort_acceptable = false;
  assert_failed(build(input), SnapshotSourceStatus::SETPOINT_RECOVERY_ACTIVE);
  const auto dynamic = [](const LearningSourceInput& value) {
    return build_learning_snapshot(value, QualityConfig{}, SnapshotPurpose::THERMAL_DYNAMIC);
  };
  assert(dynamic(input).measurement_valid);
  input.operation.setpoint_recovery_valid = false;
  input.operation.comfort_acceptable_valid = false;
  assert(dynamic(input).measurement_valid);
  input.boiler_heat.value = BoilerHeatState::HEAT_ACTIVE;
  assert_failed(dynamic(input), SnapshotSourceStatus::BOILER_ACTIVE);
  input.boiler_heat.value = BoilerHeatState::NO_HEAT;
  input.hp1.defrost_active.value = true;
  assert_failed(dynamic(input), SnapshotSourceStatus::PROTECTION_ACTIVE);
  input.hp1.defrost_active.value = false;
  input.hp1.water_in_c.valid = false;
  assert_failed(dynamic(input), SnapshotSourceStatus::MISSING_MEASUREMENT);
  input.hp1.water_in_c.valid = true;
  input.operation.active_limit = true;
  assert_failed(dynamic(input), SnapshotSourceStatus::ACTIVE_LIMIT);
  input.operation.active_limit = false;
  input.operation.service_or_ota_valid = false;
  assert_failed(dynamic(input), SnapshotSourceStatus::OPERATIONAL_CONTEXT_UNKNOWN);
  assert_failed(build_learning_snapshot(valid_input(), QualityConfig{}, static_cast<SnapshotPurpose>(255)),
                SnapshotSourceStatus::INVALID_CONFIGURATION);
}

void test_combined_diagnostics_explain_batch_only_blocks() {
  auto input = valid_input();
  const auto diagnostics = [](const LearningSourceInput& value) {
    return combined_snapshot_diagnostics(
        build_learning_snapshot(value, QualityConfig{}, SnapshotPurpose::STRUCTURAL_BATCH),
        build_learning_snapshot(value, QualityConfig{}, SnapshotPurpose::THERMAL_DYNAMIC));
  };
  auto result = diagnostics(input);
  assert(result.status == SnapshotSourceStatus::OK && result.invalid_reasons == INVALID_NONE);

  input.operation.setpoint_recovery = true;
  result = diagnostics(input);
  assert(result.status == SnapshotSourceStatus::SETPOINT_RECOVERY_ACTIVE);
  assert((result.invalid_reasons & INVALID_SETPOINT_RECOVERY) != 0U);

  input.boiler_heat.value = BoilerHeatState::HEAT_ACTIVE;
  result = diagnostics(input);
  assert(result.status == SnapshotSourceStatus::BOILER_ACTIVE);
  assert((result.invalid_reasons & INVALID_BOILER_HEAT) != 0U);
  assert((result.invalid_reasons & INVALID_SETPOINT_RECOVERY) != 0U);

  input = valid_input();
  input.operation.comfort_acceptable = false;
  result = diagnostics(input);
  assert(result.status == SnapshotSourceStatus::COMFORT_UNACCEPTABLE);
  assert((result.invalid_reasons & INVALID_CONTROL_MODE) != 0U);
}

}  // namespace

int main() {
  test_single_and_series_calorimetry();
  test_missing_and_unsupported_topology_fail_closed();
  test_freshness_and_skew_are_per_field();
  test_context_revision_binds_each_snapshot();
  test_unit_identity_and_distinct_temperature_paths();
  test_off_periods_keep_signed_heat_and_allow_zero_flow();
  test_invalid_values_contracts_and_unknowns_never_become_zero();
  test_provenance_and_active_exclusions();
  test_operational_context_is_explicit_and_fail_closed();
  test_power_cap_only_excludes_a_constrained_request();
  test_invalid_raw_event_poisoning_reaches_aggregate();
  test_dynamic_learning_keeps_temperature_response_but_not_hidden_heat();
  test_combined_diagnostics_explain_batch_only_blocks();
  return 0;
}
