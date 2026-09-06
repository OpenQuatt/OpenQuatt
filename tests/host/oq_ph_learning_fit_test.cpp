#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/learning/oq_ph_learning_fit.h"

namespace {
using namespace oq_power_house;
using namespace oq_power_house::learning;

constexpr uint32_t kBaseEpoch = 20000U * 86400U;

SegmentRecord fitted_record(uint8_t day, uint8_t slot, float outside_c, float residual_w = 0.0f) {
  SegmentRecord record;
  record.start_epoch_s = kBaseEpoch + static_cast<uint32_t>(day) * 86400U + static_cast<uint32_t>(slot) * 28800U;
  record.end_epoch_s = record.start_epoch_s + 14400U;
  record.duration_s = 14400;
  record.source_generation = 1;
  record.physical_context_generation = 2;
  record.control_generation = day < 5 ? 3 : 4;  // Control generations may differ between complete segments.
  record.mean_room_c = 20.0f;
  record.mean_setpoint_c = 20.0f;
  record.mean_outside_c = outside_c;
  record.mean_heat_w = 200.0f * (16.0f - outside_c) + residual_w;
  record.mean_heat_uncertainty_w = 50.0f;
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
  assert(result.source_generation == 1 && result.physical_context_generation == 2);
  assert(result.latest_control_generation == 4);
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
  records[8].source_generation = 9;
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
         LearningStatus::MIXED_CONTEXT);

  make_dataset(records);
  records[4].start_epoch_s = kBaseEpoch + 2U * 86400U + 22U * 3600U;
  records[4].end_epoch_s = records[4].start_epoch_s + 14400U;
  assert(begin_advice_fit(records, 18, now, {150.0f, 15.0f}, quality, config, workspace) ==
         LearningStatus::TIME_DISCONTINUITY);

  make_dataset(records);
  records[0].control_generation = 0;
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

void test_temperature_dominance_and_uncertainty_gate() {
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

  SegmentRecord records[18];
  make_dataset(records);
  for (size_t index = 12; index < 18; ++index) records[index].mean_heat_uncertainty_w = 400.0f;
  quality.max_heat_uncertainty_fraction = 0.50f;
  config.min_holdout_improvement_w = 0.0f;
  assert(begin_advice_fit(records, 18, kBaseEpoch + 10U * 86400U, {195.0f, 16.0f}, quality, config, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish_fit(workspace) == LearningStatus::NO_HOLDOUT_IMPROVEMENT);
  assert(workspace.result.holdout_mean_uncertainty_w == 400.0f);
  assert(!workspace.result.advice_ready);
}

void test_exact_retention_boundary_allows_43_utc_days() {
  SegmentRecord records[43];
  for (uint8_t day = 0; day < 43; ++day)
    records[day] = fitted_record(day, 0, -5.0f + 2.0f * static_cast<float>(day % 10U));

  AdviceFitWorkspace workspace;
  FitConfig config = fit_config();
  config.min_holdout_segments = 3;
  const uint32_t now = records[42].end_epoch_s;
  assert(now - records[0].end_epoch_s == kMaxRecordAgeS);
  assert(begin_advice_fit(records, 43, now, {150.0f, 15.0f}, QualityConfig{}, config, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(workspace.day_count == 43 && workspace.train_day_count == 40 && workspace.train_count == 40);
  assert(finish_fit(workspace) == LearningStatus::ADVICE_READY);
  assert(workspace.result.holdout_days == 3 && workspace.result.holdout_segments == 3);
  assert(workspace.result.validated_temp_min_c == records[40].mean_outside_c);
  assert(workspace.result.validated_temp_max_c == records[42].mean_outside_c);
}
}  // namespace

int main() {
  test_advice_and_chronological_holdout();
  test_no_improvement_is_not_advice_ready();
  test_fail_closed_dataset_gates();
  test_replayed_record_bypasses_fail_closed();
  test_temperature_dominance_and_uncertainty_gate();
  test_exact_retention_boundary_allows_43_utc_days();
  static_assert(kMaxRecordsVisitedPerHuberFit == 768);
  static_assert(kMaxSegmentRecords == 64);
  static_assert(kMaxCalendarDays == 43);
  return 0;
}
