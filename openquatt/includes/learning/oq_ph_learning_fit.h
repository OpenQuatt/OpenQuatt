#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include <new>

#include "oq_ph_daily_record_quality.h"

namespace oq_power_house::learning {

constexpr size_t kMaxRecordsVisitedPerHuberFit = 2U * static_cast<size_t>(kMaxHuberPasses) * kMaxSegmentRecords;
constexpr size_t kMaxPreparationSortComparisons = kMaxSegmentRecords * (kMaxSegmentRecords - 1U) / 2U;
constexpr size_t kMaxAdviceFitSteps = kMaxCalendarDays + 3U;

struct FitConfig {
  uint8_t huber_passes = kMaxHuberPasses;
  float huber_delta_w = 300.0f;
  uint8_t min_train_segments = 6;
  uint8_t min_train_days = 6;
  uint8_t min_holdout_segments = 3;
  uint8_t min_holdout_days = 3;
  float min_outside_p90_p10_k = 6.0f;
  uint8_t min_segments_per_temperature_bin = 2;
  float max_temperature_bin_fraction = 0.67f;
  float max_raw_day_fraction = 0.34f;
  float min_heat_loss_w_per_k = 10.0f;
  float max_heat_loss_w_per_k = 2000.0f;
  float min_zero_power_temp_c = 5.0f;
  float max_zero_power_temp_c = 30.0f;
  double min_holdout_improvement_fraction = 0.05;
  float min_holdout_improvement_w = 50.0f;
  float max_lodo_heat_loss_relative_deviation = 0.20f;
  float max_lodo_zero_power_range_k = 2.0f;
};

// Display-only fit over all usable days. Never an independently checked model.
struct PreviewLine {
  bool available = false;
  HouseLine line;
  uint8_t segments = 0;
  uint8_t days = 0;
  float min_c = NAN;
  float max_c = NAN;
  float cov_00 = NAN;
  float cov_01 = NAN;
  float cov_11 = NAN;
};

struct AdviceResult {
  LearningStatus status = LearningStatus::INSUFFICIENT_DATA;
  bool candidate_available = false;
  bool advice_ready = false;
  // Counts and training percentiles belong to a fully checked record view.
  bool metadata_prepared = false;
  uint16_t algorithm_version = kLearningAlgorithmVersion;
  uint16_t quality_policy_version = kDailyQualityPolicyVersion;
  uint32_t context_revision = 0;
  HouseLine candidate;
  PreviewLine preview;
  uint8_t train_segments = 0;
  uint8_t holdout_segments = 0;
  uint8_t train_days = 0;
  uint8_t holdout_days = 0;
  uint8_t legacy_train_segments = 0;
  float train_quality_weight_sum = 0.0f;
  float holdout_quality_weight_sum = 0.0f;
  float max_train_day_weight_fraction = NAN;
  float observed_temp_min_c = NAN;
  float observed_temp_max_c = NAN;
  // Actual outdoor range shared with 1R1C; fit/plot coordinates are normalized.
  float validated_temp_min_c = NAN;
  float validated_temp_max_c = NAN;
  float train_p10_c = NAN;
  float train_p90_c = NAN;
  float train_min_c = NAN;
  float train_max_c = NAN;
  float holdout_candidate_mae_w = NAN;
  float holdout_active_mae_w = NAN;
  float holdout_candidate_signed_bias_w = NAN;  // Predicted minus measured.
  float holdout_active_signed_bias_w = NAN;
  float holdout_candidate_signed_bias_by_temp_w[3] = {NAN, NAN, NAN};
  float holdout_active_signed_bias_by_temp_w[3] = {NAN, NAN, NAN};
  float holdout_improvement_fraction = NAN;
  double holdout_robust_improvement_w = NAN;
  double holdout_robust_improvement_fraction = NAN;
  double holdout_active_mae_upper_w = NAN;
  float lodo_heat_loss_min_w_per_k = NAN;
  float lodo_heat_loss_max_w_per_k = NAN;
  float lodo_zero_power_min_c = NAN;
  float lodo_zero_power_max_c = NAN;
  float max_abs_room_trend_k_per_h = NAN;
  float max_abs_room_trend_per_heat_k_per_h_w = NAN;
  float candidate_cov_00 = NAN;
  float candidate_cov_01 = NAN;
  float candidate_cov_11 = NAN;
};

enum class FitPhase : uint8_t { IDLE = 0, FIT_PREVIEW, FIT_BASE, FIT_LODO, FINALIZE, DONE };

// Caller-owned workspace. Keep it alive, together with the immutable record array,
// until advance_advice_fit() returns a status other than FIT_IN_PROGRESS.
struct AdviceFitWorkspace {
  const SegmentRecord* records = nullptr;
  size_t record_count = 0;
  size_t input_record_count = 0;
  size_t train_count = 0;
  uint32_t now_epoch_s = 0;
  HouseLine active_line;
  QualityConfig quality_config;
  FitConfig config;
  FitPhase phase = FitPhase::IDLE;
  LearningStatus advice_blocker = LearningStatus::OK;
  LearningStatus preparation_status = LearningStatus::OK;
  uint8_t day_count = 0;
  uint8_t train_day_count = 0;
  uint8_t next_lodo_day = 0;
  uint32_t day_ids[kMaxCalendarDays]{};
  uint8_t day_record_counts[kMaxCalendarDays]{};
  uint8_t selected_record_indices[kMaxSegmentRecords]{};
  uint8_t record_day_index[kMaxSegmentRecords]{};
  float sorted_train_temperatures[kMaxSegmentRecords]{};
  // Zero excludes a raw row; otherwise normalize its weight within its day.
  uint8_t preview_day_record_counts[kMaxSegmentRecords]{};
  AdviceResult result;
};

inline bool valid_fit_config(const FitConfig& config) {
  return config.huber_passes >= 1 && config.huber_passes <= kMaxHuberPasses && isfinite(config.huber_delta_w) &&
         config.huber_delta_w > 0.0f && config.huber_delta_w <= 10000.0f && config.min_train_segments >= 2 &&
         config.min_train_segments <= kMaxSegmentRecords && config.min_train_days >= 2 &&
         config.min_train_days < kMaxCalendarDays && config.min_holdout_segments >= 1 &&
         config.min_holdout_segments < kMaxSegmentRecords && config.min_holdout_days >= 1 &&
         config.min_holdout_days < kMaxCalendarDays && isfinite(config.min_outside_p90_p10_k) &&
         config.min_outside_p90_p10_k > 0.0f && config.min_outside_p90_p10_k <= 40.0f &&
         config.min_segments_per_temperature_bin >= 1 && config.min_segments_per_temperature_bin <= 16 &&
         isfinite(config.max_temperature_bin_fraction) && config.max_temperature_bin_fraction > 0.0f &&
         config.max_temperature_bin_fraction <= 1.0f && isfinite(config.max_raw_day_fraction) &&
         config.max_raw_day_fraction > 0.0f && config.max_raw_day_fraction <= 1.0f &&
         isfinite(config.min_heat_loss_w_per_k) && isfinite(config.max_heat_loss_w_per_k) &&
         config.min_heat_loss_w_per_k > 0.0f && config.min_heat_loss_w_per_k < config.max_heat_loss_w_per_k &&
         config.max_heat_loss_w_per_k <= 10000.0f && isfinite(config.min_zero_power_temp_c) &&
         isfinite(config.max_zero_power_temp_c) && config.min_zero_power_temp_c < config.max_zero_power_temp_c &&
         config.min_zero_power_temp_c >= -20.0f && config.max_zero_power_temp_c <= 60.0f &&
         isfinite(config.min_holdout_improvement_fraction) && config.min_holdout_improvement_fraction >= 0.0f &&
         config.min_holdout_improvement_fraction <= 1.0f && isfinite(config.min_holdout_improvement_w) &&
         config.min_holdout_improvement_w >= 0.0f && config.min_holdout_improvement_w <= 10000.0f &&
         isfinite(config.max_lodo_heat_loss_relative_deviation) &&
         config.max_lodo_heat_loss_relative_deviation >= 0.0f && config.max_lodo_heat_loss_relative_deviation <= 1.0f &&
         isfinite(config.max_lodo_zero_power_range_k) && config.max_lodo_zero_power_range_k >= 0.0f &&
         config.max_lodo_zero_power_range_k <= 20.0f;
}

namespace detail {

inline const SegmentRecord& fit_record(const AdviceFitWorkspace& workspace, size_t index) {
  return workspace.records[workspace.selected_record_indices[index]];
}

inline uint32_t record_day(const SegmentRecord& record) { return record.start_epoch_s / 86400U; }

inline bool plausible_line(const HouseLine& line, const FitConfig& config) {
  return valid_house_line(line) && line.heat_loss_w_per_k >= config.min_heat_loss_w_per_k &&
         line.heat_loss_w_per_k <= config.max_heat_loss_w_per_k &&
         line.zero_power_temp_c >= config.min_zero_power_temp_c &&
         line.zero_power_temp_c <= config.max_zero_power_temp_c;
}

inline double record_base_weight(const AdviceFitWorkspace& workspace, size_t index) {
  const uint8_t day_index = workspace.record_day_index[index];
  return daily_record_quality_weight(fit_record(workspace, index), workspace.quality_config) /
         workspace.day_record_counts[day_index];
}

// The day prediction averages max(0, H * (T0 - effective temperature)).
// For a fixed set of temperatures below T0 this is linear in H*T0 and H.
// Re-evaluate that set on each of the existing bounded Huber passes.
inline bool fit_huber_line(const AdviceFitWorkspace& workspace, int omitted_day_index, HouseLine& output,
                           bool preview = false, float* cov_00 = nullptr, float* cov_01 = nullptr,
                           float* cov_11 = nullptr) {
  HouseLine current;
  for (uint8_t pass = 0; pass < workspace.config.huber_passes; ++pass) {
    double aa = 0.0, ab = 0.0, bb = 0.0, ay = 0.0, by = 0.0;
    const size_t count = preview ? workspace.input_record_count : workspace.train_count;
    for (size_t index = 0; index < count; ++index) {
      if (preview ? workspace.preview_day_record_counts[index] == 0
                  : workspace.record_day_index[index] == omitted_day_index)
        continue;
      const SegmentRecord& record = preview ? workspace.records[index] : fit_record(workspace, index);
      const float correction = effective_outside_c(record) - profile_mean_c(record);
      double a = 0.0, b = 0.0;
      for (size_t point = 0; point < kDailyTemperatureProfileSize; ++point) {
        const double temperature = profile_temperature_c(record, point, correction);
        if (pass == 0 || temperature < current.zero_power_temp_c) {
          a += 1.0 / kDailyTemperatureProfileSize;
          b += temperature / kDailyTemperatureProfileSize;
        }
      }
      double weight = preview ? daily_record_quality_weight(record, workspace.quality_config) /
                                    workspace.preview_day_record_counts[index]
                              : record_base_weight(workspace, index);
      if (pass > 0) {
        const double residual = fabs(record_house_line_power_w(current, record) - record.mean_heat_w);
        if (residual > workspace.config.huber_delta_w) weight *= workspace.config.huber_delta_w / residual;
      }
      aa += weight * a * a;
      ab += weight * a * b;
      bb += weight * b * b;
      ay += weight * a * record.mean_heat_w;
      by += weight * b * record.mean_heat_w;
    }
    const double determinant = aa * bb - ab * ab;
    if (!(determinant > 1e-9)) return false;
    const double heat_loss = (ab * ay - aa * by) / determinant;
    const double intercept = (bb * ay - ab * by) / determinant;
    if (!(heat_loss > 0.0) || !isfinite(heat_loss) || !isfinite(intercept)) return false;
    current = {static_cast<float>(heat_loss), static_cast<float>(intercept / heat_loss)};
    if (!valid_house_line(current)) return false;
  }
  if (cov_00 != nullptr && cov_01 != nullptr && cov_11 != nullptr) {
    double last_aa = 0.0, last_ab = 0.0, last_bb = 0.0, rss = 0.0, weight_sum = 0.0;
    size_t active_records = 0;
    const size_t count = preview ? workspace.input_record_count : workspace.train_count;
    for (size_t index = 0; index < count; ++index) {
      if (preview ? workspace.preview_day_record_counts[index] == 0
                  : workspace.record_day_index[index] == omitted_day_index)
        continue;
      const SegmentRecord& record = preview ? workspace.records[index] : fit_record(workspace, index);
      const float correction = effective_outside_c(record) - profile_mean_c(record);
      double a = 0.0, b = 0.0;
      for (size_t point = 0; point < kDailyTemperatureProfileSize; ++point) {
        const double temperature = profile_temperature_c(record, point, correction);
        if (temperature < current.zero_power_temp_c) {
          a += 1.0 / kDailyTemperatureProfileSize;
          b += temperature / kDailyTemperatureProfileSize;
        }
      }
      double weight = preview ? daily_record_quality_weight(record, workspace.quality_config) /
                                    workspace.preview_day_record_counts[index]
                              : record_base_weight(workspace, index);
      const double residual = record_house_line_power_w(current, record) - record.mean_heat_w;
      if (fabs(residual) > workspace.config.huber_delta_w) weight *= workspace.config.huber_delta_w / fabs(residual);
      last_aa += weight * a * a;
      last_ab += weight * a * b;
      last_bb += weight * b * b;
      rss += weight * residual * residual;
      if (weight > 0.0) {
        weight_sum += weight;
        ++active_records;
      }
    }
    const double det = last_aa * last_bb - last_ab * last_ab;
    if (det > 1e-9 && weight_sum > 0.0 && active_records > 2) {
      const double s2 =
          (rss / weight_sum) * (static_cast<double>(active_records) / (static_cast<double>(active_records) - 2.0));
      *cov_00 = static_cast<float>(s2 * last_bb / det);
      *cov_01 = static_cast<float>(s2 * last_ab / det);
      *cov_11 = static_cast<float>(s2 * last_aa / det);
    }
  }
  output = current;
  return true;
}

inline LearningStatus prepare_workspace(AdviceFitWorkspace& workspace) {
  if (workspace.records == nullptr || workspace.record_count == 0 || workspace.record_count > kMaxSegmentRecords ||
      workspace.now_epoch_s == 0 || !valid_quality_config(workspace.quality_config) ||
      !valid_fit_config(workspace.config))
    return LearningStatus::INVALID_CONFIGURATION;
  if (!plausible_line(workspace.active_line, workspace.config)) return LearningStatus::INVALID_ACTIVE_MODEL;
  uint32_t previous_end = 0;
  uint32_t context_revision = 0;
  size_t selected_count = 0;
  // Retain legacy measurements for diagnostics; only normalized full days
  // enter the new model and its independent validation.
  for (size_t index = 0; index < workspace.record_count; ++index) {
    const SegmentRecord& record = workspace.records[index];
    const LearningStatus record_status = validate_segment_record_structure(record, workspace.quality_config);
    if (record_status != LearningStatus::OK) return record_status;
    if (record.end_epoch_s > workspace.now_epoch_s || workspace.now_epoch_s - record.end_epoch_s > kMaxRecordAgeS)
      return LearningStatus::STALE_DATA;
    if (index > 0 && record.start_epoch_s < previous_end) return LearningStatus::TIME_DISCONTINUITY;
    if (index == 0)
      context_revision = record.context_revision;
    else if (record.context_revision != context_revision)
      return LearningStatus::MIXED_CONTEXT;
    previous_end = record.end_epoch_s;
    if (!daily_record_fit_eligible(record, workspace.quality_config)) continue;
    workspace.selected_record_indices[selected_count++] = static_cast<uint8_t>(index);
  }
  if (selected_count == 0) return LearningStatus::INSUFFICIENT_DATA;

  // Preserve an all-days view before the independent split compacts its indices.
  // Counts live in the caller-owned PSRAM workspace; the source array is immutable.
  auto& preview = workspace.result.preview;
  preview.segments = static_cast<uint8_t>(selected_count);
  preview.min_c = INFINITY;
  preview.max_c = -INFINITY;
  for (size_t start = 0; start < selected_count;) {
    size_t end = start + 1;
    const auto day = record_day(workspace.records[workspace.selected_record_indices[start]]);
    while (end < selected_count && record_day(workspace.records[workspace.selected_record_indices[end]]) == day) ++end;
    ++preview.days;
    for (size_t index = start; index < end; ++index) {
      const auto original = workspace.selected_record_indices[index];
      workspace.preview_day_record_counts[original] = static_cast<uint8_t>(end - start);
      preview.min_c = fminf(preview.min_c, effective_outside_c(workspace.records[original]));
      preview.max_c = fmaxf(preview.max_c, effective_outside_c(workspace.records[original]));
    }
    start = end;
  }

  // The latest eligible known days define one chronological boundary. Legacy
  // assumptions can train only before it, never validate or leak from the future.
  uint32_t boundary_day = UINT32_MAX;
  uint32_t previous_known_day = UINT32_MAX;
  size_t known_segments = 0;
  size_t known_days = 0;
  for (size_t reverse = selected_count; reverse > 0; --reverse) {
    const SegmentRecord& record = workspace.records[workspace.selected_record_indices[reverse - 1]];
    if (!daily_record_holdout_eligible(record, workspace.quality_config)) continue;
    ++known_segments;
    const uint32_t day = record_day(record);
    boundary_day = day;
    if (day != previous_known_day) {
      ++known_days;
      previous_known_day = day;
    }
    if (known_segments >= workspace.config.min_holdout_segments && known_days >= workspace.config.min_holdout_days) {
      break;
    }
  }
  if (known_segments < workspace.config.min_holdout_segments || known_days < workspace.config.min_holdout_days)
    workspace.advice_blocker = LearningStatus::INSUFFICIENT_DATA;
  workspace.record_count = 0;
  for (size_t index = 0; index < selected_count; ++index) {
    const uint8_t original_index = workspace.selected_record_indices[index];
    const SegmentRecord& record = workspace.records[original_index];
    if (record_day(record) >= boundary_day && !daily_record_holdout_eligible(record, workspace.quality_config))
      continue;
    workspace.selected_record_indices[workspace.record_count++] = original_index;
    if (record_day(record) < boundary_day) ++workspace.train_count;
  }
  workspace.result.observed_temp_min_c = INFINITY;
  workspace.result.observed_temp_max_c = -INFINITY;
  workspace.result.max_abs_room_trend_k_per_h = 0.0f;
  workspace.result.max_abs_room_trend_per_heat_k_per_h_w = 0.0f;
  for (size_t index = 0; index < workspace.record_count; ++index) {
    const SegmentRecord& record = fit_record(workspace, index);
    workspace.result.observed_temp_min_c = fminf(workspace.result.observed_temp_min_c, effective_outside_c(record));
    workspace.result.observed_temp_max_c = fmaxf(workspace.result.observed_temp_max_c, effective_outside_c(record));
    workspace.result.max_abs_room_trend_k_per_h =
        fmaxf(workspace.result.max_abs_room_trend_k_per_h, fabsf(record.room_trend_k_per_h));
    workspace.result.max_abs_room_trend_per_heat_k_per_h_w =
        fmaxf(workspace.result.max_abs_room_trend_per_heat_k_per_h_w,
              fabsf(record.room_trend_k_per_h) / fmaxf(1.0f, fabsf(record.mean_heat_w)));
    const uint32_t day = record_day(record);
    if (workspace.day_count == 0 || workspace.day_ids[workspace.day_count - 1] != day) {
      if (workspace.day_count >= kMaxCalendarDays) return LearningStatus::INVALID_CONFIGURATION;
      workspace.day_ids[workspace.day_count] = day;
      ++workspace.day_count;
    }
    workspace.record_day_index[index] = workspace.day_count - 1;
    ++workspace.day_record_counts[workspace.day_count - 1];
  }
  workspace.result.context_revision = context_revision;
  for (uint8_t day = 0; day < workspace.day_count; ++day)
    if (workspace.day_ids[day] < boundary_day) ++workspace.train_day_count;
  workspace.result.train_segments = static_cast<uint8_t>(workspace.train_count);
  workspace.result.holdout_segments = static_cast<uint8_t>(workspace.record_count - workspace.train_count);
  workspace.result.train_days = workspace.train_day_count;
  workspace.result.holdout_days = workspace.day_count - workspace.train_day_count;
  const bool insufficient_training = workspace.train_count < workspace.config.min_train_segments ||
                                     workspace.train_day_count < workspace.config.min_train_days;
  if (insufficient_training ||
      workspace.day_count < workspace.config.min_train_days + workspace.config.min_holdout_days ||
      workspace.record_count - workspace.train_count < workspace.config.min_holdout_segments)
    workspace.advice_blocker = LearningStatus::INSUFFICIENT_DATA;
  if (workspace.train_count == 0) return LearningStatus::INSUFFICIENT_DATA;

  uint8_t max_day_records = 0;
  for (uint8_t day = 0; day < workspace.train_day_count; ++day)
    if (workspace.day_record_counts[day] > max_day_records) max_day_records = workspace.day_record_counts[day];
  if (workspace.advice_blocker == LearningStatus::OK &&
      static_cast<float>(max_day_records) / workspace.train_count > workspace.config.max_raw_day_fraction)
    workspace.advice_blocker = LearningStatus::DAY_DOMINANCE;

  double train_weight = 0.0;
  double maximum_day_weight = 0.0;
  double day_weight = 0.0;
  uint8_t previous_day = 0;
  for (size_t index = 0; index < workspace.train_count; ++index) {
    const uint8_t day = workspace.record_day_index[index];
    if (day != previous_day) {
      maximum_day_weight = fmax(maximum_day_weight, day_weight);
      day_weight = 0.0;
      previous_day = day;
    }
    const double weight = record_base_weight(workspace, index);
    train_weight += weight;
    day_weight += weight;
    if (legacy_daily_record_quality(fit_record(workspace, index).daily_quality))
      ++workspace.result.legacy_train_segments;
  }
  maximum_day_weight = fmax(maximum_day_weight, day_weight);
  if (!(train_weight > 0.0) || !isfinite(train_weight)) return LearningStatus::INVALID_MEASUREMENT;
  workspace.result.train_quality_weight_sum = static_cast<float>(train_weight);
  workspace.result.max_train_day_weight_fraction = static_cast<float>(maximum_day_weight / train_weight);
  if (workspace.advice_blocker == LearningStatus::OK &&
      maximum_day_weight / train_weight > workspace.config.max_raw_day_fraction)
    workspace.advice_blocker = LearningStatus::DAY_DOMINANCE;

  for (size_t index = 0; index < workspace.train_count; ++index) {
    const float value = effective_outside_c(fit_record(workspace, index));
    size_t insert_at = index;
    while (insert_at > 0 && workspace.sorted_train_temperatures[insert_at - 1] > value) {
      workspace.sorted_train_temperatures[insert_at] = workspace.sorted_train_temperatures[insert_at - 1];
      --insert_at;
    }
    workspace.sorted_train_temperatures[insert_at] = value;
  }
  const size_t p10_index = (workspace.train_count - 1U) / 10U;
  const size_t p90_index = (9U * (workspace.train_count - 1U) + 9U) / 10U;
  workspace.result.train_p10_c = workspace.sorted_train_temperatures[p10_index];
  workspace.result.train_p90_c = workspace.sorted_train_temperatures[p90_index];
  workspace.result.train_min_c = workspace.sorted_train_temperatures[0];
  workspace.result.train_max_c = workspace.sorted_train_temperatures[workspace.train_count - 1U];
  workspace.result.metadata_prepared = true;
  if (insufficient_training) return LearningStatus::INSUFFICIENT_DATA;
  const float spread = workspace.result.train_p90_c - workspace.result.train_p10_c;
  if (spread < workspace.config.min_outside_p90_p10_k) {
    if (workspace.advice_blocker == LearningStatus::OK) workspace.advice_blocker = LearningStatus::INSUFFICIENT_SPREAD;
    return LearningStatus::OK;
  }

  uint8_t bins[3]{};
  const float bin_width = (workspace.result.train_p90_c - workspace.result.train_p10_c) / 3.0f;
  for (size_t index = 0; index < workspace.train_count; ++index) {
    const float temperature = effective_outside_c(fit_record(workspace, index));
    uint8_t bin = 0;
    if (temperature >= workspace.result.train_p90_c)
      bin = 2;
    else if (temperature > workspace.result.train_p10_c)
      bin = static_cast<uint8_t>((temperature - workspace.result.train_p10_c) / bin_width);
    if (bin > 2) bin = 2;
    ++bins[bin];
  }
  if (workspace.advice_blocker == LearningStatus::OK) {
    for (uint8_t bin : bins)
      if (bin < workspace.config.min_segments_per_temperature_bin)
        workspace.advice_blocker = LearningStatus::INSUFFICIENT_SPREAD;
    for (uint8_t bin : bins)
      if (workspace.advice_blocker == LearningStatus::OK &&
          static_cast<float>(bin) / workspace.train_count > workspace.config.max_temperature_bin_fraction)
        workspace.advice_blocker = LearningStatus::DAY_DOMINANCE;
  }
  return LearningStatus::OK;
}

inline bool evaluate_holdout(AdviceFitWorkspace& workspace) {
  double duration_sum = 0.0;
  double candidate_abs_error = 0.0;
  double active_abs_error = 0.0;
  double candidate_bias = 0.0;
  double active_bias = 0.0;
  double robust_weight_sum = 0.0;
  double robust_gain_sum = 0.0;
  double active_upper_sum = 0.0;
  double quality_weight_sum = 0.0;
  double bin_duration[3]{};
  double candidate_bin_bias[3]{};
  double active_bin_bias[3]{};
  workspace.result.validated_temp_min_c = INFINITY;
  workspace.result.validated_temp_max_c = -INFINITY;
  workspace.result.holdout_robust_improvement_w = NAN;
  workspace.result.holdout_robust_improvement_fraction = NAN;
  workspace.result.holdout_active_mae_upper_w = NAN;
  workspace.result.holdout_quality_weight_sum = 0.0f;
  for (size_t index = workspace.train_count; index < workspace.record_count; ++index) {
    const SegmentRecord& record = fit_record(workspace, index);
    if (!daily_record_holdout_eligible(record, workspace.quality_config)) return false;
    const double weight = record.duration_s;
    const double candidate_w = record_house_line_power_w(workspace.result.candidate, record);
    const double active_w = record_house_line_power_w(workspace.active_line, record);
    const double candidate_error = candidate_w - record.mean_heat_w;
    const double active_error = active_w - record.mean_heat_w;
    const double uncertainty_w = daily_record_assumed_heat_uncertainty_w(record);
    const double minimum_gain =
        worst_case_absolute_error_gain_w(active_w, candidate_w, record.mean_heat_w, uncertainty_w);
    const double quality_weight = record_base_weight(workspace, index);
    const double robust_weight = weight * quality_weight;
    if (!isfinite(candidate_error) || !isfinite(active_error) || !isfinite(minimum_gain) || !isfinite(robust_weight) ||
        !(robust_weight > 0.0))
      return false;
    robust_weight_sum += robust_weight;
    robust_gain_sum += robust_weight * minimum_gain;
    active_upper_sum += robust_weight * (fabs(active_error) + uncertainty_w);
    quality_weight_sum += quality_weight;
    duration_sum += weight;
    candidate_abs_error += weight * fabs(candidate_error);
    active_abs_error += weight * fabs(active_error);
    candidate_bias += weight * candidate_error;
    active_bias += weight * active_error;
    workspace.result.validated_temp_min_c = fminf(workspace.result.validated_temp_min_c, record.mean_outside_c);
    workspace.result.validated_temp_max_c = fmaxf(workspace.result.validated_temp_max_c, record.mean_outside_c);
    uint8_t bin = 0;
    if (effective_outside_c(record) >= workspace.result.train_p90_c)
      bin = 2;
    else if (effective_outside_c(record) > workspace.result.train_p10_c)
      bin = static_cast<uint8_t>(3.0f * (effective_outside_c(record) - workspace.result.train_p10_c) /
                                 (workspace.result.train_p90_c - workspace.result.train_p10_c));
    if (bin > 2) bin = 2;
    bin_duration[bin] += weight;
    candidate_bin_bias[bin] += weight * candidate_error;
    active_bin_bias[bin] += weight * active_error;
  }
  if (!(duration_sum > 0.0) || !(robust_weight_sum > 0.0) || !isfinite(robust_gain_sum) || !isfinite(active_upper_sum))
    return false;
  workspace.result.holdout_candidate_mae_w = static_cast<float>(candidate_abs_error / duration_sum);
  workspace.result.holdout_active_mae_w = static_cast<float>(active_abs_error / duration_sum);
  workspace.result.holdout_candidate_signed_bias_w = static_cast<float>(candidate_bias / duration_sum);
  workspace.result.holdout_active_signed_bias_w = static_cast<float>(active_bias / duration_sum);
  for (uint8_t bin = 0; bin < 3; ++bin) {
    if (bin_duration[bin] > 0.0) {
      workspace.result.holdout_candidate_signed_bias_by_temp_w[bin] =
          static_cast<float>(candidate_bin_bias[bin] / bin_duration[bin]);
      workspace.result.holdout_active_signed_bias_by_temp_w[bin] =
          static_cast<float>(active_bin_bias[bin] / bin_duration[bin]);
    }
  }
  if (workspace.result.holdout_active_mae_w > 0.0f) {
    workspace.result.holdout_improvement_fraction =
        (workspace.result.holdout_active_mae_w - workspace.result.holdout_candidate_mae_w) /
        workspace.result.holdout_active_mae_w;
  }
  workspace.result.holdout_quality_weight_sum = static_cast<float>(quality_weight_sum);
  workspace.result.holdout_robust_improvement_w = robust_gain_sum / robust_weight_sum;
  workspace.result.holdout_active_mae_upper_w = active_upper_sum / robust_weight_sum;
  if (workspace.result.holdout_active_mae_upper_w > 0.0)
    workspace.result.holdout_robust_improvement_fraction =
        workspace.result.holdout_robust_improvement_w / workspace.result.holdout_active_mae_upper_w;
  return true;
}

}  // namespace detail

inline LearningStatus begin_advice_fit(const SegmentRecord* records, size_t record_count, uint32_t now_epoch_s,
                                       const HouseLine& active_line, const QualityConfig& quality_config,
                                       const FitConfig& config, AdviceFitWorkspace& workspace) {
  workspace.~AdviceFitWorkspace();
  new (&workspace) AdviceFitWorkspace();
  workspace.records = records;
  workspace.record_count = record_count;
  workspace.input_record_count = record_count;
  workspace.now_epoch_s = now_epoch_s;
  workspace.active_line = active_line;
  workspace.quality_config = quality_config;
  workspace.config = config;
  const LearningStatus status = detail::prepare_workspace(workspace);
  workspace.preparation_status = status;
  const bool preview_ready = workspace.result.preview.segments >= config.min_train_segments &&
                             workspace.result.preview.days >= config.min_train_days;
  if (status != LearningStatus::OK && !(status == LearningStatus::INSUFFICIENT_DATA && preview_ready)) {
    workspace.result.status = status;
    workspace.phase = FitPhase::DONE;
    return status;
  }
  workspace.phase = FitPhase::FIT_PREVIEW;
  workspace.result.status = LearningStatus::FIT_IN_PROGRESS;
  return LearningStatus::FIT_IN_PROGRESS;
}

// Each call performs at most one Huber fit: at most
// kMaxRecordsVisitedPerHuberFit record visits. Preparation is separate and bounded
// by kMaxPreparationSortComparisons; holdout finalization visits at most kMaxSegmentRecords records.
inline LearningStatus advance_advice_fit(AdviceFitWorkspace& workspace) {
  if (workspace.phase == FitPhase::DONE) return workspace.result.status;
  if (workspace.phase == FitPhase::IDLE) return LearningStatus::INVALID_CONFIGURATION;
  if (workspace.phase == FitPhase::FIT_PREVIEW) {
    auto& preview = workspace.result.preview;
    preview.available =
        preview.segments >= workspace.config.min_train_segments && preview.days >= workspace.config.min_train_days &&
        detail::fit_huber_line(workspace, -1, preview.line, true, &preview.cov_00, &preview.cov_01, &preview.cov_11) &&
        detail::plausible_line(preview.line, workspace.config);
    if (workspace.preparation_status != LearningStatus::OK) {
      workspace.result.status = workspace.preparation_status;
      workspace.phase = FitPhase::DONE;
      return workspace.result.status;
    }
    workspace.phase = FitPhase::FIT_BASE;
    return LearningStatus::FIT_IN_PROGRESS;
  }
  if (workspace.phase == FitPhase::FIT_BASE) {
    // Preparation gates precede numerical fitting. Only the separate preview
    // may expose a line when the training evidence is insufficient.
    if (workspace.advice_blocker != LearningStatus::OK) {
      workspace.result.status = workspace.advice_blocker;
      workspace.phase = FitPhase::DONE;
      return workspace.result.status;
    }
    if (!detail::fit_huber_line(workspace, -1, workspace.result.candidate, false, &workspace.result.candidate_cov_00,
                                &workspace.result.candidate_cov_01, &workspace.result.candidate_cov_11)) {
      workspace.result.status = LearningStatus::FIT_FAILED;
      workspace.phase = FitPhase::DONE;
      return workspace.result.status;
    }
    workspace.result.candidate_available = true;
    if (!detail::plausible_line(workspace.result.candidate, workspace.config)) {
      workspace.result.status = LearningStatus::CANDIDATE_IMPLAUSIBLE;
      workspace.phase = FitPhase::DONE;
      return workspace.result.status;
    }
    workspace.result.lodo_heat_loss_min_w_per_k = INFINITY;
    workspace.result.lodo_heat_loss_max_w_per_k = -INFINITY;
    workspace.result.lodo_zero_power_min_c = INFINITY;
    workspace.result.lodo_zero_power_max_c = -INFINITY;
    workspace.phase = FitPhase::FIT_LODO;
    return LearningStatus::FIT_IN_PROGRESS;
  }
  if (workspace.phase == FitPhase::FIT_LODO) {
    if (workspace.next_lodo_day < workspace.train_day_count) {
      HouseLine lodo;
      if (!detail::fit_huber_line(workspace, workspace.next_lodo_day, lodo) ||
          !detail::plausible_line(lodo, workspace.config)) {
        workspace.result.status = LearningStatus::UNSTABLE_FIT;
        workspace.phase = FitPhase::DONE;
        return workspace.result.status;
      }
      workspace.result.lodo_heat_loss_min_w_per_k =
          fminf(workspace.result.lodo_heat_loss_min_w_per_k, lodo.heat_loss_w_per_k);
      workspace.result.lodo_heat_loss_max_w_per_k =
          fmaxf(workspace.result.lodo_heat_loss_max_w_per_k, lodo.heat_loss_w_per_k);
      workspace.result.lodo_zero_power_min_c = fminf(workspace.result.lodo_zero_power_min_c, lodo.zero_power_temp_c);
      workspace.result.lodo_zero_power_max_c = fmaxf(workspace.result.lodo_zero_power_max_c, lodo.zero_power_temp_c);
      ++workspace.next_lodo_day;
      return LearningStatus::FIT_IN_PROGRESS;
    }
    workspace.phase = FitPhase::FINALIZE;
  }
  if (workspace.phase == FitPhase::FINALIZE) {
    const float heat_loss_deviation =
        fmaxf(fabsf(workspace.result.lodo_heat_loss_min_w_per_k - workspace.result.candidate.heat_loss_w_per_k),
              fabsf(workspace.result.lodo_heat_loss_max_w_per_k - workspace.result.candidate.heat_loss_w_per_k)) /
        workspace.result.candidate.heat_loss_w_per_k;
    const float zero_power_range = workspace.result.lodo_zero_power_max_c - workspace.result.lodo_zero_power_min_c;
    if (heat_loss_deviation > workspace.config.max_lodo_heat_loss_relative_deviation ||
        zero_power_range > workspace.config.max_lodo_zero_power_range_k) {
      workspace.result.status = LearningStatus::UNSTABLE_FIT;
      workspace.phase = FitPhase::DONE;
      return workspace.result.status;
    }
    if (!detail::evaluate_holdout(workspace)) {
      workspace.result.status = LearningStatus::INVALID_MEASUREMENT;
      workspace.phase = FitPhase::DONE;
      return workspace.result.status;
    }
    if (!isfinite(workspace.result.holdout_robust_improvement_w) ||
        !isfinite(workspace.result.holdout_robust_improvement_fraction) ||
        workspace.result.holdout_robust_improvement_fraction < workspace.config.min_holdout_improvement_fraction ||
        workspace.result.holdout_robust_improvement_w < workspace.config.min_holdout_improvement_w) {
      workspace.result.status = LearningStatus::NO_HOLDOUT_IMPROVEMENT;
      workspace.phase = FitPhase::DONE;
      return workspace.result.status;
    }
    workspace.result.advice_ready = true;
    workspace.result.status = LearningStatus::ADVICE_READY;
    workspace.phase = FitPhase::DONE;
    return workspace.result.status;
  }
  return LearningStatus::FIT_IN_PROGRESS;
}

inline const AdviceResult& advice_result(const AdviceFitWorkspace& workspace) { return workspace.result; }

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
