#include <assert.h>
#include <math.h>
#include <initializer_list>

#include "../../openquatt/includes/learning/oq_ph_learning_aggregate.h"

namespace {
using namespace oq_power_house::learning;

LearningSnapshot snapshot(uint64_t monotonic_ms, uint32_t epoch_s, float heat_w) {
  LearningSnapshot value;
  value.monotonic_ms = monotonic_ms;
  value.epoch_s = epoch_s;
  value.context_revision = 1;
  value.room_c = 20.0f;
  value.setpoint_c = 20.0f;
  value.outside_c = 5.0f;
  value.heat_to_water_w = heat_w;
  value.mean_water_c = 30.0f;
  return value;
}

SegmentRecord record(uint32_t end_epoch_s, float heat_w = 1000.0f, float outside_c = 5.0f) {
  SegmentRecord value;
  value.start_epoch_s = end_epoch_s - 14400;
  value.end_epoch_s = end_epoch_s;
  value.duration_s = 14400;
  value.context_revision = 1;
  value.mean_room_c = 20.0f;
  value.mean_setpoint_c = 20.0f;
  value.mean_outside_c = outside_c;
  value.mean_heat_w = heat_w;
  value.room_trend_k_per_h = 0.0f;
  value.room_range_k = 0.0f;
  value.setpoint_range_c = 0.0f;
  value.water_start_c = 30.0f;
  value.water_end_c = 30.0f;
  return value;
}

void test_time_weighted_signed_heat() {
  QualityConfig config;
  config.max_interval_ms = 5U * 60U * 1000U;
  SegmentAccumulator accumulator;
  const uint64_t start_ms = 1000;
  const uint32_t start_epoch = 20000U * 86400U + 3600U;
  double expected_energy_ws = 0.0;
  float previous_heat = 3000.0f;
  ObserveResult result;
  for (uint32_t step = 0; step <= 288; ++step) {
    float heat_w = 0.0f;
    switch (step % 12U) {
      case 0:
      case 1:
      case 2:
      case 3:
        heat_w = 3000.0f;
        break;
      case 11:
        heat_w = -200.0f;
        break;
      default:
        heat_w = 0.0f;
    }
    if (step > 0) expected_energy_ws += 0.5 * (previous_heat + heat_w) * 300.0;
    result =
        observe_snapshot(accumulator, snapshot(start_ms + step * 300000ULL, start_epoch + step * 300U, heat_w), config);
    previous_heat = heat_w;
  }
  assert(result.status == LearningStatus::SEGMENT_READY && result.has_record);
  assert(result.record.duration_s == 86400U);
  assert(fabs(result.record.mean_heat_w - expected_energy_ws / 86400.0) < 0.1);
  assert(result.record.mean_heat_w < 3000.0f);  // Compressor-off and signed periods were integrated.
}

void test_midnight_and_quantized_room() {
  for (bool rising : {false, true}) {
    SegmentAccumulator accumulator;
    ObserveResult result;
    const uint32_t start = 20000U * 86400U + 22U * 3600U;
    for (uint32_t minute = 0; minute <= 1440; ++minute) {
      auto value = snapshot(1000ULL + minute * 60000ULL, start + minute * 60U, 2500.0f);
      // A thermostat alternating between adjacent readings versus a lasting step.
      value.room_c = rising ? (minute < 120U ? 20.0f : 20.25f) : (minute % 2U == 0U ? 20.0f : 20.25f);
      result = observe_snapshot(accumulator, value, QualityConfig{});
    }
    // Quantized readings and ordinary room changes no longer need to be flat.
    assert(result.has_record && result.status == LearningStatus::SEGMENT_READY);
    assert(fabsf(profile_mean_c(result.record) - effective_outside_c(result.record)) < 0.01f);
  }
}

void test_fail_closed_boundaries() {
  QualityConfig config;
  config.max_interval_ms = 5U * 60U * 1000U;
  const uint64_t start_ms = 1000;
  const uint32_t start_epoch = 20000U * 86400U + 3600U;
  SegmentAccumulator accumulator;
  observe_snapshot(accumulator, snapshot(start_ms, start_epoch, 1000.0f), config);
  auto stale = snapshot(start_ms + 300000, start_epoch + 300, 1000.0f);
  stale.invalid_reasons = INVALID_SOURCE_STALE;
  assert(observe_snapshot(accumulator, stale, config).status == LearningStatus::INVALID_MEASUREMENT);
  assert(!accumulator.active);
  ObserveResult result;
  for (uint32_t step = 2; step <= 288; ++step)
    result = observe_snapshot(accumulator, snapshot(start_ms + step * 300000ULL, start_epoch + step * 300U, 1000.0f),
                              config);
  assert(result.status == LearningStatus::COLLECTING && !result.has_record && accumulator.active);
  for (uint32_t step = 289; step <= 290; ++step)
    result = observe_snapshot(accumulator, snapshot(start_ms + step * 300000ULL, start_epoch + step * 300U, 1000.0f),
                              config);
  assert(result.has_record && result.record.start_epoch_s == start_epoch + 600U);
  assert(result.record.duration_s == 86400U);
  assert(fabsf(result.record.mean_heat_w - 1000.0f) < 0.01f);

  observe_snapshot(accumulator, snapshot(start_ms + 100000000ULL, start_epoch + 100000U, 1000.0f), config);
  auto changed = snapshot(start_ms + 100300000ULL, start_epoch + 100300U, 1000.0f);
  changed.context_revision = 4;
  assert(observe_snapshot(accumulator, changed, config).status == LearningStatus::MIXED_CONTEXT);
  assert(accumulator.active && accumulator.context_revision == 4);

  auto jumped = snapshot(start_ms + 100600000ULL, start_epoch + 200000U, 1000.0f);
  jumped.context_revision = 4;
  assert(observe_snapshot(accumulator, jumped, config).status == LearningStatus::TIME_DISCONTINUITY);

  reset_segment(accumulator);
  for (uint32_t step = 0; step < 4; ++step) {
    result = observe_snapshot(accumulator, snapshot(start_ms + step * 300000ULL, start_epoch + step * 301U, 1000.0f),
                              config);
  }
  assert(result.status == LearningStatus::TIME_DISCONTINUITY);  // Small adjacent UTC drift accumulated at the origin.
}

void test_invalid_sample_restarts_only_unfinished_window() {
  for (uint32_t invalid_second : {10U, 86390U}) {
    SegmentAccumulator accumulator;
    const uint32_t epoch = 20000U * 86400U;
    const uint32_t first_valid = invalid_second + 20U;
    for (uint32_t second = 0; second <= first_valid + 86400U; second += 10U) {
      auto value = snapshot(1000ULL + second * 1000ULL, epoch + second, 2000.0f);
      // Two consecutive invalid samples cannot be bridged or seed a new window.
      if (second == invalid_second || second == invalid_second + 10U) {
        value.invalid_reasons = INVALID_SOURCE_STALE;
        value.heat_to_water_w = 50000.0f;
      }
      const auto result = observe_snapshot(accumulator, value, QualityConfig{});
      if (second < first_valid + 86400U)
        assert(!result.has_record);
      else {
        assert(result.has_record);
        assert(result.record.start_epoch_s == epoch + first_valid);
        assert(result.record.duration_s == 86400U);
        assert(fabsf(result.record.mean_heat_w - 2000.0f) < 0.01f);
      }
      if (value.invalid_reasons != INVALID_NONE) assert(!accumulator.active);
    }
  }
}

void test_nonpositive_segment_and_record_bounds() {
  QualityConfig config;
  config.max_interval_ms = 5U * 60U * 1000U;
  SegmentAccumulator accumulator;
  ObserveResult result;
  for (uint32_t step = 0; step <= 288; ++step)
    result = observe_snapshot(accumulator,
                              snapshot(1000 + step * 300000ULL, 20000U * 86400U + 3600U + step * 300U, 0.0f), config);
  assert(result.status == LearningStatus::SEGMENT_READY && result.has_record && result.record.mean_heat_w == 0.0f);

  SegmentRecord storage[kMaxSegmentRecords];
  RecordBuffer buffer{storage, 0, kMaxSegmentRecords};
  const uint32_t base = 21000U * 86400U + 14400U;
  for (size_t index = 0; index < kMaxSegmentRecords + 1U; ++index) {
    const uint32_t end = base + static_cast<uint32_t>(index) * 14400U;
    assert(append_record(buffer, record(end), end, config) == LearningStatus::OK);
  }
  assert(buffer.count == kMaxSegmentRecords);
  assert(buffer.records[0].end_epoch_s == base + 14400U);
  assert(prune_expired_records(buffer, buffer.records[0].end_epoch_s + kMaxRecordAgeS + 1U) == LearningStatus::OK);
  assert(buffer.count < kMaxSegmentRecords);

  assert(append_record(buffer, record(base + (kMaxSegmentRecords + 2U) * 14400U),
                       base + (kMaxSegmentRecords + 2U) * 14400U, config) == LearningStatus::OK);
  SegmentRecord mixed = record(base + (kMaxSegmentRecords + 3U) * 14400U);
  mixed.context_revision = 9;
  assert(append_record(buffer, mixed, mixed.end_epoch_s, config) == LearningStatus::MIXED_CONTEXT);
}

void test_full_dataset_keeps_recent_records_and_temperature_coverage() {
  QualityConfig config;
  SegmentRecord storage[kMaxSegmentRecords];
  RecordBuffer buffer{storage, 0, kMaxSegmentRecords};
  const uint32_t base = 22000U * 86400U + 14400U;
  for (size_t index = 0; index < kMaxSegmentRecords + 20U; ++index) {
    float outside_c = 8.0f;
    if (index == 0U)
      outside_c = -5.0f;
    else if (index == 1U)
      outside_c = 2.0f;
    else if (index == 2U)
      outside_c = 12.0f;
    const uint32_t end = base + static_cast<uint32_t>(index) * 14400U;
    assert(append_record(buffer, record(end, 1000.0f, outside_c), end, config) == LearningStatus::OK);
  }
  assert(buffer.count == kMaxSegmentRecords);
  bool has_cold = false;
  bool has_mild = false;
  bool has_warm = false;
  for (size_t index = 0; index < buffer.count; ++index) {
    has_cold = has_cold || buffer.records[index].mean_outside_c < 0.0f;
    has_mild =
        has_mild || (buffer.records[index].mean_outside_c >= 0.0f && buffer.records[index].mean_outside_c < 5.0f);
    has_warm = has_warm || buffer.records[index].mean_outside_c >= 10.0f;
  }
  assert(has_cold && has_mild && has_warm);
  for (size_t index = kMaxSegmentRecords - kRecentSegmentRecords; index < buffer.count; ++index)
    assert(buffer.records[index].mean_outside_c == 8.0f);
}
void test_day_boundary_preserves_energy_and_temperature_profile() {
  QualityConfig config;
  config.max_interval_ms = 60000;
  SegmentAccumulator state;
  const uint32_t epoch = 23000U * 86400U + 123U;
  double first_energy = 0.0;
  ObserveResult closed;
  for (uint32_t second = 0; second <= 86440U; second += 37U) {
    auto value = snapshot(1000ULL + second * 1000ULL, epoch + second, 1000.0f + second * 0.01f);
    value.outside_c = -4.0f + second * 0.0002f;
    value.room_c = second % 74U == 0U ? 19.8f : 20.1f;
    value.setpoint_c = second < 43200U ? 20.0f : 18.0f;
    value.mean_water_c = second < 43200U ? 30.0f : 25.0f;
    const auto observed = observe_snapshot(state, value, config);
    if (observed.has_record) closed = observed;
  }
  assert(closed.has_record && closed.record.duration_s == 86400U);
  assert(closed.record.end_epoch_s == epoch + 86400U);
  first_energy = closed.record.mean_heat_w * 86400.0;
  assert(fabs(first_energy - (1000.0 * 86400.0 + 0.005 * 86400.0 * 86400.0)) < 10.0);
  assert(state.active && state.start_epoch_s == closed.record.end_epoch_s);
  const double remainder_s = state.integrated_duration_s;
  assert(remainder_s > 0.0 && remainder_s < 37.0);
  assert(fabs(state.heat_integral - (1864.0 * remainder_s + 0.005 * remainder_s * remainder_s)) < 0.1);
  assert(fabsf(profile_mean_c(closed.record) - effective_outside_c(closed.record)) < 0.01f);
  for (size_t index = 1; index < kDailyTemperatureProfileSize; ++index)
    assert(closed.record.effective_outside_profile_centi[index] >=
           closed.record.effective_outside_profile_centi[index - 1]);
}

void test_tolerated_utc_drift_does_not_lose_a_day() {
  for (int drift_s : {-1, 1}) {
    SegmentAccumulator state;
    ObserveResult result;
    const uint32_t epoch = 24000U * 86400U;
    for (uint32_t minute = 0; minute <= 1440U; ++minute) {
      const uint32_t current_epoch = epoch + minute * 60U + (minute == 1440U ? drift_s : 0);
      result = observe_snapshot(state, snapshot(1000ULL + minute * 60000ULL, current_epoch, 2000.0f), QualityConfig{});
    }
    assert(result.has_record && result.record.duration_s == 86400U);
    const uint32_t now = epoch + 86400U + drift_s;
    assert(result.record.end_epoch_s == now && state.start_epoch_s == now);
    SegmentRecord retained[2];
    RecordBuffer records{retained, 0, 2};
    assert(append_record(records, result.record, now, QualityConfig{}) == LearningStatus::OK);
    assert(records.count == 1);
    assert(observe_snapshot(state, snapshot(1000ULL + 86460000ULL, now + 60U, 2000.0f), QualityConfig{}).status ==
           LearningStatus::COLLECTING);
  }
}

}  // namespace

int main() {
  test_tolerated_utc_drift_does_not_lose_a_day();
  test_day_boundary_preserves_energy_and_temperature_profile();
  test_time_weighted_signed_heat();
  test_midnight_and_quantized_room();
  test_fail_closed_boundaries();
  test_invalid_sample_restarts_only_unfinished_window();
  test_nonpositive_segment_and_record_bounds();
  test_full_dataset_keeps_recent_records_and_temperature_coverage();
  return 0;
}
