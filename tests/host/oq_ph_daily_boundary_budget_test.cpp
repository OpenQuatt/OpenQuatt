#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <cstring>
#include <initializer_list>

#include "oq_ph_daily_source_fixture.h"

namespace {
using namespace daily_source_fixture;
constexpr float kHeatW = 49000.0f;  // Raw float calorimetry stays below the unchanged 50 kW limit.

LearningSourceInput observation(uint32_t second, bool missing_room = false, bool missing_heat = false) {
  auto raw = source(second, kHeatW);
  raw.room_c.valid = !missing_room;
  raw.flow_lph.valid = !missing_heat;
  const auto input = production_tick(raw);
  if (missing_room || missing_heat) {
    assert(input.may_bridge_daily_gap && input.batch_snapshot.invalid_reasons == INVALID_ESSENTIAL_SOURCE);
    if (missing_heat)
      assert(std::isnan(input.batch_snapshot.partial_heat_to_water_w));
    else
      assert(std::fabs(input.batch_snapshot.partial_heat_to_water_w - kHeatW) < 0.1);
  } else {
    assert(input.batch_source_status == SnapshotSourceStatus::OK);
    assert(validate_snapshot(input.batch_snapshot, QualityConfig{}) == LearningStatus::OK);
    assert(input.batch_snapshot.heat_to_water_w < 50000.0f);
  }
  return raw;
}

bool aggregate_keeps_eligible_old_day() {
  SegmentAccumulator state;
  const auto advance = [&state](const LearningSourceInput& raw) {
    const auto input = production_tick(raw);
    return observe_snapshot(state, input.batch_snapshot, QualityConfig{}, input.may_bridge_daily_gap);
  };
  for (uint32_t second = 0; second < 86380; second += 60) advance(observation(second));
  advance(observation(86380));
  advance(observation(86420, true));
  advance(observation(86460, true));
  assert(state.source_gap_pending && state.integrated_duration_s == 86380);
  const auto result = advance(observation(86500));
  std::printf("aggregate room-gap/retained-heat records=%u status=%s cause=%s new_elapsed=%.0f\n",
              result.has_record ? 1U : 0U, learning_status_name(result.status),
              daily_collection_cause_name(result.cause), state.integrated_duration_s);
  if (!result.has_record) return false;
  assert(!result.discarded_next_day_remainder);
  assert(result.record.start_epoch_s == kEpoch && result.record.end_epoch_s == kEpoch + 86400);
  assert(result.record.duration_s == 86400 && std::fabs(result.record.mean_heat_w - kHeatW) < 0.1);
  assert(result.record.daily_quality.heat_uncertainty_mean_w == 0);
  assert(result.record.daily_quality.heat_coverage_bp == 10000);
  assert(result.record.daily_quality.temperature_coverage_bp == 9997);
  assert(state.start_epoch_s == kEpoch + 86400 && state.integrated_duration_s == 100.0);
  assert(state.missing_energy_uncertainty_ws == 0.0 && !state.source_gap_pending);
  assert(state.heat_observed_ms == 100000 && state.temperature_observed_ms == 0);
  assert(!advance(observation(86560)).has_record && state.integrated_duration_s == 160.0);
  return true;
}

bool runtime_keeps_fixed_days_after_a_temperature_gap() {
  auto state = std::make_unique<PassiveRuntimeStorage>();
  initialize_passive_runtime(*state, production_tick(source(0)).context, PassiveRuntimeConfig{}, true);
  const auto advance = [&](const LearningSourceInput& raw) { tick_passive_runtime(*state, production_tick(raw)); };
  for (uint32_t second = 0; second < 86380; second += 60) advance(observation(second));
  advance(observation(86380));
  advance(observation(86420, true));
  advance(observation(86460, true));
  assert(state->batch_accumulator.integrated_duration_s == 86380 && state->record_count == 0);
  assert(state->batch_accumulator.source_gap_pending);
  advance(observation(86500));
  assert(state->record_count == 1 && state->diagnostics.accepted_batch_records == 1);
  assert(state->daily_diagnostics.bridged_gaps == 1 && state->daily_diagnostics.interrupted_periods == 0);
  assert(state->diagnostics.last_batch_rejection == LearningStatus::OK);
  assert(!passive_runtime_summary(*state, state->last_monotonic_ms).auto_apply_allowed);
  const auto first = state->records[0];
  assert(first.start_epoch_s == kEpoch && first.end_epoch_s == kEpoch + 86400);
  assert(std::fabs(first.mean_heat_w - kHeatW) < 0.1);
  assert(first.daily_quality.heat_uncertainty_mean_w == 0 && first.daily_quality.heat_coverage_bp == 10000);
  assert(first.daily_quality.temperature_coverage_bp == 9997);
  assert(state->batch_accumulator.integrated_duration_s == 100 && state->batch_accumulator.active);
  advance(observation(86560));
  assert(state->record_count == 1 && state->batch_accumulator.integrated_duration_s == 160);
  for (uint32_t second = 86620; second <= 172900; second += 60) advance(observation(second));
  assert(state->record_count == 2 && state->batch_accumulator.integrated_duration_s == 100);
  assert(first.start_epoch_s == state->records[0].start_epoch_s && first.end_epoch_s == state->records[0].end_epoch_s);
  assert(first.mean_heat_w == state->records[0].mean_heat_w);
  assert(first.daily_quality.heat_coverage_bp == state->records[0].daily_quality.heat_coverage_bp &&
         first.daily_quality.temperature_coverage_bp == state->records[0].daily_quality.temperature_coverage_bp &&
         first.daily_quality.heat_uncertainty_mean_w == state->records[0].daily_quality.heat_uncertainty_mean_w);
  assert(state->records[1].start_epoch_s == kEpoch + 86400 && state->records[1].end_epoch_s == kEpoch + 172800);
  assert(state->records[1].daily_quality.heat_uncertainty_mean_w == 0);
  assert(state->records[1].daily_quality.heat_coverage_bp == 10000);
  assert(state->records[1].daily_quality.temperature_coverage_bp == 9988);
  return true;
}

void runtime_and_aggregate_preserve_valid_zero_and_signed_heat_at_day_boundary() {
  for (float heat : {0.0f, -2000.0f}) {
    SegmentAccumulator aggregate;
    auto runtime = std::make_unique<PassiveRuntimeStorage>();
    initialize_passive_runtime(*runtime, production_tick(source(0)).context, PassiveRuntimeConfig{}, true);
    double measured_heat = NAN;
    const auto advance = [&](uint32_t second, bool missing_room = false) {
      auto raw = source(second, heat);
      if (heat < 0) {
        // Daily calorimetry retains actual signed defrost heat; the dynamic
        // model independently excludes these operating intervals.
        raw.hp1.mode.value = raw.hp2.mode.value = HeatPumpMode::COOLING;
        raw.hp1.defrost_active.value = raw.hp2.defrost_active.value = true;
      }
      raw.room_c.valid = !missing_room;
      const auto input = production_tick(raw);
      const double observed_heat =
          missing_room ? input.batch_snapshot.partial_heat_to_water_w : input.batch_snapshot.heat_to_water_w;
      assert(std::isfinite(observed_heat) && std::fabs(observed_heat - heat) < 0.01);
      if (second == 0) measured_heat = observed_heat;
      assert(observed_heat == measured_heat);
      assert(input.may_bridge_daily_gap == missing_room);
      if (missing_room) {
        assert(input.batch_snapshot.invalid_reasons == INVALID_ESSENTIAL_SOURCE);
      } else {
        assert(validate_snapshot(input.batch_snapshot, QualityConfig{}) == LearningStatus::OK);
        if (heat < 0) assert(validate_snapshot(input.dynamic_snapshot, QualityConfig{}) != LearningStatus::OK);
      }
      const auto result =
          observe_snapshot(aggregate, input.batch_snapshot, QualityConfig{}, input.may_bridge_daily_gap);
      tick_passive_runtime(*runtime, input);
      return result;
    };
    for (uint32_t second = 0; second <= 86340; second += 60) assert(!advance(second).has_record);
    assert(!advance(86380).has_record);
    assert(!advance(86420, true).has_record);
    assert(!advance(86460, true).has_record);
    const auto completed = advance(86500);
    assert(completed.has_record && runtime->record_count == 1);
    const auto& record = runtime->records[0];
    assert(record.start_epoch_s == kEpoch && record.end_epoch_s == kEpoch + 86400);
    assert(record.mean_heat_w == completed.record.mean_heat_w && std::fabs(record.mean_heat_w - measured_heat) < 0.001);
    assert(record.daily_quality.heat_uncertainty_mean_w == 0 && record.daily_quality.heat_coverage_bp == 10000);
    assert(record.daily_quality.temperature_coverage_bp == 9997);
    for (size_t i = 0; i < kDailyTemperatureProfileSize; ++i)
      assert(record.effective_outside_profile_centi[i] == completed.record.effective_outside_profile_centi[i]);
    assert(aggregate.start_epoch_s == kEpoch + 86400 && runtime->batch_accumulator.start_epoch_s == kEpoch + 86400);
    assert(aggregate.integrated_duration_s == 100 && runtime->batch_accumulator.integrated_duration_s == 100);
    assert(std::fabs(aggregate.heat_integral - 100.0 * measured_heat) < 0.000001);
    assert(runtime->batch_accumulator.heat_integral == aggregate.heat_integral);
    assert(aggregate.missing_energy_uncertainty_ws == 0 &&
           runtime->batch_accumulator.missing_energy_uncertainty_ws == 0);
    assert(runtime->daily_diagnostics.interrupted_periods == 0 && runtime->daily_diagnostics.bridged_gaps == 1);
  }
}

std::unique_ptr<PassiveRuntimeStorage> collect_runtime(uint32_t last_valid, bool prior_gap = false,
                                                       bool unstable_room = false) {
  auto state = std::make_unique<PassiveRuntimeStorage>();
  initialize_passive_runtime(*state, production_tick(source(0)).context, PassiveRuntimeConfig{}, true);
  const auto advance = [&](uint32_t second) {
    auto raw = source(second, prior_gap && second <= 120 ? 21000.0f : kHeatW);
    if (unstable_room) raw.room_c.value += second * (0.004f / 60.0f);
    if (prior_gap && second == 60) raw.flow_lph.valid = false;
    const auto input = production_tick(raw);
    assert(input.may_bridge_daily_gap ||
           validate_snapshot(input.batch_snapshot, QualityConfig{}) == LearningStatus::OK);
    tick_passive_runtime(*state, input);
    assert(!state->blocked && state->record_count == 0);
  };
  for (uint32_t second = 0; second < last_valid; second += 60) advance(second);
  advance(last_valid);
  if (prior_gap) {
    assert(state->daily_diagnostics.bridged_gaps == 1);
    assert(state->batch_accumulator.missing_energy_uncertainty_ws > 8500000.0);
    assert(state->batch_accumulator.missing_energy_uncertainty_ws < 8600000.0);
  }
  assert(state->batch_accumulator.integrated_duration_s == last_valid);
  return state;
}

PassiveTickInput endpoint_with_gap(PassiveRuntimeStorage& state, uint32_t last_valid, uint32_t endpoint,
                                   bool unstable_room = false, bool missing_heat = false) {
  const uint32_t step = endpoint - last_valid == 120 && last_valid == 86340 ? 60 : 40;
  for (uint32_t second = last_valid + step; second < endpoint; second += step) {
    auto raw = observation(second, true, missing_heat);
    if (unstable_room) raw.room_c.value += second * (0.004f / 60.0f);
    tick_passive_runtime(state, production_tick(raw));
    assert(state.batch_accumulator.source_gap_pending && state.record_count == 0);
  }
  auto raw = observation(endpoint);
  if (unstable_room) raw.room_c.value += endpoint * (0.004f / 60.0f);
  return production_tick(raw);
}

void runtime_retains_heat_uncertainty_and_separates_advice_quality() {
  for (unsigned scenario = 0; scenario < 5; ++scenario) {
    const bool prior_heat_gap = scenario == 1 || scenario == 2;
    const bool missing_heat = scenario < 3;
    const bool unstable_room = scenario == 4;
    const uint32_t last_valid = scenario == 1 || scenario == 3 ? 86340 : 86380;
    const uint32_t endpoint = last_valid + 120;
    auto state = collect_runtime(last_valid, prior_heat_gap, unstable_room);
    const double prior_uncertainty = state->batch_accumulator.missing_energy_uncertainty_ws;
    auto input = endpoint_with_gap(*state, last_valid, endpoint, unstable_room, missing_heat);
    tick_passive_runtime(*state, input);
    const auto& diagnostics = state->daily_diagnostics;
    assert(state->record_count == 1 && state->diagnostics.accepted_batch_records == 1);
    assert(diagnostics.interrupted_periods == 0 && diagnostics.completed_quality_rejections == 0);
    assert(diagnostics.discarded_collected_ms == 0 && diagnostics.rejected_next_day_remainders == 0);
    assert(diagnostics.last_loss.cause == DailyCollectionCause::NONE);
    const auto& record = state->records[0];
    assert(record.start_epoch_s == kEpoch && record.end_epoch_s == kEpoch + 86400);
    assert(validate_segment_record_structure(record, QualityConfig{}) == LearningStatus::OK);
    const double old_gap_charge = missing_heat ? 99000.0 * (86400 - last_valid) : 0.0;
    assert(std::fabs(diagnostics.last_completed_uncertainty_ws - prior_uncertainty - old_gap_charge) < 20.0);
    assert(record.daily_quality.heat_uncertainty_mean_w ==
           static_cast<uint16_t>(std::ceil((prior_uncertainty + old_gap_charge) / 86400.0)));
    const uint32_t missing_heat_s = (prior_heat_gap ? 120 : 0) + (missing_heat ? 86400 - last_valid : 0);
    const uint32_t missing_temperature_s = (prior_heat_gap ? 120 : 0) + 86400 - last_valid;
    assert(record.daily_quality.heat_coverage_bp == (86400 - missing_heat_s) * 10000ULL / 86400);
    assert(record.daily_quality.temperature_coverage_bp == (86400 - missing_temperature_s) * 10000ULL / 86400);
    assert(daily_record_fit_eligible(record, QualityConfig{}) == (!prior_heat_gap && !unstable_room));
    if (prior_heat_gap) assert(record.daily_quality.heat_uncertainty_mean_w > 100);
    if (unstable_room) assert(evaluate_segment_quality(record, QualityConfig{}) == LearningStatus::ROOM_UNSTABLE);
    const auto& accumulator = state->batch_accumulator;
    assert(accumulator.active && !accumulator.source_gap_pending);
    assert(accumulator.start_epoch_s == kEpoch + 86400 && accumulator.integrated_duration_s == endpoint - 86400);
    const double next_charge = missing_heat ? 99000.0 * (endpoint - 86400) : 0.0;
    assert(std::fabs(accumulator.missing_energy_uncertainty_ws - next_charge) < 20.0);
    assert(accumulator.heat_observed_ms == (missing_heat ? 0U : (endpoint - 86400) * 1000U));
    assert(accumulator.temperature_observed_ms == 0);
    assert(!passive_runtime_summary(*state, input.now_monotonic_ms).auto_apply_allowed);
  }
}

void invalid_clock_context_gap_length_and_heat_source_never_release_the_old_day() {
  for (unsigned scenario = 0; scenario < 4; ++scenario) {
    auto state = collect_runtime(86380);
    auto input = endpoint_with_gap(*state, 86380, scenario == 2 ? 86501 : 86500);
    if (scenario == 0) {
      input.now_epoch_s += 10;
      input.batch_snapshot.epoch_s = input.dynamic_snapshot.epoch_s = input.now_epoch_s;
    } else if (scenario == 1) {
      input.context.context_revision = 2;
      input.batch_snapshot.context_revision = input.dynamic_snapshot.context_revision = 2;
    } else if (scenario == 3) {
      auto raw = observation(86500);
      raw.boiler_heat.value = BoilerHeatState::HEAT_ACTIVE;
      input = production_tick(raw);
      assert(!input.may_bridge_daily_gap && input.batch_source_status == SnapshotSourceStatus::BOILER_ACTIVE);
      assert((input.batch_snapshot.invalid_reasons & INVALID_BOILER_HEAT) != 0);
    }
    tick_passive_runtime(*state, input);
    assert(state->record_count == 0 && state->diagnostics.accepted_batch_records == 0);
    assert(state->daily_diagnostics.interrupted_periods == 1);
    assert(state->daily_diagnostics.rejected_next_day_remainders == 0);
    const auto cause = state->daily_diagnostics.last_loss.cause;
    assert(cause == (scenario == 0   ? DailyCollectionCause::CLOCK_DISCONTINUITY
                     : scenario == 1 ? DailyCollectionCause::CONTEXT_CHANGED
                     : scenario == 2 ? DailyCollectionCause::GAP_TOO_LONG
                                     : DailyCollectionCause::INVALID_MEASUREMENT));
    if (scenario == 3) {
      assert(!state->batch_accumulator.active);
      assert(state->daily_diagnostics.last_loss.source_status == SnapshotSourceStatus::BOILER_ACTIVE);
      assert((state->daily_diagnostics.last_loss.invalid_reasons & INVALID_BOILER_HEAT) != 0);
    }
  }
}

SegmentRecord reference_record() {
  auto state = collect_runtime(86380);
  auto input = endpoint_with_gap(*state, 86380, 86500);
  tick_passive_runtime(*state, input);
  assert(state->record_count == 1);
  return state->records[0];
}

void append_refusal_keeps_history_and_remainder_diagnostics_separate() {
  auto state = collect_runtime(86380);
  auto input = endpoint_with_gap(*state, 86380, 86500);
  const auto retained = reference_record();
  state->records[0] = retained;
  state->record_count = 1;
  tick_passive_runtime(*state, input);
  assert(state->record_count == 1 && std::memcmp(&retained, &state->records[0], sizeof(retained)) == 0);
  assert(state->diagnostics.accepted_batch_records == 0 && state->daily_diagnostics.append_rejections == 1);
  assert(state->daily_diagnostics.last_loss.cause == DailyCollectionCause::APPEND_REJECTED);
  assert(state->daily_diagnostics.completed_quality_rejections == 0 &&
         state->daily_diagnostics.interrupted_periods == 0);
  assert(state->daily_diagnostics.rejected_next_day_remainders == 0);
  assert(state->batch_accumulator.integrated_duration_s == 100.0 &&
         state->batch_accumulator.missing_energy_uncertainty_ws == 0.0);
}

void post_boot_boundary_uses_current_boot_endpoint_and_preserves_prior_history() {
  auto collected = collect_runtime(86380);
  auto state = std::make_unique<PassiveRuntimeStorage>();
  initialize_passive_runtime(*state, production_tick(source(0)).context, PassiveRuntimeConfig{}, true);
  state->batch_accumulator = collected->batch_accumulator;
  assert(valid_daily_checkpoint(state->batch_accumulator, QualityConfig{}));
  state->batch_accumulator.restart_pending = true;
  state->batch_accumulator.heat_gap_pending = true;  // Match the v9 journal decoder.
  auto retained = reference_record();
  retained.start_epoch_s -= 86400;
  retained.end_epoch_s -= 86400;
  assert(validate_segment_record(retained, QualityConfig{}) == LearningStatus::OK);
  state->records[0] = retained;
  state->record_count = 1;
  auto input = production_tick(observation(86500));
  input.now_monotonic_ms = input.batch_snapshot.monotonic_ms = input.dynamic_snapshot.monotonic_ms = 5000;
  tick_passive_runtime(*state, input);
  assert(state->record_count == 2 && state->diagnostics.accepted_batch_records == 1);
  assert(std::memcmp(&retained, &state->records[0], sizeof(retained)) == 0);
  assert(state->records[1].start_epoch_s == kEpoch && state->records[1].end_epoch_s == kEpoch + 86400);
  assert(state->daily_diagnostics.rejected_next_day_remainders == 0 &&
         state->daily_diagnostics.interrupted_periods == 0);
  auto& accumulator = state->batch_accumulator;
  assert(accumulator.active && accumulator.start_monotonic_ms == 5000 && accumulator.last_monotonic_ms == 5000);
  assert(accumulator.carried_duration_ms == 100000 && accumulator.integrated_duration_s == 100.0);
  assert(accumulator.start_epoch_s == kEpoch + 86400);
  assert(std::fabs(accumulator.missing_energy_uncertainty_ws - 9900000.0) < 20.0);
  assert(state->records[1].daily_quality.heat_uncertainty_mean_w == 23);
  input = production_tick(observation(86510));
  input.now_monotonic_ms = input.batch_snapshot.monotonic_ms = input.dynamic_snapshot.monotonic_ms = 15000;
  tick_passive_runtime(*state, input);
  assert(state->record_count == 2 && segment_elapsed_ms(accumulator) == 110000);
  // A duplicate observation can never append the completed old day again.
  tick_passive_runtime(*state, input);
  assert(state->record_count == 2 && state->diagnostics.accepted_batch_records == 1);
  assert(std::memcmp(&retained, &state->records[0], sizeof(retained)) == 0);
}
}  // namespace

int main() {
  const bool aggregate_passed = aggregate_keeps_eligible_old_day();
  const bool runtime_passed = runtime_keeps_fixed_days_after_a_temperature_gap();
  runtime_and_aggregate_preserve_valid_zero_and_signed_heat_at_day_boundary();
  if (!aggregate_passed || !runtime_passed) return 1;
  runtime_retains_heat_uncertainty_and_separates_advice_quality();
  invalid_clock_context_gap_length_and_heat_source_never_release_the_old_day();
  append_refusal_keeps_history_and_remainder_diagnostics_separate();
  post_boot_boundary_uses_current_boot_endpoint_and_preserves_prior_history();
  return aggregate_passed && runtime_passed ? 0 : 1;
}
