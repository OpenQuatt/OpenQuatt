#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include "oq_ph_learning_quality.h"
#include "oq_ph_daily_gap_uncertainty.h"

namespace oq_power_house::learning {

constexpr uint32_t kDailyMaximumGapMs = 120000;
// Only the owner of a verified, one-use software-restart ticket may select
// this boot/source-startup ceiling. Model-advice quality stays separate.
constexpr uint32_t kDailyMaximumRestartGapMs = 300000;
constexpr double kDailyMissingHeatBudgetW = 100.0;

enum class DailyRestartPolicy : uint8_t { STANDARD = 0, VERIFIED_SOFTWARE_RESTART };

inline uint32_t daily_restart_gap_limit_ms(DailyRestartPolicy policy) {
  return policy == DailyRestartPolicy::VERIFIED_SOFTWARE_RESTART ? kDailyMaximumRestartGapMs : kDailyMaximumGapMs;
}

// The planned-restart ceiling covers the first source-startup observation only.
// Once WAIT is observed, every later owner tick must keep the normal cadence.
inline bool coherent_daily_restart_tick(uint64_t previous_ms, uint64_t now_ms, const QualityConfig& config) {
  return now_ms != 0 && (previous_ms == 0 || (now_ms > previous_ms && now_ms - previous_ms <= config.max_interval_ms));
}

struct SegmentAccumulator {
  bool active = false;
  bool source_gap_pending = false;
  bool restart_pending = false;
  // Boot-local permission, deliberately absent from the journal encoding.
  DailyRestartPolicy restart_policy = DailyRestartPolicy::STANDARD;
  // Time collected in earlier boots; boot-local clocks are never persisted.
  uint64_t carried_duration_ms = 0;
  uint64_t last_gap_observation_ms = 0;
  double missing_energy_uncertainty_ws = 0.0;
  // Bound evidence while a total snapshot is incomplete, never an integrated
  // endpoint. Schema8 persists at least the configured envelope plus this floor.
  float gap_heat_bound_floor_w = 0.0f;
  // Independent signed heat cursor; elapsed time is relative to this day's
  // origin, never a persisted monotonic clock from another boot.
  float heat_last_w = NAN;
  uint32_t heat_last_elapsed_ms = 0, heat_last_epoch_s = 0;
  uint32_t heat_observed_ms = 0, temperature_observed_ms = 0, next_heat_observed_ms = 0;
  double next_heat_integral_ws = 0.0, next_heat_uncertainty_ws = 0.0;
  bool heat_gap_pending = false, daily_quality_known = false;
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

// Diagnostic only: these causes do not change eligibility, thresholds or the
// persisted accumulator/record formats.
enum class DailyCollectionCause : uint8_t {
  NONE = 0,
  INVALID_MEASUREMENT,
  MISSED_TICK,
  GAP_TOO_LONG,
  CLOCK_DISCONTINUITY,
  CONTEXT_CHANGED,
  UNCERTAINTY_BUDGET,
  COMPLETED_QUALITY,
  RESTART_DISCARDED,
  APPEND_REJECTED,
  PAUSED,
  DISABLED,
  INVALID_INPUT,
  INVALID_CONFIGURATION,
  EVIDENCE_REPLACED,
  COUNT,
};

struct ObserveResult {
  LearningStatus status = LearningStatus::COLLECTING;
  bool has_record = false;
  DailyCollectionCause cause = DailyCollectionCause::NONE;
  bool discarded_next_day_remainder = false;
  double completed_missing_energy_uncertainty_ws = NAN;
  SegmentRecord record;
};

inline void reset_segment(SegmentAccumulator& state) { state = {}; }

inline uint64_t segment_elapsed_ms(const SegmentAccumulator& state) {
  return state.carried_duration_ms + state.last_monotonic_ms - state.start_monotonic_ms;
}

// Validate the compact checkpoint, not the final day's learning quality. Room
// drift is evaluated only when a complete 24-hour record is available.
inline bool valid_daily_checkpoint(const SegmentAccumulator& state, const QualityConfig& config) {
  if (!valid_quality_config(config) || !state.active || state.context_revision == 0 || state.start_epoch_s == 0 ||
      state.last_epoch_s < state.start_epoch_s || !isfinite(state.integrated_duration_s) ||
      state.integrated_duration_s < 0.0 || state.integrated_duration_s >= 86400.0 ||
      !isfinite(state.missing_energy_uncertainty_ws) || state.missing_energy_uncertainty_ws < 0.0 ||
      state.missing_energy_uncertainty_ws > 2.0 * config.max_abs_heat_w * 86400.0 ||
      !isfinite(state.gap_heat_bound_floor_w) || state.gap_heat_bound_floor_w < 0.0f ||
      state.gap_heat_bound_floor_w > config.max_abs_heat_w)
    return false;
  const uint64_t elapsed_ms = segment_elapsed_ms(state);
  const uint64_t heat_ms = state.heat_last_elapsed_ms;
  const uint64_t day_ms = kSegmentDurationMs;
  const uint64_t next_ms = heat_ms > day_ms ? heat_ms - day_ms : 0;
  if (!isfinite(state.heat_last_w) || fabsf(state.heat_last_w) > config.max_abs_heat_w || heat_ms < elapsed_ms ||
      heat_ms > day_ms + kDailyMaximumRestartGapMs || state.heat_last_epoch_s < state.last_epoch_s ||
      state.heat_observed_ms > (heat_ms < day_ms ? heat_ms : day_ms) || state.temperature_observed_ms > elapsed_ms ||
      state.next_heat_observed_ms > next_ms || !isfinite(state.next_heat_integral_ws) ||
      !isfinite(state.next_heat_uncertainty_ws) || state.next_heat_uncertainty_ws < 0.0 ||
      state.next_heat_uncertainty_ws > 2.0 * config.max_abs_heat_w * static_cast<double>(next_ms) / 1000.0 + 1.0 ||
      fabs(state.next_heat_integral_ws) > config.max_abs_heat_w * static_cast<double>(next_ms) / 1000.0 + 1.0 ||
      fabs(static_cast<double>(state.heat_last_epoch_s - state.start_epoch_s) * 1000.0 - heat_ms) >
          (config.utc_tolerance_s + 1U) * 1000.0)
    return false;
  const double utc_span = state.last_epoch_s - state.start_epoch_s;
  if (fabs(utc_span - state.integrated_duration_s) > config.utc_tolerance_s + 1.0) return false;
  LearningSnapshot endpoint;
  endpoint.monotonic_ms = 1;
  endpoint.epoch_s = state.last_epoch_s;
  endpoint.context_revision = state.context_revision;
  endpoint.room_c = state.last_room_c;
  endpoint.setpoint_c = state.last_setpoint_c;
  endpoint.outside_c = state.last_outside_c;
  endpoint.heat_to_water_w = state.last_heat_w;
  endpoint.mean_water_c = state.water_end_c;
  if (validate_snapshot_structure(endpoint, config) != LearningStatus::OK || !isfinite(state.room_start_c) ||
      state.room_start_c < config.room_min_c || state.room_start_c > config.room_max_c ||
      !isfinite(state.water_start_c) || state.water_start_c < config.water_min_c ||
      state.water_start_c > config.water_max_c || !isfinite(state.room_min_c) || !isfinite(state.room_max_c) ||
      state.room_min_c < config.room_min_c || state.room_max_c > config.room_max_c ||
      state.room_min_c > fminf(state.room_start_c, state.last_room_c) ||
      state.room_max_c < fmaxf(state.room_start_c, state.last_room_c) || !isfinite(state.setpoint_min_c) ||
      !isfinite(state.setpoint_max_c) || state.setpoint_min_c < config.setpoint_min_c ||
      state.setpoint_max_c > config.setpoint_max_c || state.setpoint_min_c > state.last_setpoint_c ||
      state.setpoint_max_c < state.last_setpoint_c)
    return false;
  const double sums[]{state.room_integral,
                      state.setpoint_integral,
                      state.outside_integral,
                      state.heat_integral,
                      state.hour_effective_integral,
                      state.trend_w,
                      state.trend_wt,
                      state.trend_wtt,
                      state.trend_wr,
                      state.trend_wtr};
  for (double value : sums)
    if (!isfinite(value)) return false;
  for (float value : state.hourly_effective_outside_c)
    if (!isfinite(value)) return false;
  return fabs(state.trend_w - state.integrated_duration_s) < 0.001;
}

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
  state.heat_last_w = snapshot.heat_to_water_w;
  state.heat_last_epoch_s = snapshot.epoch_s;
  state.daily_quality_known = true;
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
  const uint64_t span_ms = state.carried_duration_ms + snapshot.monotonic_ms - state.start_monotonic_ms;
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

struct DailyHeatInterval {
  bool valid = false, measured_endpoint = false;
  float endpoint_w = NAN;
  uint32_t elapsed_ms = 0, observed_ms = 0, next_observed_ms = 0;
  double energy_ws = 0.0, next_energy_ws = 0.0, uncertainty_ws = 0.0, next_uncertainty_ws = 0.0;
};

inline double signed_heat_interval_ws(float first, float last, uint32_t total_ms, uint32_t begin_ms, uint32_t end_ms) {
  if (begin_ms == end_ms) return 0.0;
  const double midpoint = static_cast<double>(begin_ms + end_ms) / (2.0 * total_ms);
  return (static_cast<double>(first) + midpoint * (static_cast<double>(last) - first)) * (end_ms - begin_ms) / 1000.0;
}

inline DailyHeatInterval daily_heat_interval(const SegmentAccumulator& state, const LearningSnapshot& snapshot,
                                             const QualityConfig& config, uint64_t elapsed_ms, bool force_gap = false) {
  DailyHeatInterval out;
  const bool full = validate_snapshot(snapshot, config) == LearningStatus::OK;
  const float heat = full ? snapshot.heat_to_water_w : snapshot.partial_heat_to_water_w;
  if (isnan(heat)) {
    out.valid = true;
    return out;
  }
  if (!isfinite(heat) || fabsf(heat) > configured_measurement_heat_limit_w(config) || !isfinite(state.heat_last_w) ||
      elapsed_ms <= state.heat_last_elapsed_ms || elapsed_ms > kSegmentDurationMs + kDailyMaximumRestartGapMs)
    return out;
  const uint32_t begin = state.heat_last_elapsed_ms;
  const uint32_t end = static_cast<uint32_t>(elapsed_ms);
  const uint32_t duration = end - begin;
  const uint32_t old_end = end < kSegmentDurationMs ? end : static_cast<uint32_t>(kSegmentDurationMs);
  const uint32_t first_ms = begin < old_end ? old_end - begin : 0U;
  const uint32_t next_begin = begin > kSegmentDurationMs ? begin : static_cast<uint32_t>(kSegmentDurationMs);
  const uint32_t next_ms = end > next_begin ? end - next_begin : 0U;
  const bool missing = state.heat_gap_pending || force_gap;
  const double bound_w = daily_missing_heat_bound_w(config, state.heat_last_w, heat, state.gap_heat_bound_floor_w);
  out.energy_ws = signed_heat_interval_ws(state.heat_last_w, heat, duration, 0, first_ms);
  const uint32_t next_offset = next_ms > 0 ? next_begin - begin : duration;
  out.next_energy_ws = signed_heat_interval_ws(state.heat_last_w, heat, duration, next_offset, duration);
  if (missing) {
    out.uncertainty_ws = daily_gap_uncertainty_ws(bound_w, state.heat_last_w, heat, duration, 0, first_ms);
    out.next_uncertainty_ws =
        daily_gap_uncertainty_ws(bound_w, state.heat_last_w, heat, duration, next_offset, duration);
  } else {
    out.observed_ms = first_ms;
    out.next_observed_ms = next_ms;
  }
  out.valid = isfinite(out.energy_ws) && isfinite(out.next_energy_ws) && isfinite(out.uncertainty_ws) &&
              isfinite(out.next_uncertainty_ws);
  out.measured_endpoint = true;
  out.endpoint_w = heat;
  out.elapsed_ms = end;
  return out;
}

inline bool observe_daily_heat(SegmentAccumulator& state, const LearningSnapshot& snapshot, const QualityConfig& config,
                               uint64_t elapsed_ms, bool force_gap = false) {
  const auto interval = daily_heat_interval(state, snapshot, config, elapsed_ms, force_gap);
  if (!interval.valid) return false;
  if (!interval.measured_endpoint) {
    state.heat_gap_pending = true;
    return true;
  }
  state.heat_integral += interval.energy_ws;
  state.next_heat_integral_ws += interval.next_energy_ws;
  state.missing_energy_uncertainty_ws += interval.uncertainty_ws;
  state.next_heat_uncertainty_ws += interval.next_uncertainty_ws;
  state.heat_observed_ms += interval.observed_ms;
  state.next_heat_observed_ms += interval.next_observed_ms;
  state.heat_last_w = interval.endpoint_w;
  state.heat_last_elapsed_ms = interval.elapsed_ms;
  state.heat_last_epoch_s = snapshot.epoch_s;
  state.heat_gap_pending = false;
  return true;
}

// During verified restart WAIT, retain only independently valid heat in a
// caller-owned copy. The immutable journal and temperature checkpoint stay put.
inline bool observe_restart_heat_only(SegmentAccumulator& state, const LearningSnapshot& snapshot,
                                      const QualityConfig& config, uint32_t maximum_gap_ms) {
  if (!valid_daily_checkpoint(state, config) || snapshot.epoch_s < state.last_epoch_s ||
      snapshot.epoch_s < state.heat_last_epoch_s || !isfinite(snapshot.gap_heat_observed_abs_w) ||
      snapshot.gap_heat_observed_abs_w < 0.0f || snapshot.gap_heat_observed_abs_w > config.max_abs_heat_w ||
      static_cast<uint64_t>(snapshot.epoch_s - state.last_epoch_s) * 1000ULL > maximum_gap_ms)
    return false;
  const uint64_t elapsed_ms =
      segment_elapsed_ms(state) + static_cast<uint64_t>(snapshot.epoch_s - state.last_epoch_s) * 1000ULL;
  state.gap_heat_bound_floor_w = fmaxf(state.gap_heat_bound_floor_w, snapshot.gap_heat_observed_abs_w);
  if (elapsed_ms == state.heat_last_elapsed_ms) {
    // Repeated UTC must not hide a missing/rejected heat receipt. Keep the
    // cursor and measured energy unchanged, but bound the next interval.
    if (isnan(snapshot.partial_heat_to_water_w) && validate_snapshot(snapshot, config) != LearningStatus::OK)
      state.heat_gap_pending = true;
    return true;
  }
  return observe_daily_heat(state, snapshot, config, elapsed_ms);
}

inline void integrate_interval(SegmentAccumulator& state, const LearningSnapshot& snapshot) {
  const double dt_s = static_cast<double>(snapshot.monotonic_ms - state.last_monotonic_ms) / 1000.0;
  const double start_s = static_cast<double>(segment_elapsed_ms(state)) / 1000.0;
  const double stop_s =
      static_cast<double>(state.carried_duration_ms + snapshot.monotonic_ms - state.start_monotonic_ms) / 1000.0;
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
  // Signed heat is integrated separately, including independently valid heat
  // while this complete temperature/operation snapshot was unavailable.
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
  record.duration_s = static_cast<uint32_t>(segment_elapsed_ms(state) / 1000ULL);
  record.context_revision = state.context_revision;
  if (!(state.integrated_duration_s > 0.0)) return record;
  const double inverse_duration = 1.0 / state.integrated_duration_s;
  record.mean_room_c = static_cast<float>(state.room_integral * inverse_duration);
  record.mean_setpoint_c = static_cast<float>(state.setpoint_integral * inverse_duration);
  record.mean_outside_c = static_cast<float>(state.outside_integral * inverse_duration);
  record.mean_heat_w = static_cast<float>(state.heat_integral * inverse_duration);
  const double mean_uncertainty_w = ceil(state.missing_energy_uncertainty_ws / 86400.0);
  record.daily_quality.heat_uncertainty_mean_w = mean_uncertainty_w >= kDailyUncertaintyOverflow
                                                     ? kDailyUncertaintyOverflow
                                                     : static_cast<uint16_t>(mean_uncertainty_w);
  if (state.daily_quality_known) {
    record.daily_quality.heat_coverage_bp =
        static_cast<uint16_t>(static_cast<uint64_t>(state.heat_observed_ms) * kDailyCoverageScale / kSegmentDurationMs);
    record.daily_quality.temperature_coverage_bp = static_cast<uint16_t>(
        static_cast<uint64_t>(state.temperature_observed_ms) * kDailyCoverageScale / kSegmentDurationMs);
  }
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

namespace detail {

inline ObserveResult observe_snapshot_core(SegmentAccumulator& state, const LearningSnapshot& snapshot,
                                           const QualityConfig& config, bool may_bridge_daily_gap,
                                           bool resuming_restart = false,
                                           DailyRestartPolicy restart_policy = DailyRestartPolicy::STANDARD) {
  ObserveResult result;
  const uint32_t maximum_gap_ms = resuming_restart ? daily_restart_gap_limit_ms(restart_policy) : kDailyMaximumGapMs;
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
  if (!timestamp_valid || snapshot.monotonic_ms <= last_observation_ms)
    result.cause = DailyCollectionCause::CLOCK_DISCONTINUITY;
  else if (!resuming_restart && snapshot.monotonic_ms - last_observation_ms > config.max_interval_ms)
    result.cause = DailyCollectionCause::MISSED_TICK;
  else if (state.source_gap_pending && snapshot.monotonic_ms - state.last_monotonic_ms > maximum_gap_ms)
    result.cause = DailyCollectionCause::GAP_TOO_LONG;
  else if (!detail::coherent_time(state, snapshot, config, state.source_gap_pending ? maximum_gap_ms : 0))
    result.cause = DailyCollectionCause::CLOCK_DISCONTINUITY;
  if (result.cause != DailyCollectionCause::NONE) {
    reset_segment(state);
    if (measurement_status == LearningStatus::OK) detail::seed_segment(state, snapshot);
    result.status = LearningStatus::TIME_DISCONTINUITY;
    return result;
  }
  if (!detail::same_context(state, snapshot)) {
    reset_segment(state);
    if (measurement_status == LearningStatus::OK) detail::seed_segment(state, snapshot);
    result.status = LearningStatus::MIXED_CONTEXT;
    result.cause = DailyCollectionCause::CONTEXT_CHANGED;
    return result;
  }

  const bool full = measurement_status == LearningStatus::OK;
  if (!full) {
    const bool scalar_gap = snapshot.invalid_reasons != INVALID_NONE &&
                            (snapshot.invalid_reasons & ~(INVALID_ESSENTIAL_SOURCE | INVALID_SOURCE_STALE)) == 0;
    const bool stop_transition = snapshot.invalid_reasons == INVALID_CONTROL_MODE;
    if (!may_bridge_daily_gap || (!scalar_gap && !stop_transition) || !isfinite(snapshot.gap_heat_observed_abs_w) ||
        snapshot.gap_heat_observed_abs_w < 0.0f || snapshot.gap_heat_observed_abs_w > config.max_abs_heat_w) {
      reset_segment(state);
      result.status = measurement_status;
      result.cause = DailyCollectionCause::INVALID_MEASUREMENT;
      return result;
    }
    state.gap_heat_bound_floor_w = fmaxf(state.gap_heat_bound_floor_w, snapshot.gap_heat_observed_abs_w);
  }
  const uint64_t elapsed_ms = state.carried_duration_ms + snapshot.monotonic_ms - state.start_monotonic_ms;
  if (!observe_daily_heat(state, snapshot, config, elapsed_ms)) {
    reset_segment(state);
    if (full) detail::seed_segment(state, snapshot);
    result.status = LearningStatus::INVALID_MEASUREMENT;
    result.cause = DailyCollectionCause::INVALID_INPUT;
    return result;
  }
  if (!full) {
    state.source_gap_pending = true;
    state.last_gap_observation_ms = snapshot.monotonic_ms;
    return result;
  }

  // Retention is independent of the model-advice budget. Temperature intervals
  // spanning any incomplete total frame remain explicitly interpolated.
  const bool temperature_measured = !state.source_gap_pending && !resuming_restart;
  const uint64_t boundary_ms = state.start_monotonic_ms + kSegmentDurationMs - state.carried_duration_ms;
  if (snapshot.monotonic_ms < boundary_ms) {
    if (temperature_measured)
      state.temperature_observed_ms += static_cast<uint32_t>(snapshot.monotonic_ms - state.last_monotonic_ms);
    detail::integrate_interval(state, snapshot);
    state.source_gap_pending = false;
    state.last_gap_observation_ms = 0;
    state.gap_heat_bound_floor_w = 0.0f;
    return result;
  }
  if (temperature_measured)
    state.temperature_observed_ms += static_cast<uint32_t>(boundary_ms - state.last_monotonic_ms);
  const LearningSnapshot boundary = detail::interpolate_boundary(state, snapshot, boundary_ms);
  detail::integrate_interval(state, boundary);
  result.record = detail::make_record(state);
  result.completed_missing_energy_uncertainty_ws = state.missing_energy_uncertainty_ws;
  result.status = validate_segment_record_structure(result.record, config);
  result.has_record = result.status == LearningStatus::OK;
  if (result.has_record)
    result.status = LearningStatus::SEGMENT_READY;
  else
    result.cause = DailyCollectionCause::COMPLETED_QUALITY;

  // Q can have advanced through the boundary while temperature was missing.
  // Transfer only the already split next-day bucket, never integrate it twice.
  const double next_heat_ws = state.next_heat_integral_ws;
  const double next_uncertainty_ws = state.next_heat_uncertainty_ws;
  const uint32_t next_heat_observed_ms = state.next_heat_observed_ms;
  const uint32_t heat_phase = state.heat_last_elapsed_ms - static_cast<uint32_t>(kSegmentDurationMs);
  const float heat_last_w = state.heat_last_w;
  const uint32_t heat_epoch = state.heat_last_epoch_s;
  detail::seed_segment(state, boundary);
  if (snapshot.monotonic_ms > boundary_ms) {
    if (temperature_measured)
      state.temperature_observed_ms = static_cast<uint32_t>(snapshot.monotonic_ms - boundary_ms);
    detail::integrate_interval(state, snapshot);
  }
  state.heat_integral = next_heat_ws;
  state.missing_energy_uncertainty_ws = next_uncertainty_ws;
  state.heat_observed_ms = next_heat_observed_ms;
  state.heat_last_elapsed_ms = heat_phase;
  state.heat_last_w = heat_last_w;
  state.heat_last_epoch_s = heat_epoch;
  return result;
}

}  // namespace detail

inline ObserveResult observe_snapshot(SegmentAccumulator& state, const LearningSnapshot& snapshot,
                                      const QualityConfig& config, bool may_bridge_daily_gap = false) {
  if (!state.restart_pending) return detail::observe_snapshot_core(state, snapshot, config, may_bridge_daily_gap);
  const auto restart_policy = state.restart_policy;
  state.restart_pending = false;
  state.restart_policy = DailyRestartPolicy::STANDARD;
  // The first fully valid post-boot observation bounds the *whole* interval
  // since the saved endpoint, including upload, reboot and source warm-up.
  if (!valid_daily_checkpoint(state, config) || validate_snapshot(snapshot, config) != LearningStatus::OK ||
      snapshot.epoch_s <= state.last_epoch_s ||
      static_cast<uint64_t>(snapshot.epoch_s - state.last_epoch_s) * 1000ULL >
          daily_restart_gap_limit_ms(restart_policy)) {
    reset_segment(state);
    auto result = detail::observe_snapshot_core(state, snapshot, config, false);
    result.status = LearningStatus::TIME_DISCONTINUITY;
    result.cause = DailyCollectionCause::RESTART_DISCARDED;
    return result;
  }
  LearningSnapshot resumed = snapshot;
  resumed.monotonic_ms =
      state.last_monotonic_ms + static_cast<uint64_t>(snapshot.epoch_s - state.last_epoch_s) * 1000ULL;
  state.source_gap_pending = true;
  state.last_gap_observation_ms = state.last_monotonic_ms;
  auto result = detail::observe_snapshot_core(state, resumed, config, false, true, restart_policy);
  if (state.active) {
    state.carried_duration_ms = segment_elapsed_ms(state);
    state.start_monotonic_ms = state.last_monotonic_ms = snapshot.monotonic_ms;
  }
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
  const LearningStatus record_status = validate_segment_record_structure(record, config);
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
