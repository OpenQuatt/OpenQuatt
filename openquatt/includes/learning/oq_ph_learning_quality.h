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
  // Historical structural ceiling. Lower live limits must not invalidate stored
  // records, RLS history, checkpoint endpoints or previously accrued uncertainty.
  float max_abs_heat_w = 50000.0f;
  float max_setpoint_range_c = 0.05f;
  float max_abs_room_trend_k_per_h = 0.05f;  // Legacy four-hour records.
  float max_daily_room_trend_k_per_h = 0.20f;
  float max_room_range_c = 0.30f;
  float max_abs_water_endpoint_delta_c = 1.0f;
  // Daily gap policy only. Zero retains the broad measurement-validation bound.
  // Never use this envelope to reject or clamp otherwise valid measured heat.
  float max_daily_missing_heat_w = 0.0f;
  // Live observations only; the production adapter always binds this to the
  // configured reference plus margin. Zero keeps pure/legacy callers compatible.
  float max_observed_heat_w = 0.0f;
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
         isfinite(config.max_daily_room_trend_k_per_h) && config.max_daily_room_trend_k_per_h >= 0.0f &&
         config.max_daily_room_trend_k_per_h <= 2.0f && isfinite(config.max_room_range_c) &&
         config.max_room_range_c >= 0.0f && config.max_room_range_c <= 5.0f &&
         isfinite(config.max_abs_water_endpoint_delta_c) && config.max_abs_water_endpoint_delta_c >= 0.0f &&
         config.max_abs_water_endpoint_delta_c <= 20.0f && isfinite(config.max_daily_missing_heat_w) &&
         (config.max_daily_missing_heat_w == 0.0f || config.max_daily_missing_heat_w >= 1000.0f) &&
         config.max_daily_missing_heat_w <= config.max_abs_heat_w && isfinite(config.max_observed_heat_w) &&
         (config.max_observed_heat_w == 0.0f || config.max_observed_heat_w >= 1000.0f) &&
         config.max_observed_heat_w <= config.max_abs_heat_w &&
         (config.max_observed_heat_w == 0.0f || config.max_daily_missing_heat_w <= config.max_observed_heat_w);
}

inline float configured_measurement_heat_limit_w(const QualityConfig& config) {
  return config.max_observed_heat_w > 0.0f ? config.max_observed_heat_w : config.max_abs_heat_w;
}

inline float configured_daily_missing_heat_bound_w(const QualityConfig& config) {
  return config.max_daily_missing_heat_w > 0.0f ? config.max_daily_missing_heat_w : config.max_abs_heat_w;
}

inline QualityConfig daily_heat_quality(const QualityConfig& hp_quality, bool system_heat_enabled) {
  auto quality = hp_quality;
  if (system_heat_enabled) {
    // Independently triggered boiler heat can exceed the dispatch target.
    quality.max_daily_missing_heat_w = quality.max_abs_heat_w;
    quality.max_observed_heat_w = quality.max_abs_heat_w;
  }
  return quality;
}

inline QualityConfig live_daily_heat_quality(const QualityConfig& hp_quality, bool system_heat_enabled,
                                             bool boiler_off_between_ticks) {
  auto quality = daily_heat_quality(hp_quality, system_heat_enabled);
  if (system_heat_enabled) {
    constexpr float kAssumedSystemHeatBoundW = 24000.0f;
    float bound_w = configured_daily_missing_heat_bound_w(hp_quality);
    if (!boiler_off_between_ticks) bound_w = fmaxf(bound_w, kAssumedSystemHeatBoundW);
    quality.max_daily_missing_heat_w = fminf(bound_w, quality.max_abs_heat_w);
  }
  return quality;
}

// Real endpoint evidence can exceed the reference envelope, e.g. during signed
// defrost or a thermal transition. Enlarge the conditional bound; never hide the
// observation or reduce already accrued debt. Unobserved heat remains an assumption.
inline double daily_missing_heat_bound_w(const QualityConfig& config, float first_heat_w, float last_heat_w,
                                         float observed_abs_heat_w = 0.0f) {
  if (!valid_quality_config(config) || !isfinite(first_heat_w) || !isfinite(last_heat_w) ||
      fabsf(first_heat_w) > config.max_abs_heat_w || fabsf(last_heat_w) > config.max_abs_heat_w ||
      !isfinite(observed_abs_heat_w) || observed_abs_heat_w < 0.0f || observed_abs_heat_w > config.max_abs_heat_w)
    return NAN;
  return fmax(fmax(configured_daily_missing_heat_bound_w(config), observed_abs_heat_w),
              fmax(fabs(first_heat_w), fabs(last_heat_w)));
}

inline LearningStatus validate_snapshot_structure(const LearningSnapshot& snapshot, const QualityConfig& config) {
  if (!valid_quality_config(config)) return LearningStatus::INVALID_CONFIGURATION;
  if (snapshot.monotonic_ms == 0 || snapshot.epoch_s == 0 || snapshot.context_revision == 0 ||
      snapshot.invalid_reasons != INVALID_NONE)
    return LearningStatus::INVALID_MEASUREMENT;
  if (!isfinite(snapshot.room_c) || !isfinite(snapshot.setpoint_c) || !isfinite(snapshot.outside_c) ||
      !isfinite(snapshot.heat_to_water_w) || !isfinite(snapshot.mean_water_c) || snapshot.room_c < config.room_min_c ||
      snapshot.room_c > config.room_max_c || snapshot.setpoint_c < config.setpoint_min_c ||
      snapshot.setpoint_c > config.setpoint_max_c || snapshot.outside_c < config.outside_min_c ||
      snapshot.outside_c > config.outside_max_c || snapshot.mean_water_c < config.water_min_c ||
      snapshot.mean_water_c > config.water_max_c || fabsf(snapshot.heat_to_water_w) > config.max_abs_heat_w)
    return LearningStatus::INVALID_MEASUREMENT;
  return LearningStatus::OK;
}

inline LearningStatus validate_snapshot(const LearningSnapshot& snapshot, const QualityConfig& config) {
  if (!valid_quality_config(config)) return LearningStatus::INVALID_CONFIGURATION;
  const auto structural = validate_snapshot_structure(snapshot, config);
  if (structural != LearningStatus::OK) return structural;
  return fabsf(snapshot.heat_to_water_w) <= configured_measurement_heat_limit_w(config)
             ? LearningStatus::OK
             : LearningStatus::INVALID_MEASUREMENT;
}

inline LearningStatus evaluate_segment_quality(const SegmentRecord& record, const QualityConfig& config) {
  if (!valid_quality_config(config)) return LearningStatus::INVALID_CONFIGURATION;
  if (record.duration_s == 0 || !isfinite(record.mean_room_c) || !isfinite(record.mean_setpoint_c) ||
      !isfinite(record.mean_outside_c) || !isfinite(record.mean_heat_w) || !isfinite(record.room_trend_k_per_h))
    return LearningStatus::INVALID_MEASUREMENT;
  constexpr float tolerance = 0.00001f;  // Float subtraction at an inclusive limit.
  if (is_daily_record(record)) {
    // Energy over a full day includes compressor pauses and signed defrost heat.
    // Only gross room drift is excluded; no C estimate is used to correct it.
    return fabsf(record.room_trend_k_per_h) > config.max_daily_room_trend_k_per_h + tolerance
               ? LearningStatus::ROOM_UNSTABLE
               : LearningStatus::OK;
  }
  if (!isfinite(record.room_range_k) || !isfinite(record.setpoint_range_c) || !isfinite(record.water_start_c) ||
      !isfinite(record.water_end_c))
    return LearningStatus::INVALID_MEASUREMENT;
  if (!(record.mean_heat_w > 0.0f)) return LearningStatus::NONPOSITIVE_HEAT;
  if (record.setpoint_range_c > config.max_setpoint_range_c + tolerance) return LearningStatus::SETPOINT_CHANGED;
  if (fabsf(record.room_trend_k_per_h) > config.max_abs_room_trend_k_per_h + tolerance ||
      record.room_range_k > config.max_room_range_c + tolerance)
    return LearningStatus::ROOM_UNSTABLE;
  if (fabsf(record.water_end_c - record.water_start_c) > config.max_abs_water_endpoint_delta_c + tolerance)
    return LearningStatus::WATER_STORAGE_UNSTABLE;
  return LearningStatus::OK;
}

// Legacy records remain readable. New records cover exactly 24 hours, including
// UTC midnight, and the fit assigns each record to its start day.
inline LearningStatus validate_segment_record_structure(const SegmentRecord& record, const QualityConfig& config) {
  if (!valid_quality_config(config)) return LearningStatus::INVALID_CONFIGURATION;
  const bool daily = is_daily_record(record);
  const uint32_t legacy_duration_s = static_cast<uint32_t>(kLegacySegmentDurationMs / 1000ULL);
  if (record.start_epoch_s == 0 || record.end_epoch_s <= record.start_epoch_s || record.context_revision == 0 ||
      (!daily && (record.duration_s < legacy_duration_s ||
                  record.duration_s > legacy_duration_s + (config.max_interval_ms + 999U) / 1000U)))
    return LearningStatus::SEGMENT_INELIGIBLE;
  const uint32_t epoch_duration_s = record.end_epoch_s - record.start_epoch_s;
  const uint32_t mismatch = epoch_duration_s > record.duration_s ? epoch_duration_s - record.duration_s
                                                                 : record.duration_s - epoch_duration_s;
  if (mismatch > config.utc_tolerance_s + 1U) return LearningStatus::TIME_DISCONTINUITY;
  if (!isfinite(record.mean_room_c) || record.mean_room_c < config.room_min_c ||
      record.mean_room_c > config.room_max_c || !isfinite(record.mean_setpoint_c) ||
      record.mean_setpoint_c < config.setpoint_min_c || record.mean_setpoint_c > config.setpoint_max_c ||
      !isfinite(record.mean_outside_c) || record.mean_outside_c < config.outside_min_c ||
      record.mean_outside_c > config.outside_max_c || !isfinite(record.mean_heat_w) ||
      fabsf(record.mean_heat_w) > config.max_abs_heat_w)
    return LearningStatus::INVALID_MEASUREMENT;
  if (daily) {
    const float low = config.outside_min_c + kReferenceRoomC - config.room_max_c - 0.01f;
    const float high = config.outside_max_c + kReferenceRoomC - config.room_min_c + 0.01f;
    for (size_t index = 0; index < kDailyTemperatureProfileSize; ++index) {
      const float value = record.effective_outside_profile_centi[index] * 0.01f;
      if (value < low || value > high ||
          (index > 0 &&
           record.effective_outside_profile_centi[index] < record.effective_outside_profile_centi[index - 1]))
        return LearningStatus::INVALID_MEASUREMENT;
    }
  } else if (!isfinite(record.room_range_k) || record.room_range_k < 0.0f || !isfinite(record.setpoint_range_c) ||
             record.setpoint_range_c < 0.0f || !isfinite(record.water_start_c) ||
             record.water_start_c < config.water_min_c || record.water_start_c > config.water_max_c ||
             !isfinite(record.water_end_c) || record.water_end_c < config.water_min_c ||
             record.water_end_c > config.water_max_c) {
    return LearningStatus::INVALID_MEASUREMENT;
  }
  if (!isfinite(record.room_trend_k_per_h) || !valid_daily_record_quality_codes(record.daily_quality))
    return LearningStatus::INVALID_MEASUREMENT;
  return LearningStatus::OK;
}

// Legacy call sites that explicitly ask for model quality keep their contract.
// Retention/journal paths use structural validity instead.
inline LearningStatus validate_segment_record(const SegmentRecord& record, const QualityConfig& config) {
  const auto structural = validate_segment_record_structure(record, config);
  return structural == LearningStatus::OK ? evaluate_segment_quality(record, config) : structural;
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
