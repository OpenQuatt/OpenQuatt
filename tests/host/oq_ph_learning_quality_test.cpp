#include <assert.h>
#include <string.h>

#include "../../openquatt/includes/learning/oq_ph_learning_quality.h"

namespace {
using namespace oq_power_house::learning;

LearningSnapshot valid_snapshot() {
  LearningSnapshot snapshot;
  snapshot.monotonic_ms = 1000;
  snapshot.epoch_s = 1700000000;
  snapshot.context_revision = 1;
  snapshot.room_c = 20.0f;
  snapshot.setpoint_c = 20.0f;
  snapshot.outside_c = 5.0f;
  snapshot.heat_to_water_w = 2000.0f;
  snapshot.heat_uncertainty_w = 100.0f;
  snapshot.mean_water_c = 30.0f;
  return snapshot;
}

SegmentRecord valid_record() {
  SegmentRecord record;
  record.start_epoch_s = 20000U * 86400U + 3600U;
  record.end_epoch_s = record.start_epoch_s + 14400;
  record.duration_s = 14400;
  record.context_revision = 1;
  record.mean_room_c = 20.0f;
  record.mean_setpoint_c = 20.0f;
  record.mean_outside_c = 5.0f;
  record.mean_heat_w = 2000.0f;
  record.mean_heat_uncertainty_w = 100.0f;
  record.room_trend_k_per_h = 0.01f;
  record.room_range_k = 0.1f;
  record.setpoint_range_c = 0.01f;
  record.water_start_c = 30.0f;
  record.water_end_c = 30.5f;
  return record;
}
}  // namespace

int main() {
  using namespace oq_power_house::learning;
  QualityConfig config;
  assert(valid_quality_config(config));
  assert(validate_snapshot(valid_snapshot(), config) == LearningStatus::OK);

  auto snapshot = valid_snapshot();
  snapshot.invalid_reasons = INVALID_SOURCE_STALE;
  assert(validate_snapshot(snapshot, config) == LearningStatus::INVALID_MEASUREMENT);
  snapshot = valid_snapshot();
  snapshot.heat_to_water_w = NAN;
  assert(validate_snapshot(snapshot, config) == LearningStatus::INVALID_MEASUREMENT);

  auto record = valid_record();
  assert(evaluate_segment_quality(record, config) == LearningStatus::OK);
  assert(validate_segment_record(record, config) == LearningStatus::OK);
  record.mean_heat_w = 0.0f;
  assert(evaluate_segment_quality(record, config) == LearningStatus::NONPOSITIVE_HEAT);
  record = valid_record();
  record.setpoint_range_c = 0.1f;
  assert(evaluate_segment_quality(record, config) == LearningStatus::SETPOINT_CHANGED);
  record = valid_record();
  record.room_trend_k_per_h = 0.2f;
  assert(evaluate_segment_quality(record, config) == LearningStatus::ROOM_UNSTABLE);
  record = valid_record();
  record.water_end_c = 32.0f;
  assert(evaluate_segment_quality(record, config) == LearningStatus::WATER_STORAGE_UNSTABLE);
  record = valid_record();
  record.mean_heat_uncertainty_w = 500.0f;
  assert(evaluate_segment_quality(record, config) == LearningStatus::MEASUREMENT_UNCERTAIN);

  record = valid_record();
  record.duration_s = 1;
  record.end_epoch_s = record.start_epoch_s + 1;
  assert(validate_segment_record(record, config) == LearningStatus::SEGMENT_INELIGIBLE);
  record = valid_record();
  record.room_range_k = -0.1f;
  assert(validate_segment_record(record, config) == LearningStatus::INVALID_MEASUREMENT);
  record = valid_record();
  record.start_epoch_s = 20000U * 86400U + 22U * 3600U;
  record.end_epoch_s = record.start_epoch_s + 14400U;
  assert(validate_segment_record(record, config) == LearningStatus::TIME_DISCONTINUITY);

  snapshot = valid_snapshot();
  snapshot.context_revision = 0;
  assert(validate_snapshot(snapshot, config) == LearningStatus::INVALID_MEASUREMENT);
  snapshot = valid_snapshot();
  snapshot.invalid_reasons = INVALID_SETPOINT_RECOVERY;
  assert(validate_snapshot(snapshot, config) == LearningStatus::INVALID_MEASUREMENT);

  config.max_interval_ms = 0;
  assert(!valid_quality_config(config));
  assert(validate_snapshot(valid_snapshot(), config) == LearningStatus::INVALID_CONFIGURATION);
  assert(strcmp(learning_status_name(LearningStatus::MIXED_CONTEXT), "mixed_context") == 0);
  assert(strcmp(learning_status_name(LearningStatus::INSUFFICIENT_DATA), "insufficient_data") == 0);
  return 0;
}
