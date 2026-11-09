#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/control/oq_input_source_logic.h"
#include "../../openquatt/includes/learning/oq_ph_learning_fit.h"
#include "../../openquatt/includes/learning/oq_ph_learning_source_logic.h"
#include "../../openquatt/includes/learning/oq_ph_learning_live_logic.h"
#include "../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

namespace {
using namespace oq_power_house;
using namespace oq_power_house::learning;

// These are explicit synthetic physical observations, not a claim that live
// selected sensors already expose this provenance or uncertainty contract.
template <typename T>
PhysicalMeasurement<T> measured(T value, uint32_t field, PhysicalUnit unit, uint64_t now_ms) {
  return {value,
          true,
          {PhysicalSourceKind::MODBUS_REGISTER, field, unit},
          1,
          now_ms,
          {60000, 1000},
          MeasurementProvenance::PHYSICAL_RECEIPT};
}

HeatPumpRawMeasurements unit(PhysicalUnit identity, uint64_t now_ms, float water_in, float water_out) {
  return {true,
          measured(water_in, 2133, identity, now_ms),
          measured(water_out, 2134, identity, now_ms),
          measured(HeatPumpMode::HEATING, 2099, identity, now_ms),
          measured(true, 2108, identity, now_ms),
          measured(false, 2118, identity, now_ms),
          measured(false, 2120, identity, now_ms),
          measured(false, 2119, identity, now_ms)};
}

LearningSourceInput observation(uint64_t now_ms, uint32_t epoch_s, float outside_c) {
  LearningSourceInput input;
  input.monotonic_ms = now_ms;
  input.epoch_s = epoch_s;
  input.context_revision = 1;
  input.topology = HydronicTopology::DUO_SERIES;
  input.room_c = measured(20.0f, 1, PhysicalUnit::SYSTEM, now_ms);
  input.setpoint_c = measured(20.0f, 2, PhysicalUnit::SYSTEM, now_ms);
  input.outside_c = measured(outside_c, 2110, PhysicalUnit::HP1, now_ms);
  input.flow_lph = measured(1000.0f, 2138, PhysicalUnit::HP1, now_ms);
  const float total_delta = 200.0f * (16.0f - outside_c) * 3600.0f / (1000.0f * 4180.0f);
  input.hp1 = unit(PhysicalUnit::HP1, now_ms, 30.0f, 30.0f + total_delta / 2.0f);
  input.hp2 = unit(PhysicalUnit::HP2, now_ms, 30.0f + total_delta / 2.0f, 30.0f + total_delta);
  input.boiler_heat = measured(BoilerHeatState::NO_HEAT, 3, PhysicalUnit::SYSTEM, now_ms);
  input.operation.control_mode_valid = true;
  input.operation.captured_monotonic_ms = now_ms;
  input.operation.captured_context_revision = input.context_revision;
  input.operation.control_mode = LearningControlMode::HEATING;
  input.operation.active_limit_valid = true;
  input.operation.service_or_ota_valid = true;
  return input;
}

void raw_series_observations_to_advice() {
  constexpr uint32_t start_epoch = 20000U * 86400U;
  SegmentRecord records[kMaxSegmentRecords];
  RecordBuffer buffer{records, 0, kMaxSegmentRecords};
  SegmentAccumulator aggregate;
  QualityConfig quality;
  for (uint32_t minute = 0; minute <= 9U * 1440U; ++minute) {
    const uint32_t day = minute / 1440U < 9U ? minute / 1440U : 8U;
    const uint32_t seconds = minute * 60U;
    const auto raw = observation(1000ULL + seconds * 1000ULL, start_epoch + seconds, -5.0f + 2.0f * day);
    const auto event = observe_source_input(aggregate, raw, quality);
    assert(event.source.measurement_valid);
    if (event.aggregate.has_record)
      assert(append_record(buffer, event.aggregate.record, raw.epoch_s, quality) == LearningStatus::OK);
  }
  assert(buffer.count == 9);
  FitConfig config;
  AdviceFitWorkspace workspace;
  const HouseLine active{150.0f, 15.0f};
  auto status = begin_advice_fit(records, buffer.count, start_epoch + 9U * 86400U, active, quality, config, workspace);
  size_t steps = 0;
  while (status == LearningStatus::FIT_IN_PROGRESS) {
    assert(++steps <= kMaxAdviceFitSteps);
    status = advance_advice_fit(workspace);
  }
  assert(status == LearningStatus::ADVICE_READY);
  assert(fabsf(workspace.result.candidate.heat_loss_w_per_k - 200.0f) < 0.01f);
  assert(fabsf(workspace.result.candidate.zero_power_temp_c - 16.0f) < 0.001f);
  assert(workspace.result.holdout_candidate_mae_w < 0.01f);
  assert(active.heat_loss_w_per_k == 150.0f && active.zero_power_temp_c == 15.0f);
}

void raw_daily_profiles_cross_zero_power_temperature() {
  constexpr uint32_t start_epoch = 20000U * 86400U;
  const float day_mean[]{-4.0f, -1.0f, 2.0f, 5.0f, 8.0f, 11.0f, 14.0f, 17.0f, 20.0f, 5.0f, 17.0f, 29.0f};
  SegmentRecord records[kMaxSegmentRecords];
  RecordBuffer buffer{records, 0, kMaxSegmentRecords};
  SegmentAccumulator aggregate;
  QualityConfig quality;
  for (uint32_t minute = 0; minute <= 12U * 1440U; ++minute) {
    const uint32_t day = minute / 1440U < 12U ? minute / 1440U : 11U;
    const uint32_t hour_group = (minute % 1440U) / 180U;
    const float effective = day_mean[day] - 7.0f + 2.0f * hour_group;
    const float room = 18.0f + static_cast<float>(day % 5U);
    auto raw = observation(1000ULL + minute * 60000ULL, start_epoch + minute * 60U, effective);
    raw.room_c.value = room;
    raw.setpoint_c.value = 17.0f + day % 7U;
    raw.outside_c.value += room - 20.0f;
    const float heat = fmaxf(0.0f, 200.0f * (16.0f - effective));
    const float delta = heat * 3600.0f / (1000.0f * 4180.0f);
    raw.hp1 = unit(PhysicalUnit::HP1, raw.monotonic_ms, 30.0f, 30.0f + delta / 2.0f);
    raw.hp2 = unit(PhysicalUnit::HP2, raw.monotonic_ms, 30.0f + delta / 2.0f, 30.0f + delta);
    const auto event = observe_source_input(aggregate, raw, quality);
    assert(event.source.measurement_valid);
    if (event.aggregate.has_record)
      assert(append_record(buffer, event.aggregate.record, raw.epoch_s, quality) == LearningStatus::OK);
  }
  assert(buffer.count == 12);
  assert(records[11].mean_heat_w == 0.0f);
  assert(records[7].mean_heat_w > 250.0f);
  assert(records[7].effective_outside_profile_centi[0] < 1600 && records[7].effective_outside_profile_centi[7] > 1600);
  AdviceFitWorkspace workspace;
  auto status =
      begin_advice_fit(records, buffer.count, start_epoch + 12U * 86400U, {150.0f, 15.0f}, quality, {}, workspace);
  assert(status == LearningStatus::FIT_IN_PROGRESS);
  while (status == LearningStatus::FIT_IN_PROGRESS) status = advance_advice_fit(workspace);
  assert(status == LearningStatus::ADVICE_READY);
  assert(fabsf(workspace.result.candidate.heat_loss_w_per_k - 200.0f) < 1.0f);
  assert(fabsf(workspace.result.candidate.zero_power_temp_c - 16.0f) < 0.1f);
  assert(workspace.result.holdout_candidate_mae_w < 10.0f);
}

void heating_cycles_keep_idle_time_and_pump_runout(bool interrupt_idle = false) {
  PassiveRuntimeStorage state;
  PassiveRuntimeConfig config;
  const uint8_t bytes[]{1};
  const PassiveContextView context{bytes, sizeof(bytes), 1};
  assert(initialize_passive_runtime(state, context, config, true) == PassiveRuntimeStatus::COLLECTING);
  oq_sources::SourceConfigurationGeneration flow_generation;
  const oq_sources::SourceConfigurationKey flow_configuration{2U, 0U, 0U, 0U};
  flow_generation.observe(flow_configuration);
  const auto startup_flow = oq_sources::selected_source(
      0.0f, true, oq_sources::LearningSourceRoute::SYNTHESIZED_ZERO_FLOW, flow_generation.current());
  const uint32_t initial_flow_generation = flow_generation.observe_resolution(startup_flow);
  constexpr uint32_t epoch = 20000U * 86400U;
  // Twenty-four hourly cycles: 30 min heating, 5 min pump runout with signed loss,
  // 25 min zero flow. Mean water temperature stays at 30 C at the boundaries.
  for (uint32_t minute = 0; minute <= 1440; ++minute) {
    const uint32_t phase = minute % 60;
    const int cm = phase < 30 ? 2 : phase < 35 ? 1 : 0;
    const uint64_t now = 1000ULL + minute * 60000ULL;
    auto raw = observation(now, epoch + minute * 60U, 5.0f);
    const float flow = cm == 0 ? 0.0f : 1000.0f;
    const float total_delta = cm == 2 ? 4000.0f * 3600.0f / (1000.0f * 4180.0f) : -0.2f;
    oq_input_source::FlowInputs flow_input;
    flow_input.selected = oq_input_source::Source::OUTDOOR;
    flow_input.duo = true;
    flow_input.aggregate = {1000.0f, true};
    flow_input.all_relevant_pumps_stopped = cm == 0;
    const auto selected_flow = oq_input_source::select_flow(flow_input);
    const auto flow_route = selected_flow.route == oq_input_source::FlowRoute::PUMPS_STOPPED
                                ? oq_sources::LearningSourceRoute::SYNTHESIZED_ZERO_FLOW
                                : oq_sources::LearningSourceRoute::FLOW_AGGREGATE;
    auto resolved_flow = oq_sources::selected_source(selected_flow.value, selected_flow.valid, flow_route,
                                                     flow_generation.observe(flow_configuration));
    resolved_flow.configuration_generation = flow_generation.observe_resolution(resolved_flow);
    assert(resolved_flow.configuration_generation == initial_flow_generation);
    raw.flow_lph = resolved_learning_measurement(resolved_flow, now);
    assert(raw.flow_lph.valid && raw.flow_lph.value == flow);
    raw.hp1 = unit(PhysicalUnit::HP1, now, 30.0f - total_delta / 2.0f, 30.0f);
    raw.hp2 = unit(PhysicalUnit::HP2, now, 30.0f, 30.0f + total_delta / 2.0f);
    raw.hp1.compressor_active.value = raw.hp2.compressor_active.value = cm == 2;
    raw.hp1.mode.value = raw.hp2.mode.value = cm == 2 ? HeatPumpMode::HEATING : HeatPumpMode::OFF;
    const oq_boiler::BoilerCommand off{
        true, false, false, NAN, NAN, oq_boiler::COMMAND_SOURCE_NONE, static_cast<uint32_t>(now)};
    raw.boiler_heat = learning_no_boiler_heat_contract(true, cm, off, false, false, now, false);
    const bool invalid_idle = interrupt_idle && minute == 100;
    if (invalid_idle) {
      assert(cm == 0);
      raw.flow_lph.valid = false;
    }
    const auto calorimetry = evaluate_calorimetry(raw, config.quality);
    const auto batch = build_learning_snapshot(raw, config.quality, SnapshotPurpose::STRUCTURAL_BATCH, &calorimetry);
    const auto thermal = build_learning_snapshot(raw, config.quality, SnapshotPurpose::THERMAL_DYNAMIC, &calorimetry);
    assert(batch.measurement_valid == !invalid_idle && thermal.measurement_valid == !invalid_idle);
    if (!invalid_idle && cm == 0) assert(batch.snapshot.heat_to_water_w == 0.0f);
    if (!invalid_idle && cm == 1) assert(batch.snapshot.heat_to_water_w < 0.0f);
    PassiveTickInput input;
    input.context = context;
    input.now_monotonic_ms = now;
    input.now_epoch_s = raw.epoch_s;
    input.opted_in = input.context_valid = input.active_line_valid = true;
    input.active_line = {150.0f, 16.0f};
    input.batch_snapshot_available = batch.has_snapshot;
    input.dynamic_snapshot_available = thermal.has_snapshot;
    input.batch_snapshot = batch.snapshot;
    input.dynamic_snapshot = thermal.snapshot;
    tick_passive_runtime(state, input);
    if (!interrupt_idle) assert(state.diagnostics.rejected_batch_observations == 0);
    assert(!passive_runtime_summary(state, now).auto_apply_allowed);
  }
  if (interrupt_idle) {
    assert(state.record_count == 0);
    assert(state.thermal_state.accepted_samples < 48);
    return;
  }
  assert(state.record_count == 1);
  assert(state.records[0].duration_s == 86400U);
  const float expected_mean = (4000.0f * 30.0f - (1000.0f / 3600.0f * 4180.0f * 0.2f) * 5.0f) / 60.0f;
  assert(fabsf(state.records[0].mean_heat_w - expected_mean) < 0.1f);
  assert(state.thermal_state.accepted_samples == 48);
}

void daily_dhw_only_preserves_collection_and_restart_eligibility() {
  for (uint16_t payload : {0x0000U, 0x000CU, 0x000EU, 0x0008U, 0x002CU}) {
    SegmentAccumulator day;
    unsigned records = 0;
    const bool allowed = payload == 0x0000U || payload == 0x000CU;
    for (uint32_t minute = 0; minute <= 1440U; ++minute) {
      const uint64_t now = 1000ULL + minute * 60000ULL;
      auto raw = observation(now, 20000U * 86400U + minute * 60U, 5.0f);
      const oq_boiler::BoilerCommand off{
          true, false, false, NAN, NAN, oq_boiler::COMMAND_SOURCE_NONE, static_cast<uint32_t>(now)};
      const auto contract = learning_no_boiler_heat_contract(true, 2, off, false, false, now, false);
      const bool during_dhw = minute >= 600U && minute < 610U;
      const auto telemetry = learning_boiler_status(during_dhw ? payload : 0U, now, true, true);
      raw.boiler_heat = learning_boiler_heat(true, contract, telemetry);
      const bool interrupted = during_dhw && !allowed;
      const auto event = observe_source_input(day, raw, QualityConfig{});
      assert(event.source.measurement_valid == !interrupted);
      const auto thermal = build_learning_snapshot(raw, QualityConfig{}, SnapshotPurpose::THERMAL_DYNAMIC);
      assert(thermal.measurement_valid == !interrupted);
      if (during_dhw) {
        // Live and pre-SNTP/boot recovery share the same physical classifier.
        assert(known_daily_restart_interruption(raw, telemetry.value == BoilerHeatState::HEAT_ACTIVE) == !allowed);
        assert(event.source.may_bridge_daily_gap == false);
      }
      if (event.aggregate.has_record) {
        ++records;
        assert(event.aggregate.record.duration_s == 86400U);
        assert(fabsf(event.aggregate.record.mean_heat_w - 2200.0f) < 0.1f);
      }
    }
    assert(records == (allowed ? 1U : 0U));
  }
}

LearningSourceInput system_observation(uint32_t second, int cm) {
  auto raw = observation(1000ULL + second * 1000ULL, 20000U * 86400U + second, 5.0f);
  raw.system_heat_enabled = true;
  raw.operation.control_mode = learning_control_mode(0, cm);
  raw.circuit_flow_lph = raw.flow_lph;
  raw.system_supply_c = measured(44.0f, 4, PhysicalUnit::SYSTEM, raw.monotonic_ms);
  raw.system_supply_c.source.kind = PhysicalSourceKind::DIRECT_SENSOR;
  return raw;
}

void daily_system_heat_survives_boiler_modes_and_ot_changes() {
  for (const auto topology : {HydronicTopology::SINGLE, HydronicTopology::DUO_SERIES}) {
    QualityConfig quality;
    SegmentAccumulator day;
    SegmentRecord record;
    for (uint32_t second = 0; second <= 86400U; second += 60U) {
      const int cm = second < 28800U ? 2 : second < 57600U ? 3 : 4;
      auto raw = system_observation(second, cm);
      raw.topology = topology;
      if (topology == HydronicTopology::SINGLE) raw.hp2 = {};
      const auto boiler_status = second % 180U == 0 ? 0x0002U : second % 180U == 60 ? 0x0008U : 0x000CU;
      raw.boiler_heat = learning_boiler_status(boiler_status, raw.monotonic_ms, second % 240U != 0, true);
      if (cm == 4) {
        // The heat pumps are idle. Their outlets and operating receipts aren't needed to measure the whole circuit.
        raw.hp1.water_out_c = {};
        raw.hp2.water_out_c = {};
        raw.hp1.mode = raw.hp2.mode = {};
      }
      const auto result = observe_source_input(day, raw, quality);
      assert(result.source.measurement_valid);
      assert(known_daily_restart_interruption(raw, true, quality) == false);
      const auto thermal = build_learning_snapshot(raw, quality, SnapshotPurpose::THERMAL_DYNAMIC);
      assert(!thermal.measurement_valid || (cm == 2 && thermal.snapshot.heat_to_water_w < 2300.0f));
      if (result.aggregate.has_record) record = result.aggregate.record;
    }
    assert(record.duration_s == 86400U);
    assert(fabsf(record.mean_heat_w - 1000.0f / 3600.0f * 4180.0f * 14.0f) < 0.02f);
    assert(record.daily_quality.heat_coverage_bp == 10000U);
  }
}

void daily_system_heat_only_bridges_missing_measurements() {
  const QualityConfig quality;
  for (const int cm : {2, 3, 4}) {
    SegmentAccumulator day;
    assert(observe_source_input(day, system_observation(0, cm), quality).source.measurement_valid);
    auto raw = system_observation(10, cm);
    raw.system_supply_c = {};
    // Even fresh OpenTherm NO_HEAT and valid HP calorimetry cannot replace the system measurement.
    assert(evaluate_calorimetry(raw, quality).valid);
    for (const auto& telemetry :
         {PhysicalMeasurement<BoilerHeatState>{}, learning_boiler_status(0x0000, raw.monotonic_ms, true, true),
          learning_boiler_status(0x0002, raw.monotonic_ms, true, true)}) {
      raw.boiler_heat = telemetry;
      const auto gap = build_learning_snapshot(raw, quality);
      assert(!gap.measurement_valid && gap.may_bridge_daily_gap);
      assert(isnan(gap.snapshot.heat_to_water_w) && isnan(gap.snapshot.partial_heat_to_water_w));
      assert(!known_daily_restart_interruption(raw, telemetry.value == BoilerHeatState::HEAT_ACTIVE, quality));
      assert(daily_boot_sources_pending(raw, gap, quality));
    }
    const auto gap = observe_source_input(day, raw, quality);
    assert(!gap.source.measurement_valid && day.active && day.source_gap_pending && day.heat_gap_pending);
    raw = system_observation(20, cm);
    const auto recovery = observe_source_input(day, raw, quality);
    assert(recovery.source.measurement_valid && !day.source_gap_pending && day.missing_energy_uncertainty_ws > 0.0);
    assert(day.start_epoch_s == 20000U * 86400U);
  }

  auto raw = system_observation(40, 3);
  raw.circuit_flow_lph = {};
  assert(build_learning_snapshot(raw, quality).may_bridge_daily_gap);
  raw = system_observation(50, 3);
  raw.circuit_flow_lph.value = 0.0f;
  assert(build_learning_snapshot(raw, quality).measurement_valid);
  assert(build_learning_snapshot(raw, quality).snapshot.heat_to_water_w == 0.0f);

  raw = system_observation(60, 3);
  raw.system_supply_c.provenance = MeasurementProvenance::HELD;
  const auto held = build_learning_snapshot(raw, quality);
  assert(held.status == SnapshotSourceStatus::UNTRUSTED_PROVENANCE && !held.may_bridge_daily_gap);

  raw = system_observation(70, 3);
  raw.operation.service_or_ota = true;
  assert(build_learning_snapshot(raw, quality).status == SnapshotSourceStatus::SERVICE_OR_OTA_ACTIVE);
  raw = system_observation(80, 3);
  raw.hp1.mode.value = HeatPumpMode::COOLING;
  assert(build_learning_snapshot(raw, quality).status == SnapshotSourceStatus::COOLING_ACTIVE);
  raw.hp1.defrost_active.value = true;
  raw.system_supply_c.value = 28.0f;
  const auto defrost = build_learning_snapshot(raw, quality);
  assert(defrost.measurement_valid && defrost.snapshot.heat_to_water_w < 0.0f);
}

void daily_without_system_sensor_keeps_hp_measurements() {
  const QualityConfig quality;
  bool supply_observed = false;
  SegmentAccumulator day;
  for (uint32_t second = 0; second <= 180; second += 10) {
    auto raw = system_observation(second, 2);
    raw.system_supply_c = {};
    raw.system_heat_enabled = daily_system_heat_enabled(true, supply_observed, {}, raw.monotonic_ms);
    const auto result = observe_source_input(day, raw, quality);
    assert(!raw.system_heat_enabled && result.source.measurement_valid);
    assert(result.source.snapshot.heat_to_water_w == evaluate_calorimetry(raw, quality).heat_to_water_w);
    assert(day.active && !day.source_gap_pending);
  }
  auto raw = system_observation(190, 2);
  oq_sources::RawFloatReceipt supply;
  supply.observe(44.0f, raw.monotonic_ms, true);
  assert(daily_system_heat_enabled(true, supply_observed, supply, raw.monotonic_ms));
  assert(observe_source_input(day, raw, quality).source.measurement_valid);
  raw = system_observation(200, 2);
  raw.system_supply_c = {};
  raw.system_heat_enabled = daily_system_heat_enabled(true, supply_observed, {}, raw.monotonic_ms);
  assert(raw.system_heat_enabled);
  const auto missing = observe_source_input(day, raw, quality);
  assert(!missing.source.measurement_valid && missing.source.may_bridge_daily_gap && day.active);
  raw = system_observation(210, 2);
  assert(observe_source_input(day, raw, quality).source.measurement_valid);
  assert(day.start_epoch_s == 20000U * 86400U);
  assert(!daily_system_heat_enabled(false, supply_observed, {}, raw.monotonic_ms));
  assert(daily_system_heat_enabled(true, supply_observed, {}, raw.monotonic_ms));

  supply_observed = false;
  assert(!daily_system_heat_enabled(true, supply_observed, supply, raw.monotonic_ms + 30001));
  supply.invalidate();
  assert(!daily_system_heat_enabled(true, supply_observed, supply, raw.monotonic_ms));
}

void daily_idle_without_boiler_heat_counts_zero_beyond_the_gap_limit() {
  using Route = oq_sources::LearningSourceRoute;
  const QualityConfig quality;
  for (const auto route : {Route::SYNTHESIZED_ZERO_FLOW, Route::CONTROLLER_FLOW}) {
    SegmentAccumulator day;
    assert(observe_source_input(day, system_observation(0, 2), quality).source.measurement_valid);
    for (uint32_t seconds = 10; seconds <= 180; seconds += 10) {
      auto raw = system_observation(seconds, 0);
      raw.flow_lph.value = 0.0f;
      PhysicalMeasurement<float> physical;
      if (route == Route::CONTROLLER_FLOW && seconds <= 45) {
        // The same zero receipt ages past the 30 s skew limit and then expires.
        physical = measured(0.0f, 8, PhysicalUnit::SYSTEM, 1000);
        physical.timing = kHpLearningTiming;
      }
      raw.circuit_flow_lph = learning_system_flow(physical, raw.flow_lph, route, raw.boiler_heat, raw.monotonic_ms,
                                                  true, route == Route::CONTROLLER_FLOW);
      const auto result = observe_source_input(day, raw, quality);
      assert(result.source.measurement_valid && result.source.snapshot.heat_to_water_w == 0.0f);
      assert(day.active && !day.source_gap_pending && day.start_epoch_s == 20000U * 86400U);
    }

    auto boiler = system_observation(190, 4);
    boiler.boiler_heat.value = BoilerHeatState::HEAT_ACTIVE;
    boiler.hp1.mode.value = boiler.hp2.mode.value = HeatPumpMode::OFF;
    boiler.hp1.compressor_active.value = boiler.hp2.compressor_active.value = false;
    boiler.flow_lph.value = 0.0f;  // The displayed flow can lag the physical receipt.
    const auto physical = boiler.circuit_flow_lph;
    assert(!learning_system_flow({}, boiler.flow_lph, route, boiler.boiler_heat, boiler.monotonic_ms, true,
                                 route == Route::CONTROLLER_FLOW)
                .valid);
    boiler.circuit_flow_lph =
        learning_system_flow(physical, boiler.flow_lph, route, boiler.boiler_heat, boiler.monotonic_ms, true, false);
    const auto result = observe_source_input(day, boiler, quality);
    assert(result.source.measurement_valid && result.source.snapshot.heat_to_water_w > 0.0f);
    assert(day.active && !day.source_gap_pending && day.start_epoch_s == 20000U * 86400U);
  }

  auto raw = system_observation(190, 0);
  raw.flow_lph.value = 0.0f;
  const auto flow = [&](const PhysicalMeasurement<BoilerHeatState>& boiler) {
    return learning_system_flow({}, raw.flow_lph, Route::CONTROLLER_FLOW, boiler, raw.monotonic_ms, true, true);
  };
  assert(!flow({}).valid);
  auto boiler = raw.boiler_heat;
  boiler.value = BoilerHeatState::HEAT_ACTIVE;
  assert(!flow(boiler).valid);
  boiler = raw.boiler_heat;
  boiler.received_monotonic_ms = 1;
  assert(!flow(boiler).valid);
  assert(!learning_system_flow({}, raw.flow_lph, Route::CONTROLLER_FLOW, raw.boiler_heat, raw.monotonic_ms, true, false)
              .valid);
  assert(!learning_system_flow({}, raw.flow_lph, Route::CONTROLLER_FLOW, raw.boiler_heat, raw.monotonic_ms, false, true)
              .valid);
  oq_sources::RawFloatReceipt pump;
  pump.observe(0.0f, raw.monotonic_ms, true);
  assert(learning_pump_stopped(pump, raw.monotonic_ms));
  assert(!learning_pump_stopped(pump, raw.monotonic_ms + 30001));
  pump.observe(0x0800, raw.monotonic_ms, true);
  assert(!learning_pump_stopped(pump, raw.monotonic_ms));
  pump.invalidate();
  assert(!learning_pump_stopped(pump, raw.monotonic_ms));
  raw.circuit_flow_lph = flow(raw.boiler_heat);
  raw.system_supply_c = {};
  assert(!build_learning_snapshot(raw, quality).measurement_valid);  // No zero shortcut for a lost supply sensor.
  raw = system_observation(200, 0);
  raw.flow_lph.value = 1.0f;
  assert(!flow(raw.boiler_heat).valid);
}

void daily_system_heat_limit_is_inclusive_signed_and_separate_from_hp_measurement() {
  QualityConfig quality;
  quality.max_daily_missing_heat_w = 12230.56f;
  quality.max_observed_heat_w = 1.20f * quality.max_daily_missing_heat_w;
  // At 900 L/h, the production calorimetry multiplies the temperature delta
  // by exactly 1045 W/K. These temperatures produce exactly 50000 float watts.
  const float delta_c = 50000.0f / 1045.0f;
  for (const float sign : {-1.0f, 1.0f}) {
    const auto source = [&](uint32_t second, float delta) {
      auto raw = system_observation(second, 3);
      raw.circuit_flow_lph.value = 900.0f;
      raw.hp1.water_in_c.value = sign > 0.0f ? 0.0f : delta;
      raw.system_supply_c.value = sign > 0.0f ? delta : 0.0f;
      return raw;
    };
    SegmentAccumulator day;
    const auto boundary = observe_source_input(day, source(0, delta_c), quality);
    assert(boundary.source.measurement_valid && boundary.source.status == SnapshotSourceStatus::OK);
    assert(boundary.source.snapshot.heat_to_water_w == sign * 50000.0f && day.active);
    const auto continued = observe_source_input(day, source(5, delta_c), quality);
    assert(continued.source.measurement_valid && day.active && day.heat_integral != 0.0);

    const auto outside = observe_source_input(day, source(10, nextafterf(delta_c, INFINITY)), quality);
    assert(outside.source.status == SnapshotSourceStatus::INVALID_VALUE);
    assert(!outside.source.measurement_valid && !outside.source.may_bridge_daily_gap);
    assert(isnan(outside.source.snapshot.heat_to_water_w) && isnan(outside.source.snapshot.partial_heat_to_water_w));
    assert(outside.aggregate.status == LearningStatus::INVALID_MEASUREMENT && !outside.aggregate.has_record);
    assert(!day.active && day.heat_integral == 0.0);

    auto hp_only = source(20, delta_c);
    hp_only.system_heat_enabled = false;
    hp_only.operation.control_mode = learning_control_mode(0, 2);
    hp_only.system_supply_c = {};
    hp_only.flow_lph.value = 900.0f;
    hp_only.hp1.water_out_c.value = hp_only.hp1.water_in_c.value;
    hp_only.hp2.water_in_c.value = hp_only.hp1.water_in_c.value;
    hp_only.hp2.water_out_c.value = sign > 0.0f ? delta_c : 0.0f;
    const auto hp = build_learning_snapshot(hp_only, quality);
    assert(hp.status == SnapshotSourceStatus::HEAT_OUT_OF_RANGE && !hp.measurement_valid && hp.may_bridge_daily_gap);
    assert(isnan(hp.snapshot.heat_to_water_w) && isnan(hp.snapshot.partial_heat_to_water_w));
    assert(hp.snapshot.gap_heat_observed_abs_w == 50000.0f);
  }
}

void daily_boiler_limits_stay_separate_and_preserve_an_open_gap() {
  PassiveRuntimeConfig config;
  config.quality.max_daily_missing_heat_w = 6115.28f;
  config.quality.max_observed_heat_w = 7338.336f;
  const uint8_t context_bytes[]{1};
  const PassiveContextView context{context_bytes, sizeof(context_bytes), 1};
  PassiveRuntimeStorage state;
  initialize_passive_runtime(state, context, config, true);
  const auto advance = [&](const LearningSourceInput& raw) {
    const auto batch = build_learning_snapshot(raw, config.quality, SnapshotPurpose::STRUCTURAL_BATCH);
    const auto dynamic = build_learning_snapshot(raw, config.quality, SnapshotPurpose::THERMAL_DYNAMIC);
    PassiveTickInput tick;
    tick.context = context;
    tick.now_monotonic_ms = raw.monotonic_ms;
    tick.now_epoch_s = raw.epoch_s;
    tick.opted_in = tick.context_valid = tick.active_line_valid = true;
    tick.active_line = {150.0f, 16.0f};
    tick.batch_snapshot_available = batch.has_snapshot;
    tick.may_bridge_daily_gap = batch.may_bridge_daily_gap;
    tick.batch_source_status = batch.status;
    tick.batch_snapshot = batch.snapshot;
    tick.dynamic_snapshot_available = dynamic.has_snapshot;
    tick.dynamic_snapshot = dynamic.snapshot;
    const auto daily_quality = daily_heat_quality(config.quality, raw.system_heat_enabled);
    tick_passive_runtime(state, tick, &daily_quality);
    assert(state.config.quality.max_observed_heat_w == config.quality.max_observed_heat_w);
    assert(state.config.quality.max_daily_missing_heat_w == config.quality.max_daily_missing_heat_w);
    return batch;
  };

  auto raw = system_observation(0, 2);
  raw.hp2.water_out_c.value = 40.0f;
  assert(evaluate_calorimetry(raw, config.quality).status == SnapshotSourceStatus::HEAT_OUT_OF_RANGE);
  const auto system = advance(raw);
  assert(system.measurement_valid && system.snapshot.heat_to_water_w > config.quality.max_observed_heat_w);
  assert(state.batch_accumulator.active && !state.thermal_accumulator.active);

  raw = system_observation(10, 2);
  raw.hp2.water_out_c.value = 40.0f;
  raw.system_supply_c = {};
  const auto gap = advance(raw);
  assert(gap.status == SnapshotSourceStatus::MISSING_MEASUREMENT && !gap.measurement_valid);
  assert(gap.may_bridge_daily_gap && isnan(gap.snapshot.partial_heat_to_water_w));
  assert(state.batch_accumulator.heat_gap_pending && state.batch_accumulator.gap_heat_bound_floor_w == 50000.0f);

  raw = system_observation(20, 2);
  raw.system_heat_enabled = false;
  const auto recovered = advance(raw);
  const auto& day = state.batch_accumulator;
  assert(recovered.measurement_valid && !day.heat_gap_pending && day.gap_heat_bound_floor_w == 0.0f);
  assert(day.start_epoch_s == system.snapshot.epoch_s);
  const double expected = daily_gap_uncertainty_ws(50000.0, system.snapshot.heat_to_water_w,
                                                   recovered.snapshot.heat_to_water_w, 20000U, 0U, 20000U);
  assert(fabs(day.missing_energy_uncertainty_ws - expected) < 0.01);

  raw = system_observation(30, 2);
  raw.system_heat_enabled = false;
  raw.flow_lph = {};
  assert(advance(raw).may_bridge_daily_gap);
  assert(day.heat_gap_pending && day.gap_heat_bound_floor_w == config.quality.max_daily_missing_heat_w);
}

// A host-side size guard for the pure data only. This is not an ESP32 footprint
// or a substitute for the future strict-PSRAM and internal-heap HIL checks.
static_assert(sizeof(SegmentRecord) * kMaxSegmentRecords + sizeof(AdviceFitWorkspace) + sizeof(SegmentAccumulator) <
              32U * 1024U);
}  // namespace

int main() {
  daily_dhw_only_preserves_collection_and_restart_eligibility();
  daily_system_heat_survives_boiler_modes_and_ot_changes();
  daily_system_heat_only_bridges_missing_measurements();
  daily_without_system_sensor_keeps_hp_measurements();
  daily_idle_without_boiler_heat_counts_zero_beyond_the_gap_limit();
  daily_system_heat_limit_is_inclusive_signed_and_separate_from_hp_measurement();
  daily_boiler_limits_stay_separate_and_preserve_an_open_gap();
  raw_series_observations_to_advice();
  raw_daily_profiles_cross_zero_power_temperature();
  heating_cycles_keep_idle_time_and_pump_runout();
  heating_cycles_keep_idle_time_and_pump_runout(true);
}
