#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/learning/oq_ph_learning_aggregate.h"
#include "../../openquatt/includes/learning/oq_ph_learning_fit.h"

namespace {
using namespace oq_power_house;
using namespace oq_power_house::learning;

constexpr uint32_t kBaseEpoch = 20000U * 86400U;
constexpr uint32_t kLegacyRecordAgeS = 42U * 24U * 60U * 60U;

struct Dataset {
  SegmentRecord records[kMaxSegmentRecords]{};
  size_t count = 0;
};

SegmentRecord season_record(uint16_t day, float outside_c, const HouseLine& line, float noise_w = 0.0f,
                            uint32_t context_revision = 1) {
  SegmentRecord value;
  value.start_epoch_s = kBaseEpoch + static_cast<uint32_t>(day) * 86400U + 12U * 3600U;
  value.end_epoch_s = value.start_epoch_s + 4U * 3600U;
  value.duration_s = 4U * 3600U;
  value.context_revision = context_revision;
  value.mean_room_c = 20.0f;
  value.mean_setpoint_c = 20.0f;
  value.mean_outside_c = outside_c;
  value.mean_heat_w = house_line_power_w(line, outside_c) + noise_w;
  value.room_trend_k_per_h = 0.0f;
  value.room_range_k = 0.0f;
  value.setpoint_range_c = 0.0f;
  value.water_start_c = 30.0f;
  value.water_end_c = 30.0f;
  return value;
}

void append_current_policy(Dataset& dataset, const SegmentRecord& value) {
  size_t kept = 0;
  for (size_t index = 0; index < dataset.count; ++index) {
    const SegmentRecord& record = dataset.records[index];
    if (value.end_epoch_s >= record.end_epoch_s && value.end_epoch_s - record.end_epoch_s <= kLegacyRecordAgeS)
      dataset.records[kept++] = record;
  }
  dataset.count = kept;
  if (dataset.count == kMaxSegmentRecords) {
    for (size_t index = 1; index < dataset.count; ++index) dataset.records[index - 1] = dataset.records[index];
    --dataset.count;
  }
  dataset.records[dataset.count++] = value;
}

void append_representative_policy(Dataset& dataset, const SegmentRecord& value) {
  RecordBuffer buffer{dataset.records, dataset.count, kMaxSegmentRecords};
  QualityConfig quality;
  assert(append_record(buffer, value, value.end_epoch_s, quality) == LearningStatus::OK);
  dataset.count = buffer.count;
}

HouseLine fit_for_comparison(const Dataset& dataset) {
  assert(dataset.count >= 2U && dataset.count <= kMaxSegmentRecords);
  AdviceFitWorkspace workspace;
  workspace.records = dataset.records;
  workspace.train_count = dataset.count;
  workspace.config.reference_room_c = 20.0f;
  workspace.config.reference_setpoint_c = 20.0f;
  // This is the production Huber line fit, evaluated against an external recent holdout.
  // Day-normalization keeps this benchmark focused on record selection rather than
  // the incidental density of calendar days in each synthetic season.
  for (size_t index = 0; index < dataset.count; ++index) {
    const uint8_t day = static_cast<uint8_t>(index / 2U);
    assert(day < kMaxCalendarDays);
    workspace.record_day_index[index] = day;
    ++workspace.day_record_counts[day];
  }
  HouseLine line;
  assert(detail::fit_huber_line(workspace, -1, line));
  return line;
}

float mean_absolute_error(const HouseLine& line, const SegmentRecord* heldout, size_t count) {
  double total = 0.0;
  for (size_t index = 0; index < count; ++index)
    total += fabsf(house_line_power_w(line, heldout[index].mean_outside_c) - heldout[index].mean_heat_w);
  return static_cast<float>(total / count);
}

bool contains_temperature_below(const Dataset& dataset, float limit_c) {
  for (size_t index = 0; index < dataset.count; ++index)
    if (dataset.records[index].mean_outside_c < limit_c) return true;
  return false;
}

bool contains_temperature_above(const Dataset& dataset, float limit_c) {
  for (size_t index = 0; index < dataset.count; ++index)
    if (dataset.records[index].mean_outside_c > limit_c) return true;
  return false;
}

void test_soft_winter_is_not_penalized() {
  const HouseLine truth{200.0f, 16.0f};
  Dataset current;
  Dataset representative;
  for (uint16_t day = 0; day < 80U; ++day) {
    const float outside_c = 6.0f + static_cast<float>(day % 9U);
    const SegmentRecord value = season_record(day, outside_c, truth);
    append_current_policy(current, value);
    append_representative_policy(representative, value);
  }
  SegmentRecord heldout[9];
  for (uint16_t index = 0; index < 9U; ++index) heldout[index] = season_record(80U + index, 7.0f + index % 7U, truth);
  const float current_error = mean_absolute_error(fit_for_comparison(current), heldout, 9U);
  const float representative_error = mean_absolute_error(fit_for_comparison(representative), heldout, 9U);
  assert(current_error < 0.1f && representative_error < 0.1f);
}

void test_short_cold_period_survives_and_improves_recent_prediction() {
  const HouseLine truth{200.0f, 16.0f};
  Dataset current;
  Dataset representative;
  for (uint16_t day = 0; day < 80U; ++day) {
    const float outside_c = day < 16U ? -7.0f + static_cast<float>(day) : 7.0f + static_cast<float>(day % 3U);
    const float noise_w = day < 16U ? 0.0f : (outside_c < 8.0f ? 180.0f : -180.0f);
    const SegmentRecord value = season_record(day, outside_c, truth, noise_w);
    append_current_policy(current, value);
    append_representative_policy(representative, value);
  }
  const Dataset& retained = representative;
  assert(!contains_temperature_below(current, 0.0f));
  assert(contains_temperature_below(retained, 0.0f));
  SegmentRecord heldout[9];
  for (uint16_t index = 0; index < 9U; ++index) heldout[index] = season_record(80U + index, 7.0f + index % 3U, truth);
  const float current_error = mean_absolute_error(fit_for_comparison(current), heldout, 9U);
  const float representative_error = mean_absolute_error(fit_for_comparison(retained), heldout, 9U);
  assert(representative_error < current_error * 0.75f);
}

void test_repeated_mild_records_do_not_evict_all_temperature_coverage() {
  const HouseLine truth{200.0f, 16.0f};
  Dataset current;
  Dataset representative;
  for (uint16_t day = 0; day < 80U; ++day) {
    const float outside_c = day < 8U ? -6.0f + static_cast<float>(day) * 3.0f : 8.0f;
    const SegmentRecord value = season_record(day, outside_c, truth);
    append_current_policy(current, value);
    append_representative_policy(representative, value);
  }
  const Dataset& retained = representative;
  assert(!contains_temperature_below(current, 7.9f));
  assert(contains_temperature_below(retained, 0.0f));
  assert(contains_temperature_above(retained, 10.0f));
}

void test_unmarked_heat_loss_change_remains_a_rejection_case() {
  const HouseLine before{200.0f, 16.0f};
  const HouseLine after{300.0f, 16.0f};
  Dataset current;
  Dataset representative;
  for (uint16_t day = 0; day < 80U; ++day) {
    const HouseLine truth = day < 60U ? before : after;
    const SegmentRecord value = season_record(day, -4.0f + static_cast<float>(day % 15U), truth);
    append_current_policy(current, value);
    append_representative_policy(representative, value);
  }
  SegmentRecord heldout[9];
  for (uint16_t index = 0; index < 9U; ++index) heldout[index] = season_record(80U + index, -2.0f + index, after);
  const float current_error = mean_absolute_error(fit_for_comparison(current), heldout, 9U);
  const float representative_error = mean_absolute_error(fit_for_comparison(representative), heldout, 9U);
  assert(representative_error > current_error * 1.2f);
}

void test_proposed_retention_boundary_is_one_year() {
  const HouseLine truth{200.0f, 16.0f};
  Dataset representative;
  const SegmentRecord value = season_record(0U, -4.0f, truth);
  append_representative_policy(representative, value);
  RecordBuffer buffer{representative.records, representative.count, kMaxSegmentRecords};
  assert(prune_expired_records(buffer, value.end_epoch_s + kMaxRecordAgeS) == LearningStatus::OK);
  assert(buffer.count == 1U);
  assert(prune_expired_records(buffer, value.end_epoch_s + kMaxRecordAgeS + 1U) == LearningStatus::OK);
  assert(buffer.count == 0U);
}
}  // namespace

int main() {
  test_soft_winter_is_not_penalized();
  test_short_cold_period_survives_and_improves_recent_prediction();
  test_repeated_mild_records_do_not_evict_all_temperature_coverage();
  test_unmarked_heat_loss_change_remains_a_rejection_case();
  test_proposed_retention_boundary_is_one_year();
  return 0;
}
