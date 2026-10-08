#include <assert.h>
#include <math.h>
#include <string.h>

#define OQ_PH_LEARNING_HOST_TEST 1
#include "../../openquatt/includes/learning/oq_ph_learning_live_logic.h"

using namespace oq_power_house::learning;

namespace {

constexpr uint64_t kNowMs = 200000U;

oq_sources::ResolvedLearningSource selected_source(
    oq_sources::LearningSourceRoute route, float value, uint32_t generation = 4U,
    oq_sources::LearningSourceProvenance provenance = oq_sources::LearningSourceProvenance::SELECTED_VALUE) {
  return oq_sources::selected_source(value, true, route, generation, provenance);
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

void test_compile_time_topology_maps_single_and_duo() {
  LearningSourceInput single;
  apply_compile_time_topology(single, false);
  assert(single.topology == HydronicTopology::SINGLE);

  LearningSourceInput duo;
  apply_compile_time_topology(duo, true);
  assert(duo.topology == HydronicTopology::DUO_SERIES);
}

void test_every_selected_route_uses_its_selected_value() {
  constexpr oq_sources::LearningSourceRoute routes[] = {
      oq_sources::LearningSourceRoute::OPENTHERM_ROOM,    oq_sources::LearningSourceRoute::OPENTHERM_SETPOINT,
      oq_sources::LearningSourceRoute::CIC_ROOM,          oq_sources::LearningSourceRoute::CIC_SETPOINT,
      oq_sources::LearningSourceRoute::CIC_FLOW,          oq_sources::LearningSourceRoute::HA_ROOM,
      oq_sources::LearningSourceRoute::HA_SETPOINT,       oq_sources::LearningSourceRoute::HA_OUTSIDE,
      oq_sources::LearningSourceRoute::API_ROOM,          oq_sources::LearningSourceRoute::API_SETPOINT,
      oq_sources::LearningSourceRoute::API_OUTSIDE,       oq_sources::LearningSourceRoute::MQTT_ROOM,
      oq_sources::LearningSourceRoute::MQTT_SETPOINT,     oq_sources::LearningSourceRoute::MQTT_OUTSIDE,
      oq_sources::LearningSourceRoute::HP1_OUTSIDE,       oq_sources::LearningSourceRoute::HP2_OUTSIDE,
      oq_sources::LearningSourceRoute::OUTSIDE_AGGREGATE, oq_sources::LearningSourceRoute::CONTROLLER_FLOW,
      oq_sources::LearningSourceRoute::HP1_FLOW,          oq_sources::LearningSourceRoute::HP2_FLOW,
      oq_sources::LearningSourceRoute::FLOW_AGGREGATE,    oq_sources::LearningSourceRoute::SYNTHESIZED_ZERO_FLOW,
  };
  for (const auto route : routes) {
    const auto measured = resolved_learning_measurement(selected_source(route, 20.5f), kNowMs);
    assert(measured.valid && measured.value == 20.5f);
    assert(measured.source.kind == PhysicalSourceKind::CONTROL_CONTRACT);
    assert(measured.source.id == static_cast<uint32_t>(route));
    assert(measured.source.unit == PhysicalUnit::SYSTEM && measured.source_generation == 4U);
    assert(measured.received_monotonic_ms == kNowMs);
    assert(measured.provenance == MeasurementProvenance::SELECTED_VALUE);
  }

  const auto room =
      resolved_learning_measurement(selected_source(oq_sources::LearningSourceRoute::API_ROOM, 20.5f), kNowMs);
  assert(room.timing.max_age_ms == 120000U && room.timing.max_skew_ms == 30000U);
  const auto flow =
      resolved_learning_measurement(selected_source(oq_sources::LearningSourceRoute::FLOW_AGGREGATE, 750.0f), kNowMs);
  assert(flow.timing.max_age_ms == 45000U && flow.timing.max_skew_ms == 30000U);

  source_detail::MeasurementMeta measurements[1];
  size_t count = 0;
  assert(source_detail::append_measurement(room, kNowMs, measurements, count) == SnapshotSourceStatus::OK);
}

void test_selected_hold_uses_the_selected_value() {
  const auto held = resolved_learning_measurement(
      selected_source(oq_sources::LearningSourceRoute::HA_ROOM, 20.0f, 4U, oq_sources::LearningSourceProvenance::HELD),
      kNowMs);
  assert(held.valid && held.value == 20.0f);

  auto no_generation = selected_source(oq_sources::LearningSourceRoute::MQTT_ROOM, 20.0f);
  no_generation.configuration_generation = 0U;
  assert(!resolved_learning_measurement(no_generation, kNowMs).valid);
}

void test_missing_selection_is_not_a_learning_measurement() {
  auto missing = selected_source(oq_sources::LearningSourceRoute::API_ROOM, 20.0f);
  missing.valid = false;
  assert(!resolved_learning_measurement(missing, kNowMs).valid);

  auto no_route = selected_source(oq_sources::LearningSourceRoute::NONE, 20.0f);
  assert(!resolved_learning_measurement(no_route, kNowMs).valid);
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
  assert(diagnostic_coverage_seconds(100U, 110U, true, true, 7U, 7U, 60000U) == 10U);
  assert(diagnostic_coverage_seconds(100U, 110U, false, true, 7U, 7U, 60000U) == 0U);
  assert(diagnostic_coverage_seconds(100U, 110U, true, true, 7U, 9U, 60000U) == 0U);
  assert(diagnostic_coverage_seconds(100U, 161U, true, true, 7U, 7U, 60000U) == 0U);
  assert(diagnostic_coverage_seconds(100U, 100U, true, true, 7U, 7U, 60000U) == 0U);
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

void test_heating_and_idle_contract_requires_current_withdrawn_boiler_command() {
  const oq_boiler::BoilerCommand off{
      true, false, false, NAN, NAN, oq_boiler::COMMAND_SOURCE_NONE, static_cast<uint32_t>(kNowMs)};
  const auto contract = learning_no_boiler_heat_contract(true, 2, off, false, false, kNowMs, false);
  assert(contract.valid && contract.value == BoilerHeatState::NO_HEAT);
  assert(contract.provenance == MeasurementProvenance::CONTROL_CONTRACT);
  source_detail::MeasurementMeta measurements[1];
  size_t count = 0;
  assert(source_detail::append_measurement(contract, kNowMs, measurements, count) ==
         SnapshotSourceStatus::UNTRUSTED_PROVENANCE);
  assert(source_detail::append_measurement(contract, kNowMs, measurements, count, true) == SnapshotSourceStatus::OK);

  constexpr int blocked_modes[] = {-1, 3, 4, 5, 98, 99, 100};
  for (int cm : blocked_modes) {
    const auto blocked = learning_no_boiler_heat_contract(true, cm, off, false, false, kNowMs, false);
    assert(!blocked.valid);
    assert(blocked.source.kind == contract.source.kind && blocked.source.id == contract.source.id);
    assert(blocked.source_generation == contract.source_generation && blocked.provenance == contract.provenance);
  }
  for (int cm = 0; cm <= 2; ++cm) {
    const auto idle = learning_no_boiler_heat_contract(true, cm, off, false, false, kNowMs, false);
    assert(idle.valid && idle.value == BoilerHeatState::NO_HEAT);
    assert(idle.source.id == contract.source.id && idle.source_generation == contract.source_generation);
    assert(!learning_no_boiler_heat_contract(false, cm, off, false, false, kNowMs, false).valid);
    assert(!learning_no_boiler_heat_contract(true, cm, off, true, false, kNowMs, false).valid);
    assert(!learning_no_boiler_heat_contract(true, cm, off, false, true, kNowMs, false).valid);
    assert(!learning_no_boiler_heat_contract(true, cm, off, false, false, kNowMs, true).valid);
    assert(!learning_no_boiler_heat_contract(true, cm, off, false, false, kNowMs + 15001U, false).valid);
    const auto burning = learning_boiler_status(8U, kNowMs, true, true);
    assert(!learning_boiler_heat(true, idle, burning).valid);
  }
  assert(!learning_no_boiler_heat_contract(false, 2, off, false, false, kNowMs, false).valid);
  assert(!learning_no_boiler_heat_contract(true, 2, off, true, false, kNowMs, false).valid);
  assert(!learning_no_boiler_heat_contract(true, 2, off, false, true, kNowMs, false).valid);
  assert(!learning_no_boiler_heat_contract(true, 2, off, false, false, kNowMs, true).valid);
  assert(!learning_no_boiler_heat_contract(true, 2, off, false, false, 0U, false).valid);
  assert(learning_no_boiler_heat_contract(true, 2, off, false, false, kNowMs + 15000U, false).valid);
  assert(!learning_no_boiler_heat_contract(true, 2, off, false, false, kNowMs + 15001U, false).valid);
  for (unsigned failure = 0; failure < 6; ++failure) {
    auto command = off;
    if (failure == 0) command.valid = false;
    if (failure == 1) command.demand_present = true;
    if (failure == 2) command.heat_request = true;
    if (failure == 3) command.source = oq_boiler::COMMAND_SOURCE_FALLBACK;
    if (failure == 4) command.updated_at_ms = 0;
    if (failure == 5) ++command.updated_at_ms;
    assert(!learning_no_boiler_heat_contract(true, 2, command, false, false, kNowMs, false).valid);
  }
  auto wrapped = off;
  wrapped.updated_at_ms = UINT32_MAX - 4999U;
  assert(learning_no_boiler_heat_contract(true, 2, wrapped, false, false, (1ULL << 32U) + 5000U, false).valid);

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
  const auto heating = evaluate_calorimetry(input, quality);
  assert(heating.valid);
  assert(fabsf(heating.heat_to_water_w - 2322.2222f) < 0.01f);

  input.hp1.water_out_c.value = 28.0f;
  const auto cooling = evaluate_calorimetry(input, quality);
  assert(cooling.valid);
  assert(fabsf(cooling.heat_to_water_w + 2322.2222f) < 0.01f);

  input = diagnostic_input(HydronicTopology::DUO_SERIES);
  const auto duo = evaluate_calorimetry(input, quality);
  assert(duo.valid);
  assert(fabsf(duo.heat_to_water_w - 5805.5557f) < 0.01f);
}

void test_diagnostic_heat_rejects_incoherent_inputs() {
  QualityConfig quality;
  auto input = diagnostic_input();
  input.flow_lph.received_monotonic_ms = kNowMs - kHpLearningTiming.max_age_ms - 1U;
  assert(!evaluate_calorimetry(input, quality).valid);

  input = diagnostic_input();
  input.hp1.water_out_c.received_monotonic_ms = kNowMs - kHpLearningTiming.max_skew_ms - 101U;
  assert(!evaluate_calorimetry(input, quality).valid);

  input = diagnostic_input();
  input.flow_lph.provenance = MeasurementProvenance::REPUBLISHED;
  assert(!evaluate_calorimetry(input, quality).valid);

  input = diagnostic_input();
  input.hp1.water_out_c.source = input.hp1.water_in_c.source;
  assert(!evaluate_calorimetry(input, quality).valid);

  input = diagnostic_input();
  input.flow_lph.value = kPassiveMaximumFlowLph + 1.0f;
  assert(!evaluate_calorimetry(input, quality).valid);

  input = diagnostic_input();
  input.flow_lph.value = 0.0f;
  const auto zero = evaluate_calorimetry(input, quality);
  assert(zero.valid && zero.heat_to_water_w == 0.0f);
}

void test_delayed_initial_resolution_does_not_look_like_a_route_change() {
  uint32_t valid_revisions[4]{};
  oq_sources::ResolvedLearningSource sources[4];
  for (auto& source : sources) source.configuration_generation = 1;
  assert(!observe_valid_source_revisions(valid_revisions, sources));
  sources[0] = selected_source(oq_sources::LearningSourceRoute::HA_ROOM, 20.0f, 2);
  assert(!observe_valid_source_revisions(valid_revisions, sources));
  sources[1] = selected_source(oq_sources::LearningSourceRoute::OPENTHERM_SETPOINT, 20.0f, 2);
  assert(!observe_valid_source_revisions(valid_revisions, sources));
  sources[0].valid = false;
  sources[0].configuration_generation = 3;
  assert(!observe_valid_source_revisions(valid_revisions, sources));
  sources[0].valid = true;
  sources[0].configuration_generation = 4;  // Includes intermediate A -> B -> A.
  assert(observe_valid_source_revisions(valid_revisions, sources));
}

void test_positive_unsafe_evidence_during_sntp_startup_prevents_recovery() {
  auto input = diagnostic_input();
  input.epoch_s = 0;  // SNTP is not ready; positive evidence remains meaningful.
  input.operation.control_mode_valid = false;
  assert(!known_daily_restart_interruption(input, false));
  assert(known_daily_restart_interruption(input, true));
  assert(strcmp(daily_restart_interruption_reason(input, true), "boiler_heat") == 0);
  input.operation.control_mode_valid = true;
  input.operation.control_mode = LearningControlMode::HEATING;
  assert(!known_daily_restart_interruption(input, false));
  input.operation.control_mode = LearningControlMode::UNKNOWN;
  assert(known_daily_restart_interruption(input, false));
  assert(strcmp(daily_restart_interruption_reason(input, false), "control_mode") == 0);
  input.operation.control_mode = LearningControlMode::HEATING;
  input.hp1.mode = physical_measurement(HeatPumpMode::COOLING, 2099, PhysicalUnit::HP1);
  assert(known_daily_restart_interruption(input, false));
  assert(strcmp(daily_restart_interruption_reason(input, false), "hp_mode") == 0);
  input.hp1.defrost_active = physical_measurement(true, 2118, PhysicalUnit::HP1);
  assert(!known_daily_restart_interruption(input, false));  // Signed daily defrost remains valid.
  input.hp1.defrost_active.received_monotonic_ms = kNowMs - kHpLearningTiming.max_age_ms - 1;
  assert(known_daily_restart_interruption(input, false));
  input.hp1.mode.received_monotonic_ms = kNowMs - kHpLearningTiming.max_age_ms - 1;
  assert(!known_daily_restart_interruption(input, false));
  input.hp1.mode = physical_measurement(HeatPumpMode::OFF, 2099, PhysicalUnit::HP1);
  input.hp1.compressor_active = physical_measurement(true, 2103, PhysicalUnit::HP1);
  assert(known_daily_restart_interruption(input, false));
  assert(strcmp(daily_restart_interruption_reason(input, false), "off_compressor_active") == 0);
  input.hp1.compressor_active.value = false;
  assert(!known_daily_restart_interruption(input, false));
  assert(daily_restart_interruption_reason(input, false) == nullptr);
}

LearningSourceInput boot_input() {
  auto input = diagnostic_input(HydronicTopology::DUO_SERIES);
  input.epoch_s = 20000U * 86400U;
  input.context_revision = 1;
  input.room_c = physical_measurement(20.0f, 1U);
  input.setpoint_c = physical_measurement(21.0f, 2U);
  input.outside_c = physical_measurement(7.0f, 3U);
  HeatPumpRawMeasurements* pumps[]{&input.hp1, &input.hp2};
  const PhysicalUnit units[]{PhysicalUnit::HP1, PhysicalUnit::HP2};
  for (size_t index = 0; index < 2; ++index) {
    auto& hp = *pumps[index];
    hp.mode = physical_measurement(HeatPumpMode::HEATING, 2099U, units[index]);
    hp.compressor_active = physical_measurement(true, 2103U, units[index]);
    hp.defrost_active = physical_measurement(false, 2118U, units[index]);
    hp.valve_transition_active = physical_measurement(false, 2108U, units[index]);
    hp.oil_return_active = physical_measurement(false, 2119U, units[index]);
  }
  input.boiler_heat = physical_measurement(BoilerHeatState::NO_HEAT, 4U);
  input.operation.captured_monotonic_ms = input.monotonic_ms;
  input.operation.captured_context_revision = input.context_revision;
  input.operation.control_mode_valid = input.operation.active_limit_valid = input.operation.service_or_ota_valid = true;
  input.operation.control_mode = LearningControlMode::HEATING;
  assert(build_learning_snapshot(input, QualityConfig{}).measurement_valid);
  return input;
}

template <typename T>
void missing_boot_receipt(PhysicalMeasurement<T>& value) {
  value.valid = false;
  value.received_monotonic_ms = 0;
}

void test_boot_receipt_gaps_wait_without_changing_running_day_bridging() {
  for (int gap = 0; gap < 5; ++gap) {
    auto input = boot_input();
    if (gap == 0) missing_boot_receipt(input.hp1.mode);
    if (gap == 1) missing_boot_receipt(input.hp2.compressor_active);
    if (gap == 2) missing_boot_receipt(input.boiler_heat);
    if (gap == 3) input.hp2.mode.received_monotonic_ms = kNowMs - kHpLearningTiming.max_age_ms - 1U;
    if (gap == 4) input.hp2.mode.received_monotonic_ms = kNowMs - kHpLearningTiming.max_skew_ms - 101U;
    const auto batch = build_learning_snapshot(input, QualityConfig{});
    assert(!batch.measurement_valid && !batch.may_bridge_daily_gap);
    assert(!known_daily_restart_interruption(input, false));
    assert(daily_boot_sources_pending(input, batch));
  }
  auto input = boot_input();
  input.boiler_heat.source = {PhysicalSourceKind::CONTROL_CONTRACT, 2U, PhysicalUnit::SYSTEM};
  input.boiler_heat.provenance = MeasurementProvenance::CONTROL_CONTRACT;
  input.boiler_heat.valid = false;  // Fresh controller contract is not yet ready.
  assert(daily_boot_sources_pending(input, build_learning_snapshot(input, QualityConfig{})));
  const auto complete = boot_input();
  assert(!daily_boot_sources_pending(complete, build_learning_snapshot(complete, QualityConfig{})));
}

void test_boot_missing_first_receipt_never_hides_unsafe_or_malformed_operation() {
  for (int fault = 0; fault < 11; ++fault) {
    auto input = boot_input();
    missing_boot_receipt(input.hp1.mode);
    if (fault == 0) input.hp2.mode.value = HeatPumpMode::COOLING;
    if (fault == 1) input.hp2.mode.value = HeatPumpMode::OFF;  // Compressor is still active.
    if (fault == 2) input.hp2.mode.source.unit = PhysicalUnit::HP1;
    if (fault == 3) input.hp2.compressor_active.source.id = 0;
    if (fault == 4) input.hp2.defrost_active.provenance = MeasurementProvenance::REPUBLISHED;
    if (fault == 5) input.hp2.oil_return_active.timing.max_age_ms = 0;
    if (fault == 6) input.hp2.valve_transition_active.received_monotonic_ms = kNowMs + 1U;
    if (fault == 7) input.hp2.mode.valid = false;  // A received invalid value, not an initial absence.
    if (fault == 8) input.hp2.mode.source_generation = 0;
    if (fault == 9) input.boiler_heat.value = BoilerHeatState::UNKNOWN;
    if (fault == 10) input.hp2.defrost_active.source.kind = PhysicalSourceKind::UNKNOWN;
    const auto batch = build_learning_snapshot(input, QualityConfig{});
    assert(batch.status == SnapshotSourceStatus::MISSING_MEASUREMENT);
    assert(!daily_boot_sources_pending(input, batch));
    input.epoch_s = 0;  // The permanent runtime latch must see faults before UTC is ready.
    assert(known_daily_restart_interruption(input, false));
    const char* expected = fault == 0 ? "hp_mode" : fault == 1 ? "off_compressor_active" : "invalid_operating_receipt";
    assert(strcmp(daily_restart_interruption_reason(input, false), expected) == 0);
  }
  auto boiler = boot_input();
  missing_boot_receipt(boiler.hp1.mode);
  boiler.boiler_heat.value = BoilerHeatState::HEAT_ACTIVE;
  assert(!daily_boot_sources_pending(boiler, build_learning_snapshot(boiler, QualityConfig{})));
  auto service = boiler;
  service.boiler_heat.value = BoilerHeatState::NO_HEAT;
  service.operation.service_or_ota = true;
  assert(known_daily_restart_interruption(service, false));
  assert(strcmp(daily_restart_interruption_reason(service, false), "service_or_ota") == 0);
  assert(!daily_boot_sources_pending(service, build_learning_snapshot(service, QualityConfig{})));
}

void test_boot_received_invalid_operation_cannot_be_forgotten_before_clock_start() {
  auto input = boot_input();
  input.epoch_s = 0;
  input.hp2.compressor_active.valid = false;
  bool recovery_allowed = true;
  if (known_daily_restart_interruption(input, false)) recovery_allowed = false;
  input.hp2.compressor_active.valid = true;
  assert(!known_daily_restart_interruption(input, false));
  assert(!recovery_allowed);
}

void test_boot_missing_operation_does_not_hide_hard_scalar_failure() {
  for (int fault = 0; fault < 5; ++fault) {
    auto input = boot_input();
    missing_boot_receipt(input.hp1.mode);
    if (fault == 0) input.hp2.water_out_c.value = 100.0f;
    if (fault == 1) input.flow_lph.value = kPassiveMaximumFlowLph + 1.0f;
    if (fault == 2) input.room_c.provenance = MeasurementProvenance::REPUBLISHED;
    if (fault == 3) input.hp2.water_out_c.source = input.hp2.water_in_c.source;
    if (fault == 4) input.hp2.water_out_c.value = 80.0f;  // Individually valid, but heat exceeds its existing bound.
    const auto batch = build_learning_snapshot(input, QualityConfig{});
    assert(batch.status == SnapshotSourceStatus::MISSING_MEASUREMENT);
    assert(!daily_boot_sources_pending(input, batch));
  }
}

void test_pre_sntp_scalar_failures_remain_rejected_after_values_recover() {
  for (int fault = 0; fault < 10; ++fault) {
    auto input = boot_input();
    input.epoch_s = 0;
    missing_boot_receipt(input.hp1.mode);
    if (fault == 0) input.hp2.water_out_c.value = 100.0f;
    if (fault == 1) input.flow_lph.value = kPassiveMaximumFlowLph + 1.0f;
    if (fault == 2) input.room_c.provenance = MeasurementProvenance::REPUBLISHED;
    if (fault == 3) input.hp2.water_out_c.source = input.hp2.water_in_c.source;
    if (fault == 4) input.hp2.water_out_c.value = 80.0f;  // Excessive calculated heat.
    if (fault == 5) input.room_c.value = NAN;
    if (fault == 6) input.setpoint_c.value = QualityConfig{}.setpoint_max_c + 1;
    if (fault == 7) input.outside_c.value = QualityConfig{}.outside_min_c - 1;
    if (fault == 8) input.hp2.water_out_c.valid = false;  // Received, unlike a missing startup receipt.
    if (fault == 9) input.hp2.water_out_c.source.unit = PhysicalUnit::HP1;
    bool recovery_allowed = true;
    if (daily_restart_interruption_reason(input, false) != nullptr) recovery_allowed = false;
    assert(!recovery_allowed);
    input = boot_input();
    assert(daily_restart_interruption_reason(input, false) == nullptr);
    assert(!recovery_allowed);
  }
}

void test_missing_or_stale_startup_scalars_do_not_block_recovery() {
  auto input = boot_input();
  input.epoch_s = 0;
  missing_boot_receipt(input.room_c);
  missing_boot_receipt(input.hp2.water_out_c);
  assert(daily_restart_interruption_reason(input, false) == nullptr);
  input = boot_input();
  input.epoch_s = 0;
  input.hp2.water_out_c.received_monotonic_ms = kNowMs - kHpLearningTiming.max_age_ms - 1;
  assert(daily_restart_interruption_reason(input, false) == nullptr);
}

void test_selected_source_adapter_distinguishes_absence_from_received_invalid() {
  using namespace oq_input_source;
  auto input = boot_input();
  input.epoch_s = 0;
  NumericSources sources;
  HoldState hold;
  const auto adapt = [](const NumericSelection& selected, oq_sources::LearningSourceRoute route) {
    return resolved_learning_measurement(
        oq_sources::selected_source(selected.value, selected.valid, route, 4U,
                                    oq_sources::LearningSourceProvenance::SELECTED_VALUE, {},
                                    selected.invalid_received),
        kNowMs);
  };
  const auto missing = select_outside(Source::AUTO, sources, kNowMs, 0, hold);
  assert(missing.route == Source::NONE);
  input.outside_c = adapt(missing, oq_sources::LearningSourceRoute::NONE);
  assert(input.outside_c.received_monotonic_ms == 0U);
  assert(daily_restart_interruption_reason(input, false) == nullptr);
  sources.ha = numeric_sample(false, false, NAN);
  input.room_c =
      adapt(select_direct(Source::HA, sources, true, kNowMs, 0, hold), oq_sources::LearningSourceRoute::HA_ROOM);
  assert(daily_restart_interruption_reason(input, false) == nullptr);
  sources.ha = numeric_sample(false, true, NAN);  // Received NaN, not the startup default.
  input.room_c =
      adapt(select_direct(Source::HA, sources, true, kNowMs, 0, hold), oq_sources::LearningSourceRoute::HA_ROOM);
  assert(strcmp(daily_restart_interruption_reason(input, false), "invalid_measurement") == 0);
  sources.ha = numeric_sample(true, true, 20.0f);
  select_direct(Source::HA, sources, true, kNowMs - 1000, 10000, hold);
  sources.ha = numeric_sample(false, true, NAN);
  const auto held_invalid = select_direct(Source::HA, sources, true, kNowMs, 10000, hold);
  assert(held_invalid.valid && held_invalid.held && held_invalid.value == 20.0f);  // Controls unchanged.
  input.room_c = adapt(held_invalid, oq_sources::LearningSourceRoute::HA_ROOM);
  assert(strcmp(daily_restart_interruption_reason(input, false), "invalid_measurement") == 0);
  sources.outdoor = numeric_sample(true, true, 7.0f);
  const auto fallback = select_outside(Source::AUTO, sources, kNowMs, 0, hold);
  assert(fallback.valid && fallback.route == Source::OUTDOOR && !fallback.invalid_received);
  input.outside_c = adapt(fallback, oq_sources::LearningSourceRoute::OUTSIDE_AGGREGATE);
  sources.ha = numeric_sample(false, true, 20.0f);  // Stale/unavailable but finite is only a gap.
  input.room_c =
      adapt(select_direct(Source::HA, sources, true, kNowMs, 0, hold), oq_sources::LearningSourceRoute::HA_ROOM);
  assert(daily_restart_interruption_reason(input, false) == nullptr);
  sources.ha = room_setpoint_sample(true, true, 100.0f);
  input.setpoint_c =
      adapt(select_direct(Source::HA, sources, true, kNowMs, 0, hold), oq_sources::LearningSourceRoute::HA_SETPOINT);
  assert(strcmp(daily_restart_interruption_reason(input, false), "invalid_measurement") == 0);
}

void test_auto_and_flow_keep_invalid_evidence_without_inventing_startup_receipts() {
  using namespace oq_input_source;
  auto input = boot_input();
  input.epoch_s = 0;
  oq_sources::RawFloatReceipt receipt;
  NumericSources sources;
  sources.outdoor = numeric_sample(true, true, NAN);  // Derived template can already have state.
  sources.outdoor.invalid_received = receipt.invalid_value_received();
  HoldState hold;
  auto outside = select_outside(Source::AUTO, sources, kNowMs, 0, hold);
  assert(!outside.valid && !outside.invalid_received);
  receipt.observe(NAN, kNowMs, false);
  sources.outdoor.invalid_received = receipt.invalid_value_received();
  outside = select_outside(Source::AUTO, sources, kNowMs, 0, hold);
  assert(!outside.valid && outside.invalid_received);
  input.outside_c = resolved_learning_measurement(
      oq_sources::selected_source(outside.value, outside.valid, oq_sources::LearningSourceRoute::NONE, 4U,
                                  oq_sources::LearningSourceProvenance::SELECTED_VALUE, {}, outside.invalid_received),
      kNowMs);
  assert(strcmp(daily_restart_interruption_reason(input, false), "invalid_measurement") == 0);
  sources.api = numeric_sample(true, true, 7.0f);
  outside = select_outside(Source::AUTO, sources, kNowMs, 0, hold);
  assert(outside.valid && outside.route == Source::API && !outside.invalid_received);
  input = boot_input();
  input.epoch_s = 0;
  FlowInputs flow;
  flow.selected = Source::OUTDOOR;
  flow.q_hardware = true;
  flow.controller_mode = ControllerFlowMode::LOCAL;
  flow.controller = numeric_sample(true, false, NAN);
  auto selected = select_flow(flow);
  assert(!selected.valid && !selected.invalid_received);
  flow.controller = numeric_sample(true, true, NAN);
  selected = select_flow(flow);
  assert(!selected.valid && selected.invalid_received);
  input.flow_lph = resolved_learning_measurement(
      oq_sources::selected_source(selected.value, selected.valid, oq_sources::LearningSourceRoute::CONTROLLER_FLOW, 4U,
                                  oq_sources::LearningSourceProvenance::SELECTED_VALUE, {}, selected.invalid_received),
      kNowMs);
  assert(strcmp(daily_restart_interruption_reason(input, false), "invalid_measurement") == 0);
  receipt.observe(1000.0f, kNowMs, true);
  receipt.invalidate();  // Availability changed, but no bad numeric sample was received.
  assert(!receipt.invalid_value_received());
}

void test_filtered_outside_fault_checks_only_selected_contributors() {
  oq_sources::RawFloatReceipt first, second;
  first.observe(7.0f, kNowMs - 1000, true);
  second.observe(8.0f, kNowMs - 1000, true);
  oq_sources::LocalOutsideSelection local;
  local.observe(oq_sources::LocalOutsideRoute::HP1, oq_sources::LocalOutsideOperation::NONE, true, first, second);
  first.observe(120.0f, kNowMs, false);  // The clamp retains the earlier 7 C sensor state.
  oq_input_source::NumericSources sources;
  sources.outdoor = oq_input_source::numeric_sample(true, true, 7.0f);
  sources.outdoor.invalid_received = local.invalid_received(first, second);
  oq_input_source::HoldState hold;
  auto selected = oq_input_source::select_outside(oq_input_source::Source::AUTO, sources, kNowMs, 0, hold);
  assert(selected.valid && selected.value == 7.0f && selected.invalid_received);
  auto input = boot_input();
  input.epoch_s = 0;
  input.outside_c = resolved_learning_measurement(
      oq_sources::selected_source(selected.value, selected.valid, oq_sources::LearningSourceRoute::OUTSIDE_AGGREGATE,
                                  4U, oq_sources::LearningSourceProvenance::SELECTED_VALUE, {},
                                  selected.invalid_received),
      kNowMs);
  assert(strcmp(daily_restart_interruption_reason(input, false), "invalid_measurement") == 0);
  local.route = oq_sources::LocalOutsideRoute::HP2;
  assert(!local.invalid_received(first, second));  // Bad HP1 does not contribute.
  local.route = oq_sources::LocalOutsideRoute::COMPOSITE;
  assert(local.invalid_received(first, second));
  sources.ha = oq_input_source::numeric_sample(true, true, 5.0f);
  selected = oq_input_source::select_outside(oq_input_source::Source::AUTO, sources, kNowMs, 0, hold);
  assert(selected.valid && selected.route == oq_input_source::Source::HA && !selected.invalid_received);
}

}  // namespace

int main() {
  test_filtered_outside_fault_checks_only_selected_contributors();
  test_auto_and_flow_keep_invalid_evidence_without_inventing_startup_receipts();
  test_selected_source_adapter_distinguishes_absence_from_received_invalid();
  test_pre_sntp_scalar_failures_remain_rejected_after_values_recover();
  test_missing_or_stale_startup_scalars_do_not_block_recovery();
  // A -> B -> A between learner ticks retains the same durable route but must
  // break collection through the resolver's revision, once per observed change.
  uint32_t revisions[4]{};
  oq_sources::ResolvedLearningSource sources[4];
  for (auto& source : sources) source.configuration_generation = 1;
  assert(oq_power_house::learning::observe_source_revisions(revisions, sources));
  assert(!oq_power_house::learning::observe_source_revisions(revisions, sources));
  assert(source_configuration_available(sources));
  sources[0].valid = false;
  assert(source_configuration_available(sources));
  assert(!observe_source_revisions(revisions, sources));
  sources[2].configuration_generation = 3;
  assert(oq_power_house::learning::observe_source_revisions(revisions, sources));
  assert(!oq_power_house::learning::observe_source_revisions(revisions, sources));

  test_positive_unsafe_evidence_during_sntp_startup_prevents_recovery();
  test_boot_receipt_gaps_wait_without_changing_running_day_bridging();
  test_boot_missing_first_receipt_never_hides_unsafe_or_malformed_operation();
  test_boot_received_invalid_operation_cannot_be_forgotten_before_clock_start();
  test_boot_missing_operation_does_not_hide_hard_scalar_failure();
  test_delayed_initial_resolution_does_not_look_like_a_route_change();
  test_compile_time_topology_maps_single_and_duo();
  sources[0].configuration_generation = 0;
  assert(!source_configuration_available(sources));
  test_every_selected_route_uses_its_selected_value();
  test_selected_hold_uses_the_selected_value();
  test_missing_selection_is_not_a_learning_measurement();
  test_hp_decode_and_boiler_bits_fail_closed();
  test_strategy_output_requires_current_matching_owner();
  test_diagnostic_coverage_never_crosses_invalid_gap_or_generation_edge();
  test_diagnostic_capture_requires_opt_in_and_breaks_pause_continuity();
  test_heating_and_idle_contract_requires_current_withdrawn_boiler_command();
  test_diagnostic_heat_preserves_signed_physical_heat_outside_training_gates();
  test_diagnostic_heat_rejects_incoherent_inputs();
  return 0;
}
