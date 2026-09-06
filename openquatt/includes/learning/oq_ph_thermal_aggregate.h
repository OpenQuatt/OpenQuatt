#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include "oq_ph_learning_quality.h"
#include "oq_ph_thermal_model_logic.h"

namespace oq_power_house::learning {

// Both learners receive observations before segment selection. A dynamic source
// snapshot is built with THERMAL_DYNAMIC; never remove invalid bits from a batch
// snapshot whose rejected measurements may be absent.
struct ThermalWindowConfig {
  uint64_t target_duration_ms = 30ULL * 60ULL * 1000ULL;
  bool unmodeled_gain_bound_valid = false;
  double unmodeled_gain_bound_w = NAN;
};

struct ThermalWindowAccumulator {
  bool active = false;
  LearningSnapshot first;
  LearningSnapshot last;
  double indoor_integral_ms = 0.0;
  double outside_integral_ms = 0.0;
  double heat_integral_ms = 0.0;
  double uncertainty_integral_ms = 0.0;
  ThermalWindowConfig config;
};

enum class ThermalWindowStatus : uint8_t {
  COLLECTING = 0,
  READY,
  INVALID_CONFIGURATION,
  INVALID_MEASUREMENT,
  TIME_DISCONTINUITY,
  CONTEXT_CHANGED,
};

struct ThermalWindowResult {
  ThermalWindowStatus status = ThermalWindowStatus::COLLECTING;
  bool has_interval = false;
  ThermalInterval interval;
};

namespace thermal_window_detail {

inline bool valid_config(const ThermalWindowConfig& config, const QualityConfig& quality) {
  return valid_quality_config(quality) && config.target_duration_ms >= kThermalMinimumIntervalMs &&
         config.target_duration_ms <= kThermalMaximumIntervalMs - quality.max_interval_ms &&
         (!config.unmodeled_gain_bound_valid ||
          (isfinite(config.unmodeled_gain_bound_w) && config.unmodeled_gain_bound_w >= 0.0 &&
           config.unmodeled_gain_bound_w <= quality.max_abs_heat_w));
}

inline bool same_config(const ThermalWindowConfig& lhs, const ThermalWindowConfig& rhs) {
  return lhs.target_duration_ms == rhs.target_duration_ms &&
         lhs.unmodeled_gain_bound_valid == rhs.unmodeled_gain_bound_valid &&
         (!lhs.unmodeled_gain_bound_valid || lhs.unmodeled_gain_bound_w == rhs.unmodeled_gain_bound_w);
}

inline void seed(ThermalWindowAccumulator& state, const LearningSnapshot& snapshot, const ThermalWindowConfig& config) {
  state = {};
  state.active = true;
  state.first = snapshot;
  state.last = snapshot;
  state.config = config;
}

inline bool same_context(const LearningSnapshot& lhs, const LearningSnapshot& rhs) {
  return lhs.context_revision == rhs.context_revision;
}

inline bool coherent_time(const LearningSnapshot& earlier, const LearningSnapshot& later,
                          const QualityConfig& quality) {
  if (later.monotonic_ms <= earlier.monotonic_ms || later.epoch_s < earlier.epoch_s) return false;
  const uint64_t span_ms = later.monotonic_ms - earlier.monotonic_ms;
  const uint64_t utc_ms = static_cast<uint64_t>(later.epoch_s - earlier.epoch_s) * 1000ULL;
  const uint64_t difference = span_ms > utc_ms ? span_ms - utc_ms : utc_ms - span_ms;
  return difference <= static_cast<uint64_t>(quality.utc_tolerance_s) * 1000ULL + 999ULL;
}

}  // namespace thermal_window_detail

inline ThermalWindowResult observe_thermal_snapshot(ThermalWindowAccumulator& state, const LearningSnapshot& snapshot,
                                                    const QualityConfig& quality, const ThermalWindowConfig& config) {
  using namespace thermal_window_detail;
  ThermalWindowResult result;
  if (!valid_config(config, quality)) {
    state = {};
    result.status = ThermalWindowStatus::INVALID_CONFIGURATION;
    return result;
  }
  if (validate_snapshot(snapshot, quality) != LearningStatus::OK ||
      snapshot.heat_uncertainty_w > quality.max_heat_uncertainty_w) {
    state = {};
    result.status = ThermalWindowStatus::INVALID_MEASUREMENT;
    return result;
  }
  if (!state.active) {
    seed(state, snapshot, config);
    return result;
  }
  if (!same_context(state.last, snapshot) || !same_config(state.config, config)) {
    seed(state, snapshot, config);
    result.status = ThermalWindowStatus::CONTEXT_CHANGED;
    return result;
  }
  if (!coherent_time(state.last, snapshot, quality) || !coherent_time(state.first, snapshot, quality) ||
      snapshot.monotonic_ms - state.last.monotonic_ms > quality.max_interval_ms) {
    seed(state, snapshot, config);
    result.status = ThermalWindowStatus::TIME_DISCONTINUITY;
    return result;
  }
  const double dt_ms = static_cast<double>(snapshot.monotonic_ms - state.last.monotonic_ms);
  state.indoor_integral_ms += 0.5 * (static_cast<double>(state.last.room_c) + snapshot.room_c) * dt_ms;
  state.outside_integral_ms += 0.5 * (static_cast<double>(state.last.outside_c) + snapshot.outside_c) * dt_ms;
  state.heat_integral_ms += 0.5 * (static_cast<double>(state.last.heat_to_water_w) + snapshot.heat_to_water_w) * dt_ms;
  state.uncertainty_integral_ms +=
      0.5 * (static_cast<double>(state.last.heat_uncertainty_w) + snapshot.heat_uncertainty_w) * dt_ms;
  state.last = snapshot;
  const uint64_t span_ms = snapshot.monotonic_ms - state.first.monotonic_ms;
  if (span_ms < config.target_duration_ms) return result;
  auto& interval = result.interval;
  interval.start_monotonic_ms = state.first.monotonic_ms;
  interval.end_monotonic_ms = snapshot.monotonic_ms;
  interval.context_revision = snapshot.context_revision;
  interval.complete = true;
  interval.inputs_fresh = true;
  interval.generations_consistent = true;
  interval.operational_gates_passed = true;
  // INVALID_NONE asserts the upstream raw-source contract, including no boiler
  // heat. These booleans are not inferred from room response or compressor demand.
  interval.hidden_heat_exclusion_valid = true;
  interval.hidden_heat_excluded = true;
  interval.unmodeled_gain_bound_valid = config.unmodeled_gain_bound_valid;
  interval.unmodeled_gain_bound_w = config.unmodeled_gain_bound_w;
  interval.indoor_start_c = state.first.room_c;
  interval.indoor_end_c = snapshot.room_c;
  interval.mean_indoor_c = state.indoor_integral_ms / static_cast<double>(span_ms);
  interval.mean_outside_c = state.outside_integral_ms / static_cast<double>(span_ms);
  interval.mean_heat_w = state.heat_integral_ms / static_cast<double>(span_ms);
  interval.heat_uncertainty_w = state.uncertainty_integral_ms / static_cast<double>(span_ms);
  result.has_interval = true;
  result.status = ThermalWindowStatus::READY;
  // Adjacent windows share one measured endpoint, never a time interval.
  seed(state, snapshot, config);
  return result;
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
