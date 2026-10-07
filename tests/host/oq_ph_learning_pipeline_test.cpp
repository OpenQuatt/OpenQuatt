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

// A host-side size guard for the pure data only. This is not an ESP32 footprint
// or a substitute for the future strict-PSRAM and internal-heap HIL checks.
static_assert(sizeof(SegmentRecord) * kMaxSegmentRecords + sizeof(AdviceFitWorkspace) + sizeof(SegmentAccumulator) <
              32U * 1024U);
}  // namespace

int main() {
  raw_series_observations_to_advice();
  raw_daily_profiles_cross_zero_power_temperature();
  heating_cycles_keep_idle_time_and_pump_runout();
  heating_cycles_keep_idle_time_and_pump_runout(true);
}
