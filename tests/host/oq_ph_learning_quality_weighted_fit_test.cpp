#include <assert.h>
#include <math.h>
#include <string.h>

#include "../../openquatt/includes/learning/oq_ph_learning_fit.h"

namespace {
using namespace oq_power_house;
using namespace oq_power_house::learning;
constexpr uint32_t kEpoch = 20000U * 86400U;

SegmentRecord day_record(uint8_t day) {
  SegmentRecord record;
  record.start_epoch_s = kEpoch + day * 86400U;
  record.end_epoch_s = record.start_epoch_s + 86400U;
  record.duration_s = 86400U;
  record.context_revision = 1;
  record.mean_room_c = record.mean_setpoint_c = 20;
  record.mean_outside_c = -5.0f + 2.0f * (day % 9);
  record.mean_heat_w = 200.0f * (16.0f - record.mean_outside_c);
  record.room_trend_k_per_h = 0;
  record.room_range_k = 0.1f;
  record.setpoint_range_c = 0;
  record.water_start_c = record.water_end_c = 30;
  for (auto& value : record.effective_outside_profile_centi)
    value = static_cast<int16_t>(lroundf(record.mean_outside_c * 100));
  record.daily_quality = {10000, 10000, 0};
  return record;
}

LearningStatus finish(AdviceFitWorkspace& workspace) {
  LearningStatus status = LearningStatus::FIT_IN_PROGRESS;
  size_t steps = 0;
  while (status == LearningStatus::FIT_IN_PROGRESS) {
    status = advance_advice_fit(workspace);
    assert(++steps <= kMaxAdviceFitSteps);
  }
  return status;
}

void less_influence_from_uncertain_biased_day() {
  SegmentRecord ordinary[18], uncertain[18];
  for (uint8_t day = 0; day < 18; ++day) ordinary[day] = day_record(day);
  ordinary[2].mean_heat_w += 150;
  memcpy(uncertain, ordinary, sizeof(ordinary));
  uncertain[2].daily_quality.heat_uncertainty_mean_w = 100;
  AdviceFitWorkspace a, b;
  assert(begin_advice_fit(ordinary, 18, ordinary[17].end_epoch_s, {150, 15}, {}, {}, a) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(begin_advice_fit(uncertain, 18, uncertain[17].end_epoch_s, {150, 15}, {}, {}, b) ==
         LearningStatus::FIT_IN_PROGRESS);
  HouseLine ordinary_line, uncertain_line;
  assert(detail::fit_huber_line(a, -1, ordinary_line));
  assert(detail::fit_huber_line(b, -1, uncertain_line));
  const double true_heat = record_house_line_power_w({200, 16}, ordinary[2]);
  assert(fabs(record_house_line_power_w(uncertain_line, ordinary[2]) - true_heat) <
         fabs(record_house_line_power_w(ordinary_line, ordinary[2]) - true_heat));
  assert(b.result.train_quality_weight_sum < a.result.train_quality_weight_sum);
  assert(detail::record_base_weight(b, 2) < detail::record_base_weight(a, 2));
}

void all_days_preview_uses_quality_and_never_changes_advice_fit() {
  SegmentRecord good[9], uncertain[9];
  for (uint8_t day = 0; day < 9; ++day) good[day] = day_record(day);
  // Bias a reserved checking day, within the Huber threshold.
  good[8].mean_heat_w += 150;
  memcpy(uncertain, good, sizeof(good));
  uncertain[8].daily_quality.heat_uncertainty_mean_w = 100;
  AdviceFitWorkspace a, b;
  assert(begin_advice_fit(good, 9, good[8].end_epoch_s, {150, 15}, {}, {}, a) == LearningStatus::FIT_IN_PROGRESS);
  assert(begin_advice_fit(uncertain, 9, uncertain[8].end_epoch_s, {150, 15}, {}, {}, b) ==
         LearningStatus::FIT_IN_PROGRESS);
  finish(a);
  finish(b);
  assert(a.result.preview.available && b.result.preview.available);
  assert(a.result.candidate.heat_loss_w_per_k == b.result.candidate.heat_loss_w_per_k);
  assert(a.result.candidate.zero_power_temp_c == b.result.candidate.zero_power_temp_c);
  const double expected = record_house_line_power_w({200, 16}, good[8]);
  assert(fabs(record_house_line_power_w(b.result.preview.line, good[8]) - expected) <
         fabs(record_house_line_power_w(a.result.preview.line, good[8]) - expected));
}

void apparent_improvement_can_fail_robust_holdout() {
  SegmentRecord records[9];
  for (uint8_t day = 0; day < 9; ++day) {
    records[day] = day_record(day);
    records[day].mean_heat_w += 100;
    records[day].daily_quality.heat_uncertainty_mean_w = 100;
  }
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 9, records[8].end_epoch_s, {200, 16}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish(workspace) == LearningStatus::NO_HOLDOUT_IMPROVEMENT);
  assert(workspace.result.candidate_available && !workspace.result.advice_ready);
  assert(workspace.result.holdout_active_mae_w - workspace.result.holdout_candidate_mae_w > 99.0f);
  assert(workspace.result.holdout_robust_improvement_w < 0);
  assert(workspace.result.holdout_robust_improvement_fraction < 0);
}

void real_robust_gain_passes_with_known_metadata() {
  SegmentRecord records[9];
  for (uint8_t day = 0; day < 9; ++day) {
    records[day] = day_record(day);
    if (day >= 6) records[day].daily_quality.heat_uncertainty_mean_w = 100;
  }
  SegmentRecord original[9];
  memcpy(original, records, sizeof(records));
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 9, records[8].end_epoch_s, {150, 15}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish(workspace) == LearningStatus::ADVICE_READY);
  assert(workspace.result.holdout_robust_improvement_w > 299);
  assert(workspace.result.holdout_robust_improvement_fraction > 0.49);
  assert(workspace.result.holdout_active_mae_upper_w > workspace.result.holdout_active_mae_w);
  assert(fabs(workspace.result.holdout_quality_weight_sum - 1.5) < 1e-6);
  assert(memcmp(original, records, sizeof(records)) == 0);

  for (auto& record : records) record.daily_quality.heat_uncertainty_mean_w = 0;
  assert(begin_advice_fit(records, 9, records[8].end_epoch_s, {200, 16}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish(workspace) == LearningStatus::NO_HOLDOUT_IMPROVEMENT);
  assert(workspace.result.holdout_active_mae_upper_w == 0);
  assert(!isfinite(workspace.result.holdout_robust_improvement_fraction));
  assert(!workspace.result.advice_ready);
}

void legacy_is_train_only_and_never_crosses_holdout_boundary() {
  SegmentRecord records[12];
  for (uint8_t day = 0; day < 12; ++day) {
    records[day] = day_record(day);
    if (day < 6 || day == 7 || day == 9 || day == 11) records[day].daily_quality = {};
    if (day == 7 || day == 9 || day == 11) records[day].mean_heat_w += 2000;
  }
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 12, records[11].end_epoch_s, {150, 15}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(workspace.train_count == 6 && workspace.record_count == 9);
  assert(workspace.result.legacy_train_segments == 6);
  assert(workspace.result.train_quality_weight_sum == 3);
  const uint8_t holdout[] = {6, 8, 10};
  for (size_t index = 0; index < 3; ++index) {
    assert(workspace.selected_record_indices[6 + index] == holdout[index]);
    assert(daily_record_holdout_eligible(detail::fit_record(workspace, 6 + index), {}));
  }
  assert(finish(workspace) == LearningStatus::ADVICE_READY);

  for (uint8_t day = 0; day < 12; ++day) {
    records[day] = day_record(day);
    records[day].daily_quality = {};
  }
  assert(begin_advice_fit(records, 12, records[11].end_epoch_s, {150, 15}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish(workspace) == LearningStatus::INSUFFICIENT_DATA);
  assert(workspace.result.candidate_available && !workspace.result.advice_ready);
  assert(workspace.result.train_days == 12 && workspace.result.holdout_days == 0);
  records[10].daily_quality = records[11].daily_quality = {10000, 10000, 0};
  assert(begin_advice_fit(records, 12, records[11].end_epoch_s, {150, 15}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish(workspace) == LearningStatus::INSUFFICIENT_DATA);
  assert(workspace.result.candidate_available && !workspace.result.advice_ready);
  assert(workspace.result.train_days == 10 && workspace.result.holdout_days == 2);
}

void retained_low_quality_days_are_excluded_without_poisoning_good_days() {
  SegmentRecord records[12];
  for (uint8_t day = 0; day < 12; ++day) records[day] = day_record(day);
  records[9].daily_quality.heat_uncertainty_mean_w = 101;
  records[10].daily_quality.temperature_coverage_bp = 9899;
  records[11].room_trend_k_per_h = 0.3f;
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 12, records[11].end_epoch_s, {150, 15}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(workspace.train_count == 6 && workspace.record_count == 9);
  assert(finish(workspace) == LearningStatus::ADVICE_READY);
}

void weighted_day_dominance_is_checked_in_addition_to_counts() {
  SegmentRecord records[9];
  for (uint8_t day = 0; day < 9; ++day) {
    records[day] = day_record(day);
    if (day > 0 && day < 6) records[day].daily_quality.heat_uncertainty_mean_w = 100;
  }
  FitConfig config;
  // A deliberately tight test-only value: raw1/6 passes, weighted1/3.5 fails.
  config.max_raw_day_fraction = 0.19f;
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 9, records[8].end_epoch_s, {150, 15}, {}, config, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish(workspace) == LearningStatus::DAY_DOMINANCE);
  assert(workspace.result.candidate_available && !workspace.result.advice_ready);
  assert(workspace.result.max_train_day_weight_fraction > 0.28f);
}

void maximum_record_selection_stays_bounded() {
  SegmentRecord records[kMaxSegmentRecords];
  for (size_t day = 0; day < kMaxSegmentRecords; ++day) records[day] = day_record(static_cast<uint8_t>(day));
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, kMaxSegmentRecords, records[kMaxSegmentRecords - 1].end_epoch_s, {150, 15}, {}, {},
                          workspace) == LearningStatus::FIT_IN_PROGRESS);
  assert(workspace.record_count == kMaxSegmentRecords && workspace.day_count == kMaxCalendarDays);
  assert(workspace.train_count == kMaxSegmentRecords - 3);
  for (size_t index = 0; index < workspace.record_count; ++index)
    assert(workspace.selected_record_indices[index] == index);
}
void exact_five_percent_and_fifty_watt_bound_is_inclusive() {
  SegmentRecord records[9];
  for (uint8_t day = 0; day < 9; ++day) {
    records[day] = day_record(day);
    if (day >= 6) {
      records[day].mean_heat_w = record_house_line_power_w({200, 16.25f}, records[day]) - 900;
      records[day].daily_quality.heat_uncertainty_mean_w = 100;
    }
  }
  AdviceFitWorkspace workspace;
  assert(begin_advice_fit(records, 9, records[8].end_epoch_s, {200, 16.25f}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish(workspace) == LearningStatus::ADVICE_READY);
  assert(workspace.result.holdout_robust_improvement_w == 50);
  assert(workspace.result.holdout_active_mae_upper_w == 1000);
  assert(workspace.result.holdout_robust_improvement_fraction == 0.05);
  assert(workspace.result.quality_policy_version == 1);
  // Same50W gain, but upper error1001W: strictly below5% must fail.
  for (uint8_t day = 6; day < 9; ++day) records[day].mean_heat_w -= 1;
  assert(begin_advice_fit(records, 9, records[8].end_epoch_s, {200, 16.25f}, {}, {}, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish(workspace) == LearningStatus::NO_HOLDOUT_IMPROVEMENT);
  assert(workspace.result.holdout_robust_improvement_w == 50);
  assert(workspace.result.holdout_robust_improvement_fraction < 0.05);
  // Disable only the fractional test in this fixture, isolating the50W gate.
  FitConfig gate;
  gate.min_holdout_improvement_fraction = 0;
  assert(begin_advice_fit(records, 9, records[8].end_epoch_s, {200, 16.249f}, {}, gate, workspace) ==
         LearningStatus::FIT_IN_PROGRESS);
  assert(finish(workspace) == LearningStatus::NO_HOLDOUT_IMPROVEMENT);
  assert(workspace.result.holdout_robust_improvement_w < 50);
}

}  // namespace

int main() {
  exact_five_percent_and_fifty_watt_bound_is_inclusive();
  less_influence_from_uncertain_biased_day();
  all_days_preview_uses_quality_and_never_changes_advice_fit();
  apparent_improvement_can_fail_robust_holdout();
  real_robust_gain_passes_with_known_metadata();
  legacy_is_train_only_and_never_crosses_holdout_boundary();
  retained_low_quality_days_are_excluded_without_poisoning_good_days();
  weighted_day_dominance_is_checked_in_addition_to_counts();
  maximum_record_selection_stays_bounded();
}
