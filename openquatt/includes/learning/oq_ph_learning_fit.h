#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include <new>

#include "oq_ph_learning_quality.h"

namespace oq_power_house::learning {

constexpr size_t kMaxRecordsVisitedPerHuberFit = 2U * static_cast<size_t>(kMaxHuberPasses) * kMaxSegmentRecords;
constexpr size_t kMaxPreparationSortComparisons = kMaxSegmentRecords * (kMaxSegmentRecords - 1U) / 2U;
constexpr size_t kMaxAdviceFitSteps = kMaxCalendarDays + 2U;

struct FitConfig {
  uint8_t huber_passes = kMaxHuberPasses;
  float huber_delta_w = 300.0f;
  uint8_t min_train_segments = 12;
  uint8_t min_train_days = 6;
  uint8_t min_holdout_segments = 6;
  uint8_t min_holdout_days = 3;
  float min_outside_p90_p10_k = 6.0f;
  uint8_t min_segments_per_temperature_bin = 2;
  float max_temperature_bin_fraction = 0.67f;
  float max_raw_day_fraction = 0.34f;
  float min_heat_loss_w_per_k = 10.0f;
  float max_heat_loss_w_per_k = 2000.0f;
  float min_zero_power_temp_c = 5.0f;
  float max_zero_power_temp_c = 30.0f;
  float min_holdout_improvement_fraction = 0.05f;
  float min_holdout_improvement_w = 50.0f;
  float max_lodo_heat_loss_relative_deviation = 0.20f;
  float max_lodo_zero_power_range_k = 2.0f;
  float reference_room_c = NAN;
  float reference_setpoint_c = NAN;
  float max_room_context_delta_c = 0.30f;
  float max_setpoint_context_delta_c = 0.30f;
};

struct AdviceResult {
  LearningStatus status = LearningStatus::INSUFFICIENT_DATA;
  bool candidate_available = false;
  bool advice_ready = false;
  uint16_t algorithm_version = kLearningAlgorithmVersion;
  uint32_t context_revision = 0;
  HouseLine candidate;
  uint8_t train_segments = 0;
  uint8_t holdout_segments = 0;
  uint8_t train_days = 0;
  uint8_t holdout_days = 0;
  float observed_temp_min_c = NAN;
  float observed_temp_max_c = NAN;
  float validated_temp_min_c = NAN;
  float validated_temp_max_c = NAN;
  float train_p10_c = NAN;
  float train_p90_c = NAN;
  float holdout_candidate_mae_w = NAN;
  float holdout_active_mae_w = NAN;
  float holdout_candidate_signed_bias_w = NAN;  // Predicted minus measured.
  float holdout_active_signed_bias_w = NAN;
  float holdout_candidate_signed_bias_by_temp_w[3] = {NAN, NAN, NAN};
  float holdout_active_signed_bias_by_temp_w[3] = {NAN, NAN, NAN};
  float holdout_improvement_fraction = NAN;
  float lodo_heat_loss_min_w_per_k = NAN;
  float lodo_heat_loss_max_w_per_k = NAN;
  float lodo_zero_power_min_c = NAN;
  float lodo_zero_power_max_c = NAN;
  float max_abs_room_trend_k_per_h = NAN;
  float max_abs_room_trend_per_heat_k_per_h_w = NAN;
};

enum class FitPhase : uint8_t { IDLE = 0, FIT_BASE, FIT_LODO, FINALIZE, DONE };

// Caller-owned workspace. Keep it alive, together with the immutable record array,
// until advance_advice_fit() returns a status other than FIT_IN_PROGRESS.
struct AdviceFitWorkspace {
  const SegmentRecord* records = nullptr;
  size_t record_count = 0;
  size_t train_count = 0;
  uint32_t now_epoch_s = 0;
  HouseLine active_line;
  QualityConfig quality_config;
  FitConfig config;
  FitPhase phase = FitPhase::IDLE;
  uint8_t day_count = 0;
  uint8_t train_day_count = 0;
  uint8_t next_lodo_day = 0;
  uint32_t day_ids[kMaxCalendarDays]{};
  uint8_t day_record_counts[kMaxCalendarDays]{};
  uint8_t selected_record_indices[kMaxSegmentRecords]{};
  uint8_t record_day_index[kMaxSegmentRecords]{};
  float sorted_train_temperatures[kMaxSegmentRecords]{};
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
         config.max_lodo_zero_power_range_k <= 20.0f && isfinite(config.reference_room_c) &&
         config.reference_room_c >= -20.0f && config.reference_room_c <= 60.0f &&
         isfinite(config.reference_setpoint_c) && config.reference_setpoint_c >= -20.0f &&
         config.reference_setpoint_c <= 60.0f && isfinite(config.max_room_context_delta_c) &&
         config.max_room_context_delta_c >= 0.0f && config.max_room_context_delta_c <= 2.0f &&
         isfinite(config.max_setpoint_context_delta_c) && config.max_setpoint_context_delta_c >= 0.0f &&
         config.max_setpoint_context_delta_c <= 2.0f;
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
  return 1.0 / workspace.day_record_counts[day_index];
}

inline bool fit_huber_line(const AdviceFitWorkspace& workspace, int omitted_day_index, HouseLine& output) {
  HouseLine current;
  for (uint8_t pass = 0; pass < workspace.config.huber_passes; ++pass) {
    double sum_w = 0.0;
    double sum_x = 0.0;
    double sum_y = 0.0;
    for (size_t index = 0; index < workspace.train_count; ++index) {
      if (workspace.record_day_index[index] == omitted_day_index) continue;
      const SegmentRecord& record = fit_record(workspace, index);
      double robust_weight = 1.0;
      if (valid_house_line(current)) {
        const double predicted = current.heat_loss_w_per_k * (current.zero_power_temp_c - record.mean_outside_c);
        const double residual = fabs(predicted - record.mean_heat_w);
        if (residual > workspace.config.huber_delta_w) robust_weight = workspace.config.huber_delta_w / residual;
      }
      const double weight = record_base_weight(workspace, index) * robust_weight;
      sum_w += weight;
      sum_x += weight * record.mean_outside_c;
      sum_y += weight * record.mean_heat_w;
    }
    if (!(sum_w > 0.0)) return false;
    const double center_x = sum_x / sum_w;
    const double center_y = sum_y / sum_w;
    double covariance = 0.0;
    double variance = 0.0;
    for (size_t index = 0; index < workspace.train_count; ++index) {
      if (workspace.record_day_index[index] == omitted_day_index) continue;
      const SegmentRecord& record = fit_record(workspace, index);
      double robust_weight = 1.0;
      if (valid_house_line(current)) {
        const double predicted = current.heat_loss_w_per_k * (current.zero_power_temp_c - record.mean_outside_c);
        const double residual = fabs(predicted - record.mean_heat_w);
        if (residual > workspace.config.huber_delta_w) robust_weight = workspace.config.huber_delta_w / residual;
      }
      const double weight = record_base_weight(workspace, index) * robust_weight;
      const double centered_x = record.mean_outside_c - center_x;
      covariance += weight * centered_x * (record.mean_heat_w - center_y);
      variance += weight * centered_x * centered_x;
    }
    if (!(variance > 1e-9)) return false;
    const double heat_loss = -covariance / variance;
    const double zero_power = center_x + center_y / heat_loss;
    if (!isfinite(heat_loss) || !isfinite(zero_power)) return false;
    current = {static_cast<float>(heat_loss), static_cast<float>(zero_power)};
  }
  output = current;
  return valid_house_line(output);
}

inline LearningStatus prepare_workspace(AdviceFitWorkspace& workspace) {
  if (workspace.records == nullptr || workspace.record_count == 0 || workspace.record_count > kMaxSegmentRecords ||
      workspace.now_epoch_s == 0 || !valid_quality_config(workspace.quality_config) ||
      !valid_fit_config(workspace.config))
    return LearningStatus::INVALID_CONFIGURATION;
  if (!plausible_line(workspace.active_line, workspace.config)) return LearningStatus::INVALID_ACTIVE_MODEL;
  if (workspace.config.reference_room_c < workspace.quality_config.room_min_c ||
      workspace.config.reference_room_c > workspace.quality_config.room_max_c ||
      workspace.config.reference_setpoint_c < workspace.quality_config.setpoint_min_c ||
      workspace.config.reference_setpoint_c > workspace.quality_config.setpoint_max_c)
    return LearningStatus::INVALID_CONFIGURATION;
  uint32_t previous_end = 0;
  uint32_t context_revision = 0;
  size_t selected_count = 0;
  // Retain every validated record in the owner. Evaluate only observations near
  // the current room/setpoint; day/night history must not poison the whole fit.
  for (size_t index = 0; index < workspace.record_count; ++index) {
    const SegmentRecord& record = workspace.records[index];
    const LearningStatus record_status = validate_segment_record(record, workspace.quality_config);
    if (record_status != LearningStatus::OK) return record_status;
    if (record.end_epoch_s > workspace.now_epoch_s || workspace.now_epoch_s - record.end_epoch_s > kMaxRecordAgeS)
      return LearningStatus::STALE_DATA;
    if (index > 0 && record.start_epoch_s < previous_end) return LearningStatus::TIME_DISCONTINUITY;
    if (index == 0)
      context_revision = record.context_revision;
    else if (record.context_revision != context_revision)
      return LearningStatus::MIXED_CONTEXT;
    previous_end = record.end_epoch_s;
    if (fabsf(record.mean_room_c - workspace.config.reference_room_c) > workspace.config.max_room_context_delta_c ||
        fabsf(record.mean_setpoint_c - workspace.config.reference_setpoint_c) >
            workspace.config.max_setpoint_context_delta_c)
      continue;
    workspace.selected_record_indices[selected_count++] = static_cast<uint8_t>(index);
  }
  workspace.record_count = selected_count;
  if (selected_count == 0) return LearningStatus::INSUFFICIENT_DATA;
  workspace.result.observed_temp_min_c = INFINITY;
  workspace.result.observed_temp_max_c = -INFINITY;
  workspace.result.max_abs_room_trend_k_per_h = 0.0f;
  workspace.result.max_abs_room_trend_per_heat_k_per_h_w = 0.0f;
  for (size_t index = 0; index < workspace.record_count; ++index) {
    const SegmentRecord& record = fit_record(workspace, index);
    workspace.result.observed_temp_min_c = fminf(workspace.result.observed_temp_min_c, record.mean_outside_c);
    workspace.result.observed_temp_max_c = fmaxf(workspace.result.observed_temp_max_c, record.mean_outside_c);
    workspace.result.max_abs_room_trend_k_per_h =
        fmaxf(workspace.result.max_abs_room_trend_k_per_h, fabsf(record.room_trend_k_per_h));
    workspace.result.max_abs_room_trend_per_heat_k_per_h_w = fmaxf(
        workspace.result.max_abs_room_trend_per_heat_k_per_h_w, fabsf(record.room_trend_k_per_h) / record.mean_heat_w);
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
  if (workspace.day_count < workspace.config.min_train_days + workspace.config.min_holdout_days)
    return LearningStatus::INSUFFICIENT_DATA;

  workspace.train_day_count = workspace.day_count - workspace.config.min_holdout_days;
  workspace.train_count = 0;
  for (size_t index = 0; index < workspace.record_count; ++index)
    if (workspace.record_day_index[index] < workspace.train_day_count) ++workspace.train_count;
  while (workspace.record_count - workspace.train_count < workspace.config.min_holdout_segments &&
         workspace.train_day_count > workspace.config.min_train_days) {
    --workspace.train_day_count;
    workspace.train_count -= workspace.day_record_counts[workspace.train_day_count];
  }
  if (workspace.train_count < workspace.config.min_train_segments ||
      workspace.record_count - workspace.train_count < workspace.config.min_holdout_segments ||
      workspace.train_day_count < workspace.config.min_train_days)
    return LearningStatus::INSUFFICIENT_DATA;

  uint8_t max_day_records = 0;
  for (uint8_t day = 0; day < workspace.train_day_count; ++day)
    if (workspace.day_record_counts[day] > max_day_records) max_day_records = workspace.day_record_counts[day];
  if (static_cast<float>(max_day_records) / workspace.train_count > workspace.config.max_raw_day_fraction)
    return LearningStatus::DAY_DOMINANCE;

  for (size_t index = 0; index < workspace.train_count; ++index) {
    const float value = fit_record(workspace, index).mean_outside_c;
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
  if (workspace.result.train_p90_c - workspace.result.train_p10_c < workspace.config.min_outside_p90_p10_k)
    return LearningStatus::INSUFFICIENT_SPREAD;

  uint8_t bins[3]{};
  const float bin_width = (workspace.result.train_p90_c - workspace.result.train_p10_c) / 3.0f;
  for (size_t index = 0; index < workspace.train_count; ++index) {
    const float temperature = fit_record(workspace, index).mean_outside_c;
    uint8_t bin = 0;
    if (temperature >= workspace.result.train_p90_c)
      bin = 2;
    else if (temperature > workspace.result.train_p10_c)
      bin = static_cast<uint8_t>((temperature - workspace.result.train_p10_c) / bin_width);
    if (bin > 2) bin = 2;
    ++bins[bin];
  }
  for (uint8_t bin : bins)
    if (bin < workspace.config.min_segments_per_temperature_bin) return LearningStatus::INSUFFICIENT_SPREAD;
  for (uint8_t bin : bins)
    if (static_cast<float>(bin) / workspace.train_count > workspace.config.max_temperature_bin_fraction)
      return LearningStatus::DAY_DOMINANCE;

  workspace.result.train_segments = static_cast<uint8_t>(workspace.train_count);
  workspace.result.holdout_segments = static_cast<uint8_t>(workspace.record_count - workspace.train_count);
  workspace.result.train_days = workspace.train_day_count;
  workspace.result.holdout_days = workspace.day_count - workspace.train_day_count;
  return LearningStatus::OK;
}

inline void evaluate_holdout(AdviceFitWorkspace& workspace) {
  double duration_sum = 0.0;
  double candidate_abs_error = 0.0;
  double active_abs_error = 0.0;
  double candidate_bias = 0.0;
  double active_bias = 0.0;
  double bin_duration[3]{};
  double candidate_bin_bias[3]{};
  double active_bin_bias[3]{};
  workspace.result.validated_temp_min_c = INFINITY;
  workspace.result.validated_temp_max_c = -INFINITY;
  for (size_t index = workspace.train_count; index < workspace.record_count; ++index) {
    const SegmentRecord& record = fit_record(workspace, index);
    const double weight = record.duration_s;
    const double candidate_w = house_line_power_w(workspace.result.candidate, record.mean_outside_c);
    const double active_w = house_line_power_w(workspace.active_line, record.mean_outside_c);
    const double candidate_error = candidate_w - record.mean_heat_w;
    const double active_error = active_w - record.mean_heat_w;
    duration_sum += weight;
    candidate_abs_error += weight * fabs(candidate_error);
    active_abs_error += weight * fabs(active_error);
    candidate_bias += weight * candidate_error;
    active_bias += weight * active_error;
    workspace.result.validated_temp_min_c = fminf(workspace.result.validated_temp_min_c, record.mean_outside_c);
    workspace.result.validated_temp_max_c = fmaxf(workspace.result.validated_temp_max_c, record.mean_outside_c);
    uint8_t bin = 0;
    if (record.mean_outside_c >= workspace.result.train_p90_c)
      bin = 2;
    else if (record.mean_outside_c > workspace.result.train_p10_c)
      bin = static_cast<uint8_t>(3.0f * (record.mean_outside_c - workspace.result.train_p10_c) /
                                 (workspace.result.train_p90_c - workspace.result.train_p10_c));
    if (bin > 2) bin = 2;
    bin_duration[bin] += weight;
    candidate_bin_bias[bin] += weight * candidate_error;
    active_bin_bias[bin] += weight * active_error;
  }
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
}

}  // namespace detail

inline LearningStatus begin_advice_fit(const SegmentRecord* records, size_t record_count, uint32_t now_epoch_s,
                                       const HouseLine& active_line, const QualityConfig& quality_config,
                                       const FitConfig& config, AdviceFitWorkspace& workspace) {
  workspace.~AdviceFitWorkspace();
  new (&workspace) AdviceFitWorkspace();
  workspace.records = records;
  workspace.record_count = record_count;
  workspace.now_epoch_s = now_epoch_s;
  workspace.active_line = active_line;
  workspace.quality_config = quality_config;
  workspace.config = config;
  const LearningStatus status = detail::prepare_workspace(workspace);
  if (status != LearningStatus::OK) {
    workspace.result.status = status;
    workspace.phase = FitPhase::DONE;
    return status;
  }
  workspace.phase = FitPhase::FIT_BASE;
  workspace.result.status = LearningStatus::FIT_IN_PROGRESS;
  return LearningStatus::FIT_IN_PROGRESS;
}

// Each call performs at most one Huber fit: at most
// kMaxRecordsVisitedPerHuberFit record visits. Preparation is separate and bounded
// by kMaxPreparationSortComparisons; holdout finalization visits at most kMaxSegmentRecords records.
inline LearningStatus advance_advice_fit(AdviceFitWorkspace& workspace) {
  if (workspace.phase == FitPhase::DONE) return workspace.result.status;
  if (workspace.phase == FitPhase::IDLE) return LearningStatus::INVALID_CONFIGURATION;
  if (workspace.phase == FitPhase::FIT_BASE) {
    if (!detail::fit_huber_line(workspace, -1, workspace.result.candidate)) {
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
    detail::evaluate_holdout(workspace);
    const float absolute_improvement = workspace.result.holdout_active_mae_w - workspace.result.holdout_candidate_mae_w;
    const float required_absolute_improvement = workspace.config.min_holdout_improvement_w;
    if (!isfinite(workspace.result.holdout_improvement_fraction) ||
        workspace.result.holdout_improvement_fraction < workspace.config.min_holdout_improvement_fraction ||
        absolute_improvement < required_absolute_improvement) {
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
