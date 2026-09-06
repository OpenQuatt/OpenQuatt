#include <assert.h>
#include <math.h>

#define OQ_PH_LEARNING_HOST_TEST 1
#include "../../openquatt/includes/learning/oq_ph_learning_live_logic.h"

using namespace oq_power_house::learning;

namespace {

constexpr uint64_t kNowMs = 200000U;

oq_sources::ResolvedLearningSource direct_source(oq_sources::LearningSourceRoute route, float value,
                                                 oq_input_source::Source selected, uint32_t endpoint_generation = 0U,
                                                 uint64_t received_ms = kNowMs - 100U) {
  oq_sources::RawFloatReceipt receipt;
  receipt.observe(value, received_ms, true);
  const oq_sources::SourceConfigurationKey configuration{static_cast<uint8_t>(selected), 0U, 0U, endpoint_generation};
  return oq_sources::physical_source(route, receipt, 4U, true, configuration);
}

oq_sources::ResolvedLearningSource composite_flow(float hp1, float hp2, uint64_t hp1_ms = kNowMs - 200U,
                                                  uint64_t hp2_ms = kNowMs - 100U) {
  oq_sources::RawFloatReceipt first;
  oq_sources::RawFloatReceipt second;
  first.observe(hp1, hp1_ms, true);
  second.observe(hp2, hp2_ms, true);
  const oq_sources::SourceConfigurationKey configuration{
      static_cast<uint8_t>(oq_input_source::Source::OUTDOOR), 0U,
      static_cast<uint8_t>(oq_input_source::OutdoorFlowMode::AGGREGATE), 0U};
  return oq_sources::unsupported_composite_source(
      0.5f * (hp1 + hp2), true, oq_sources::LearningSourceRoute::FLOW_AGGREGATE,
      oq_sources::LearningSourceRoute::HP1_FLOW, oq_sources::LearningSourceRoute::HP2_FLOW, first, second,
      oq_sources::LearningCompositeOperation::ARITHMETIC_MEAN, configuration, 5U);
}

template <typename T>
PhysicalMeasurement<T> physical_measurement(T value, uint32_t id, PhysicalUnit unit = PhysicalUnit::SYSTEM,
                                            uint64_t received_ms = kNowMs - 100U) {
  PhysicalMeasurement<T> result;
  result.value = value;
  result.valid = true;
  result.source = {PhysicalSourceKind::MODBUS_REGISTER, id, unit};
  result.source_generation = id + 100U;
  result.received_monotonic_ms = received_ms;
  result.timing = kHpLearningTiming;
  result.provenance = MeasurementProvenance::PHYSICAL_RECEIPT;
  return result;
}

LearningSourceInput diagnostic_input(HydronicTopology topology = HydronicTopology::SINGLE) {
  LearningSourceInput input;
  input.monotonic_ms = kNowMs;
  input.topology = topology;
  input.calorimetry.meter_boundary = topology == HydronicTopology::SINGLE ? MeterBoundary::SINGLE_HEAT_PUMP_CIRCUIT
                                                                          : MeterBoundary::SHARED_DUO_SERIES_CIRCUIT;
  input.calorimetry.fluid_model = FluidHeatCapacityModel::WATER_CP_4180;
  input.calorimetry.calorimetry_generation = 1U;
  input.calorimetry.uncertainty_proven = true;
  input.calorimetry.heat_uncertainty_w = 100.0f;
  input.calorimetry.max_flow_lph = 2000.0f;
  input.calorimetry.duo_series_order = DuoSeriesOrder::HP1_TO_HP2;
  input.calorimetry.max_series_junction_delta_c = 0.5f;
  input.flow_lph = physical_measurement(1000.0f, 2138U, PhysicalUnit::HP1);
  input.hp1.present = true;
  input.hp1.water_in_c = physical_measurement(30.0f, 2133U, PhysicalUnit::HP1);
  input.hp1.water_out_c = physical_measurement(32.0f, 2134U, PhysicalUnit::HP1);
  if (topology == HydronicTopology::DUO_SERIES) {
    input.hp2.present = true;
    input.hp2.water_in_c = physical_measurement(32.0f, 2133U, PhysicalUnit::HP2);
    input.hp2.water_out_c = physical_measurement(35.0f, 2134U, PhysicalUnit::HP2);
  }
  return input;
}

void test_compile_time_installation_contract_maps_single_and_duo() {
  LearningSourceInput single;
  apply_compile_time_installation_contract(single, false);
  assert(single.topology == HydronicTopology::SINGLE);
  assert(single.calorimetry.meter_boundary == MeterBoundary::SINGLE_HEAT_PUMP_CIRCUIT);
  assert(single.calorimetry.fluid_model == FluidHeatCapacityModel::WATER_CP_4180);
  assert(single.calorimetry.duo_series_order == DuoSeriesOrder::UNKNOWN);

  LearningSourceInput duo;
  apply_compile_time_installation_contract(duo, true);
  assert(duo.topology == HydronicTopology::DUO_SERIES);
  assert(duo.calorimetry.meter_boundary == MeterBoundary::SHARED_DUO_SERIES_CIRCUIT);
  assert(duo.calorimetry.fluid_model == FluidHeatCapacityModel::WATER_CP_4180);
  assert(duo.calorimetry.duo_series_order == DuoSeriesOrder::HP1_TO_HP2);
}

void test_direct_routes_map_to_exact_source_and_timing() {
  const auto room = resolved_learning_measurement(
      direct_source(oq_sources::LearningSourceRoute::OPENTHERM_ROOM, 20.5f, oq_input_source::Source::OPENTHERM),
      kNowMs);
  assert(room.valid && room.value == 20.5f);
  assert(room.source.kind == PhysicalSourceKind::OPENTHERM_FRAME && room.source.id == 24U);
  assert(room.source.unit == PhysicalUnit::SYSTEM && room.source_generation == 4U);
  assert(room.timing.max_age_ms == 120000U && room.timing.max_skew_ms == 30000U);

  const auto outside = resolved_learning_measurement(
      direct_source(oq_sources::LearningSourceRoute::HP2_OUTSIDE, 4.0f, oq_input_source::Source::AUTO), kNowMs);
  assert(outside.valid && outside.source.kind == PhysicalSourceKind::MODBUS_REGISTER);
  assert(outside.source.id == 2110U && outside.source.unit == PhysicalUnit::HP2);

  const auto cic = resolved_learning_measurement(
      direct_source(oq_sources::LearningSourceRoute::CIC_FLOW, 750.0f, oq_input_source::Source::CIC, 9U), kNowMs);
  assert(cic.valid && cic.source.kind == PhysicalSourceKind::CIC_FIELD && cic.source.id == 3U);
}

void test_direct_route_fails_closed_for_spoof_stale_future_and_offline() {
  auto spoof = direct_source(oq_sources::LearningSourceRoute::HA_ROOM, 20.0f, oq_input_source::Source::HA);
  spoof.provenance = oq_sources::LearningSourceProvenance::PHYSICAL_RECEIPT;
  assert(!resolved_learning_measurement(spoof, kNowMs).valid);

  auto wrong_configuration =
      direct_source(oq_sources::LearningSourceRoute::HP1_FLOW, 700.0f, oq_input_source::Source::CIC, 3U);
  assert(!resolved_learning_measurement(wrong_configuration, kNowMs).valid);

  auto stale = direct_source(oq_sources::LearningSourceRoute::HP1_FLOW, 700.0f, oq_input_source::Source::OUTDOOR, 0U,
                             kNowMs - 45001U);
  assert(!resolved_learning_measurement(stale, kNowMs).valid);

  auto future = direct_source(oq_sources::LearningSourceRoute::HP1_FLOW, 700.0f, oq_input_source::Source::OUTDOOR, 0U,
                              kNowMs + 1U);
  assert(!resolved_learning_measurement(future, kNowMs).valid);

  auto offline = direct_source(oq_sources::LearningSourceRoute::HP1_FLOW, 700.0f, oq_input_source::Source::OUTDOOR);
  offline.receipt.invalidate();
  assert(!resolved_learning_measurement(offline, kNowMs).valid);

  auto changed_value =
      direct_source(oq_sources::LearningSourceRoute::HP1_FLOW, 700.0f, oq_input_source::Source::OUTDOOR);
  changed_value.value = 701.0f;
  assert(!resolved_learning_measurement(changed_value, kNowMs).valid);

  auto generation_overflow =
      direct_source(oq_sources::LearningSourceRoute::HP1_FLOW, 700.0f, oq_input_source::Source::OUTDOOR);
  generation_overflow.configuration_generation = 0U;
  assert(!resolved_learning_measurement(generation_overflow, kNowMs).valid);
}

void test_composition_requires_both_receipts_exact_routes_freshness_and_skew() {
  auto source = composite_flow(700.0f, 900.0f);
  auto measured = resolved_learning_measurement(source, kNowMs);
  assert(measured.valid && measured.value == 800.0f);
  assert(measured.source.kind == PhysicalSourceKind::PHYSICAL_COMPOSITION);
  assert(measured.source.id != 0U && measured.source_generation == 5U);
  assert(measured.received_monotonic_ms == kNowMs - 100U);
  assert(measured.provenance == MeasurementProvenance::PHYSICAL_COMPOSITION);

  source.secondary_receipt.invalidate();
  assert(!resolved_learning_measurement(source, kNowMs).valid);

  source = composite_flow(700.0f, 900.0f, kNowMs - 30001U, kNowMs);
  assert(!resolved_learning_measurement(source, kNowMs).valid);

  source = composite_flow(700.0f, 900.0f, kNowMs - 45001U, kNowMs - 20000U);
  assert(!resolved_learning_measurement(source, kNowMs).valid);

  source = composite_flow(700.0f, 900.0f);
  source.secondary_route = source.component_route;
  assert(!resolved_learning_measurement(source, kNowMs).valid);

  source = composite_flow(700.0f, 900.0f);
  source.value = 850.0f;
  assert(!resolved_learning_measurement(source, kNowMs).valid);
}

void test_hp_decode_and_boiler_bits_fail_closed() {
  assert(decode_learning_hp_mode(0.0f) == HeatPumpMode::OFF);
  assert(decode_learning_hp_mode(2.0f) == HeatPumpMode::HEATING);
  assert(decode_learning_hp_mode(1.0f) == HeatPumpMode::COOLING);
  assert(decode_learning_hp_mode(3.0f) == HeatPumpMode::COOLING);
  assert(decode_learning_hp_mode(4.0f) == HeatPumpMode::UNKNOWN);

  oq_sources::HeatPumpReceipts raw;
  raw.working_mode.observe(1.0f, kNowMs, true);
  raw.compressor_frequency.observe(0.0f, kNowMs, true);
  raw.defrost.observe(0.0f, kNowMs, true);
  raw.status_2108.observe(0x0040U, kNowMs, true);
  raw.status_2119.observe(0x0008U, kNowMs, true);
  auto hp = learning_hp_measurements(raw, PhysicalUnit::HP1, 0.0f, 0.0f, 1U);
  assert(hp.mode.valid && hp.mode.value == HeatPumpMode::COOLING);
  assert(hp.compressor_active.valid && !hp.compressor_active.value);
  assert(hp.valve_transition_active.valid && hp.valve_transition_active.value);
  assert(hp.oil_return_active.valid && hp.oil_return_active.value);
  raw.compressor_frequency.observe(35.0f, kNowMs, true);
  hp = learning_hp_measurements(raw, PhysicalUnit::HP1, 0.0f, 0.0f, 1U);
  assert(hp.compressor_active.valid && hp.compressor_active.value);
  raw.compressor_frequency.observe(121.0f, kNowMs, true);
  hp = learning_hp_measurements(raw, PhysicalUnit::HP1, 0.0f, 0.0f, 1U);
  assert(!hp.compressor_active.valid);

  assert(learning_boiler_status(0x0000U, kNowMs, true, true).value == BoilerHeatState::NO_HEAT);
  assert(learning_boiler_status(0x0002U, kNowMs, true, true).value == BoilerHeatState::HEAT_ACTIVE);
  assert(learning_boiler_status(0x0008U, kNowMs, true, true).value == BoilerHeatState::HEAT_ACTIVE);
  assert(!learning_boiler_status(0x0000U, kNowMs, true, false).valid);
  assert(!learning_boiler_status(NAN, kNowMs, true, true).valid);
  assert(!learning_boiler_status(65536.0f, kNowMs, true, true).valid);
  assert(!learning_boiler_status(2.5f, kNowMs, true, true).valid);
}

void test_strategy_output_requires_current_matching_owner() {
  assert(learning_strategy_output_current(true, 3U, 3U, 20000U, 10000U));
  assert(!learning_strategy_output_current(true, 2U, 3U, 20000U, 10000U));
  assert(!learning_strategy_output_current(true, 3U, 1U, 20000U, 10000U));
  assert(!learning_strategy_output_current(true, 3U, 3U, 30001U, 10000U));
  assert(!learning_strategy_output_current(true, 3U, 3U, 10000U, 0U));
  assert(!learning_strategy_output_current(false, 3U, 3U, 20000U, 10000U));
  assert(learning_strategy_output_current(true, 3U, 3U, 5000U, UINT32_MAX - 4999U));
}

void test_diagnostic_coverage_never_crosses_invalid_gap_or_generation_edge() {
  assert(diagnostic_coverage_seconds(100U, 110U, true, true, 7U, 7U, 8U, 8U, 60000U) == 10U);
  assert(diagnostic_coverage_seconds(100U, 110U, false, true, 7U, 7U, 8U, 8U, 60000U) == 0U);
  assert(diagnostic_coverage_seconds(100U, 110U, true, true, 7U, 9U, 8U, 8U, 60000U) == 0U);
  assert(diagnostic_coverage_seconds(100U, 110U, true, true, 7U, 7U, 8U, 9U, 60000U) == 0U);
  assert(diagnostic_coverage_seconds(100U, 161U, true, true, 7U, 7U, 8U, 8U, 60000U) == 0U);
  assert(diagnostic_coverage_seconds(100U, 100U, true, true, 7U, 7U, 8U, 8U, 60000U) == 0U);
}

void test_diagnostic_capture_requires_opt_in_and_breaks_pause_continuity() {
  DiagnosticCaptureGate gate;
  size_t retained_rows = 0;
  const auto tick = [&gate, &retained_rows](bool opted_in) {
    const auto decision = diagnostic_capture_decision(gate, opted_in);
    if (decision.capture) ++retained_rows;
    return decision;
  };

  auto decision = tick(false);
  assert(!decision.capture && !decision.use_previous && retained_rows == 0U);

  decision = tick(true);
  assert(decision.capture && !decision.use_previous && retained_rows == 1U);
  decision = tick(true);
  assert(decision.capture && decision.use_previous && retained_rows == 2U);

  pause_diagnostic_capture(gate);
  decision = tick(false);
  assert(!decision.capture && !decision.use_previous && retained_rows == 2U);
  decision = tick(true);
  assert(decision.capture && !decision.use_previous && retained_rows == 3U);

  reset_diagnostic_capture(gate);
  retained_rows = 0;
  decision = tick(false);
  assert(!decision.capture && retained_rows == 0U);
  decision = tick(true);
  assert(decision.capture && !decision.use_previous && retained_rows == 1U);
}

void test_cm2_contract_requires_current_withdrawn_boiler_command() {
  const oq_boiler::BoilerCommand off{
      true, false, false, NAN, NAN, oq_boiler::COMMAND_SOURCE_NONE, static_cast<uint32_t>(kNowMs)};
  const auto contract = learning_cm2_boiler_contract(true, 2, off, false, false, kNowMs, false);
  assert(contract.valid && contract.value == BoilerHeatState::NO_HEAT);
  assert(contract.provenance == MeasurementProvenance::CONTROL_CONTRACT);
  source_detail::MeasurementMeta measurements[1];
  size_t count = 0;
  assert(source_detail::append_measurement(contract, kNowMs, measurements, count) ==
         SnapshotSourceStatus::UNTRUSTED_PROVENANCE);
  assert(source_detail::append_measurement(contract, kNowMs, measurements, count, true) == SnapshotSourceStatus::OK);

  constexpr int blocked_modes[] = {-1, 0, 1, 3, 4, 5, 98, 99, 100};
  for (int cm : blocked_modes) {
    const auto blocked = learning_cm2_boiler_contract(true, cm, off, false, false, kNowMs, false);
    assert(!blocked.valid);
    assert(blocked.source.kind == contract.source.kind && blocked.source.id == contract.source.id);
    assert(blocked.source_generation == contract.source_generation && blocked.provenance == contract.provenance);
  }
  assert(!learning_cm2_boiler_contract(false, 2, off, false, false, kNowMs, false).valid);
  assert(!learning_cm2_boiler_contract(true, 2, off, true, false, kNowMs, false).valid);
  assert(!learning_cm2_boiler_contract(true, 2, off, false, true, kNowMs, false).valid);
  assert(!learning_cm2_boiler_contract(true, 2, off, false, false, kNowMs, true).valid);
  assert(!learning_cm2_boiler_contract(true, 2, off, false, false, 0U, false).valid);
  assert(learning_cm2_boiler_contract(true, 2, off, false, false, kNowMs + 15000U, false).valid);
  assert(!learning_cm2_boiler_contract(true, 2, off, false, false, kNowMs + 15001U, false).valid);
  for (unsigned failure = 0; failure < 6; ++failure) {
    auto command = off;
    if (failure == 0) command.valid = false;
    if (failure == 1) command.demand_present = true;
    if (failure == 2) command.heat_request = true;
    if (failure == 3) command.source = oq_boiler::COMMAND_SOURCE_FALLBACK;
    if (failure == 4) command.updated_at_ms = 0;
    if (failure == 5) ++command.updated_at_ms;
    assert(!learning_cm2_boiler_contract(true, 2, command, false, false, kNowMs, false).valid);
  }
  auto wrapped = off;
  wrapped.updated_at_ms = UINT32_MAX - 4999U;
  assert(learning_cm2_boiler_contract(true, 2, wrapped, false, false, (1ULL << 32U) + 5000U, false).valid);

  // CM2 needs no OpenTherm receipt. Only current, physical heat activity
  // contradicts the contract; invalid/stale/future receipts cannot veto it.
  auto telemetry = learning_boiler_status(0U, kNowMs, true, true);
  assert(learning_boiler_heat(true, contract, telemetry).valid);
  assert(learning_boiler_heat(true, contract, telemetry).provenance == MeasurementProvenance::CONTROL_CONTRACT);
  assert(learning_boiler_heat(false, contract, {}).valid);
  assert(learning_boiler_heat(true, contract, {}).valid);
  const auto disconnected = learning_boiler_status(8U, kNowMs, true, false);
  assert(learning_boiler_heat(true, contract, disconnected).valid);
  assert(learning_boiler_heat(true, contract, learning_boiler_status(8U, kNowMs - 10001U, true, true)).valid);
  assert(learning_boiler_heat(true, contract, learning_boiler_status(8U, kNowMs + 1U, true, true)).valid);
  constexpr float active_payloads[] = {2.0f, 8.0f};
  for (float payload : active_payloads) {
    const auto active = learning_boiler_heat(true, contract, learning_boiler_status(payload, kNowMs, true, true));
    assert(!active.valid);
    assert(active.source.kind == contract.source.kind && active.source.id == contract.source.id);
    assert(active.source_generation == contract.source_generation && active.provenance == contract.provenance);
    assert(learning_boiler_heat(false, contract, learning_boiler_status(payload, kNowMs, true, true)).valid);
  }
  auto withdrawn = contract;
  withdrawn.valid = false;
  assert(!learning_boiler_heat(true, withdrawn, telemetry).valid);
}

void test_diagnostic_heat_preserves_signed_physical_heat_outside_training_gates() {
  QualityConfig quality;
  auto input = diagnostic_input();
  input.operation.control_mode_valid = false;
  input.hp1.mode = physical_measurement(HeatPumpMode::COOLING, 2099U, PhysicalUnit::HP1);
  input.hp1.defrost_active = physical_measurement(true, 2118U, PhysicalUnit::HP1);
  input.boiler_heat = physical_measurement(BoilerHeatState::HEAT_ACTIVE, 7U);
  const auto heating = diagnostic_signed_heat(input, quality);
  assert(heating.valid);
  assert(fabsf(heating.heat_to_water_w - 2322.2222f) < 0.01f);

  input.hp1.water_out_c.value = 28.0f;
  const auto cooling = diagnostic_signed_heat(input, quality);
  assert(cooling.valid);
  assert(fabsf(cooling.heat_to_water_w + 2322.2222f) < 0.01f);

  input = diagnostic_input(HydronicTopology::DUO_SERIES);
  const auto duo = diagnostic_signed_heat(input, quality);
  assert(duo.valid);
  assert(fabsf(duo.heat_to_water_w - 5805.5557f) < 0.01f);
}

void test_diagnostic_heat_rejects_unproved_or_incoherent_inputs() {
  QualityConfig quality;
  auto input = diagnostic_input();
  input.flow_lph.received_monotonic_ms = kNowMs - kHpLearningTiming.max_age_ms - 1U;
  assert(!diagnostic_signed_heat(input, quality).valid);

  input = diagnostic_input();
  input.hp1.water_out_c.received_monotonic_ms = kNowMs - kHpLearningTiming.max_skew_ms - 101U;
  assert(!diagnostic_signed_heat(input, quality).valid);

  input = diagnostic_input();
  input.flow_lph.provenance = MeasurementProvenance::REPUBLISHED;
  assert(!diagnostic_signed_heat(input, quality).valid);

  input = diagnostic_input();
  input.hp1.water_out_c.source = input.hp1.water_in_c.source;
  assert(!diagnostic_signed_heat(input, quality).valid);

  input = diagnostic_input();
  input.calorimetry.heat_uncertainty_w = NAN;
  assert(!diagnostic_signed_heat(input, quality).valid);

  input = diagnostic_input();
  input.flow_lph.value = 0.0f;
  assert(!diagnostic_signed_heat(input, quality).valid);
  input.calorimetry.zero_flow_proof = ZeroFlowProof::PHYSICAL_METER_COVERS_BOUNDARY;
  const auto zero = diagnostic_signed_heat(input, quality);
  assert(zero.valid && zero.heat_to_water_w == 0.0f);
}

}  // namespace

int main() {
  test_compile_time_installation_contract_maps_single_and_duo();
  test_direct_routes_map_to_exact_source_and_timing();
  test_direct_route_fails_closed_for_spoof_stale_future_and_offline();
  test_composition_requires_both_receipts_exact_routes_freshness_and_skew();
  test_hp_decode_and_boiler_bits_fail_closed();
  test_strategy_output_requires_current_matching_owner();
  test_diagnostic_coverage_never_crosses_invalid_gap_or_generation_edge();
  test_diagnostic_capture_requires_opt_in_and_breaks_pause_continuity();
  test_cm2_contract_requires_current_withdrawn_boiler_command();
  test_diagnostic_heat_preserves_signed_physical_heat_outside_training_gates();
  test_diagnostic_heat_rejects_unproved_or_incoherent_inputs();
  return 0;
}
