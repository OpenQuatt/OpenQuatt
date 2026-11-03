#include <assert.h>
#include <math.h>
#include <string.h>
#include <initializer_list>

#include "../../openquatt/includes/control/oq_ph_daily_collection_json.h"
#include "../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

namespace {
using namespace oq_power_house;
using namespace oq_power_house::learning;
constexpr uint32_t kEpoch = 20000U * 86400U;
constexpr uint64_t kStartMs = 1000ULL;
constexpr uint8_t kContext[] = {1, 7, 4, 9, 2, 6};

template <typename Value>
void assert_value_equal(Value a, Value b) {
  assert(a == b || (isnan(static_cast<double>(a)) && isnan(static_cast<double>(b))));
}

template <typename State, typename Value>
void assert_fields_equal(const State& a, const State& b, std::initializer_list<Value State::*> fields) {
  for (const auto field : fields) assert_value_equal(a.*field, b.*field);
}

// Padding bytes are not state. Compare every field, including NaN sentinels.
void assert_same(const SegmentAccumulator& a, const SegmentAccumulator& b) {
  using S = SegmentAccumulator;
  assert_fields_equal(
      a, b, {&S::active, &S::source_gap_pending, &S::restart_pending, &S::heat_gap_pending, &S::daily_quality_known});
  assert(a.restart_policy == b.restart_policy);
  assert_fields_equal(
      a, b, {&S::carried_duration_ms, &S::last_gap_observation_ms, &S::start_monotonic_ms, &S::last_monotonic_ms});
  assert_fields_equal(a, b, {&S::start_epoch_s, &S::last_epoch_s, &S::context_revision});
  assert_fields_equal(a, b,
                      {&S::heat_last_elapsed_ms, &S::heat_last_epoch_s, &S::heat_observed_ms,
                       &S::temperature_observed_ms, &S::next_heat_observed_ms});
  assert_fields_equal(a, b,
                      {&S::gap_heat_bound_floor_w, &S::heat_last_w, &S::room_start_c, &S::last_room_c,
                       &S::last_setpoint_c, &S::last_outside_c, &S::last_heat_w, &S::water_start_c, &S::water_end_c,
                       &S::room_min_c, &S::room_max_c, &S::setpoint_min_c, &S::setpoint_max_c});
  assert_fields_equal(
      a, b,
      {&S::missing_energy_uncertainty_ws, &S::next_heat_integral_ws, &S::next_heat_uncertainty_ws,
       &S::integrated_duration_s, &S::room_integral, &S::setpoint_integral, &S::outside_integral, &S::heat_integral,
       &S::hour_effective_integral, &S::trend_w, &S::trend_wt, &S::trend_wtt, &S::trend_wr, &S::trend_wtr});
  for (size_t i = 0; i < 24; ++i) assert_value_equal(a.hourly_effective_outside_c[i], b.hourly_effective_outside_c[i]);
}

void assert_same(const SegmentRecord& a, const SegmentRecord& b) {
  using S = SegmentRecord;
  assert_fields_equal(a, b, {&S::start_epoch_s, &S::end_epoch_s, &S::duration_s, &S::context_revision});
  assert_fields_equal(
      a, b,
      {&S::mean_room_c, &S::mean_setpoint_c, &S::mean_outside_c, &S::mean_heat_w, &S::room_trend_k_per_h,
       &S::room_range_k, &S::setpoint_range_c, &S::water_start_c, &S::water_end_c});
  assert(a.daily_quality.heat_coverage_bp == b.daily_quality.heat_coverage_bp);
  assert(a.daily_quality.temperature_coverage_bp == b.daily_quality.temperature_coverage_bp);
  assert(a.daily_quality.heat_uncertainty_mean_w == b.daily_quality.heat_uncertainty_mean_w);
  for (size_t i = 0; i < kDailyTemperatureProfileSize; ++i)
    assert_value_equal(a.effective_outside_profile_centi[i], b.effective_outside_profile_centi[i]);
}

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
    assert_same(plain, instrumented);
  }
  const auto gap = missing(86400);
  observe_snapshot(plain, gap, QualityConfig{}, true);
  observe(diagnostics, instrumented, gap, true, SnapshotSourceStatus::SOURCE_STALE);
  const auto value = snapshot(86460);
  const auto expected = observe_snapshot(plain, value, QualityConfig{}, true);
  const auto actual = observe(diagnostics, instrumented, value);
  assert(expected.has_record && actual.has_record);
  assert_same(expected.record, actual.record);
  assert_same(plain, instrumented);
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

void test_retention_keeps_old_progress_and_both_day_charges() {
  SegmentAccumulator state;
  DailyCollectionDiagnostics diagnostics;
  observe(diagnostics, state, snapshot(0, 22000));
  observe(diagnostics, state, missing(60), true, SnapshotSourceStatus::SOURCE_STALE);
  observe(diagnostics, state, snapshot(120, 22000));
  assert(state.missing_energy_uncertainty_ws == 8640000.0);
  assert(diagnostics.interrupted_periods == 0 && diagnostics.bridged_gaps == 1);
  observe(diagnostics, state, missing(180), true, SnapshotSourceStatus::MISSING_MEASUREMENT);
  const auto result = observe(diagnostics, state, snapshot(240, 22000));
  assert(result.cause == DailyCollectionCause::NONE && !result.has_record);
  assert(state.start_epoch_s == kEpoch && state.integrated_duration_s == 240.0);
  assert(state.missing_energy_uncertainty_ws == 17280000.0);
  assert(diagnostics.interrupted_periods == 0 && diagnostics.bridged_gaps == 2);
  assert(diagnostics.last_loss.cause == DailyCollectionCause::NONE && diagnostics.discarded_collected_ms == 0);
  ObserveResult complete;
  for (uint32_t second = 300; second <= 86400; second += 60)
    complete = observe(diagnostics, state, snapshot(second, 22000));
  assert(complete.has_record && diagnostics.last_completed_uncertainty_ws == 17280000.0);
  assert(complete.record.daily_quality.heat_uncertainty_mean_w == 200);
  assert(complete.record.daily_quality.heat_coverage_bp == 9972);
  assert(complete.record.daily_quality.temperature_coverage_bp == 9972);
  assert(!daily_record_fit_eligible(complete.record, QualityConfig{}));
  assert(diagnostics.completed_quality_rejections == 0 && diagnostics.interrupted_periods == 0);

  // Diagnostics preserve the collector's day-boundary split without treating
  // either day's uncertainty as an acquisition discard.
  state = {};
  diagnostics = {};
  SegmentAccumulator plain;
  const auto replay = [&](const LearningSnapshot& value, SnapshotSourceStatus source = SnapshotSourceStatus::OK) {
    const auto expected = observe_snapshot(plain, value, QualityConfig{}, true);
    const auto actual = observe(diagnostics, state, value, true, source);
    assert(expected.status == actual.status && expected.has_record == actual.has_record);
    assert_same(plain, state);
    if (actual.has_record) assert_same(expected.record, actual.record);
    return actual;
  };
  for (uint32_t second = 0; second < 86380; second += 60) replay(snapshot(second, 50000));
  replay(snapshot(86380, 50000));
  replay(missing(86420), SnapshotSourceStatus::SOURCE_STALE);
  replay(missing(86460), SnapshotSourceStatus::SOURCE_STALE);
  const auto boundary = replay(snapshot(86500, 50000));
  assert(boundary.has_record && boundary.cause == DailyCollectionCause::NONE);
  assert(!boundary.discarded_next_day_remainder);
  assert(diagnostics.last_loss.cause == DailyCollectionCause::NONE && diagnostics.discarded_collected_ms == 0);
  assert(diagnostics.rejected_next_day_remainders == 0 && diagnostics.interrupted_periods == 0);
  assert(diagnostics.last_completed_uncertainty_ws == 2000000.0);
  assert(boundary.record.daily_quality.heat_uncertainty_mean_w == 24);
  assert(boundary.record.daily_quality.heat_coverage_bp == 9997);
  assert(boundary.record.daily_quality.temperature_coverage_bp == 9997);
  assert(state.start_epoch_s == kEpoch + 86400 && state.integrated_duration_s == 100.0);
  assert(state.missing_energy_uncertainty_ws == 10000000.0 && !state.source_gap_pending);
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

void test_retained_room_trend_and_append_rejections_are_separate() {
  SegmentAccumulator state;
  DailyCollectionDiagnostics diagnostics;
  ObserveResult result;
  for (uint32_t minute = 0; minute <= 1440; ++minute) {
    auto value = snapshot(minute * 60U);
    value.room_c += minute * 0.004f;
    result = observe(diagnostics, state, value);
  }
  assert(result.status == LearningStatus::SEGMENT_READY && result.has_record);
  assert(validate_segment_record_structure(result.record, QualityConfig{}) == LearningStatus::OK);
  assert(evaluate_segment_quality(result.record, QualityConfig{}) == LearningStatus::ROOM_UNSTABLE);
  assert(!daily_record_fit_eligible(result.record, QualityConfig{}));
  assert(result.record.daily_quality.heat_coverage_bp == 10000);
  assert(result.record.daily_quality.temperature_coverage_bp == 10000);
  assert(result.record.daily_quality.heat_uncertainty_mean_w == 0);
  assert(diagnostics.interrupted_periods == 0 && diagnostics.completed_quality_rejections == 0);
  assert(diagnostics.last_loss.cause == DailyCollectionCause::NONE && diagnostics.discarded_collected_ms == 0);
  assert(state.active && state.integrated_duration_s == 0.0);
  diagnose_daily_append_rejection(diagnostics, result.record, LearningStatus::TIME_DISCONTINUITY);
  assert(diagnostics.interrupted_periods == 0 && diagnostics.completed_quality_rejections == 0);
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
    assert(state.record_count == 1);
    assert_same(expected_record, state.records[0]);
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
  assert(state.record_count == 1);
  assert_same(retained, state.records[0]);
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

void test_signed_interpolation_splits_the_bound_at_the_actual_day_boundary() {
  for (const bool restart : {false, true}) {
    SegmentAccumulator state;
    DailyCollectionDiagnostics diagnostics;
    for (uint32_t t = 0; t <= 86340; t += 60) observe(diagnostics, state, snapshot(t, -4000));
    observe(diagnostics, state, snapshot(86360, -4000));
    observe(diagnostics, state, missing(86400));
    observe(diagnostics, state, missing(86440));
    auto endpoint = snapshot(86480, 12000);
    if (restart) {
      state.restart_pending = true;
      state.heat_gap_pending = true;
      endpoint.monotonic_ms = 5000;
    }
    const auto result = observe(diagnostics, state, endpoint);
    assert(result.has_record && result.record.duration_s == 86400);
    // Zero is30s into the120s gap. Old day includes30s negative +10s positive;
    // the next day contains the last80s of the same line, not a new max endpoint.
    const double old_day = 50000.0 * 40.0 + 4000.0 * 30.0 / 2.0 + (4000.0 / 3.0) * 10.0 / 2.0;
    const double new_day = 50000.0 * 80.0 + (4000.0 / 3.0 + 12000.0) * 80.0 / 2.0;
    assert(fabs(diagnostics.last_completed_uncertainty_ws - old_day) < 0.000001);
    assert(fabs(state.missing_energy_uncertainty_ws - new_day) < 0.000001);
    assert(fabs(old_day + new_day - 6600000.0) < 0.000001);
    assert(state.start_epoch_s == kEpoch + 86400 && state.integrated_duration_s == 80.0);
    assert(diagnostics.bridged_gaps == 1 && diagnostics.rejected_next_day_remainders == 0);
  }
}

}  // namespace

int main() {
  test_day_boundary_replay_preserves_records_and_remainder();
  test_distinguishes_missing_tick_long_source_gap_and_clock();
  test_retention_keeps_old_progress_and_both_day_charges();
  test_pending_gap_without_valid_endpoint_has_unknown_attempt();
  test_retained_room_trend_and_append_rejections_are_separate();
  test_runtime_lifecycle_counts_once_and_preserves_history();
  test_runtime_append_refusal_does_not_count_as_quality_or_partial_loss();
  test_restart_invalid_checkpoint_never_reports_fabricated_progress();
  test_saturating_counters_and_unknown_json();
  test_signed_interpolation_splits_the_bound_at_the_actual_day_boundary();
}
