#include <assert.h>
#include <math.h>

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
  for (uint32_t step = 0; step <= 48; ++step) {
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
  assert(result.record.duration_s == 14400U);
  assert(fabs(result.record.mean_heat_w - expected_energy_ws / 14400.0) < 0.1);
  assert(result.record.mean_heat_w < 3000.0f);  // Compressor-off and signed periods were integrated.
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
  ObserveResult result;
  for (uint32_t step = 2; step <= 48; ++step)
    result = observe_snapshot(accumulator, snapshot(start_ms + step * 300000ULL, start_epoch + step * 300U, 1000.0f),
                              config);
  assert(result.status == LearningStatus::INVALID_MEASUREMENT && !result.has_record && !accumulator.active);

  observe_snapshot(accumulator, snapshot(start_ms + 20000000ULL, start_epoch + 20000U, 1000.0f), config);
  auto changed = snapshot(start_ms + 20300000ULL, start_epoch + 20300U, 1000.0f);
  changed.context_revision = 4;
  assert(observe_snapshot(accumulator, changed, config).status == LearningStatus::MIXED_CONTEXT);
  assert(accumulator.active && accumulator.context_revision == 4);

  auto jumped = snapshot(start_ms + 20600000ULL, start_epoch + 30000U, 1000.0f);
  jumped.context_revision = 4;
  assert(observe_snapshot(accumulator, jumped, config).status == LearningStatus::TIME_DISCONTINUITY);

  reset_segment(accumulator);
  for (uint32_t step = 0; step < 4; ++step) {
    result = observe_snapshot(accumulator, snapshot(start_ms + step * 300000ULL, start_epoch + step * 301U, 1000.0f),
                              config);
  }
  assert(result.status == LearningStatus::TIME_DISCONTINUITY);  // Small adjacent UTC drift accumulated at the origin.
}

void test_nonpositive_segment_and_record_bounds() {
  QualityConfig config;
  config.max_interval_ms = 5U * 60U * 1000U;
  SegmentAccumulator accumulator;
  ObserveResult result;
  for (uint32_t step = 0; step <= 48; ++step)
    result = observe_snapshot(accumulator,
                              snapshot(1000 + step * 300000ULL, 20000U * 86400U + 3600U + step * 300U, 0.0f), config);
  assert(result.status == LearningStatus::NONPOSITIVE_HEAT && !result.has_record);

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

  assert(append_record(buffer, record(base + 100U * 14400U), base + 100U * 14400U, config) == LearningStatus::OK);
  SegmentRecord mixed = record(base + 101U * 14400U);
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
}  // namespace

int main() {
  test_time_weighted_signed_heat();
  test_fail_closed_boundaries();
  test_nonpositive_segment_and_record_bounds();
  test_full_dataset_keeps_recent_records_and_temperature_coverage();
  return 0;
}
