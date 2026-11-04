#include <assert.h>
#include <math.h>
#include <string.h>

#include <limits>

#include "../../openquatt/includes/learning/oq_ph_daily_record_quality.h"

namespace {
using namespace oq_power_house::learning;

SegmentRecord record() {
  SegmentRecord value;
  value.start_epoch_s = 20000U * 86400U;
  value.end_epoch_s = value.start_epoch_s + 86400U;
  value.duration_s = 86400;
  value.context_revision = 1;
  value.mean_room_c = value.mean_setpoint_c = 20;
  value.mean_outside_c = 5;
  value.mean_heat_w = 2200;
  value.room_trend_k_per_h = 0;
  value.room_range_k = 0.1f;
  value.setpoint_range_c = 0;
  value.water_start_c = value.water_end_c = 30;
  for (auto& temperature : value.effective_outside_profile_centi) temperature = 500;
  return value;
}

void metadata_codes_and_legacy() {
  auto value = record();
  const auto original = value.daily_quality;
  assert(valid_daily_record_quality(original));
  assert(legacy_daily_record_quality(original));
  assert(!known_daily_record_quality(original));
  assert(daily_record_fit_eligible(value, {}));
  assert(!daily_record_holdout_eligible(value, {}));
  assert(daily_record_quality_weight(value, {}) == 0.5);
  assert(daily_record_assumed_heat_uncertainty_w(value) == 100);
  assert(memcmp(&original, &value.daily_quality, sizeof(original)) == 0);

  value.daily_quality = {10000, 10000, 0};
  assert(known_daily_record_quality(value.daily_quality));
  assert(daily_record_holdout_eligible(value, {}));
  assert(daily_record_quality_weight(value, {}) == 1);
  value.daily_quality.heat_coverage_bp = 10001;
  assert(!valid_daily_record_quality(value.daily_quality));
  assert(daily_record_quality_weight(value, {}) == 0);
  value.daily_quality.heat_coverage_bp = kDailyUncertaintyOverflow;
  assert(!valid_daily_record_quality(value.daily_quality));
  value.daily_quality = {10000, kDailyQualityUnknown, 0};
  assert(valid_daily_record_quality(value.daily_quality));
  assert(!legacy_daily_record_quality(value.daily_quality));
  assert(!daily_record_fit_eligible(value, {}));
  assert(isnan(daily_record_assumed_heat_uncertainty_w(value)));
  value.daily_quality = {10000, 10000, kDailyUncertaintyOverflow};
  assert(valid_daily_record_quality(value.daily_quality));
  assert(!known_daily_record_quality(value.daily_quality));
  assert(!daily_record_fit_eligible(value, {}));
  assert(daily_record_quality_weight(value, {}) == 0);
  value.daily_quality = {10000, 10000, kDailyQualityUnknown};
  assert(!daily_record_fit_eligible(value, {}));
}

void monotonic_influence_and_separate_eligibility() {
  auto value = record();
  value.daily_quality = {10000, 10000, 0};
  double previous = 1.0;
  for (uint16_t uncertainty = 0; uncertainty <= 100; ++uncertainty) {
    value.daily_quality.heat_uncertainty_mean_w = uncertainty;
    const double weight = daily_record_quality_weight(value, {});
    assert(weight <= previous && weight >= 0.5);
    previous = weight;
  }
  assert(previous == 0.5);
  value.daily_quality = {9900, 9900, 100};
  assert(daily_record_fit_eligible(value, {}));
  assert(fabs(daily_record_quality_weight(value, {}) - 0.49005) < 1e-12);
  value.daily_quality.heat_coverage_bp = 9899;
  assert(validate_segment_record_structure(value, {}) == LearningStatus::OK);
  assert(!daily_record_fit_eligible(value, {}));
  value.daily_quality = {10000, 10000, 101};
  assert(validate_segment_record_structure(value, {}) == LearningStatus::OK);
  assert(daily_record_quality_weight(value, {}) == 0);
  value.daily_quality = {10000, 10000, 0};
  value.room_trend_k_per_h = 0.3f;
  assert(validate_segment_record_structure(value, {}) == LearningStatus::OK);
  assert(!daily_record_fit_eligible(value, {}));
}

void paired_interval_loss_and_failure_boundaries() {
  assert(worst_case_absolute_error_gain_w(1000, 1100, 1100, 100) == -100);
  assert(worst_case_absolute_error_gain_w(1100, 1050, 950, 50) == 50);
  assert(worst_case_absolute_error_gain_w(1300, 1000, 1000, 100) == 100);
  assert(worst_case_absolute_error_gain_w(1000, 1000, 1100, 100) == 0);
  assert(worst_case_absolute_error_gain_w(1300, 1000, 1100, 0) == 100);
  assert(worst_case_absolute_error_gain_w(-200, -100, -100, 100) == -100);
  for (int active = -300; active <= 300; active += 60) {
    for (int candidate = -300; candidate <= 300; candidate += 60) {
      const double lower = worst_case_absolute_error_gain_w(active, candidate, 20, 100);
      double independently_sampled = INFINITY;
      for (int label = -80; label <= 120; ++label)
        independently_sampled = fmin(independently_sampled, fabs(active - label) - fabs(candidate - label));
      assert(lower == independently_sampled);
    }
  }
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  assert(isnan(worst_case_absolute_error_gain_w(0, 0, 0, -1)));
  assert(isnan(worst_case_absolute_error_gain_w(nan, 0, 0, 1)));
  assert(isnan(worst_case_absolute_error_gain_w(0, infinity, 0, 1)));
  assert(isnan(worst_case_absolute_error_gain_w(0, 0, nan, 1)));
  assert(isnan(worst_case_absolute_error_gain_w(0, 0, 0, infinity)));
  const double largest = std::numeric_limits<double>::max();
  assert(isnan(worst_case_absolute_error_gain_w(0, 0, largest, largest)));
}
}  // namespace

int main() {
  metadata_codes_and_legacy();
  monotonic_influence_and_separate_eligibility();
  paired_interval_loss_and_failure_boundaries();
}
