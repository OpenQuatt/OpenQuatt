#include <cassert>
#include <cmath>
#include <cstring>

#define OQ_PH_LEARNING_HOST_TEST 1
#include "../../host/oq_ph_daily_source_fixture.h"

using namespace oq_power_house::learning;
using namespace daily_source_fixture;

int oq_control_mode_code = 0, oq_heat_mode_code = 0;
bool oq_commissioning_active = false, oq_hp_water_calibration_active = false;
struct Switch {
  bool state;
} oq_runtime_polling_paused{false};
#define id(value) value

void production_operation(LearningSourceInput& input) {
  auto& op = input.operation;
#include "production_learning_mode.inc"
#include "production_learning_service.inc"
}

oq_boiler::BoilerCommand withdrawn_command(uint64_t now) {
  return {true, false, false, NAN, NAN, oq_boiler::COMMAND_SOURCE_NONE, static_cast<uint32_t>(now)};
}

LearningSourceInput frost_sample(uint32_t second, float heat_w = -100.0f) {
  auto raw = source(second, heat_w);
  raw.hp1.compressor_active.value = raw.hp2.compressor_active.value = false;
  raw.hp1.mode.value = raw.hp2.mode.value = HeatPumpMode::OFF;
  raw.boiler_heat = learning_no_boiler_heat_contract(true, oq_control_mode_code, withdrawn_command(raw.monotonic_ms),
                                                     false, false, raw.monotonic_ms, false);
  production_operation(raw);
  return raw;
}

void ordinary_frost_is_measurable() {
  for (int cm : {0, 1, 2, 98}) {
    oq_control_mode_code = cm;
    const auto raw = frost_sample(60);
    const auto batch = build_learning_snapshot(raw, QualityConfig{});
    assert(raw.operation.control_mode == LearningControlMode::HEATING && !raw.operation.service_or_ota);
    assert(batch.measurement_valid && std::fabs(batch.snapshot.heat_to_water_w + 100.0f) < .01f);
    assert(daily_restart_interruption_reason(raw, false) == nullptr);
  }
  for (int cm : {-1, 3, 4, 5, 6, 97, 99, 100, 101}) {
    oq_control_mode_code = cm;
    const auto raw = frost_sample(60);
    assert(!build_learning_snapshot(raw, QualityConfig{}).measurement_valid);
    assert(!raw.boiler_heat.valid);
    assert(daily_restart_interruption_reason(raw, false) != nullptr);
  }
}

void frost_preserves_service_cooling_and_boiler_vetoes() {
  oq_control_mode_code = 98;
  for (unsigned failure = 0; failure < 13; ++failure) {
    auto raw = frost_sample(60);
    auto command = withdrawn_command(raw.monotonic_ms);
    if (failure == 0) oq_heat_mode_code = 1;
    if (failure == 1) oq_commissioning_active = true;
    if (failure == 2) oq_hp_water_calibration_active = true;
    if (failure == 3) oq_runtime_polling_paused.state = true;
    if (failure == 4) command.valid = false;
    if (failure == 5) command.updated_at_ms -= 15001U;
    if (failure == 6) command.heat_request = true;
    if (failure == 7) command.demand_present = true;
    if (failure == 8) command.source = oq_boiler::COMMAND_SOURCE_FALLBACK;
    raw.boiler_heat = learning_no_boiler_heat_contract(failure != 9, 98, command, failure == 10, failure == 11,
                                                       raw.monotonic_ms, failure == 12);
    production_operation(raw);
    assert(!build_learning_snapshot(raw, QualityConfig{}).measurement_valid);
    oq_heat_mode_code = 0;
    oq_commissioning_active = oq_hp_water_calibration_active = oq_runtime_polling_paused.state = false;
  }
  auto raw = frost_sample(60);
  const auto contract = raw.boiler_heat;
  raw.boiler_heat = learning_boiler_heat(true, contract, learning_boiler_status(2U, raw.monotonic_ms, true, true));
  assert(!build_learning_snapshot(raw, QualityConfig{}).measurement_valid);
  assert(daily_restart_interruption_reason(raw, true) != nullptr);
  raw = frost_sample(60);
  raw.flow_lph.valid = false;
  assert(!build_learning_snapshot(raw, QualityConfig{}).measurement_valid);
  raw = frost_sample(60);
  raw.hp1.defrost_active.value = true;
  assert(!build_learning_snapshot(raw, QualityConfig{}, SnapshotPurpose::THERMAL_DYNAMIC).measurement_valid);
}

void real_collector_keeps_day_progress_and_signed_frost_heat() {
  PassiveRuntimeStorage state;
  PassiveRuntimeConfig config;
  assert(initialize_passive_runtime(state, {kContext, sizeof(kContext), 1}, config, true) ==
         PassiveRuntimeStatus::COLLECTING);
  SegmentRecord first{};
  uint32_t previous_rls_samples = 0;
  for (uint32_t second = 0; second <= 2U * 86400U; second += 60U) {
    const uint32_t phase = (second / 60U) % 60U;
    oq_control_mode_code = phase < 30 ? 2 : phase < 35 ? 1 : phase < 45 ? 0 : 98;
    const float heat = phase < 30 ? 4000.0f : phase < 35 || phase >= 45 ? -100.0f : 0.0f;
    auto raw = frost_sample(second, heat);
    if (phase < 30) {
      raw.hp1.compressor_active.value = raw.hp2.compressor_active.value = true;
      raw.hp1.mode.value = raw.hp2.mode.value = HeatPumpMode::HEATING;
    }
    const auto tick = production_tick(raw);
    assert(build_learning_snapshot(raw, config.quality).measurement_valid);
    assert(daily_restart_interruption_reason(raw, false) == nullptr);
    tick_passive_runtime(state, tick);
    assert(state.batch_accumulator.active && !state.batch_accumulator.source_gap_pending);
    assert(state.batch_accumulator.start_epoch_s == kEpoch + second / 86400U * 86400U);
    assert(state.batch_accumulator.last_epoch_s == kEpoch + second);
    assert(state.batch_accumulator.missing_energy_uncertainty_ws == 0.0);
    assert(state.diagnostics.rejected_batch_observations == 0);
    assert(state.thermal_state.accepted_samples >= previous_rls_samples);
    previous_rls_samples = state.thermal_state.accepted_samples;
    assert(!passive_runtime_summary(state, raw.monotonic_ms).auto_apply_allowed);
    if (second == 86400U) std::memcpy(&first, &state.records[0], sizeof(first));
    if (second > 86400U) assert(std::memcmp(&first, &state.records[0], sizeof(first)) == 0);
  }
  assert(state.record_count == 2);
  assert(previous_rls_samples > 0);
  for (size_t i = 0; i < 2; ++i) {
    const auto& record = state.records[i];
    assert(record.duration_s == 86400U && record.daily_quality.temperature_coverage_bp == 10000U);
    assert(record.daily_quality.heat_coverage_bp == 10000U && record.daily_quality.heat_uncertainty_mean_w == 0);
    assert(std::fabs(record.mean_heat_w - (4000.0f * 30.0f - 100.0f * 20.0f) / 60.0f) < .1f);
  }
}

int main() {
  ordinary_frost_is_measurable();
  frost_preserves_service_cooling_and_boiler_vetoes();
  real_collector_keeps_day_progress_and_signed_frost_heat();
}
