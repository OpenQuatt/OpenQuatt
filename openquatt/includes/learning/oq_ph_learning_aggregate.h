#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include "oq_ph_learning_quality.h"

namespace oq_power_house::learning {

constexpr uint32_t kDailyMaximumGapMs = 120000;
constexpr double kDailyMissingHeatBudgetW = 100.0;

struct SegmentAccumulator {
  bool active = false;
  bool source_gap_pending = false;
  uint64_t last_gap_observation_ms = 0;
  double missing_energy_uncertainty_ws = 0.0;
  uint64_t start_monotonic_ms = 0;
  uint64_t last_monotonic_ms = 0;
  uint32_t start_epoch_s = 0;
  uint32_t last_epoch_s = 0;
  uint32_t context_revision = 0;
  float room_start_c = NAN;
  float last_room_c = NAN;
  float last_setpoint_c = NAN;
  float last_outside_c = NAN;
  float last_heat_w = NAN;
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
  float hourly_effective_outside_c[24]{};
  double hour_effective_integral = 0.0;
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
  state.start_monotonic_ms = snapshot.monotonic_ms;
  state.last_monotonic_ms = snapshot.monotonic_ms;
  state.start_epoch_s = snapshot.epoch_s;
  state.last_epoch_s = snapshot.epoch_s;
  state.context_revision = snapshot.context_revision;
  state.room_start_c = snapshot.room_c;
  state.last_room_c = snapshot.room_c;
  state.last_setpoint_c = snapshot.setpoint_c;
  state.last_outside_c = snapshot.outside_c;
  state.last_heat_w = snapshot.heat_to_water_w;
  state.water_start_c = snapshot.mean_water_c;
  state.water_end_c = snapshot.mean_water_c;
  state.room_min_c = snapshot.room_c;
  state.room_max_c = snapshot.room_c;
  state.setpoint_min_c = snapshot.setpoint_c;
  state.setpoint_max_c = snapshot.setpoint_c;
}

inline bool same_context(const SegmentAccumulator& state, const LearningSnapshot& snapshot) {
  return state.context_revision == snapshot.context_revision;
}

inline bool coherent_time(const SegmentAccumulator& state, const LearningSnapshot& snapshot,
                          const QualityConfig& config, uint32_t max_interval_ms = 0) {
  if (snapshot.monotonic_ms <= state.last_monotonic_ms) return false;
  const uint64_t delta_ms = snapshot.monotonic_ms - state.last_monotonic_ms;
  if (delta_ms > (max_interval_ms == 0 ? config.max_interval_ms : max_interval_ms) ||
      snapshot.epoch_s < state.last_epoch_s)
    return false;
  const uint64_t epoch_delta_ms = static_cast<uint64_t>(snapshot.epoch_s - state.last_epoch_s) * 1000ULL;
  const uint64_t mismatch_ms = epoch_delta_ms > delta_ms ? epoch_delta_ms - delta_ms : delta_ms - epoch_delta_ms;
  if (mismatch_ms > static_cast<uint64_t>(config.utc_tolerance_s) * 1000ULL + 999ULL) return false;
  if (snapshot.epoch_s < state.start_epoch_s) return false;
  const uint64_t span_ms = snapshot.monotonic_ms - state.start_monotonic_ms;
  const uint64_t epoch_span_ms = static_cast<uint64_t>(snapshot.epoch_s - state.start_epoch_s) * 1000ULL;
  const uint64_t span_mismatch_ms = epoch_span_ms > span_ms ? epoch_span_ms - span_ms : span_ms - epoch_span_ms;
  return span_mismatch_ms <= static_cast<uint64_t>(config.utc_tolerance_s) * 1000ULL + 999ULL;
}

inline void update_last(SegmentAccumulator& state, const LearningSnapshot& snapshot) {
  state.last_monotonic_ms = snapshot.monotonic_ms;
  state.last_epoch_s = snapshot.epoch_s;
  state.last_room_c = snapshot.room_c;
  state.last_setpoint_c = snapshot.setpoint_c;
  state.last_outside_c = snapshot.outside_c;
  state.last_heat_w = snapshot.heat_to_water_w;
  state.water_end_c = snapshot.mean_water_c;
  state.room_min_c = fminf(state.room_min_c, snapshot.room_c);
  state.room_max_c = fmaxf(state.room_max_c, snapshot.room_c);
  state.setpoint_min_c = fminf(state.setpoint_min_c, snapshot.setpoint_c);
  state.setpoint_max_c = fmaxf(state.setpoint_max_c, snapshot.setpoint_c);
}

inline void integrate_interval(SegmentAccumulator& state, const LearningSnapshot& snapshot) {
  const double dt_s = static_cast<double>(snapshot.monotonic_ms - state.last_monotonic_ms) / 1000.0;
  const double start_s = static_cast<double>(state.last_monotonic_ms - state.start_monotonic_ms) / 1000.0;
  const double stop_s = static_cast<double>(snapshot.monotonic_ms - state.start_monotonic_ms) / 1000.0;
  const double mid_s = start_s + 0.5 * dt_s;
  const double room = 0.5 * (static_cast<double>(state.last_room_c) + snapshot.room_c);
  const double outside = 0.5 * (static_cast<double>(state.last_outside_c) + snapshot.outside_c);
  const double effective_start = state.last_outside_c + kReferenceRoomC - state.last_room_c;
  const double effective_end = snapshot.outside_c + kReferenceRoomC - snapshot.room_c;
  double cursor_s = start_s;
  while (cursor_s < stop_s) {
    const size_t hour = static_cast<size_t>(cursor_s / 3600.0);
    const double end_s = fmin(stop_s, (hour + 1U) * 3600.0);
    const double fraction = ((cursor_s + end_s) * 0.5 - start_s) / dt_s;
    state.hour_effective_integral +=
        (effective_start + fraction * (effective_end - effective_start)) * (end_s - cursor_s);
    if (end_s == (hour + 1U) * 3600.0) {
      state.hourly_effective_outside_c[hour] = static_cast<float>(state.hour_effective_integral / 3600.0);
      state.hour_effective_integral = 0.0;
    }
    cursor_s = end_s;
  }
  state.integrated_duration_s = stop_s;
  state.room_integral += room * dt_s;
  state.setpoint_integral += 0.5 * (static_cast<double>(state.last_setpoint_c) + snapshot.setpoint_c) * dt_s;
  state.outside_integral += outside * dt_s;
  state.heat_integral += 0.5 * (static_cast<double>(state.last_heat_w) + snapshot.heat_to_water_w) * dt_s;
  state.trend_w += dt_s;
  state.trend_wt += dt_s * mid_s;
  state.trend_wtt += dt_s * mid_s * mid_s;
  state.trend_wr += dt_s * room;
  state.trend_wtr += dt_s * mid_s * room;
  update_last(state, snapshot);
}

inline LearningSnapshot interpolate_boundary(const SegmentAccumulator& state, const LearningSnapshot& snapshot,
                                             uint64_t boundary_ms) {
  LearningSnapshot boundary = snapshot;
  const double fraction =
      static_cast<double>(boundary_ms - state.last_monotonic_ms) / (snapshot.monotonic_ms - state.last_monotonic_ms);
  boundary.monotonic_ms = boundary_ms;
  // Preserve tolerated UTC drift rather than assigning a synthetic future
  // timestamp that append_record() would reject as stale.
  boundary.epoch_s = state.last_epoch_s + static_cast<uint32_t>(fraction * (snapshot.epoch_s - state.last_epoch_s));
  boundary.room_c = state.last_room_c + fraction * (snapshot.room_c - state.last_room_c);
  boundary.setpoint_c = state.last_setpoint_c + fraction * (snapshot.setpoint_c - state.last_setpoint_c);
  boundary.outside_c = state.last_outside_c + fraction * (snapshot.outside_c - state.last_outside_c);
  boundary.heat_to_water_w = state.last_heat_w + fraction * (snapshot.heat_to_water_w - state.last_heat_w);
  boundary.mean_water_c = state.water_end_c + fraction * (snapshot.mean_water_c - state.water_end_c);
  return boundary;
}

inline SegmentRecord make_record(const SegmentAccumulator& state) {
  SegmentRecord record;
  record.start_epoch_s = state.start_epoch_s;
  record.end_epoch_s = state.last_epoch_s;
  record.duration_s = static_cast<uint32_t>((state.last_monotonic_ms - state.start_monotonic_ms) / 1000ULL);
  record.context_revision = state.context_revision;
  if (!(state.integrated_duration_s > 0.0)) return record;
  const double inverse_duration = 1.0 / state.integrated_duration_s;
  record.mean_room_c = static_cast<float>(state.room_integral * inverse_duration);
  record.mean_setpoint_c = static_cast<float>(state.setpoint_integral * inverse_duration);
  record.mean_outside_c = static_cast<float>(state.outside_integral * inverse_duration);
  record.mean_heat_w = static_cast<float>(state.heat_integral * inverse_duration);
  record.room_range_k = state.room_max_c - state.room_min_c;
  record.setpoint_range_c = state.setpoint_max_c - state.setpoint_min_c;
  record.water_start_c = state.water_start_c;
  record.water_end_c = state.water_end_c;
  const double denominator = state.trend_w * state.trend_wtt - state.trend_wt * state.trend_wt;
  if (denominator > 0.0) {
    const double slope_k_per_s = (state.trend_w * state.trend_wtr - state.trend_wt * state.trend_wr) / denominator;
    record.room_trend_k_per_h = static_cast<float>(slope_k_per_s * 3600.0);
  }
  const float endpoint_trend = (state.last_room_c - state.room_start_c) * 3600.0f / record.duration_s;
  if (fabsf(endpoint_trend) > fabsf(record.room_trend_k_per_h)) record.room_trend_k_per_h = endpoint_trend;
  float sorted[24];
  for (size_t index = 0; index < 24; ++index) {
    const float value = state.hourly_effective_outside_c[index];
    size_t insert = index;
    while (insert > 0 && sorted[insert - 1] > value) {
      sorted[insert] = sorted[insert - 1];
      --insert;
    }
    sorted[insert] = value;
  }
  for (size_t group = 0; group < kDailyTemperatureProfileSize; ++group)
    record.effective_outside_profile_centi[group] = static_cast<int16_t>(
        lroundf((sorted[3 * group] + sorted[3 * group + 1] + sorted[3 * group + 2]) * (100.0f / 3.0f)));
  return record;
}

}  // namespace detail

inline ObserveResult observe_snapshot(SegmentAccumulator& state, const LearningSnapshot& snapshot,
                                      const QualityConfig& config, bool may_bridge_daily_gap = false) {
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
  const uint64_t last_observation_ms =
      state.source_gap_pending ? state.last_gap_observation_ms : state.last_monotonic_ms;
  // Only explicit observations with a known operating state can hold a day.
  // A completely missing tick still obeys the normal interval limit.
  if (!timestamp_valid || snapshot.monotonic_ms <= last_observation_ms ||
      snapshot.monotonic_ms - last_observation_ms > config.max_interval_ms ||
      !detail::coherent_time(state, snapshot, config, state.source_gap_pending ? kDailyMaximumGapMs : 0)) {
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

  if (measurement_status != LearningStatus::OK) {
    if (may_bridge_daily_gap && snapshot.invalid_reasons != INVALID_NONE &&
        (snapshot.invalid_reasons & ~(INVALID_ESSENTIAL_SOURCE | INVALID_SOURCE_STALE)) == 0) {
      state.source_gap_pending = true;
      state.last_gap_observation_ms = snapshot.monotonic_ms;
      return result;  // Do not integrate missing values or advance the valid endpoint.
    }
    reset_segment(state);
    result.status = measurement_status;
    return result;
  }

  const uint64_t boundary_ms = state.start_monotonic_ms + kSegmentDurationMs;
  double remaining_uncertainty_ws = 0.0;
  if (state.source_gap_pending) {
    // Conditional bound on missing signed heat, assuming |heat| remains below
    // max_abs_heat_w. Temperature interpolation has no such error guarantee.
    const double bound_w = config.max_abs_heat_w + fmax(fabs(state.last_heat_w), fabs(snapshot.heat_to_water_w));
    const uint64_t end_ms = snapshot.monotonic_ms < boundary_ms ? snapshot.monotonic_ms : boundary_ms;
    const double uncertainty_ws = bound_w * (end_ms - state.last_monotonic_ms) / 1000.0;
    remaining_uncertainty_ws = bound_w * (snapshot.monotonic_ms - end_ms) / 1000.0;
    if (state.missing_energy_uncertainty_ws + uncertainty_ws > kDailyMissingHeatBudgetW * 86400.0 ||
        remaining_uncertainty_ws > kDailyMissingHeatBudgetW * 86400.0) {
      detail::seed_segment(state, snapshot);
      result.status = LearningStatus::SEGMENT_INELIGIBLE;
      return result;
    }
    state.missing_energy_uncertainty_ws += uncertainty_ws;
    state.source_gap_pending = false;
    state.last_gap_observation_ms = 0;
  }
  if (snapshot.monotonic_ms < boundary_ms) {
    detail::integrate_interval(state, snapshot);
    return result;
  }
  const LearningSnapshot boundary = detail::interpolate_boundary(state, snapshot, boundary_ms);
  detail::integrate_interval(state, boundary);
  result.record = detail::make_record(state);
  result.status = validate_segment_record(result.record, config);
  result.has_record = result.status == LearningStatus::OK;
  if (result.has_record) result.status = LearningStatus::SEGMENT_READY;
  // Reuse the endpoint and carry any remainder into the next day: no heat or
  // time is lost when the sample interval straddles the 24-hour boundary.
  detail::seed_segment(state, boundary);
  state.missing_energy_uncertainty_ws = remaining_uncertainty_ws;
  if (snapshot.monotonic_ms > boundary_ms) detail::integrate_interval(state, snapshot);
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

namespace detail {

inline size_t temperature_bin(float outside_c) {
  if (outside_c < 0.0f) return 0;
  if (outside_c < 5.0f) return 1;
  if (outside_c < 10.0f) return 2;
  return 3;
}

// With a full buffer, the oldest recent record becomes historical. Keep at
// most kHistoricalRecordsPerTemperatureBin records per temperature region, evicting the oldest redundant
// historical record. The remaining newest kRecentSegmentRecords records always stay recent.
inline size_t representative_record_to_evict(const RecordBuffer& buffer) {
  if (buffer.records == nullptr || buffer.count != buffer.capacity || buffer.capacity != kMaxSegmentRecords)
    return buffer.count;
  const size_t historical_count = buffer.count - kRecentSegmentRecords + 1U;
  size_t bins[kHistoricalTemperatureBins]{};
  for (size_t index = 0; index < historical_count; ++index)
    ++bins[temperature_bin(buffer.records[index].mean_outside_c)];
  size_t evict_bin = kHistoricalTemperatureBins;
  size_t largest_count = kHistoricalRecordsPerTemperatureBin;
  for (size_t bin = 0; bin < kHistoricalTemperatureBins; ++bin) {
    if (bins[bin] > largest_count) {
      evict_bin = bin;
      largest_count = bins[bin];
    }
  }
  if (evict_bin == kHistoricalTemperatureBins) return buffer.count;
  for (size_t index = 0; index < historical_count; ++index)
    if (temperature_bin(buffer.records[index].mean_outside_c) == evict_bin) return index;
  return buffer.count;
}

}  // namespace detail

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
    if (record.context_revision != previous.context_revision) return LearningStatus::MIXED_CONTEXT;
  }
  if (buffer.count == buffer.capacity) {
    const size_t selected_remove_index = detail::representative_record_to_evict(buffer);
    const size_t remove_index = selected_remove_index < buffer.count ? selected_remove_index : 0U;
    for (size_t index = remove_index + 1U; index < buffer.count; ++index)
      buffer.records[index - 1U] = buffer.records[index];
    --buffer.count;
  }
  buffer.records[buffer.count++] = record;
  return LearningStatus::OK;
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
