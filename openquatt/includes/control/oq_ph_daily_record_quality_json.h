#pragma once

#include "../learning/oq_ph_daily_record_quality.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

namespace oq_ph_learning {

template <typename Writer>
inline void write_daily_record_quality_json(Writer& json, const oq_power_house::learning::SegmentRecord& record,
                                            const oq_power_house::learning::QualityConfig& config) {
  using namespace oq_power_house::learning;
  const auto& quality = record.daily_quality;
  json.add("[");
  const uint16_t coverage[]{quality.heat_coverage_bp, quality.temperature_coverage_bp};
  for (uint16_t value : coverage) {
    if (value <= kDailyCoverageScale)
      json.add("%u,", static_cast<unsigned>(value));
    else
      json.add("null,");
  }
  if (quality.heat_uncertainty_mean_w == kDailyQualityUnknown)
    json.add("null,");
  else if (quality.heat_uncertainty_mean_w == kDailyUncertaintyOverflow)
    json.add("\"overflow\",");
  else
    json.add("%u,", static_cast<unsigned>(quality.heat_uncertainty_mean_w));
  const bool eligible = daily_record_fit_eligible(record, config);
  const double weight = daily_record_quality_weight(record, config);
  json.add("%s,%u]", eligible ? "true" : "false", static_cast<unsigned>(floor(weight * kDailyCoverageScale)));
}

}  // namespace oq_ph_learning

#endif
