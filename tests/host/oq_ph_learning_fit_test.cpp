#include <assert.h>
#include <math.h>
#include <string.h>

#include "../../openquatt/includes/learning/oq_ph_learning_fit.h"

namespace {
using namespace oq_power_house;
using namespace oq_power_house::learning;

constexpr uint32_t kBaseEpoch = 20000U * 86400U;

SegmentRecord fitted_record(uint16_t day, uint8_t slot, float outside_c, float residual_w = 0.0f) {
  SegmentRecord record;
  record.start_epoch_s = kBaseEpoch + static_cast<uint32_t>(day) * 86400U + static_cast<uint32_t>(slot) * 28800U;
  record.end_epoch_s = record.start_epoch_s + 14400U;
  record.duration_s = 14400;
  record.context_revision = 1;
  record.mean_room_c = 20.0f;
  record.mean_setpoint_c = 20.0f;
  record.mean_outside_c = outside_c;
  record.mean_heat_w = 200.0f * (16.0f - outside_c) + residual_w;
  record.room_trend_k_per_h = 0.0f;
  record.room_range_k = 0.1f;
  record.setpoint_range_c = 0.0f;
  record.water_start_c = 30.0f;
  record.water_end_c = 30.2f;
  return record;
}

void make_dataset(SegmentRecord* records) {
  for (uint8_t day = 0; day < 9; ++day) {
    const float outside = -5.0f + 2.0f * day;
    records[2U * day] = fitted_record(day, 0, outside - 0.15f, day == 2 ? 700.0f : 0.0f);
    records[2U * day + 1U] = fitted_record(day, 1, outside + 0.15f);
  }
}

FitConfig fit_config() {
  FitConfig config;
  config.reference_room_c = 20.0f;
  config.reference_setpoint_c = 20.0f;
  return config;
}

LearningStatus finish_fit(AdviceFitWorkspace& workspace) {
  LearningStatus status = LearningStatus::FIT_IN_PROGRESS;
  size_t steps = 0;
  while (status == LearningStatus::FIT_IN_PROGRESS) {
    status = advance_advice_fit(workspace);
    assert(++steps <= kMaxAdviceFitSteps);
  }
  return status;
}

void test_advice_and_chronological_holdout() {
  SegmentRecord records[18];
  make_dataset(records);
  AdviceFitWorkspace workspace;
  FitConfig config = fit_config();
  QualityConfig quality;
  const HouseLine active{150.0f, 15.0f};
  const uint32_t now = kBaseEpoch + 10U * 86400U;
  assert(begin_advice_fit(records, 18, now, active, quality, config, workspace) == LearningStatus::FIT_IN_PROGRESS);
  assert(workspace.train_count == 12 && workspace.train_day_count == 6);
  assert(finish_fit(workspace) == LearningStatus::ADVICE_READY);
  const AdviceResult& result = advice_result(workspace);
  assert(result.candidate_available && result.advice_ready);
  assert(result.train_segments == 12 && result.holdout_segments == 6);
  assert(result.train_days == 6 && result.holdout_days == 3);
  assert(fabsf(result.candidate.heat_loss_w_per_k - 200.0f) < 15.0f);
  assert(fabsf(result.candidate.zero_power_temp_c - 16.0f) < 0.8f);
  assert(result.holdout_candidate_mae_w < result.holdout_active_mae_w);
  assert(isfinite(result.holdout_candidate_signed_bias_w));
  assert(isfinite(result.holdout_candidate_signed_bias_by_temp_w[2]));
  assert(result.validated_temp_min_c < result.validated_temp_max_c);
  assert(result.algorithm_version == kLearningAlgorithmVersion);
  assert(result.lodo_heat_loss_max_w_per_k >= result.lodo_heat_loss_min_w_per_k);
  assert(result.context_revision == 1);
}

void test_no_improvement_is_not_advice_ready() {
  SegmentRecord records[18];
  make_dataset(records);
  for (auto& record : records) record.mean_heat_w = 200.0f * (16.0f - record.mean_outside_c);
  AdviceFitWorkspace workspace;
  FitConfig config = fit_config();
  QualityConfig quality;
  assert(begin_advice_fit(records, 18, kBaseEpoch + 10U * 86400U, {200.0f, 16.0f}, quality, config, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish_fit(workspace) == LearningStatus::NO_HOLDOUT_IMPROVEMENT);
  assert(workspace.result.candidate_available && !workspace.result.advice_ready);
}

void test_fail_closed_dataset_gates() {
  SegmentRecord records[18];
  make_dataset(records);
  const uint32_t now = kBaseEpoch + 10U * 86400U;
  FitConfig config = fit_config();
  QualityConfig quality;
  AdviceFitWorkspace workspace;

  for (auto& record : records) record.mean_outside_c = 5.0f;
  assert(begin_advice_fit(records, 18, now, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::INSUFFICIENT_SPREAD);

  make_dataset(records);
  records[8].context_revision = 9;
  assert(begin_advice_fit(records, 18, now, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::MIXED_CONTEXT);

  make_dataset(records);
  records[8].mean_heat_w = 0.0f;
  assert(begin_advice_fit(records, 18, now, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::NONPOSITIVE_HEAT);

  make_dataset(records);
  config.max_raw_day_fraction = 0.10f;
  assert(begin_advice_fit(records, 18, now, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::DAY_DOMINANCE);

  make_dataset(records);
  config = fit_config();
  const SegmentRecord swap = records[4];
  records[4] = records[5];
  records[5] = swap;
  assert(begin_advice_fit(records, 18, now, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::TIME_DISCONTINUITY);
}

void test_replayed_record_bypasses_fail_closed() {
  SegmentRecord records[18];
  make_dataset(records);
  const uint32_t now = kBaseEpoch + 10U * 86400U;
  FitConfig config = fit_config();
  QualityConfig quality;
  AdviceFitWorkspace workspace;

  records[0].duration_s = 1;
  records[0].end_epoch_s = records[0].start_epoch_s + 1;
  assert(begin_advice_fit(records, 18, now, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::SEGMENT_INELIGIBLE);

  make_dataset(records);
  records[5].mean_room_c = 23.0f;
  assert(begin_advice_fit(records, 18, now, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::INSUFFICIENT_DATA);
  assert(workspace.record_count == 17);

  make_dataset(records);
  records[4].start_epoch_s = kBaseEpoch + 2U * 86400U + 22U * 3600U;
  // Rejected because it overlaps the next record, not because it crosses midnight.
  records[4].end_epoch_s = records[4].start_epoch_s + 14400U;
  assert(begin_advice_fit(records, 18, now, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::TIME_DISCONTINUITY);

  make_dataset(records);
  records[0].context_revision = 0;
  assert(begin_advice_fit(records, 18, now, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::SEGMENT_INELIGIBLE);

  make_dataset(records);
  records[0].room_range_k = -0.1f;
  assert(begin_advice_fit(records, 18, now, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::INVALID_MEASUREMENT);

  make_dataset(records);
  assert(begin_advice_fit(records, 18, now, {0.001f, -100.0f}, quality, config, workspace) ==
         LearningStatus::INVALID_ACTIVE_MODEL);
}

void test_temperature_dominance() {
  SegmentRecord imbalanced[54];
  for (uint8_t day = 0; day < 27; ++day) {
    float outside_c = 5.0f;
    if (day < 3)
      outside_c = -5.0f;
    else if (day >= 21)
      outside_c = 14.0f;
    imbalanced[2U * day] = fitted_record(day, 0, outside_c - 0.1f);
    imbalanced[2U * day + 1U] = fitted_record(day, 1, outside_c + 0.1f);
  }
  FitConfig config = fit_config();
  QualityConfig quality;
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(imbalanced, 54, kBaseEpoch + 28U * 86400U, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::DAY_DOMINANCE);
}

void test_exact_retention_boundary_allows_sparse_season() {
  SegmentRecord records[kMaxSegmentRecords];
  for (size_t index = 0; index < kMaxSegmentRecords; ++index) {
    const uint16_t day = static_cast<uint16_t>(index * 365U / (kMaxSegmentRecords - 1U));
    records[index] = fitted_record(day, 0, -5.0f + 2.0f * static_cast<float>(index % 10U));
    records[index].start_epoch_s += 22U * 3600U;
    records[index].end_epoch_s += 22U * 3600U;
  }

  AdviceFitWorkspace workspace;
  FitConfig config = fit_config();
  config.min_holdout_segments = 3;
  const uint32_t now = records[kMaxSegmentRecords - 1U].end_epoch_s;
  assert(now - records[0].end_epoch_s == kMaxRecordAgeS);
  assert(begin_advice_fit(records, kMaxSegmentRecords, now, {150.0f, 15.0f}, QualityConfig{}, config, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(workspace.day_count == kMaxSegmentRecords && workspace.train_day_count == kMaxSegmentRecords - 3U &&
         workspace.train_count == kMaxSegmentRecords - 3U);
  assert(finish_fit(workspace) == LearningStatus::ADVICE_READY);
  assert(workspace.result.holdout_days == 3 && workspace.result.holdout_segments == 3);
  assert(workspace.result.validated_temp_min_c == records[kMaxSegmentRecords - 3U].mean_outside_c);
  assert(workspace.result.validated_temp_max_c == records[kMaxSegmentRecords - 1U].mean_outside_c);
}
void test_day_night_history_selects_immutable_fitting_subset() {
  SegmentRecord records[36];
  for (uint8_t day = 0; day < 9; ++day) {
    for (uint8_t slot = 0; slot < 4; ++slot) {
      auto& record = records[4U * day + slot];
      record = fitted_record(day, 0, -5.0f + 2.0f * day + (slot % 2 == 0 ? -0.15f : 0.15f));
      record.start_epoch_s += static_cast<uint32_t>(slot) * 21600U;
      record.end_epoch_s = record.start_epoch_s + 14400U;
      if (slot >= 2) {
        record.mean_room_c = record.mean_setpoint_c = 19.0f;
        record.mean_heat_w = 300.0f * (15.0f - record.mean_outside_c);
      }
    }
  }
  SegmentRecord original[36];
  memcpy(original, records, sizeof(records));
  AdviceFitWorkspace workspace;
  auto config = fit_config();
  QualityConfig quality;
  const uint32_t now = kBaseEpoch + 10U * 86400U;
  for (int target = 20; target >= 19; --target) {
    config.reference_room_c = config.reference_setpoint_c = static_cast<float>(target);
    assert(begin_advice_fit(records, 36, now, {150.0f, 15.0f}, quality, config, workspace) ==
           LearningStatus::FIT_IN_PROGRESS);
    assert(workspace.record_count == 18 && workspace.train_count == 12);
    assert(finish_fit(workspace) == LearningStatus::ADVICE_READY);
    assert(fabsf(workspace.result.candidate.heat_loss_w_per_k - (target == 20 ? 200.0f : 300.0f)) < 0.1f);
    assert(memcmp(original, records, sizeof(records)) == 0);
  }
  config.reference_room_c = config.reference_setpoint_c = 22.0f;
  assert(begin_advice_fit(records, 36, now, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::INSUFFICIENT_DATA);
  assert(workspace.record_count == 0);
  // Exclusion is not a bypass for corrupt or overlapping retained evidence.
  records[0].duration_s = 1;
  assert(begin_advice_fit(records, 36, now, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::SEGMENT_INELIGIBLE);
}

}  // namespace

int main() {
  test_day_night_history_selects_immutable_fitting_subset();
  test_advice_and_chronological_holdout();
  test_no_improvement_is_not_advice_ready();
  test_fail_closed_dataset_gates();
  test_replayed_record_bypasses_fail_closed();
  test_temperature_dominance();
  test_exact_retention_boundary_allows_sparse_season();
  static_assert(kMaxRecordsVisitedPerHuberFit == 768);
  static_assert(kMaxSegmentRecords == 64);
  static_assert(kMaxCalendarDays == kMaxSegmentRecords);
  return 0;
}
