#pragma once

#include "oq_ph_learning_quality.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

namespace oq_power_house::learning {

constexpr uint16_t kDailyQualityPolicyVersion = 1;

constexpr uint16_t kDailyFitMinimumCoverageBp = 9900;
constexpr uint16_t kDailyFitMaximumUncertaintyW = 100;

inline bool valid_daily_record_quality(const DailyRecordQuality& quality) {
  return valid_daily_record_quality_codes(quality);
}

inline bool legacy_daily_record_quality(const DailyRecordQuality& quality) {
  return quality.heat_coverage_bp == kDailyQualityUnknown && quality.temperature_coverage_bp == kDailyQualityUnknown &&
         quality.heat_uncertainty_mean_w == kDailyQualityUnknown;
}

inline bool known_daily_record_quality(const DailyRecordQuality& quality) {
  return valid_daily_record_quality(quality) && quality.heat_coverage_bp <= kDailyCoverageScale &&
         quality.temperature_coverage_bp <= kDailyCoverageScale &&
         quality.heat_uncertainty_mean_w < kDailyUncertaintyOverflow;
}

inline bool daily_record_fit_eligible(const SegmentRecord& record, const QualityConfig& config) {
  if (!is_daily_record(record) || !valid_daily_record_quality(record.daily_quality) ||
      evaluate_segment_quality(record, config) != LearningStatus::OK)
    return false;
  const auto& quality = record.daily_quality;
  return legacy_daily_record_quality(quality) ||
         (known_daily_record_quality(quality) && quality.heat_coverage_bp >= kDailyFitMinimumCoverageBp &&
          quality.temperature_coverage_bp >= kDailyFitMinimumCoverageBp &&
          quality.heat_uncertainty_mean_w <= kDailyFitMaximumUncertaintyW);
}

inline bool daily_record_holdout_eligible(const SegmentRecord& record, const QualityConfig& config) {
  return known_daily_record_quality(record.daily_quality) && daily_record_fit_eligible(record, config);
}

// This is a fixed influence heuristic, not inverse variance or model confidence.
// Never renormalize each day's quality factor back to one.
inline double daily_record_quality_weight(const SegmentRecord& record, const QualityConfig& config) {
  if (!daily_record_fit_eligible(record, config)) return 0.0;
  const auto& quality = record.daily_quality;
  if (legacy_daily_record_quality(quality)) return 0.5;
  const double heat_fraction = static_cast<double>(quality.heat_coverage_bp) / kDailyCoverageScale;
  const double temperature_fraction = static_cast<double>(quality.temperature_coverage_bp) / kDailyCoverageScale;
  return heat_fraction * temperature_fraction /
         (1.0 + static_cast<double>(quality.heat_uncertainty_mean_w) / kDailyFitMaximumUncertaintyW);
}

// Legacy values remain unknown in storage. 100 W is only an explicit historical
// producer-policy assumption for training; legacy records cannot prove holdout.
inline double daily_record_assumed_heat_uncertainty_w(const SegmentRecord& record) {
  if (legacy_daily_record_quality(record.daily_quality)) return kDailyFitMaximumUncertaintyW;
  return known_daily_record_quality(record.daily_quality) ? record.daily_quality.heat_uncertainty_mean_w : NAN;
}

// Exact minimum of paired absolute-loss gain on one conditional heat-label
// interval. Temperature/regressor uncertainty is not covered by this bound.
inline double worst_case_absolute_error_gain_w(double active_w, double candidate_w, double measured_w,
                                               double uncertainty_w) {
  if (!isfinite(active_w) || !isfinite(candidate_w) || !isfinite(measured_w) || !isfinite(uncertainty_w) ||
      uncertainty_w < 0.0)
    return NAN;
  const double lower = measured_w - uncertainty_w;
  const double upper = measured_w + uncertainty_w;
  if (!isfinite(lower) || !isfinite(upper)) return NAN;
  const double labels[] = {lower, upper, fmax(lower, fmin(upper, active_w)), fmax(lower, fmin(upper, candidate_w))};
  double minimum = INFINITY;
  for (double label : labels) {
    const double gain = fabs(active_w - label) - fabs(candidate_w - label);
    if (!isfinite(gain)) return NAN;
    minimum = fmin(minimum, gain);
  }
  return minimum;
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
