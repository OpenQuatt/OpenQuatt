#include <assert.h>
#include <math.h>
#include <initializer_list>

#include "../../openquatt/includes/learning/oq_ph_learning_aggregate.h"

namespace {
using namespace oq_power_house::learning;
constexpr uint32_t kEpoch = 20000U * 86400U;
constexpr uint64_t kStartMs = 1000ULL;

LearningSnapshot snapshot(uint32_t second, float heat_w = 5000.0f) {
  LearningSnapshot value;
  value.monotonic_ms = kStartMs + second * 1000ULL;
  value.epoch_s = kEpoch + second;
  value.context_revision = 1;
  value.room_c = value.setpoint_c = 20.0f;
  value.outside_c = 5.0f;
  value.heat_to_water_w = heat_w;
  value.mean_water_c = 30.0f;
  return value;
}

LearningSnapshot missing(uint32_t second, uint32_t reason = INVALID_ESSENTIAL_SOURCE) {
  auto value = snapshot(second);
  value.invalid_reasons = reason;
  value.room_c = value.setpoint_c = value.outside_c = value.heat_to_water_w = value.mean_water_c = NAN;
  return value;
}

void assert_seeded_at(const SegmentAccumulator& state, uint32_t second) {
  assert(state.active && !state.source_gap_pending);
  assert(state.start_monotonic_ms == kStartMs + second * 1000ULL);
  assert(state.last_monotonic_ms == state.start_monotonic_ms);
  assert(state.start_epoch_s == kEpoch + second && state.last_epoch_s == state.start_epoch_s);
  assert(state.integrated_duration_s == 0.0 && state.missing_energy_uncertainty_ws == 0.0);
}

void test_explicit_120_second_gap_keeps_signed_energy() {
  for (float start_heat : {-5000.0f, 5000.0f}) {
    const float end_heat = -start_heat;
    QualityConfig quality;
    SegmentAccumulator state;
    observe_snapshot(state, snapshot(0, start_heat), quality);
    for (uint32_t second : {30U, 60U, 90U}) {
      const auto event = observe_snapshot(state, missing(second, INVALID_SOURCE_STALE), quality, true);
      assert(!event.has_record && state.active && state.source_gap_pending);
      assert(state.last_monotonic_ms == kStartMs && state.last_epoch_s == kEpoch);
      assert(state.last_heat_w == start_heat && state.last_room_c == 20.0f);
      assert(state.integrated_duration_s == 0.0 && state.heat_integral == 0.0);
      assert(state.missing_energy_uncertainty_ws == 0.0);  // Charged once on the valid endpoint.
    }
    const auto resumed = observe_snapshot(state, snapshot(120, end_heat), quality, true);
    assert(!resumed.has_record && state.active && !state.source_gap_pending);
    assert(state.start_monotonic_ms == kStartMs && state.integrated_duration_s == 120.0);
    assert(fabs(state.heat_integral) < 0.01);  // Signed trapezoid, including the negative endpoint.
    assert(fabs(state.missing_energy_uncertainty_ws - (50000.0 + 5000.0) * 120.0) < 0.01);
    assert(state.last_heat_w == end_heat && state.last_epoch_s == kEpoch + 120U);
  }
}

void test_entire_valid_to_valid_interval_limits_the_gap() {
  QualityConfig quality;
  SegmentAccumulator state;
  observe_snapshot(state, snapshot(0), quality);
  observe_snapshot(state, missing(40), quality, true);
  observe_snapshot(state, missing(80), quality, true);
  // Every observation is within 60 s; valid-to-valid is 121 s and must fail closed.
  const auto resumed = observe_snapshot(state, snapshot(121), quality, true);
  assert(!resumed.has_record);
  assert_seeded_at(state, 121);

  observe_snapshot(state, missing(181), quality, true);
  const auto too_long = observe_snapshot(state, missing(242), quality, true);
  assert(!too_long.has_record && !state.active && !state.source_gap_pending);
  assert(state.missing_energy_uncertainty_ws == 0.0);
}

void test_unflagged_invalid_and_omitted_ticks_are_not_bridged() {
  QualityConfig quality;
  SegmentAccumulator state;
  observe_snapshot(state, snapshot(0), quality);
  assert(!observe_snapshot(state, missing(60), quality).has_record);
  assert(!state.active && !state.source_gap_pending);
  observe_snapshot(state, snapshot(120), quality);
  assert_seeded_at(state, 120);

  reset_segment(state);
  observe_snapshot(state, snapshot(0), quality);
  const auto omitted = observe_snapshot(state, snapshot(120), quality, true);
  assert(omitted.status == LearningStatus::TIME_DISCONTINUITY && !omitted.has_record);
  assert_seeded_at(state, 120);
}

void test_missing_data_cannot_seed_a_day() {
  SegmentAccumulator state;
  for (uint32_t reason :
       {static_cast<uint32_t>(INVALID_ESSENTIAL_SOURCE), static_cast<uint32_t>(INVALID_SOURCE_STALE), 0U}) {
    reset_segment(state);
    const auto event = observe_snapshot(state, missing(60, reason), QualityConfig{}, true);
    assert(!event.has_record && !state.active && !state.source_gap_pending);
    assert(state.last_monotonic_ms == 0 && state.integrated_duration_s == 0.0);
    assert(state.missing_energy_uncertainty_ws == 0.0);
  }
}

void test_nan_sources_need_the_explicit_contract() {
  SegmentAccumulator state;
  QualityConfig quality;
  observe_snapshot(state, snapshot(0), quality);
  observe_snapshot(state, missing(60, INVALID_ESSENTIAL_SOURCE), quality, true);
  assert(state.active && state.source_gap_pending);
  observe_snapshot(state, snapshot(120), quality, true);
  assert(state.start_epoch_s == kEpoch && state.integrated_duration_s == 120.0);
  assert(fabs(state.missing_energy_uncertainty_ws - 6600000.0) < 0.01);
}

void test_nan_without_explicit_missing_source_reason_fails_closed() {
  SegmentAccumulator state;
  QualityConfig quality;
  observe_snapshot(state, snapshot(0), quality);
  observe_snapshot(state, missing(60, INVALID_NONE), quality, true);
  assert(!state.active && !state.source_gap_pending);
  assert(state.missing_energy_uncertainty_ws == 0.0);
}

void test_explicit_gap_still_requires_every_observation_tick() {
  SegmentAccumulator state;
  QualityConfig quality;
  observe_snapshot(state, snapshot(0), quality);
  observe_snapshot(state, missing(30), quality, true);
  // Valid-to-valid is only 100 s, but the 70 s interval since the explicit invalid tick is unsafe.
  const auto event = observe_snapshot(state, snapshot(100), quality, true);
  assert(event.status == LearningStatus::TIME_DISCONTINUITY && !event.has_record);
  assert_seeded_at(state, 100);
}

void test_hold_permission_must_remain_valid_for_each_missing_tick() {
  SegmentAccumulator state;
  QualityConfig quality;
  observe_snapshot(state, snapshot(0), quality);
  observe_snapshot(state, missing(30), quality, true);
  assert(state.active && state.source_gap_pending);
  observe_snapshot(state, missing(60), quality, false);
  assert(!state.active && !state.source_gap_pending);
  observe_snapshot(state, snapshot(90), quality);
  assert_seeded_at(state, 90);
}

void test_valid_endpoint_context_or_utc_change_cannot_close_a_gap() {
  SegmentAccumulator state;
  QualityConfig quality;
  observe_snapshot(state, snapshot(0), quality);
  observe_snapshot(state, missing(60), quality, true);
  auto changed = snapshot(120);
  changed.context_revision = 2;
  const auto mixed = observe_snapshot(state, changed, quality, true);
  assert(mixed.status == LearningStatus::MIXED_CONTEXT && !mixed.has_record);
  assert_seeded_at(state, 120);
  assert(state.context_revision == 2);

  reset_segment(state);
  observe_snapshot(state, snapshot(0), quality);
  observe_snapshot(state, missing(60), quality, true);
  auto jumped = snapshot(120);
  jumped.epoch_s += 10;
  const auto discontinuous = observe_snapshot(state, jumped, quality, true);
  assert(discontinuous.status == LearningStatus::TIME_DISCONTINUITY && !discontinuous.has_record);
  assert(state.active && state.start_monotonic_ms == jumped.monotonic_ms);
  assert(!state.source_gap_pending && state.integrated_duration_s == 0.0);
  assert(state.missing_energy_uncertainty_ws == 0.0);
}

void test_daily_heat_budget_is_inclusive_and_uses_configuration() {
  SegmentAccumulator state;
  QualityConfig quality;
  observe_snapshot(state, snapshot(0, 22000.0f), quality);
  observe_snapshot(state, missing(60), quality, true);
  observe_snapshot(state, snapshot(120, 22000.0f), quality, true);
  assert(state.start_epoch_s == kEpoch && state.integrated_duration_s == 120.0);
  assert(state.missing_energy_uncertainty_ws == 8640000.0);
  observe_snapshot(state, snapshot(180, 22000.0f), quality);
  assert(state.missing_energy_uncertainty_ws == 8640000.0);
  observe_snapshot(state, missing(240), quality, true);
  assert(!observe_snapshot(state, snapshot(300, 22000.0f), quality, true).has_record);
  assert_seeded_at(state, 300);

  quality.max_abs_heat_w = 10000.0f;
  reset_segment(state);
  observe_snapshot(state, snapshot(0, 5000.0f), quality);
  observe_snapshot(state, missing(60), quality, true);
  observe_snapshot(state, snapshot(120, -5000.0f), quality, true);
  assert(state.start_epoch_s == kEpoch && state.integrated_duration_s == 120.0);
  assert(state.missing_energy_uncertainty_ws == 1800000.0);
}

void test_repeated_gaps_exhaust_the_daily_budget() {
  SegmentAccumulator state;
  QualityConfig quality;
  observe_snapshot(state, snapshot(0), quality);
  observe_snapshot(state, missing(60), quality, true);
  observe_snapshot(state, snapshot(120), quality, true);
  assert(fabs(state.missing_energy_uncertainty_ws - 6600000.0) < 0.01);
  observe_snapshot(state, missing(180), quality, true);
  const auto over_budget = observe_snapshot(state, snapshot(240), quality, true);
  assert(!over_budget.has_record);
  assert_seeded_at(state, 240);
}

void test_uncertainty_uses_the_maximum_absolute_endpoint() {
  SegmentAccumulator state;
  QualityConfig quality;
  observe_snapshot(state, snapshot(0, -1000.0f), quality);
  observe_snapshot(state, missing(60), quality, true);
  observe_snapshot(state, snapshot(120, 4000.0f), quality, true);
  assert(fabs(state.missing_energy_uncertainty_ws - (50000.0 + 4000.0) * 120.0) < 0.01);
  assert(fabs(state.heat_integral - 180000.0) < 0.01);
}

void test_day_boundary_splits_energy_and_uncertainty_budget() {
  SegmentAccumulator state;
  QualityConfig quality;
  observe_snapshot(state, snapshot(0, 0.0f), quality);
  observe_snapshot(state, missing(50), quality, true);
  observe_snapshot(state, snapshot(100, 0.0f), quality, true);
  assert(fabs(state.missing_energy_uncertainty_ws - 5000000.0) < 0.01);
  for (uint32_t second = 160; second < 86340U; second += 60U) observe_snapshot(state, snapshot(second), quality);
  observe_snapshot(state, snapshot(86340), quality);
  observe_snapshot(state, missing(86400), quality, true);
  const auto closed = observe_snapshot(state, snapshot(86460), quality, true);
  // Old day: 5.0M + 3.3M Ws < 8.64M. Charging all 6.6M before splitting would reject it.
  assert(closed.has_record && closed.record.duration_s == 86400U);
  assert(closed.record.start_epoch_s == kEpoch && closed.record.end_epoch_s == kEpoch + 86400U);
  const double expected_old_energy = 2500.0 * 60.0 + 5000.0 * (86400.0 - 160.0);
  assert(fabs(closed.record.mean_heat_w * 86400.0 - expected_old_energy) < 20.0);
  assert(state.start_epoch_s == kEpoch + 86400U && state.integrated_duration_s == 60.0);
  assert(!state.source_gap_pending && fabs(state.heat_integral - 300000.0) < 0.01);
  assert(fabs(state.missing_energy_uncertainty_ws - 3300000.0) < 0.01);
  observe_snapshot(state, missing(86520), quality, true);
  const auto next_over_budget = observe_snapshot(state, snapshot(86580), quality, true);
  assert(!next_over_budget.has_record);  // New day: 3.3M + 6.6M Ws exceeds its independent budget.
  assert_seeded_at(state, 86580);
}

void test_day_boundary_assesses_old_day_and_new_remainder_independently() {
  struct Case {
    uint32_t last_valid;
    float heat_w;
    bool prior_gap, unstable, record, fresh_start;
  };
  for (const auto test : {Case{86380, 49000, false, false, true, true},      // Only new day over budget.
                          Case{86340, 49000, false, false, true, false},     // Both within budget.
                          Case{86380, 49000, true, false, false, true},      // Both over budget.
                          Case{86340, 49000, true, false, false, true},      // Only old day over budget.
                          Case{86380, 49000, false, true, false, true},      // Old day fails quality.
                          Case{86370, 46000, false, false, true, false}}) {  // New budget exactly full.
    for (const bool restart : {false, true}) {
      SegmentAccumulator state;
      const QualityConfig quality;
      const auto sample = [&](uint32_t second) {
        auto value = snapshot(second, test.heat_w);
        if (test.unstable) value.room_c += second * (0.004f / 60.0f);
        return value;
      };
      if (test.prior_gap) {
        observe_snapshot(state, snapshot(0, 21000), quality);
        observe_snapshot(state, missing(60), quality, true);
        observe_snapshot(state, snapshot(120, 21000), quality, true);
        assert(state.missing_energy_uncertainty_ws == 8520000.0);
      }
      for (uint32_t second = test.prior_gap ? 180 : 0; second < test.last_valid; second += 60)
        observe_snapshot(state, sample(second), quality);
      observe_snapshot(state, sample(test.last_valid), quality);
      const uint32_t endpoint = test.last_valid + 120;
      for (uint32_t second = test.last_valid + 40; second < endpoint; second += 40)
        observe_snapshot(state, missing(second), quality, true);
      auto value = sample(endpoint);
      if (restart) {
        state.restart_pending = true;
        value.monotonic_ms = 5000;
      }
      const auto result = observe_snapshot(state, value, quality, true);
      assert(result.has_record == test.record);
      if (test.record) {
        assert(result.record.start_epoch_s == kEpoch && result.record.end_epoch_s == kEpoch + 86400);
        assert(result.record.duration_s == 86400);
      } else {
        assert(result.status == (test.unstable ? LearningStatus::ROOM_UNSTABLE : LearningStatus::SEGMENT_INELIGIBLE));
      }
      assert(state.active && !state.source_gap_pending && state.last_monotonic_ms == value.monotonic_ms);
      assert(state.start_epoch_s == kEpoch + (test.fresh_start ? endpoint : 86400));
      assert(segment_elapsed_ms(state) == (test.fresh_start ? 0 : (endpoint - 86400) * 1000ULL));
      const double uncertainty = test.fresh_start ? 0 : (50000.0 + test.heat_w) * (endpoint - 86400);
      assert(state.missing_energy_uncertainty_ws == uncertainty);
      value.epoch_s += 10;
      value.monotonic_ms += 10000;
      assert(!observe_snapshot(state, value, quality).has_record);  // No duplicate completed day.
    }
  }
}

void test_hard_faults_and_context_or_clock_changes_reset() {
  QualityConfig quality;
  for (uint32_t reason : {static_cast<uint32_t>(INVALID_COOLING), static_cast<uint32_t>(INVALID_BOILER_HEAT),
                          static_cast<uint32_t>(INVALID_CONTROL_MODE), static_cast<uint32_t>(INVALID_SERVICE_OR_OTA)}) {
    SegmentAccumulator state;
    observe_snapshot(state, snapshot(0), quality);
    auto bad = missing(60, reason | INVALID_SOURCE_STALE);
    assert(!observe_snapshot(state, bad, quality, true).has_record);
    assert(!state.active && !state.source_gap_pending && state.missing_energy_uncertainty_ws == 0.0);
  }
  SegmentAccumulator state;
  observe_snapshot(state, snapshot(0), quality);
  auto changed = missing(60);
  changed.context_revision = 2;
  assert(!observe_snapshot(state, changed, quality, true).has_record);
  assert(!state.active && !state.source_gap_pending);

  observe_snapshot(state, snapshot(120), quality);
  auto jumped = missing(180);
  jumped.epoch_s += 10;
  assert(!observe_snapshot(state, jumped, quality, true).has_record);
  assert(!state.active && !state.source_gap_pending);

  observe_snapshot(state, snapshot(240), quality);
  auto unstamped = missing(300);
  unstamped.monotonic_ms = 0;
  assert(!observe_snapshot(state, unstamped, quality, true).has_record);
  assert(!state.active && !state.source_gap_pending);
}

void test_stop_transition_requires_the_explicit_unmixed_contract() {
  SegmentAccumulator state;
  QualityConfig quality;
  observe_snapshot(state, snapshot(0), quality);
  observe_snapshot(state, missing(60, INVALID_CONTROL_MODE), quality, true);
  assert(state.active && state.source_gap_pending && state.integrated_duration_s == 0.0);
  observe_snapshot(state, snapshot(120), quality, true);
  assert(state.active && !state.source_gap_pending && state.start_epoch_s == kEpoch);
  assert(state.integrated_duration_s == 120.0 && state.missing_energy_uncertainty_ws > 0.0);

  observe_snapshot(state, missing(180, INVALID_CONTROL_MODE), quality);
  assert(!state.active && !state.source_gap_pending);
}
}  // namespace

int main() {
  test_stop_transition_requires_the_explicit_unmixed_contract();
  static_assert(kDailyMaximumGapMs == 120000U);
  static_assert(kDailyMissingHeatBudgetW == 100.0f);
  test_explicit_120_second_gap_keeps_signed_energy();
  test_entire_valid_to_valid_interval_limits_the_gap();
  test_unflagged_invalid_and_omitted_ticks_are_not_bridged();
  test_missing_data_cannot_seed_a_day();
  test_nan_sources_need_the_explicit_contract();
  test_nan_without_explicit_missing_source_reason_fails_closed();
  test_explicit_gap_still_requires_every_observation_tick();
  test_hold_permission_must_remain_valid_for_each_missing_tick();
  test_valid_endpoint_context_or_utc_change_cannot_close_a_gap();
  test_daily_heat_budget_is_inclusive_and_uses_configuration();
  test_repeated_gaps_exhaust_the_daily_budget();
  test_uncertainty_uses_the_maximum_absolute_endpoint();
  test_day_boundary_splits_energy_and_uncertainty_budget();
  test_day_boundary_assesses_old_day_and_new_remainder_independently();
  test_hard_faults_and_context_or_clock_changes_reset();
}
