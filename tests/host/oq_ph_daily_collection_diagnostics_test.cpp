#include <assert.h>
#include <math.h>
#include <string.h>

#include "../../openquatt/includes/control/oq_ph_daily_collection_json.h"
#include "../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

namespace {
using namespace oq_power_house;
using namespace oq_power_house::learning;
constexpr uint32_t kEpoch = 20000U * 86400U;
constexpr uint64_t kStartMs = 1000ULL;
constexpr uint8_t kContext[] = {1, 7, 4, 9, 2, 6};

LearningSnapshot snapshot(uint32_t second, float heat = 5000.0f) {
  LearningSnapshot value;
  value.monotonic_ms = kStartMs + second * 1000ULL;
  value.epoch_s = kEpoch + second;
  value.context_revision = 1;
  value.room_c = value.setpoint_c = 20.0f;
  value.outside_c = 5.0f;
  value.heat_to_water_w = heat;
  value.mean_water_c = 30.0f;
  return value;
}

LearningSnapshot missing(uint32_t second, uint32_t reasons = INVALID_SOURCE_STALE) {
  auto value = snapshot(second);
  value.invalid_reasons = reasons;
  value.room_c = NAN;
  return value;
}

ObserveResult observe(DailyCollectionDiagnostics& diagnostics, SegmentAccumulator& state, const LearningSnapshot& value,
                      bool may_bridge = true, SnapshotSourceStatus source = SnapshotSourceStatus::OK) {
  return observe_daily_with_diagnostics(diagnostics, state, value, QualityConfig{}, may_bridge, source);
}

void test_day_boundary_replay_preserves_records_and_remainder() {
  SegmentAccumulator plain;
  SegmentAccumulator instrumented;
  DailyCollectionDiagnostics diagnostics;
  for (uint32_t second = 0; second <= 86340; second += 60) {
    const auto value = snapshot(second);
    const auto expected = observe_snapshot(plain, value, QualityConfig{}, true);
    const auto actual = observe(diagnostics, instrumented, value);
    assert(expected.status == actual.status && expected.has_record == actual.has_record);
    assert(memcmp(&plain, &instrumented, sizeof(plain)) == 0);
  }
  const auto gap = missing(86400);
  observe_snapshot(plain, gap, QualityConfig{}, true);
  observe(diagnostics, instrumented, gap, true, SnapshotSourceStatus::SOURCE_STALE);
  const auto value = snapshot(86460);
  const auto expected = observe_snapshot(plain, value, QualityConfig{}, true);
  const auto actual = observe(diagnostics, instrumented, value);
  assert(expected.has_record && actual.has_record);
  assert(memcmp(&expected.record, &actual.record, sizeof(expected.record)) == 0);
  assert(memcmp(&plain, &instrumented, sizeof(plain)) == 0);
  assert(diagnostics.interrupted_periods == 0 && diagnostics.completed_quality_rejections == 0);
  assert(diagnostics.bridged_gaps == 1);
  assert(diagnostics.pending_source_status == SnapshotSourceStatus::OK);
  assert(instrumented.integrated_duration_s == 60.0);
  assert(instrumented.missing_energy_uncertainty_ws == 3300000.0);
}

void test_distinguishes_missing_tick_long_source_gap_and_clock() {
  for (unsigned scenario = 0; scenario < 3; ++scenario) {
    SegmentAccumulator state;
    DailyCollectionDiagnostics diagnostics;
    observe(diagnostics, state, snapshot(0));
    observe(diagnostics, state, snapshot(60));
    observe(diagnostics, state, missing(90), true, SnapshotSourceStatus::SOURCE_STALE);
    LearningSnapshot endpoint;
    if (scenario == 0) {
      endpoint = snapshot(160);  // 100s valid-to-valid, but 70s since last observation.
    } else {
      observe(diagnostics, state, missing(140), true, SnapshotSourceStatus::SOURCE_STALE);
      endpoint = snapshot(scenario == 1 ? 181 : 180);
      if (scenario == 2) endpoint.epoch_s += 10;
    }
    const auto result = observe(diagnostics, state, endpoint);
    assert(result.status == LearningStatus::TIME_DISCONTINUITY);
    assert(diagnostics.interrupted_periods == 1 && diagnostics.discarded_collected_ms == 60000);
    assert(diagnostics.last_loss.collected_ms == 60000);
    assert(diagnostics.last_loss.source_status == SnapshotSourceStatus::SOURCE_STALE);
    assert(isnan(diagnostics.last_loss.attempted_day_uncertainty_ws));
    if (scenario == 0) {
      assert(diagnostics.last_loss.cause == DailyCollectionCause::MISSED_TICK);
      assert(diagnostics.last_loss.observed_gap_ms == 100000);
    } else if (scenario == 1) {
      assert(diagnostics.last_loss.cause == DailyCollectionCause::GAP_TOO_LONG);
      assert(diagnostics.last_loss.observed_gap_ms == 121000);
    } else {
      assert(diagnostics.last_loss.cause == DailyCollectionCause::CLOCK_DISCONTINUITY);
      assert(diagnostics.last_loss.observed_gap_ms == kUnknownDailyDurationMs);
    }
  }
}

void test_budget_captures_old_progress_and_both_day_charges() {
  SegmentAccumulator state;
  DailyCollectionDiagnostics diagnostics;
  observe(diagnostics, state, snapshot(0, 22000));
  observe(diagnostics, state, missing(60), true, SnapshotSourceStatus::SOURCE_STALE);
  observe(diagnostics, state, snapshot(120, 22000));
  assert(state.missing_energy_uncertainty_ws == 8640000.0);
  assert(diagnostics.interrupted_periods == 0 && diagnostics.bridged_gaps == 1);
  observe(diagnostics, state, missing(180), true, SnapshotSourceStatus::MISSING_MEASUREMENT);
  const auto result = observe(diagnostics, state, snapshot(240, 22000));
  assert(result.cause == DailyCollectionCause::UNCERTAINTY_BUDGET);
  assert(state.integrated_duration_s == 0.0 && state.missing_energy_uncertainty_ws == 0.0);
  const auto& loss = diagnostics.last_loss;
  assert(loss.cause == DailyCollectionCause::UNCERTAINTY_BUDGET && loss.collected_ms == 120000);
  assert(loss.observed_gap_ms == 120000 && loss.charged_uncertainty_ws == 8640000.0);
  assert(loss.attempted_day_uncertainty_ws == 8640000.0 && loss.attempted_next_day_uncertainty_ws == 0.0);
  assert(loss.source_status == SnapshotSourceStatus::MISSING_MEASUREMENT);

  // Diagnostic observation must preserve the collector's decision regardless
  // of whether independent day/remainder budgeting is installed separately.
  state = {};
  diagnostics = {};
  SegmentAccumulator plain;
  const auto replay = [&](const LearningSnapshot& value, SnapshotSourceStatus source = SnapshotSourceStatus::OK) {
    const auto expected = observe_snapshot(plain, value, QualityConfig{}, true);
    const auto actual = observe(diagnostics, state, value, true, source);
    assert(expected.status == actual.status && expected.has_record == actual.has_record);
    assert(memcmp(&plain, &state, sizeof(state)) == 0);
    if (actual.has_record) assert(memcmp(&expected.record, &actual.record, sizeof(actual.record)) == 0);
    return actual;
  };
  for (uint32_t second = 0; second < 86380; second += 60) replay(snapshot(second, 50000));
  replay(snapshot(86380, 50000));
  replay(missing(86420), SnapshotSourceStatus::SOURCE_STALE);
  replay(missing(86460), SnapshotSourceStatus::SOURCE_STALE);
  const auto boundary = replay(snapshot(86500, 50000));
  if (boundary.has_record) {
    assert(diagnostics.last_loss.cause == DailyCollectionCause::NONE && diagnostics.discarded_collected_ms == 0);
    assert(diagnostics.rejected_next_day_remainders == 1 && diagnostics.interrupted_periods == 0);
    assert(diagnostics.last_remainder_rejection.duration_ms == 100000);
    assert(diagnostics.last_remainder_rejection.uncertainty_ws == 10000000.0);
    assert(diagnostics.last_remainder_rejection.completed_day_uncertainty_ws == 2000000.0);
  } else {
    assert(boundary.cause == DailyCollectionCause::UNCERTAINTY_BUDGET);
    assert(diagnostics.interrupted_periods == 1 && diagnostics.rejected_next_day_remainders == 0);
    assert(diagnostics.last_loss.collected_ms == 86380000 && diagnostics.discarded_collected_ms == 86380000);
  }
  assert(state.start_epoch_s == kEpoch + 86500 && state.integrated_duration_s == 0.0);
  assert(state.missing_energy_uncertainty_ws == 0.0 && !state.source_gap_pending);
}

void test_pending_gap_without_valid_endpoint_has_unknown_attempt() {
  SegmentAccumulator state;
  DailyCollectionDiagnostics diagnostics;
  observe(diagnostics, state, snapshot(0));
  observe(diagnostics, state, snapshot(60));
  observe(diagnostics, state, missing(100), true, SnapshotSourceStatus::SOURCE_STALE);
  auto unsafe = missing(140, INVALID_SOURCE_STALE | INVALID_BOILER_HEAT);
  observe(diagnostics, state, unsafe, true, SnapshotSourceStatus::BOILER_ACTIVE);
  assert(!state.active && diagnostics.interrupted_periods == 1);
  assert(diagnostics.last_loss.cause == DailyCollectionCause::INVALID_MEASUREMENT);
  assert(diagnostics.last_loss.source_status == SnapshotSourceStatus::BOILER_ACTIVE);
  assert(diagnostics.last_loss.invalid_reasons == (INVALID_SOURCE_STALE | INVALID_BOILER_HEAT));
  assert(diagnostics.last_loss.collected_ms == 60000 && diagnostics.last_loss.charged_uncertainty_ws == 0);
  assert(diagnostics.last_loss.observed_gap_ms == 80000);
  assert(isnan(diagnostics.last_loss.attempted_day_uncertainty_ws));
  for (uint32_t second = 180; second <= 300; second += 40)
    observe(diagnostics, state, missing(second), false, SnapshotSourceStatus::SOURCE_STALE);
  assert(diagnostics.interrupted_periods == 1);  // Invalid ticks are not lost days.
}

void test_complete_quality_and_append_rejections_are_separate() {
  SegmentAccumulator state;
  DailyCollectionDiagnostics diagnostics;
  ObserveResult result;
  for (uint32_t minute = 0; minute <= 1440; ++minute) {
    auto value = snapshot(minute * 60U);
    value.room_c += minute * 0.004f;
    result = observe(diagnostics, state, value);
  }
  assert(result.status == LearningStatus::ROOM_UNSTABLE && !result.has_record);
  assert(diagnostics.interrupted_periods == 0 && diagnostics.completed_quality_rejections == 1);
  assert(diagnostics.last_loss.collected_ms == kSegmentDurationMs);
  assert(diagnostics.discarded_collected_ms == kSegmentDurationMs);
  assert(state.active && state.integrated_duration_s == 0.0);
  diagnose_daily_append_rejection(diagnostics, result.record, LearningStatus::TIME_DISCONTINUITY);
  assert(diagnostics.interrupted_periods == 0 && diagnostics.completed_quality_rejections == 1);
  assert(diagnostics.append_rejections == 1 && diagnostics.last_loss.cause == DailyCollectionCause::APPEND_REJECTED);
}

PassiveTickInput tick(uint32_t second) {
  PassiveTickInput input;
  input.context = {kContext, sizeof(kContext), 1};
  input.now_monotonic_ms = kStartMs + second * 1000ULL;
  input.now_epoch_s = kEpoch + second;
  input.opted_in = input.context_valid = input.active_line_valid = true;
  input.active_line = {150.0f, 15.0f};
  input.batch_snapshot_available = true;
  input.batch_snapshot = snapshot(second);
  return input;
}

void test_runtime_lifecycle_counts_once_and_preserves_history() {
  for (unsigned scenario = 0; scenario < 6; ++scenario) {
    PassiveRuntimeStorage state;
    const auto initial = tick(0);
    initialize_passive_runtime(state, initial.context, PassiveRuntimeConfig{}, true);
    for (uint32_t minute = 0; minute <= 1441; ++minute) tick_passive_runtime(state, tick(minute * 60U));
    assert(state.record_count == 1 && state.daily_diagnostics.interrupted_periods == 0);
    const auto retained = state.records[0];
    auto input = tick(86520);
    DailyCollectionCause expected;
    if (scenario == 0) {
      pause_passive_runtime(state, input.now_monotonic_ms);
      expected = DailyCollectionCause::PAUSED;
      input.opted_in = false;
    } else if (scenario == 1) {
      input.opted_in = false;
      expected = DailyCollectionCause::DISABLED;
    } else if (scenario == 2) {
      input.batch_snapshot_available = false;
      expected = DailyCollectionCause::INVALID_INPUT;
    } else if (scenario == 3) {
      input.context_valid = false;
      expected = DailyCollectionCause::INVALID_INPUT;
    } else if (scenario == 4) {
      input.now_epoch_s = 0;
      expected = DailyCollectionCause::CLOCK_DISCONTINUITY;
    } else {
      // The real setting watcher pauses before the next context tick.
      pause_passive_runtime(state, input.now_monotonic_ms, LearningStatus::MIXED_CONTEXT);
      ++input.context.context_revision;
      input.batch_snapshot.context_revision = input.context.context_revision;
      expected = DailyCollectionCause::CONTEXT_CHANGED;
    }
    tick_passive_runtime(state, input);
    assert(state.daily_diagnostics.interrupted_periods == 1);
    assert(state.daily_diagnostics.last_loss.cause == expected);
    assert(state.daily_diagnostics.last_loss.collected_ms == 60000);
    tick_passive_runtime(state, input);
    assert(state.daily_diagnostics.interrupted_periods == 1);
    auto expected_record = retained;
    // A new runtime revision relabels retained evidence with identical context
    // bytes. Its measured values remain unchanged by the pause/watch sequence.
    expected_record.context_revision = state.context_revision;
    assert(state.record_count == 1 && memcmp(&expected_record, &state.records[0], sizeof(retained)) == 0);
    assert(!passive_runtime_summary(state, input.now_monotonic_ms).auto_apply_allowed);
  }
}

void test_runtime_append_refusal_does_not_count_as_quality_or_partial_loss() {
  SegmentAccumulator reference;
  SegmentRecord retained;
  for (uint32_t minute = 0; minute <= 1440; ++minute) {
    const auto event = observe_snapshot(reference, snapshot(minute * 60U), QualityConfig{});
    if (event.has_record) retained = event.record;
  }
  PassiveRuntimeStorage state;
  initialize_passive_runtime(state, tick(0).context, PassiveRuntimeConfig{}, true);
  for (uint32_t minute = 0; minute < 1440; ++minute) tick_passive_runtime(state, tick(minute * 60U));
  // A separately retained record already covers this period. The collector's
  // new record is valid, but the append must refuse an overlapping duplicate.
  state.records[0] = retained;
  state.record_count = 1;
  tick_passive_runtime(state, tick(86400));
  assert(state.record_count == 1 && memcmp(&retained, &state.records[0], sizeof(retained)) == 0);
  assert(state.daily_diagnostics.append_rejections == 1);
  assert(state.daily_diagnostics.completed_quality_rejections == 0 && state.daily_diagnostics.interrupted_periods == 0);
  assert(state.daily_diagnostics.last_loss.cause == DailyCollectionCause::APPEND_REJECTED);
  assert(state.daily_diagnostics.last_loss.collected_ms == kSegmentDurationMs);
}

void test_restart_invalid_checkpoint_never_reports_fabricated_progress() {
  SegmentAccumulator state;
  DailyCollectionDiagnostics diagnostics;
  observe(diagnostics, state, snapshot(0));
  observe(diagnostics, state, snapshot(60));
  state.restart_pending = true;
  state.integrated_duration_s = NAN;
  const auto result = observe(diagnostics, state, snapshot(90));
  assert(result.cause == DailyCollectionCause::RESTART_DISCARDED);
  assert(diagnostics.interrupted_periods == 1 && diagnostics.discarded_collected_ms == 0);
  assert(diagnostics.last_loss.collected_ms == kUnknownDailyDurationMs);
  assert(diagnostics.last_loss.observed_gap_ms == kUnknownDailyDurationMs);
  assert(isnan(diagnostics.last_loss.charged_uncertainty_ws));
  assert(isnan(diagnostics.last_loss.attempted_day_uncertainty_ws));
}

void test_saturating_counters_and_unknown_json() {
  DailyCollectionDiagnostics diagnostics;
  diagnostics.interrupted_periods = UINT32_MAX;
  diagnostics.cause_counts[static_cast<size_t>(DailyCollectionCause::PAUSED)] = UINT32_MAX;
  diagnostics.discarded_collected_ms = UINT64_MAX - 1;
  SegmentAccumulator state;
  observe(diagnostics, state, snapshot(0));
  observe(diagnostics, state, snapshot(60));
  diagnose_daily_discard(diagnostics, state, DailyCollectionCause::PAUSED, LearningStatus::SEGMENT_INELIGIBLE);
  assert(diagnostics.interrupted_periods == UINT32_MAX);
  assert(diagnostics.cause_counts[static_cast<size_t>(DailyCollectionCause::PAUSED)] == UINT32_MAX);
  assert(diagnostics.discarded_collected_ms == UINT64_MAX);
  char buffer[oq_ph_learning::kMaxDailyCollectionJsonBytes];
  oq_ph_learning::JsonWriter json(buffer, sizeof(buffer));
  oq_ph_learning::write_daily_collection_json(json, diagnostics);
  assert(json.ok());
  assert(strstr(buffer, "\"attempted_day_uncertainty_wh\":null"));
  assert(strstr(buffer, "\"observed_gap_ms\":null"));
  assert(strstr(buffer, "active_collection_since_initialize_or_reset"));
}

}  // namespace

int main() {
  test_day_boundary_replay_preserves_records_and_remainder();
  test_distinguishes_missing_tick_long_source_gap_and_clock();
  test_budget_captures_old_progress_and_both_day_charges();
  test_pending_gap_without_valid_endpoint_has_unknown_attempt();
  test_complete_quality_and_append_rejections_are_separate();
  test_runtime_lifecycle_counts_once_and_preserves_history();
  test_runtime_append_refusal_does_not_count_as_quality_or_partial_loss();
  test_restart_invalid_checkpoint_never_reports_fabricated_progress();
  test_saturating_counters_and_unknown_json();
}
