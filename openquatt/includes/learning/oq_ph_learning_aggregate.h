#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include "oq_ph_learning_quality.h"

namespace oq_power_house::learning {

struct SegmentAccumulator {
  bool active = false;
  bool poisoned = false;
  bool last_measurement_valid = false;
  LearningStatus poison_status = LearningStatus::OK;
  uint64_t start_monotonic_ms = 0;
  uint64_t last_monotonic_ms = 0;
  uint32_t start_epoch_s = 0;
  uint32_t last_epoch_s = 0;
  uint32_t source_generation = 0;
  uint32_t physical_context_generation = 0;
  uint32_t control_generation = 0;
  float last_room_c = NAN;
  float last_setpoint_c = NAN;
  float last_outside_c = NAN;
  float last_heat_w = NAN;
  float last_uncertainty_w = NAN;
  float water_start_c = NAN;
  float water_end_c = NAN;
  float room_min_c = NAN;
  float room_max_c = NAN;
  float setpoint_min_c = NAN;
  float setpoint_max_c = NAN;
  double integrated_duration_s = 0.0;
  double room_integral = 0.0;
  double setpoint_integral = 0.0;
  double outside_integral = 0.0;
  double heat_integral = 0.0;
  double uncertainty_integral = 0.0;
  double trend_w = 0.0;
  double trend_wt = 0.0;
  double trend_wtt = 0.0;
  double trend_wr = 0.0;
  double trend_wtr = 0.0;
};

struct ObserveResult {
  LearningStatus status = LearningStatus::COLLECTING;
  bool has_record = false;
  SegmentRecord record;
};

inline void reset_segment(SegmentAccumulator& state) { state = {}; }

namespace detail {

inline void seed_segment(SegmentAccumulator& state, const LearningSnapshot& snapshot) {
  state = {};
  state.active = true;
  state.last_measurement_valid = true;
  state.start_monotonic_ms = snapshot.monotonic_ms;
  state.last_monotonic_ms = snapshot.monotonic_ms;
  state.start_epoch_s = snapshot.epoch_s;
  state.last_epoch_s = snapshot.epoch_s;
  state.source_generation = snapshot.source_generation;
  state.physical_context_generation = snapshot.physical_context_generation;
  state.control_generation = snapshot.control_generation;
  state.last_room_c = snapshot.room_c;
  state.last_setpoint_c = snapshot.setpoint_c;
  state.last_outside_c = snapshot.outside_c;
  state.last_heat_w = snapshot.heat_to_water_w;
  state.last_uncertainty_w = snapshot.heat_uncertainty_w;
  state.water_start_c = snapshot.mean_water_c;
  state.water_end_c = snapshot.mean_water_c;
  state.room_min_c = snapshot.room_c;
  state.room_max_c = snapshot.room_c;
  state.setpoint_min_c = snapshot.setpoint_c;
  state.setpoint_max_c = snapshot.setpoint_c;
}

inline bool same_context(const SegmentAccumulator& state, const LearningSnapshot& snapshot) {
  return state.source_generation == snapshot.source_generation &&
         state.physical_context_generation == snapshot.physical_context_generation &&
         state.control_generation == snapshot.control_generation;
}

inline bool coherent_time(const SegmentAccumulator& state, const LearningSnapshot& snapshot,
                          const QualityConfig& config) {
  if (snapshot.monotonic_ms <= state.last_monotonic_ms) return false;
  const uint64_t delta_ms = snapshot.monotonic_ms - state.last_monotonic_ms;
  if (delta_ms > config.max_interval_ms || snapshot.epoch_s < state.last_epoch_s) return false;
  const uint64_t epoch_delta_ms = static_cast<uint64_t>(snapshot.epoch_s - state.last_epoch_s) * 1000ULL;
  const uint64_t mismatch_ms = epoch_delta_ms > delta_ms ? epoch_delta_ms - delta_ms : delta_ms - epoch_delta_ms;
  if (mismatch_ms > static_cast<uint64_t>(config.utc_tolerance_s) * 1000ULL + 999ULL) return false;
  if (snapshot.epoch_s < state.start_epoch_s) return false;
  const uint64_t span_ms = snapshot.monotonic_ms - state.start_monotonic_ms;
  const uint64_t epoch_span_ms = static_cast<uint64_t>(snapshot.epoch_s - state.start_epoch_s) * 1000ULL;
  const uint64_t span_mismatch_ms = epoch_span_ms > span_ms ? epoch_span_ms - span_ms : span_ms - epoch_span_ms;
  return span_mismatch_ms <= static_cast<uint64_t>(config.utc_tolerance_s) * 1000ULL + 999ULL;
}

inline void update_last(SegmentAccumulator& state, const LearningSnapshot& snapshot, bool valid) {
  state.last_monotonic_ms = snapshot.monotonic_ms;
  state.last_epoch_s = snapshot.epoch_s;
  state.last_measurement_valid = valid;
  if (!valid) return;
  state.last_room_c = snapshot.room_c;
  state.last_setpoint_c = snapshot.setpoint_c;
  state.last_outside_c = snapshot.outside_c;
  state.last_heat_w = snapshot.heat_to_water_w;
  state.last_uncertainty_w = snapshot.heat_uncertainty_w;
  state.water_end_c = snapshot.mean_water_c;
  state.room_min_c = fminf(state.room_min_c, snapshot.room_c);
  state.room_max_c = fmaxf(state.room_max_c, snapshot.room_c);
  state.setpoint_min_c = fminf(state.setpoint_min_c, snapshot.setpoint_c);
  state.setpoint_max_c = fmaxf(state.setpoint_max_c, snapshot.setpoint_c);
}

inline SegmentRecord make_record(const SegmentAccumulator& state) {
  SegmentRecord record;
  record.start_epoch_s = state.start_epoch_s;
  record.end_epoch_s = state.last_epoch_s;
  record.duration_s = static_cast<uint32_t>((state.last_monotonic_ms - state.start_monotonic_ms) / 1000ULL);
  record.source_generation = state.source_generation;
  record.physical_context_generation = state.physical_context_generation;
  record.control_generation = state.control_generation;
  if (!(state.integrated_duration_s > 0.0)) return record;
  const double inverse_duration = 1.0 / state.integrated_duration_s;
  record.mean_room_c = static_cast<float>(state.room_integral * inverse_duration);
  record.mean_setpoint_c = static_cast<float>(state.setpoint_integral * inverse_duration);
  record.mean_outside_c = static_cast<float>(state.outside_integral * inverse_duration);
  record.mean_heat_w = static_cast<float>(state.heat_integral * inverse_duration);
  record.mean_heat_uncertainty_w = static_cast<float>(state.uncertainty_integral * inverse_duration);
  record.room_range_k = state.room_max_c - state.room_min_c;
  record.setpoint_range_c = state.setpoint_max_c - state.setpoint_min_c;
  record.water_start_c = state.water_start_c;
  record.water_end_c = state.water_end_c;
  const double denominator = state.trend_w * state.trend_wtt - state.trend_wt * state.trend_wt;
  if (denominator > 0.0) {
    const double slope_k_per_s = (state.trend_w * state.trend_wtr - state.trend_wt * state.trend_wr) / denominator;
    record.room_trend_k_per_h = static_cast<float>(slope_k_per_s * 3600.0);
  }
  return record;
}

}  // namespace detail

inline ObserveResult observe_snapshot(SegmentAccumulator& state, const LearningSnapshot& snapshot,
                                      const QualityConfig& config) {
  ObserveResult result;
  if (!valid_quality_config(config)) {
    result.status = LearningStatus::INVALID_CONFIGURATION;
    return result;
  }
  const LearningStatus measurement_status = validate_snapshot(snapshot, config);
  const bool timestamp_valid = snapshot.monotonic_ms != 0 && snapshot.epoch_s != 0;
  if (!state.active) {
    if (measurement_status != LearningStatus::OK) {
      result.status = measurement_status;
      return result;
    }
    detail::seed_segment(state, snapshot);
    return result;
  }
  if (!timestamp_valid || !detail::coherent_time(state, snapshot, config)) {
    reset_segment(state);
    if (measurement_status == LearningStatus::OK) detail::seed_segment(state, snapshot);
    result.status = LearningStatus::TIME_DISCONTINUITY;
    return result;
  }
  if (!detail::same_context(state, snapshot)) {
    reset_segment(state);
    if (measurement_status == LearningStatus::OK) detail::seed_segment(state, snapshot);
    result.status = LearningStatus::MIXED_CONTEXT;
    return result;
  }

  const double dt_s = static_cast<double>(snapshot.monotonic_ms - state.last_monotonic_ms) / 1000.0;
  if (measurement_status != LearningStatus::OK) {
    state.poisoned = true;
    state.poison_status = LearningStatus::INVALID_MEASUREMENT;
    detail::update_last(state, snapshot, false);
  } else if (!state.last_measurement_valid) {
    detail::update_last(state, snapshot, true);
  } else {
    const double elapsed_mid_s =
        static_cast<double>(state.last_monotonic_ms - state.start_monotonic_ms) / 1000.0 + 0.5 * dt_s;
    const double mean_room_c = 0.5 * (static_cast<double>(state.last_room_c) + snapshot.room_c);
    state.integrated_duration_s += dt_s;
    state.room_integral += mean_room_c * dt_s;
    state.setpoint_integral += 0.5 * (static_cast<double>(state.last_setpoint_c) + snapshot.setpoint_c) * dt_s;
    state.outside_integral += 0.5 * (static_cast<double>(state.last_outside_c) + snapshot.outside_c) * dt_s;
    state.heat_integral += 0.5 * (static_cast<double>(state.last_heat_w) + snapshot.heat_to_water_w) * dt_s;
    state.uncertainty_integral +=
        0.5 * (static_cast<double>(state.last_uncertainty_w) + snapshot.heat_uncertainty_w) * dt_s;
    state.trend_w += dt_s;
    state.trend_wt += dt_s * elapsed_mid_s;
    state.trend_wtt += dt_s * elapsed_mid_s * elapsed_mid_s;
    state.trend_wr += dt_s * mean_room_c;
    state.trend_wtr += dt_s * elapsed_mid_s * mean_room_c;
    detail::update_last(state, snapshot, true);
  }

  if (snapshot.monotonic_ms - state.start_monotonic_ms < kSegmentDurationMs) {
    result.status = measurement_status == LearningStatus::OK ? LearningStatus::COLLECTING : measurement_status;
    return result;
  }
  if (state.poisoned) {
    result.status = state.poison_status;
    reset_segment(state);
    return result;
  }

  result.record = detail::make_record(state);
  result.status = validate_segment_record(result.record, config);
  result.has_record = result.status == LearningStatus::OK;
  if (result.has_record) result.status = LearningStatus::SEGMENT_READY;
  reset_segment(state);
  return result;
}

inline LearningStatus prune_expired_records(RecordBuffer& buffer, uint32_t now_epoch_s) {
  if (buffer.records == nullptr || buffer.capacity == 0 || buffer.capacity > kMaxSegmentRecords ||
      buffer.count > buffer.capacity || now_epoch_s == 0)
    return LearningStatus::INVALID_CONFIGURATION;
  size_t remove_count = 0;
  while (remove_count < buffer.count) {
    const SegmentRecord& record = buffer.records[remove_count];
    if (record.end_epoch_s > now_epoch_s) return LearningStatus::TIME_DISCONTINUITY;
    if (now_epoch_s - record.end_epoch_s <= kMaxRecordAgeS) break;
    ++remove_count;
  }
  for (size_t index = remove_count; index < buffer.count; ++index)
    buffer.records[index - remove_count] = buffer.records[index];
  buffer.count -= remove_count;
  return LearningStatus::OK;
}

inline LearningStatus append_record(RecordBuffer& buffer, const SegmentRecord& record, uint32_t now_epoch_s,
                                    const QualityConfig& config) {
  const LearningStatus record_status = validate_segment_record(record, config);
  if (record_status != LearningStatus::OK) return record_status;
  if (record.end_epoch_s > now_epoch_s || now_epoch_s - record.end_epoch_s > kMaxRecordAgeS)
    return LearningStatus::STALE_DATA;
  const LearningStatus prune_status = prune_expired_records(buffer, now_epoch_s);
  if (prune_status != LearningStatus::OK) return prune_status;
  if (buffer.count > 0) {
    const SegmentRecord& previous = buffer.records[buffer.count - 1];
    if (record.start_epoch_s < previous.end_epoch_s) return LearningStatus::TIME_DISCONTINUITY;
    if (record.source_generation != previous.source_generation ||
        record.physical_context_generation != previous.physical_context_generation)
      return LearningStatus::MIXED_CONTEXT;
    if (record.control_generation < previous.control_generation) return LearningStatus::TIME_DISCONTINUITY;
  }
  if (buffer.count == buffer.capacity) {
    for (size_t index = 1; index < buffer.count; ++index) buffer.records[index - 1] = buffer.records[index];
    --buffer.count;
  }
  buffer.records[buffer.count++] = record;
  return LearningStatus::OK;
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
