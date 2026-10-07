#include <assert.h>
#include <math.h>
#include <string.h>

#include "../../openquatt/includes/learning/oq_ph_learning_fit.h"

namespace {
using namespace oq_power_house;
using namespace oq_power_house::learning;
constexpr uint32_t kBaseEpoch = 20000U * 86400U;

SegmentRecord fitted_record(uint16_t day, float outside_c, float room_c = 20.0f, float profile_span_c = 0.0f) {
  SegmentRecord record;
  record.start_epoch_s = kBaseEpoch + static_cast<uint32_t>(day) * 86400U;
  record.end_epoch_s = record.start_epoch_s + 86400U;
  record.duration_s = 86400U;
  record.context_revision = 1;
  record.mean_room_c = room_c;
  record.mean_setpoint_c = room_c;
  record.mean_outside_c = outside_c;
  record.room_trend_k_per_h = 0.0f;
  record.room_range_k = 0.1f;
  record.setpoint_range_c = 0.0f;
  record.water_start_c = record.water_end_c = 30.0f;
  for (size_t point = 0; point < kDailyTemperatureProfileSize; ++point)
    record.effective_outside_profile_centi[point] = static_cast<int16_t>(
        lroundf(100.0f * (effective_outside_c(record) + profile_span_c * (2.0f * point / 7.0f - 1.0f))));
  // Synthetic energy is the independent average of eight equally weighted physical temperatures.
  record.mean_heat_w = 0.0f;
  for (int16_t point : record.effective_outside_profile_centi)
    record.mean_heat_w += fmaxf(0.0f, 200.0f * (16.0f - point * 0.01f)) / kDailyTemperatureProfileSize;
  return record;
}

void make_dataset(SegmentRecord* records) {
  for (uint8_t day = 0; day < 9; ++day) records[day] = fitted_record(day, -5.0f + 2.0f * day);
}

LearningStatus finish_fit(AdviceFitWorkspace& workspace) {
  auto status = LearningStatus::FIT_IN_PROGRESS;
  size_t steps = 0;
  while (status == LearningStatus::FIT_IN_PROGRESS) {
    status = advance_advice_fit(workspace);
    assert(++steps <= kMaxAdviceFitSteps);
  }
  return status;
}

void test_all_cold_exact_linear_and_chronological_holdout() {
  SegmentRecord records[9];
  make_dataset(records);
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 9, kBaseEpoch + 10U * 86400U, {150.0f, 15.0f}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(workspace.train_count == 6 && workspace.train_day_count == 6);
  assert(finish_fit(workspace) == LearningStatus::ADVICE_READY);
  const auto& result = advice_result(workspace);
  assert(result.candidate_available && result.advice_ready);
  assert(result.train_segments == 6 && result.holdout_segments == 3);
  assert(result.train_days == 6 && result.holdout_days == 3);
  assert(fabsf(result.candidate.heat_loss_w_per_k - 200.0f) < 0.01f);
  assert(fabsf(result.candidate.zero_power_temp_c - 16.0f) < 0.001f);
  assert(result.holdout_candidate_mae_w < 0.01f);
  assert(result.holdout_candidate_mae_w < result.holdout_active_mae_w);
  assert(isfinite(result.holdout_candidate_signed_bias_w));
  assert(result.algorithm_version == kLearningAlgorithmVersion && result.context_revision == 1);
}

void test_hinge_integrates_profiles_including_zero_heat_day() {
  SegmentRecord records[12];
  for (uint8_t day = 0; day < 12; ++day) records[day] = fitted_record(day, -4.0f + 3.0f * day, 20.0f, 7.0f);
  records[9] = fitted_record(9, 5.0f, 20.0f, 7.0f);
  records[10] = fitted_record(10, 17.0f, 20.0f, 7.0f);
  assert(records[7].effective_outside_profile_centi[0] < 1600 && records[7].effective_outside_profile_centi[7] > 1600);
  assert(records[11].mean_heat_w == 0.0f);
  assert(validate_segment_record(records[11], {}) == LearningStatus::OK);
  // Clipping the daily average would incorrectly predict no heat on this day.
  assert(fabsf(records[7].mean_heat_w - 300.0f) < 0.01f);
  assert(record_house_line_power_w({200.0f, 16.0f}, records[7]) >
         house_line_power_w({200.0f, 16.0f}, records[7].mean_outside_c));
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 12, records[11].end_epoch_s, {150.0f, 15.0f}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish_fit(workspace) == LearningStatus::ADVICE_READY);
  assert(fabsf(workspace.result.candidate.heat_loss_w_per_k - 200.0f) < 0.05f);
  assert(fabsf(workspace.result.candidate.zero_power_temp_c - 16.0f) < 0.005f);
  assert(workspace.result.holdout_candidate_mae_w < 0.05f);
}

void test_huber_resists_single_day_outlier() {
  SegmentRecord records[18];
  for (uint8_t day = 0; day < 18; ++day) records[day] = fitted_record(day, -5.0f + 2.0f * (day % 9));
  records[2].mean_heat_w += 700.0f;
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 18, records[17].end_epoch_s, {150.0f, 15.0f}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish_fit(workspace) == LearningStatus::ADVICE_READY);
  assert(fabsf(workspace.result.candidate.heat_loss_w_per_k - 200.0f) < 5.0f);
  assert(fabsf(workspace.result.candidate.zero_power_temp_c - 16.0f) < 0.5f);
  assert(workspace.result.holdout_candidate_mae_w < workspace.result.holdout_active_mae_w);
}

void test_room_and_setpoint_variation_normalizes_to_twenty_without_mutation() {
  SegmentRecord records[9];
  for (uint8_t day = 0; day < 9; ++day) {
    const float room = 18.0f + day % 5;
    records[day] = fitted_record(day, -5.0f + 2.0f * day + room - 20.0f, room);
    records[day].mean_setpoint_c = 17.0f + day % 7;
    records[day].setpoint_range_c = 3.0f;
    records[day].room_range_k = 2.0f;
    records[day].water_end_c = 38.0f;
  }
  SegmentRecord original[9];
  memcpy(original, records, sizeof(records));
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 9, records[8].end_epoch_s, {150.0f, 15.0f}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(workspace.record_count == 9);
  assert(finish_fit(workspace) == LearningStatus::ADVICE_READY);
  assert(fabsf(workspace.result.candidate.heat_loss_w_per_k - 200.0f) < 0.01f);
  assert(fabsf(workspace.result.candidate.zero_power_temp_c - 16.0f) < 0.001f);
  assert(memcmp(original, records, sizeof(records)) == 0);
}

void test_legacy_records_preserved_but_excluded_from_fit() {
  SegmentRecord records[10];
  records[0] = fitted_record(0, -12.0f);
  records[0].duration_s = 14400U;
  records[0].end_epoch_s = records[0].start_epoch_s + 14400U;
  records[0].mean_heat_w = 9000.0f;
  for (uint8_t day = 0; day < 9; ++day) records[day + 1] = fitted_record(day + 1, -5.0f + 2.0f * day);
  SegmentRecord original[10];
  memcpy(original, records, sizeof(records));
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 10, records[9].end_epoch_s, {150.0f, 15.0f}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(workspace.record_count == 9 && workspace.selected_record_indices[0] == 1);
  assert(finish_fit(workspace) == LearningStatus::ADVICE_READY);
  assert(fabsf(workspace.result.candidate.heat_loss_w_per_k - 200.0f) < 0.01f);
  assert(memcmp(original, records, sizeof(records)) == 0);
  assert(begin_advice_fit(records, 1, records[9].end_epoch_s, {150.0f, 15.0f}, {}, {}, workspace) ==
         LearningStatus::INSUFFICIENT_DATA);
}

void test_no_improvement_is_not_advice_ready() {
  SegmentRecord records[9];
  make_dataset(records);
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 9, records[8].end_epoch_s, {200.0f, 16.0f}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish_fit(workspace) == LearningStatus::NO_HOLDOUT_IMPROVEMENT);
  assert(workspace.result.candidate_available && !workspace.result.advice_ready);
}

void test_fail_closed_dataset_gates() {
  SegmentRecord records[9];
  make_dataset(records);
  const uint32_t now = records[8].end_epoch_s;
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 8, now, {150.0f, 15.0f}, {}, {}, workspace) == LearningStatus::INSUFFICIENT_DATA);
  for (auto& record : records) record.mean_outside_c = 5.0f;
  assert(begin_advice_fit(records, 9, now, {150.0f, 15.0f}, {}, {}, workspace) == LearningStatus::INSUFFICIENT_SPREAD);
  make_dataset(records);
  records[4].context_revision = 9;
  assert(begin_advice_fit(records, 9, now, {150.0f, 15.0f}, {}, {}, workspace) == LearningStatus::MIXED_CONTEXT);
  make_dataset(records);
  FitConfig config;
  config.max_raw_day_fraction = 0.10f;
  assert(begin_advice_fit(records, 9, now, {150.0f, 15.0f}, {}, config, workspace) == LearningStatus::DAY_DOMINANCE);
  make_dataset(records);
  const auto swap = records[4];
  records[4] = records[5];
  records[5] = swap;
  assert(begin_advice_fit(records, 9, now, {150.0f, 15.0f}, {}, {}, workspace) == LearningStatus::TIME_DISCONTINUITY);
  make_dataset(records);
  records[0].duration_s = 1;
  records[0].end_epoch_s = records[0].start_epoch_s + 1;
  assert(begin_advice_fit(records, 9, now, {150.0f, 15.0f}, {}, {}, workspace) == LearningStatus::SEGMENT_INELIGIBLE);
  make_dataset(records);
  records[0].room_trend_k_per_h = 0.201f;
  assert(begin_advice_fit(records, 9, now, {150.0f, 15.0f}, {}, {}, workspace) == LearningStatus::ROOM_UNSTABLE);
  make_dataset(records);
  records[0].effective_outside_profile_centi[0] = 1000;
  assert(begin_advice_fit(records, 9, now, {150.0f, 15.0f}, {}, {}, workspace) == LearningStatus::INVALID_MEASUREMENT);
  make_dataset(records);
  assert(begin_advice_fit(records, 9, now, {0.001f, -100.0f}, {}, {}, workspace) ==
         LearningStatus::INVALID_ACTIVE_MODEL);
}

void test_temperature_dominance() {
  SegmentRecord records[27];
  for (uint8_t day = 0; day < 27; ++day) records[day] = fitted_record(day, day < 3 ? -5.0f : day >= 21 ? 14.0f : 5.0f);
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 27, records[26].end_epoch_s, {150.0f, 15.0f}, {}, {}, workspace) ==
         LearningStatus::DAY_DOMINANCE);
}

void test_exact_retention_boundary_allows_sparse_season() {
  SegmentRecord records[kMaxSegmentRecords];
  for (size_t index = 0; index < kMaxSegmentRecords; ++index) {
    const uint16_t day = static_cast<uint16_t>(index * 365U / (kMaxSegmentRecords - 1U));
    records[index] = fitted_record(day, -5.0f + 2.0f * static_cast<float>(index % 10U));
  }
  AdviceFitWorkspace workspace;
  const uint32_t now = records[kMaxSegmentRecords - 1U].end_epoch_s;
  assert(now - records[0].end_epoch_s == kMaxRecordAgeS);
  assert(begin_advice_fit(records, kMaxSegmentRecords, now, {150.0f, 15.0f}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(workspace.day_count == kMaxSegmentRecords && workspace.train_count == kMaxSegmentRecords - 3U);
  assert(finish_fit(workspace) == LearningStatus::ADVICE_READY);
}
}  // namespace

int main() {
  test_all_cold_exact_linear_and_chronological_holdout();
  test_hinge_integrates_profiles_including_zero_heat_day();
  test_huber_resists_single_day_outlier();
  test_room_and_setpoint_variation_normalizes_to_twenty_without_mutation();
  test_legacy_records_preserved_but_excluded_from_fit();
  test_no_improvement_is_not_advice_ready();
  test_fail_closed_dataset_gates();
  test_temperature_dominance();
  test_exact_retention_boundary_allows_sparse_season();
  static_assert(kMaxRecordsVisitedPerHuberFit == 1536);
  static_assert(kMaxSegmentRecords == 128);
  static_assert(kMaxCalendarDays == kMaxSegmentRecords);
}
