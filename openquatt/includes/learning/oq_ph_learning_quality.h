#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include "oq_ph_learning_types.h"

namespace oq_power_house::learning {

struct QualityConfig {
  uint32_t max_interval_ms = 60000;
  uint32_t utc_tolerance_s = 2;
  float outside_min_c = -40.0f;
  float outside_max_c = 55.0f;
  float room_min_c = 0.0f;
  float room_max_c = 40.0f;
  float setpoint_min_c = 5.0f;
  float setpoint_max_c = 35.0f;
  float water_min_c = -10.0f;
  float water_max_c = 95.0f;
  float max_abs_heat_w = 50000.0f;
  float max_setpoint_range_c = 0.05f;
  float max_abs_room_trend_k_per_h = 0.05f;
  float max_room_range_c = 0.30f;
  float max_abs_water_endpoint_delta_c = 1.0f;
  float max_heat_uncertainty_w = 400.0f;
  float max_heat_uncertainty_fraction = 0.20f;
};

inline bool valid_quality_config(const QualityConfig& config) {
  return config.max_interval_ms >= 1000U && config.max_interval_ms <= 10U * 60U * 1000U &&
         config.utc_tolerance_s <= 60U && isfinite(config.outside_min_c) && isfinite(config.outside_max_c) &&
         config.outside_min_c >= -80.0f && config.outside_max_c <= 80.0f &&
         config.outside_min_c < config.outside_max_c && isfinite(config.room_min_c) && isfinite(config.room_max_c) &&
         config.room_min_c >= -20.0f && config.room_max_c <= 60.0f && config.room_min_c < config.room_max_c &&
         isfinite(config.setpoint_min_c) && isfinite(config.setpoint_max_c) && config.setpoint_min_c >= -20.0f &&
         config.setpoint_max_c <= 60.0f && config.setpoint_min_c < config.setpoint_max_c &&
         isfinite(config.water_min_c) && isfinite(config.water_max_c) && config.water_min_c >= -30.0f &&
         config.water_max_c <= 120.0f && config.water_min_c < config.water_max_c && isfinite(config.max_abs_heat_w) &&
         config.max_abs_heat_w >= 1000.0f && config.max_abs_heat_w <= 100000.0f &&
         isfinite(config.max_setpoint_range_c) && config.max_setpoint_range_c >= 0.0f &&
         config.max_setpoint_range_c <= 2.0f && isfinite(config.max_abs_room_trend_k_per_h) &&
         config.max_abs_room_trend_k_per_h >= 0.0f && config.max_abs_room_trend_k_per_h <= 2.0f &&
         isfinite(config.max_room_range_c) && config.max_room_range_c >= 0.0f && config.max_room_range_c <= 5.0f &&
         isfinite(config.max_abs_water_endpoint_delta_c) && config.max_abs_water_endpoint_delta_c >= 0.0f &&
         config.max_abs_water_endpoint_delta_c <= 20.0f && isfinite(config.max_heat_uncertainty_w) &&
         config.max_heat_uncertainty_w >= 0.0f && config.max_heat_uncertainty_w <= config.max_abs_heat_w &&
         isfinite(config.max_heat_uncertainty_fraction) && config.max_heat_uncertainty_fraction >= 0.0f &&
         config.max_heat_uncertainty_fraction <= 1.0f;
}

inline LearningStatus validate_snapshot(const LearningSnapshot& snapshot, const QualityConfig& config) {
  if (!valid_quality_config(config)) return LearningStatus::INVALID_CONFIGURATION;
  if (snapshot.monotonic_ms == 0 || snapshot.epoch_s == 0 || snapshot.context_revision == 0 ||
      snapshot.invalid_reasons != INVALID_NONE)
    return LearningStatus::INVALID_MEASUREMENT;
  if (!isfinite(snapshot.room_c) || !isfinite(snapshot.setpoint_c) || !isfinite(snapshot.outside_c) ||
      !isfinite(snapshot.heat_to_water_w) || !isfinite(snapshot.heat_uncertainty_w) ||
      !isfinite(snapshot.mean_water_c) || snapshot.room_c < config.room_min_c || snapshot.room_c > config.room_max_c ||
      snapshot.setpoint_c < config.setpoint_min_c || snapshot.setpoint_c > config.setpoint_max_c ||
      snapshot.outside_c < config.outside_min_c || snapshot.outside_c > config.outside_max_c ||
      snapshot.mean_water_c < config.water_min_c || snapshot.mean_water_c > config.water_max_c ||
      fabsf(snapshot.heat_to_water_w) > config.max_abs_heat_w || snapshot.heat_uncertainty_w < 0.0f ||
      snapshot.heat_uncertainty_w > config.max_abs_heat_w)
    return LearningStatus::INVALID_MEASUREMENT;
  return LearningStatus::OK;
}

inline LearningStatus evaluate_segment_quality(const SegmentRecord& record, const QualityConfig& config) {
  if (!valid_quality_config(config)) return LearningStatus::INVALID_CONFIGURATION;
  if (record.duration_s == 0 || !isfinite(record.mean_room_c) || !isfinite(record.mean_setpoint_c) ||
      !isfinite(record.mean_outside_c) || !isfinite(record.mean_heat_w) || !isfinite(record.mean_heat_uncertainty_w) ||
      !isfinite(record.room_trend_k_per_h) || !isfinite(record.room_range_k) || !isfinite(record.setpoint_range_c) ||
      !isfinite(record.water_start_c) || !isfinite(record.water_end_c))
    return LearningStatus::INVALID_MEASUREMENT;
  if (!(record.mean_heat_w > 0.0f)) return LearningStatus::NONPOSITIVE_HEAT;
  if (record.setpoint_range_c > config.max_setpoint_range_c) return LearningStatus::SETPOINT_CHANGED;
  if (fabsf(record.room_trend_k_per_h) > config.max_abs_room_trend_k_per_h ||
      record.room_range_k > config.max_room_range_c)
    return LearningStatus::ROOM_UNSTABLE;
  if (fabsf(record.water_end_c - record.water_start_c) > config.max_abs_water_endpoint_delta_c)
    return LearningStatus::WATER_STORAGE_UNSTABLE;
  const float relative_limit = config.max_heat_uncertainty_fraction * record.mean_heat_w;
  if (record.mean_heat_uncertainty_w > config.max_heat_uncertainty_w || record.mean_heat_uncertainty_w > relative_limit)
    return LearningStatus::MEASUREMENT_UNCERTAIN;
  return LearningStatus::OK;
}

// Rechecks persisted/replayed input independently of the collector. The end is
// exclusive, so 20:00-24:00 UTC stays in one day; actual midnight crossings fail.
inline LearningStatus validate_segment_record(const SegmentRecord& record, const QualityConfig& config) {
  if (!valid_quality_config(config)) return LearningStatus::INVALID_CONFIGURATION;
  const uint32_t max_duration_s =
      static_cast<uint32_t>(kSegmentDurationMs / 1000ULL) + (config.max_interval_ms + 999U) / 1000U;
  if (record.start_epoch_s == 0 || record.end_epoch_s <= record.start_epoch_s ||
      record.duration_s < kSegmentDurationMs / 1000ULL || record.duration_s > max_duration_s ||
      record.context_revision == 0)
    return LearningStatus::SEGMENT_INELIGIBLE;
  const uint32_t epoch_duration_s = record.end_epoch_s - record.start_epoch_s;
  const uint32_t duration_mismatch_s = epoch_duration_s > record.duration_s ? epoch_duration_s - record.duration_s
                                                                            : record.duration_s - epoch_duration_s;
  if (duration_mismatch_s > config.utc_tolerance_s + 1U) return LearningStatus::TIME_DISCONTINUITY;
  if (record.start_epoch_s / 86400U != (record.end_epoch_s - 1U) / 86400U) return LearningStatus::TIME_DISCONTINUITY;
  if (!isfinite(record.mean_room_c) || record.mean_room_c < config.room_min_c ||
      record.mean_room_c > config.room_max_c || !isfinite(record.mean_setpoint_c) ||
      record.mean_setpoint_c < config.setpoint_min_c || record.mean_setpoint_c > config.setpoint_max_c ||
      !isfinite(record.mean_outside_c) || record.mean_outside_c < config.outside_min_c ||
      record.mean_outside_c > config.outside_max_c || !isfinite(record.mean_heat_w) ||
      fabsf(record.mean_heat_w) > config.max_abs_heat_w || !isfinite(record.mean_heat_uncertainty_w) ||
      record.mean_heat_uncertainty_w < 0.0f || record.mean_heat_uncertainty_w > config.max_abs_heat_w ||
      !isfinite(record.room_trend_k_per_h) || !isfinite(record.room_range_k) || record.room_range_k < 0.0f ||
      !isfinite(record.setpoint_range_c) || record.setpoint_range_c < 0.0f || !isfinite(record.water_start_c) ||
      record.water_start_c < config.water_min_c || record.water_start_c > config.water_max_c ||
      !isfinite(record.water_end_c) || record.water_end_c < config.water_min_c ||
      record.water_end_c > config.water_max_c)
    return LearningStatus::INVALID_MEASUREMENT;
  return evaluate_segment_quality(record, config);
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
