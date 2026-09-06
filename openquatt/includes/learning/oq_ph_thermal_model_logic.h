#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace oq_power_house::learning {

constexpr uint64_t kThermalMinimumIntervalMs = 15ULL * 60ULL * 1000ULL;
constexpr uint64_t kThermalMaximumIntervalMs = 60ULL * 60ULL * 1000ULL;
constexpr uint32_t kThermalCheckpointMagic = 0x4F513152U;  // OQ1R
constexpr uint16_t kThermalCheckpointVersion = 1;

struct ThermalModelConfig {
  uint64_t min_interval_ms = kThermalMinimumIntervalMs;
  uint64_t max_interval_ms = kThermalMaximumIntervalMs;
  uint64_t max_model_gap_ms = 24ULL * 60ULL * 60ULL * 1000ULL;
  uint64_t max_estimate_age_ms = 2ULL * 60ULL * 60ULL * 1000ULL;
  double forgetting_factor_per_hour = 0.995;
  double loss_feature_scale_kh = 10.0;
  double heat_feature_scale_wh = 1000.0;
  double initial_heat_loss_w_per_k = 200.0;  // Active H is a prior only.
  double initial_thermal_capacity_wh_per_k = 6000.0;
  double initial_covariance = 100.0;
  double min_heat_loss_w_per_k = 20.0;
  double max_heat_loss_w_per_k = 1500.0;
  double min_thermal_capacity_wh_per_k = 500.0;
  double max_thermal_capacity_wh_per_k = 100000.0;
  double indoor_min_c = 0.0;
  double indoor_max_c = 40.0;
  double outside_min_c = -40.0;
  double outside_max_c = 55.0;
  double max_abs_heat_w = 50000.0;
  double max_heat_uncertainty_w = 400.0;
  double max_abs_interval_indoor_change_c = 5.0;
  double max_unmodeled_gain_w = 200.0;  // Development policy, not a physical constant.
  double min_ready_observation_hours = 16.0;
  double min_outside_span_c = 4.0;
  double min_heat_span_w = 1000.0;
  double min_information_eigenvalue = 0.05;
  double max_information_condition = 10000.0;
  double residual_ewma_alpha_per_hour = 0.10;
  double max_residual_rms_k_per_h = 0.50;
  double max_abs_residual_bias_k_per_h = 0.20;
};

struct ThermalInterval {
  uint64_t start_monotonic_ms = 0;
  uint64_t end_monotonic_ms = 0;
  uint32_t source_generation = 0;
  uint32_t physical_context_generation = 0;
  uint32_t control_generation = 0;
  bool complete = false;
  bool inputs_fresh = false;
  bool generations_consistent = false;
  bool operational_gates_passed = false;
  bool hidden_heat_exclusion_valid = false;
  bool hidden_heat_excluded = false;
  bool unmodeled_gain_bound_valid = false;
  double unmodeled_gain_bound_w = NAN;
  double indoor_start_c = NAN;
  double indoor_end_c = NAN;
  double mean_indoor_c = NAN;   // Time-weighted over the interval.
  double mean_outside_c = NAN;  // Time-weighted over the interval.
  double mean_heat_w = NAN;     // Time-weighted signed heat to water.
  double heat_uncertainty_w = NAN;
};

enum ThermalReadinessReason : uint32_t {
  THERMAL_READY_NONE = 0,
  THERMAL_READY_NOT_ENOUGH_SAMPLES = 1U << 0,
  THERMAL_READY_OUTSIDE_SPAN = 1U << 1,
  THERMAL_READY_HEAT_SPAN = 1U << 2,
  THERMAL_READY_UNOBSERVABLE = 1U << 3,
  THERMAL_READY_PARAMETER_BOUNDS = 1U << 4,
  THERMAL_READY_RESIDUAL_RMS = 1U << 5,
  THERMAL_READY_RESIDUAL_BIAS = 1U << 6,
  THERMAL_READY_UNMODELED_GAINS = 1U << 7,
  THERMAL_READY_INVALID_STATE = 1U << 8,
  THERMAL_READY_RECENT_DATA_INVALID = 1U << 9,
  THERMAL_READY_STALE_MODEL = 1U << 10,
};

struct ThermalModelState {
  bool initialized = false;
  double theta_loss_scaled = NAN;
  double theta_heat_scaled = NAN;
  double covariance_00 = NAN;
  double covariance_01 = NAN;
  double covariance_11 = NAN;
  double information_00 = 0.0;
  double information_01 = 0.0;
  double information_11 = 0.0;
  double residual_mean_k_per_h = 0.0;
  double residual_square_mean_k2_per_h2 = 0.0;
  double outside_min_c = NAN;
  double outside_max_c = NAN;
  double heat_min_w = NAN;
  double heat_max_w = NAN;
  double maximum_observed_unmodeled_gain_bound_w = 0.0;
  double effective_observation_hours = 0.0;
  bool all_unmodeled_gain_bounds_known_and_acceptable = true;
  bool recent_data_valid = false;
  uint32_t accepted_samples = 0;
  uint32_t rejected_samples = 0;
  uint32_t reset_count = 0;
  uint64_t last_interval_end_monotonic_ms = 0;
  uint64_t last_observation_monotonic_ms = 0;
  uint32_t source_generation = 0;
  uint32_t physical_context_generation = 0;
  uint32_t control_generation = 0;
  bool config_bound = false;
  ThermalModelConfig bound_config;
};

struct ThermalModelEstimate {
  bool parameters_valid = false;
  bool ready = false;
  uint32_t readiness_reasons = THERMAL_READY_INVALID_STATE;
  double heat_loss_w_per_k = NAN;
  double thermal_capacity_wh_per_k = NAN;
  double residual_rms_k_per_h = NAN;
  double residual_bias_k_per_h = NAN;
  double information_min_eigenvalue = NAN;
  double information_condition = NAN;
  double outside_span_c = NAN;
  double heat_span_w = NAN;
  uint32_t accepted_samples = 0;
  double effective_observation_hours = 0.0;
  bool unmodeled_gain_bounds_known_and_acceptable = false;
};

enum class ThermalUpdateStatus : uint8_t {
  ACCEPTED_COLLECTING = 0,
  ACCEPTED_READY,
  INVALID_CONFIGURATION,
  RESET_CONTEXT,
  RESET_TIME,
  RESET_LONG_GAP,
  RESET_NUMERIC_STATE,
  RESET_CONFIGURATION,
  REJECTED_DURATION,
  REJECTED_INVALID_INTERVAL,
  REJECTED_STALE_OR_INCOMPLETE,
  REJECTED_OPERATIONAL_GATE,
  REJECTED_HIDDEN_HEAT,
  REJECTED_STALE_CONTEXT,
};

struct ThermalUpdateResult {
  ThermalUpdateStatus status = ThermalUpdateStatus::INVALID_CONFIGURATION;
  bool accepted = false;
  ThermalModelEstimate estimate;
};

struct ThermalModelCheckpoint {
  uint32_t magic = kThermalCheckpointMagic;
  uint16_t version = kThermalCheckpointVersion;
  uint16_t reserved = 0;
  uint32_t config_fingerprint = 0;
  ThermalModelState state;
  uint32_t checksum = 0;
};

enum class ThermalCheckpointStatus : uint8_t {
  OK = 0,
  INVALID_CONFIGURATION,
  CORRUPT_RESET,
};

namespace thermal_detail {

inline bool finite_between(double value, double minimum, double maximum) {
  return isfinite(value) && value >= minimum && value <= maximum;
}

inline bool valid_config(const ThermalModelConfig& config) {
  return config.min_interval_ms >= kThermalMinimumIntervalMs && config.max_interval_ms <= kThermalMaximumIntervalMs &&
         config.min_interval_ms <= config.max_interval_ms && config.max_model_gap_ms >= config.max_interval_ms &&
         config.max_model_gap_ms <= 7ULL * 24ULL * 60ULL * 60ULL * 1000ULL &&
         config.max_estimate_age_ms >= config.max_interval_ms &&
         config.max_estimate_age_ms <= config.max_model_gap_ms &&
         finite_between(config.forgetting_factor_per_hour, 0.95, 1.0) &&
         finite_between(config.loss_feature_scale_kh, 0.1, 1000.0) &&
         finite_between(config.heat_feature_scale_wh, 1.0, 1000000.0) &&
         finite_between(config.min_heat_loss_w_per_k, 1.0, 5000.0) &&
         finite_between(config.max_heat_loss_w_per_k, config.min_heat_loss_w_per_k, 10000.0) &&
         config.max_heat_loss_w_per_k > config.min_heat_loss_w_per_k &&
         finite_between(config.initial_heat_loss_w_per_k, config.min_heat_loss_w_per_k, config.max_heat_loss_w_per_k) &&
         finite_between(config.min_thermal_capacity_wh_per_k, 10.0, 1000000.0) &&
         finite_between(config.max_thermal_capacity_wh_per_k, config.min_thermal_capacity_wh_per_k, 10000000.0) &&
         config.max_thermal_capacity_wh_per_k > config.min_thermal_capacity_wh_per_k &&
         finite_between(config.initial_thermal_capacity_wh_per_k, config.min_thermal_capacity_wh_per_k,
                        config.max_thermal_capacity_wh_per_k) &&
         finite_between(config.initial_covariance, 1e-6, 1e9) && finite_between(config.indoor_min_c, -30.0, 60.0) &&
         finite_between(config.indoor_max_c, config.indoor_min_c, 80.0) && config.indoor_max_c > config.indoor_min_c &&
         finite_between(config.outside_min_c, -80.0, 70.0) &&
         finite_between(config.outside_max_c, config.outside_min_c, 80.0) &&
         config.outside_max_c > config.outside_min_c && finite_between(config.max_abs_heat_w, 100.0, 1000000.0) &&
         finite_between(config.max_heat_uncertainty_w, 0.0, config.max_abs_heat_w) &&
         finite_between(config.max_abs_interval_indoor_change_c, 0.05, 20.0) &&
         finite_between(config.max_unmodeled_gain_w, 0.0, 10000.0) &&
         finite_between(config.min_ready_observation_hours, 2.0, 5000.0) &&
         finite_between(config.min_outside_span_c, 0.1, 50.0) &&
         finite_between(config.min_heat_span_w, 10.0, config.max_abs_heat_w * 2.0) &&
         finite_between(config.min_information_eigenvalue, 1e-9, 1e9) &&
         finite_between(config.max_information_condition, 1.0, 1e12) &&
         finite_between(config.residual_ewma_alpha_per_hour, 0.001, 1.0) &&
         finite_between(config.max_residual_rms_k_per_h, 0.001, 40.0) &&
         finite_between(config.max_abs_residual_bias_k_per_h, 0.001, config.max_residual_rms_k_per_h);
}

inline bool covariance_is_spd(double covariance_00, double covariance_01, double covariance_11) {
  if (!isfinite(covariance_00) || !isfinite(covariance_01) || !isfinite(covariance_11) || covariance_00 <= 1e-15 ||
      covariance_11 <= 1e-15 || covariance_00 > 1e12 || covariance_11 > 1e12)
    return false;
  const double determinant = covariance_00 * covariance_11 - covariance_01 * covariance_01;
  return isfinite(determinant) && determinant > 1e-24 * fmax(1.0, covariance_00 * covariance_11);
}

inline bool finite_state(const ThermalModelState& state) {
  const bool generations_unbound =
      state.source_generation == 0 && state.physical_context_generation == 0 && state.control_generation == 0;
  const bool generations_bound =
      state.source_generation != 0 && state.physical_context_generation != 0 && state.control_generation != 0;
  const bool history_consistent =
      state.last_observation_monotonic_ms >= state.last_interval_end_monotonic_ms &&
      ((state.accepted_samples == 0 && state.last_interval_end_monotonic_ms == 0) ||
       (state.accepted_samples > 0 && state.last_interval_end_monotonic_ms != 0 && generations_bound));
  const double information_determinant =
      state.information_00 * state.information_11 - state.information_01 * state.information_01;
  const bool information_valid =
      isfinite(state.information_00) && isfinite(state.information_01) && isfinite(state.information_11) &&
      state.information_00 >= 0.0 && state.information_11 >= 0.0 && state.information_00 <= 1e24 &&
      state.information_11 <= 1e24 && isfinite(information_determinant) &&
      information_determinant >= -1e-12 * fmax(1.0, state.information_00 * state.information_11);
  const bool ranges_valid =
      state.accepted_samples == 0 ||
      (isfinite(state.outside_min_c) && isfinite(state.outside_max_c) && state.outside_min_c <= state.outside_max_c &&
       isfinite(state.heat_min_w) && isfinite(state.heat_max_w) && state.heat_min_w <= state.heat_max_w &&
       state.effective_observation_hours > 0.0);
  return state.initialized && state.config_bound && valid_config(state.bound_config) &&
         (generations_unbound || generations_bound) && history_consistent && isfinite(state.theta_loss_scaled) &&
         isfinite(state.theta_heat_scaled) && fabs(state.theta_loss_scaled) <= 1e6 &&
         fabs(state.theta_heat_scaled) <= 1e6 &&
         covariance_is_spd(state.covariance_00, state.covariance_01, state.covariance_11) && information_valid &&
         ranges_valid && isfinite(state.residual_mean_k_per_h) && isfinite(state.residual_square_mean_k2_per_h2) &&
         state.residual_square_mean_k2_per_h2 >= 0.0 && isfinite(state.maximum_observed_unmodeled_gain_bound_w) &&
         state.maximum_observed_unmodeled_gain_bound_w >= 0.0 && isfinite(state.effective_observation_hours) &&
         state.effective_observation_hours >= 0.0 && state.effective_observation_hours <= 1e9;
}

inline void seed_state(ThermalModelState& state, const ThermalModelConfig& config, uint32_t reset_count) {
  state = {};
  state.initialized = true;
  state.theta_loss_scaled =
      config.initial_heat_loss_w_per_k / config.initial_thermal_capacity_wh_per_k * config.loss_feature_scale_kh;
  state.theta_heat_scaled = config.heat_feature_scale_wh / config.initial_thermal_capacity_wh_per_k;
  state.covariance_00 = config.initial_covariance;
  state.covariance_01 = 0.0;
  state.covariance_11 = config.initial_covariance;
  state.all_unmodeled_gain_bounds_known_and_acceptable = true;
  state.reset_count = reset_count;
  state.config_bound = true;
  state.bound_config = config;
}

inline void reset_state(ThermalModelState& state, const ThermalModelConfig& config) {
  const uint32_t next_reset_count = state.reset_count == UINT32_MAX ? UINT32_MAX : state.reset_count + 1U;
  const uint32_t rejected_samples = state.rejected_samples;
  const uint64_t last_observation_monotonic_ms =
      state.last_observation_monotonic_ms >= state.last_interval_end_monotonic_ms
          ? state.last_observation_monotonic_ms
          : state.last_interval_end_monotonic_ms;
  const bool generations_bound =
      state.source_generation != 0 && state.physical_context_generation != 0 && state.control_generation != 0;
  const uint32_t source_generation = generations_bound ? state.source_generation : 0;
  const uint32_t physical_context_generation = generations_bound ? state.physical_context_generation : 0;
  const uint32_t control_generation = generations_bound ? state.control_generation : 0;
  seed_state(state, config, next_reset_count);
  state.rejected_samples = rejected_samples;
  state.last_observation_monotonic_ms = last_observation_monotonic_ms;
  state.source_generation = source_generation;
  state.physical_context_generation = physical_context_generation;
  state.control_generation = control_generation;
}

inline void reset_untrusted_state(ThermalModelState& state, const ThermalModelConfig& config,
                                  uint64_t current_observation_monotonic_ms = 0) {
  // A numeric/checkpoint failure means ordering metadata is untrusted too. Do
  // not retain a corrupt future watermark or generation that can permanently
  // reject every subsequent live interval. A current callback is still
  // consumed so a stale retry cannot be accepted after the reset.
  seed_state(state, config, 1U);
  state.last_observation_monotonic_ms = current_observation_monotonic_ms;
}

inline void reject_without_reset(ThermalModelState& state, const ThermalInterval& interval) {
  if (state.rejected_samples != UINT32_MAX) ++state.rejected_samples;
  state.recent_data_valid = false;
  if (interval.end_monotonic_ms > state.last_observation_monotonic_ms)
    state.last_observation_monotonic_ms = interval.end_monotonic_ms;
}

inline bool interval_values_valid(const ThermalInterval& interval, const ThermalModelConfig& config) {
  return finite_between(interval.indoor_start_c, config.indoor_min_c, config.indoor_max_c) &&
         finite_between(interval.indoor_end_c, config.indoor_min_c, config.indoor_max_c) &&
         finite_between(interval.mean_indoor_c, config.indoor_min_c, config.indoor_max_c) &&
         finite_between(interval.mean_outside_c, config.outside_min_c, config.outside_max_c) &&
         finite_between(interval.mean_heat_w, -config.max_abs_heat_w, config.max_abs_heat_w) &&
         finite_between(interval.heat_uncertainty_w, 0.0, config.max_heat_uncertainty_w) &&
         fabs(interval.indoor_end_c - interval.indoor_start_c) <= config.max_abs_interval_indoor_change_c;
}

inline uint32_t hash_u32(uint32_t hash, uint32_t value) {
  for (uint8_t shift = 0; shift < 32; shift += 8) {
    hash ^= static_cast<uint8_t>(value >> shift);
    hash *= 16777619U;
  }
  return hash;
}

inline uint32_t hash_u64(uint32_t hash, uint64_t value) {
  hash = hash_u32(hash, static_cast<uint32_t>(value));
  return hash_u32(hash, static_cast<uint32_t>(value >> 32));
}

inline uint32_t hash_double(uint32_t hash, double value) {
  uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "double checkpoint hashing requires 64-bit double");
  memcpy(&bits, &value, sizeof(bits));
  return hash_u64(hash, bits);
}

inline uint32_t config_fingerprint(const ThermalModelConfig& config) {
  uint32_t hash = 2166136261U;
  hash = hash_u64(hash, config.min_interval_ms);
  hash = hash_u64(hash, config.max_interval_ms);
  hash = hash_u64(hash, config.max_model_gap_ms);
  hash = hash_u64(hash, config.max_estimate_age_ms);
  hash = hash_double(hash, config.forgetting_factor_per_hour);
  hash = hash_double(hash, config.loss_feature_scale_kh);
  hash = hash_double(hash, config.heat_feature_scale_wh);
  hash = hash_double(hash, config.initial_heat_loss_w_per_k);
  hash = hash_double(hash, config.initial_thermal_capacity_wh_per_k);
  hash = hash_double(hash, config.initial_covariance);
  hash = hash_double(hash, config.min_heat_loss_w_per_k);
  hash = hash_double(hash, config.max_heat_loss_w_per_k);
  hash = hash_double(hash, config.min_thermal_capacity_wh_per_k);
  hash = hash_double(hash, config.max_thermal_capacity_wh_per_k);
  hash = hash_double(hash, config.indoor_min_c);
  hash = hash_double(hash, config.indoor_max_c);
  hash = hash_double(hash, config.outside_min_c);
  hash = hash_double(hash, config.outside_max_c);
  hash = hash_double(hash, config.max_abs_heat_w);
  hash = hash_double(hash, config.max_heat_uncertainty_w);
  hash = hash_double(hash, config.max_abs_interval_indoor_change_c);
  hash = hash_double(hash, config.max_unmodeled_gain_w);
  hash = hash_double(hash, config.min_ready_observation_hours);
  hash = hash_double(hash, config.min_outside_span_c);
  hash = hash_double(hash, config.min_heat_span_w);
  hash = hash_double(hash, config.min_information_eigenvalue);
  hash = hash_double(hash, config.max_information_condition);
  hash = hash_double(hash, config.residual_ewma_alpha_per_hour);
  hash = hash_double(hash, config.max_residual_rms_k_per_h);
  return hash_double(hash, config.max_abs_residual_bias_k_per_h);
}

inline bool same_config(const ThermalModelConfig& lhs, const ThermalModelConfig& rhs) {
  return lhs.min_interval_ms == rhs.min_interval_ms && lhs.max_interval_ms == rhs.max_interval_ms &&
         lhs.max_model_gap_ms == rhs.max_model_gap_ms && lhs.max_estimate_age_ms == rhs.max_estimate_age_ms &&
         lhs.forgetting_factor_per_hour == rhs.forgetting_factor_per_hour &&
         lhs.loss_feature_scale_kh == rhs.loss_feature_scale_kh &&
         lhs.heat_feature_scale_wh == rhs.heat_feature_scale_wh &&
         lhs.initial_heat_loss_w_per_k == rhs.initial_heat_loss_w_per_k &&
         lhs.initial_thermal_capacity_wh_per_k == rhs.initial_thermal_capacity_wh_per_k &&
         lhs.initial_covariance == rhs.initial_covariance && lhs.min_heat_loss_w_per_k == rhs.min_heat_loss_w_per_k &&
         lhs.max_heat_loss_w_per_k == rhs.max_heat_loss_w_per_k &&
         lhs.min_thermal_capacity_wh_per_k == rhs.min_thermal_capacity_wh_per_k &&
         lhs.max_thermal_capacity_wh_per_k == rhs.max_thermal_capacity_wh_per_k &&
         lhs.indoor_min_c == rhs.indoor_min_c && lhs.indoor_max_c == rhs.indoor_max_c &&
         lhs.outside_min_c == rhs.outside_min_c && lhs.outside_max_c == rhs.outside_max_c &&
         lhs.max_abs_heat_w == rhs.max_abs_heat_w && lhs.max_heat_uncertainty_w == rhs.max_heat_uncertainty_w &&
         lhs.max_abs_interval_indoor_change_c == rhs.max_abs_interval_indoor_change_c &&
         lhs.max_unmodeled_gain_w == rhs.max_unmodeled_gain_w &&
         lhs.min_ready_observation_hours == rhs.min_ready_observation_hours &&
         lhs.min_outside_span_c == rhs.min_outside_span_c && lhs.min_heat_span_w == rhs.min_heat_span_w &&
         lhs.min_information_eigenvalue == rhs.min_information_eigenvalue &&
         lhs.max_information_condition == rhs.max_information_condition &&
         lhs.residual_ewma_alpha_per_hour == rhs.residual_ewma_alpha_per_hour &&
         lhs.max_residual_rms_k_per_h == rhs.max_residual_rms_k_per_h &&
         lhs.max_abs_residual_bias_k_per_h == rhs.max_abs_residual_bias_k_per_h;
}

inline uint32_t checkpoint_checksum(const ThermalModelCheckpoint& checkpoint) {
  uint32_t hash = 2166136261U;
  hash = hash_u32(hash, checkpoint.magic);
  hash = hash_u32(hash, checkpoint.version);
  hash = hash_u32(hash, checkpoint.reserved);
  hash = hash_u32(hash, checkpoint.config_fingerprint);
  const ThermalModelState& state = checkpoint.state;
  hash = hash_u32(hash, state.initialized ? 1U : 0U);
  hash = hash_double(hash, state.theta_loss_scaled);
  hash = hash_double(hash, state.theta_heat_scaled);
  hash = hash_double(hash, state.covariance_00);
  hash = hash_double(hash, state.covariance_01);
  hash = hash_double(hash, state.covariance_11);
  hash = hash_double(hash, state.information_00);
  hash = hash_double(hash, state.information_01);
  hash = hash_double(hash, state.information_11);
  hash = hash_double(hash, state.residual_mean_k_per_h);
  hash = hash_double(hash, state.residual_square_mean_k2_per_h2);
  hash = hash_double(hash, state.outside_min_c);
  hash = hash_double(hash, state.outside_max_c);
  hash = hash_double(hash, state.heat_min_w);
  hash = hash_double(hash, state.heat_max_w);
  hash = hash_double(hash, state.maximum_observed_unmodeled_gain_bound_w);
  hash = hash_double(hash, state.effective_observation_hours);
  hash = hash_u32(hash, state.all_unmodeled_gain_bounds_known_and_acceptable ? 1U : 0U);
  hash = hash_u32(hash, state.recent_data_valid ? 1U : 0U);
  hash = hash_u32(hash, state.accepted_samples);
  hash = hash_u32(hash, state.rejected_samples);
  hash = hash_u32(hash, state.reset_count);
  hash = hash_u64(hash, state.last_interval_end_monotonic_ms);
  hash = hash_u64(hash, state.last_observation_monotonic_ms);
  hash = hash_u32(hash, state.source_generation);
  hash = hash_u32(hash, state.physical_context_generation);
  hash = hash_u32(hash, state.control_generation);
  hash = hash_u32(hash, state.config_bound ? 1U : 0U);
  return hash_u32(hash, config_fingerprint(state.bound_config));
}

inline bool checkpoint_state_valid(const ThermalModelState& state) {
  if (!finite_state(state)) return false;
  if (state.accepted_samples == 0) {
    const bool generations_unbound =
        state.source_generation == 0 && state.physical_context_generation == 0 && state.control_generation == 0;
    const bool generations_bound =
        state.source_generation != 0 && state.physical_context_generation != 0 && state.control_generation != 0;
    return state.last_interval_end_monotonic_ms == 0 && (generations_unbound || generations_bound);
  }
  return state.last_interval_end_monotonic_ms != 0 && state.source_generation != 0 &&
         state.physical_context_generation != 0 && state.control_generation != 0 && isfinite(state.outside_min_c) &&
         isfinite(state.outside_max_c) && state.outside_min_c <= state.outside_max_c && isfinite(state.heat_min_w) &&
         isfinite(state.heat_max_w) && state.heat_min_w <= state.heat_max_w &&
         state.last_observation_monotonic_ms >= state.last_interval_end_monotonic_ms;
}

}  // namespace thermal_detail

inline bool valid_thermal_model_config(const ThermalModelConfig& config) {
  return thermal_detail::valid_config(config);
}

inline bool initialize_thermal_model(ThermalModelState& state, const ThermalModelConfig& config) {
  if (!valid_thermal_model_config(config)) {
    state = {};
    return false;
  }
  thermal_detail::seed_state(state, config, 0);
  return true;
}

inline ThermalModelEstimate estimate_thermal_model(const ThermalModelState& state, const ThermalModelConfig& config,
                                                   uint64_t now_monotonic_ms) {
  using namespace thermal_detail;
  ThermalModelEstimate estimate;
  estimate.accepted_samples = state.accepted_samples;
  estimate.effective_observation_hours = state.effective_observation_hours;
  if (!valid_config(config) || !finite_state(state) || !same_config(state.bound_config, config)) return estimate;

  estimate.readiness_reasons = THERMAL_READY_NONE;
  if (state.theta_heat_scaled > 1e-15) {
    estimate.thermal_capacity_wh_per_k = config.heat_feature_scale_wh / state.theta_heat_scaled;
    estimate.heat_loss_w_per_k =
        state.theta_loss_scaled / config.loss_feature_scale_kh * estimate.thermal_capacity_wh_per_k;
    estimate.parameters_valid =
        finite_between(estimate.heat_loss_w_per_k, config.min_heat_loss_w_per_k, config.max_heat_loss_w_per_k) &&
        finite_between(estimate.thermal_capacity_wh_per_k, config.min_thermal_capacity_wh_per_k,
                       config.max_thermal_capacity_wh_per_k);
  }
  if (!estimate.parameters_valid) estimate.readiness_reasons |= THERMAL_READY_PARAMETER_BOUNDS;

  const double trace = state.information_00 + state.information_11;
  const double discriminant =
      fmax(0.0, (state.information_00 - state.information_11) * (state.information_00 - state.information_11) +
                    4.0 * state.information_01 * state.information_01);
  const double root = sqrt(discriminant);
  estimate.information_min_eigenvalue = 0.5 * (trace - root);
  const double max_eigenvalue = 0.5 * (trace + root);
  estimate.information_condition =
      estimate.information_min_eigenvalue > 0.0 ? max_eigenvalue / estimate.information_min_eigenvalue : INFINITY;
  if (!isfinite(estimate.information_min_eigenvalue) ||
      estimate.information_min_eigenvalue < config.min_information_eigenvalue ||
      !isfinite(estimate.information_condition) || estimate.information_condition > config.max_information_condition)
    estimate.readiness_reasons |= THERMAL_READY_UNOBSERVABLE;

  if (state.accepted_samples == 0) {
    estimate.outside_span_c = 0.0;
    estimate.heat_span_w = 0.0;
  } else {
    estimate.outside_span_c = state.outside_max_c - state.outside_min_c;
    estimate.heat_span_w = state.heat_max_w - state.heat_min_w;
  }
  if (state.effective_observation_hours < config.min_ready_observation_hours)
    estimate.readiness_reasons |= THERMAL_READY_NOT_ENOUGH_SAMPLES;
  if (estimate.outside_span_c < config.min_outside_span_c) estimate.readiness_reasons |= THERMAL_READY_OUTSIDE_SPAN;
  if (estimate.heat_span_w < config.min_heat_span_w) estimate.readiness_reasons |= THERMAL_READY_HEAT_SPAN;

  estimate.residual_bias_k_per_h = state.residual_mean_k_per_h;
  estimate.residual_rms_k_per_h = sqrt(fmax(0.0, state.residual_square_mean_k2_per_h2));
  if (estimate.residual_rms_k_per_h > config.max_residual_rms_k_per_h)
    estimate.readiness_reasons |= THERMAL_READY_RESIDUAL_RMS;
  if (fabs(estimate.residual_bias_k_per_h) > config.max_abs_residual_bias_k_per_h)
    estimate.readiness_reasons |= THERMAL_READY_RESIDUAL_BIAS;
  estimate.unmodeled_gain_bounds_known_and_acceptable = state.all_unmodeled_gain_bounds_known_and_acceptable;
  if (!estimate.unmodeled_gain_bounds_known_and_acceptable) estimate.readiness_reasons |= THERMAL_READY_UNMODELED_GAINS;
  if (!state.recent_data_valid) estimate.readiness_reasons |= THERMAL_READY_RECENT_DATA_INVALID;
  if (now_monotonic_ms == 0 || state.last_interval_end_monotonic_ms == 0 ||
      now_monotonic_ms < state.last_interval_end_monotonic_ms ||
      now_monotonic_ms - state.last_interval_end_monotonic_ms > config.max_estimate_age_ms)
    estimate.readiness_reasons |= THERMAL_READY_STALE_MODEL;

  estimate.ready = estimate.readiness_reasons == THERMAL_READY_NONE;
  return estimate;
}

inline ThermalUpdateResult observe_thermal_interval(ThermalModelState& state, const ThermalInterval& interval,
                                                    const ThermalModelConfig& config) {
  using namespace thermal_detail;
  ThermalUpdateResult result;
  const uint64_t observation_now_ms =
      interval.end_monotonic_ms != 0 ? interval.end_monotonic_ms : state.last_observation_monotonic_ms;
  if (!valid_config(config)) {
    if (finite_state(state)) reject_without_reset(state, interval);
    result.status = ThermalUpdateStatus::INVALID_CONFIGURATION;
    return result;
  }
  if (!finite_state(state)) {
    reset_untrusted_state(state, config, interval.end_monotonic_ms);
    result.status = ThermalUpdateStatus::RESET_NUMERIC_STATE;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }
  if (!same_config(state.bound_config, config)) {
    reject_without_reset(state, interval);
    reset_state(state, config);
    result.status = ThermalUpdateStatus::RESET_CONFIGURATION;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }
  if (!interval.complete || !interval.inputs_fresh) {
    reject_without_reset(state, interval);
    result.status = ThermalUpdateStatus::REJECTED_STALE_OR_INCOMPLETE;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }
  if (!interval.generations_consistent || interval.source_generation == 0 ||
      interval.physical_context_generation == 0 || interval.control_generation == 0) {
    reject_without_reset(state, interval);
    reset_state(state, config);
    result.status = ThermalUpdateStatus::RESET_CONTEXT;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }
  if (interval.start_monotonic_ms == 0 || interval.end_monotonic_ms <= interval.start_monotonic_ms ||
      (state.last_observation_monotonic_ms != 0 && interval.start_monotonic_ms < state.last_observation_monotonic_ms)) {
    reject_without_reset(state, interval);
    reset_state(state, config);
    result.status = ThermalUpdateStatus::RESET_TIME;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }
  const bool generation_owner_bound =
      state.source_generation != 0 && state.physical_context_generation != 0 && state.control_generation != 0;
  const bool generation_mismatch =
      generation_owner_bound && (state.source_generation != interval.source_generation ||
                                 state.physical_context_generation != interval.physical_context_generation ||
                                 state.control_generation != interval.control_generation);
  const bool stale_generation =
      generation_owner_bound && (interval.source_generation < state.source_generation ||
                                 interval.physical_context_generation < state.physical_context_generation ||
                                 interval.control_generation < state.control_generation);
  if (stale_generation) {
    reject_without_reset(state, interval);
    result.status = ThermalUpdateStatus::REJECTED_STALE_CONTEXT;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }
  if (generation_mismatch) {
    reject_without_reset(state, interval);
    reset_state(state, config);
    // Generation owners are monotonic within a boot. initialize_thermal_model() is the explicit reboot boundary.
    state.source_generation = interval.source_generation;
    state.physical_context_generation = interval.physical_context_generation;
    state.control_generation = interval.control_generation;
    result.status = ThermalUpdateStatus::RESET_CONTEXT;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }
  const uint64_t duration_ms = interval.end_monotonic_ms - interval.start_monotonic_ms;
  if (duration_ms < config.min_interval_ms || duration_ms > config.max_interval_ms) {
    reject_without_reset(state, interval);
    result.status = ThermalUpdateStatus::REJECTED_DURATION;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }
  if (state.last_interval_end_monotonic_ms != 0 &&
      interval.start_monotonic_ms - state.last_interval_end_monotonic_ms > config.max_model_gap_ms) {
    reject_without_reset(state, interval);
    reset_state(state, config);
    result.status = ThermalUpdateStatus::RESET_LONG_GAP;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }
  if (!interval.operational_gates_passed) {
    reject_without_reset(state, interval);
    result.status = ThermalUpdateStatus::REJECTED_OPERATIONAL_GATE;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }
  if (!interval.hidden_heat_exclusion_valid || !interval.hidden_heat_excluded) {
    reject_without_reset(state, interval);
    result.status = ThermalUpdateStatus::REJECTED_HIDDEN_HEAT;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }
  if (!interval_values_valid(interval, config)) {
    reject_without_reset(state, interval);
    result.status = ThermalUpdateStatus::REJECTED_INVALID_INTERVAL;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }

  const double duration_h = static_cast<double>(duration_ms) / 3600000.0;
  const double loss_integral_kh = (interval.mean_outside_c - interval.mean_indoor_c) * duration_h;
  const double heat_integral_wh = interval.mean_heat_w * duration_h;
  const double feature_loss = loss_integral_kh / config.loss_feature_scale_kh;
  const double feature_heat = heat_integral_wh / config.heat_feature_scale_wh;
  const double delta_indoor_c = interval.indoor_end_c - interval.indoor_start_c;
  if (!isfinite(feature_loss) || !isfinite(feature_heat) || !isfinite(delta_indoor_c) || fabs(feature_loss) > 1e6 ||
      fabs(feature_heat) > 1e6) {
    reject_without_reset(state, interval);
    result.status = ThermalUpdateStatus::REJECTED_INVALID_INTERVAL;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }

  const uint64_t forgetting_elapsed_ms = state.last_interval_end_monotonic_ms == 0
                                             ? duration_ms
                                             : interval.end_monotonic_ms - state.last_interval_end_monotonic_ms;
  const double forgetting_elapsed_h = static_cast<double>(forgetting_elapsed_ms) / 3600000.0;
  const double interval_forgetting_factor = pow(config.forgetting_factor_per_hour, forgetting_elapsed_h);
  const double sqrt_duration_h = sqrt(duration_h);
  const double normalized_feature_loss = feature_loss / sqrt_duration_h;
  const double normalized_feature_heat = feature_heat / sqrt_duration_h;
  const double projected_loss =
      state.covariance_00 * normalized_feature_loss + state.covariance_01 * normalized_feature_heat;
  const double projected_heat =
      state.covariance_01 * normalized_feature_loss + state.covariance_11 * normalized_feature_heat;
  const double denominator =
      interval_forgetting_factor + normalized_feature_loss * projected_loss + normalized_feature_heat * projected_heat;
  const double prediction = state.theta_loss_scaled * feature_loss + state.theta_heat_scaled * feature_heat;
  const double residual = delta_indoor_c - prediction;
  const double residual_k_per_h = residual / duration_h;
  const double normalized_residual = residual / sqrt_duration_h;
  if (!isfinite(interval_forgetting_factor) || interval_forgetting_factor <= 0.0 || !isfinite(projected_loss) ||
      !isfinite(projected_heat) || !isfinite(denominator) || denominator <= 1e-15 || !isfinite(prediction) ||
      !isfinite(residual) || !isfinite(residual_k_per_h) || !isfinite(normalized_residual)) {
    reject_without_reset(state, interval);
    reset_state(state, config);
    result.status = ThermalUpdateStatus::RESET_NUMERIC_STATE;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }

  const double gain_loss = projected_loss / denominator;
  const double gain_heat = projected_heat / denominator;
  const double next_theta_loss = state.theta_loss_scaled + gain_loss * normalized_residual;
  const double next_theta_heat = state.theta_heat_scaled + gain_heat * normalized_residual;
  const double next_covariance_00 =
      (state.covariance_00 - projected_loss * projected_loss / denominator) / interval_forgetting_factor;
  const double next_covariance_01 =
      (state.covariance_01 - projected_loss * projected_heat / denominator) / interval_forgetting_factor;
  const double next_covariance_11 =
      (state.covariance_11 - projected_heat * projected_heat / denominator) / interval_forgetting_factor;
  if (!isfinite(next_theta_loss) || !isfinite(next_theta_heat) || fabs(next_theta_loss) > 1e6 ||
      fabs(next_theta_heat) > 1e6 || !covariance_is_spd(next_covariance_00, next_covariance_01, next_covariance_11)) {
    reject_without_reset(state, interval);
    reset_state(state, config);
    result.status = ThermalUpdateStatus::RESET_NUMERIC_STATE;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }

  state.theta_loss_scaled = next_theta_loss;
  state.theta_heat_scaled = next_theta_heat;
  state.covariance_00 = next_covariance_00;
  state.covariance_01 = next_covariance_01;
  state.covariance_11 = next_covariance_11;
  state.information_00 =
      interval_forgetting_factor * state.information_00 + normalized_feature_loss * normalized_feature_loss;
  state.information_01 =
      interval_forgetting_factor * state.information_01 + normalized_feature_loss * normalized_feature_heat;
  state.information_11 =
      interval_forgetting_factor * state.information_11 + normalized_feature_heat * normalized_feature_heat;
  if (!isfinite(state.information_00) || !isfinite(state.information_01) || !isfinite(state.information_11)) {
    reject_without_reset(state, interval);
    reset_state(state, config);
    result.status = ThermalUpdateStatus::RESET_NUMERIC_STATE;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }

  const double alpha = 1.0 - pow(1.0 - config.residual_ewma_alpha_per_hour, duration_h);
  if (state.accepted_samples == 0) {
    state.residual_mean_k_per_h = residual_k_per_h;
    state.residual_square_mean_k2_per_h2 = residual_k_per_h * residual_k_per_h;
    state.outside_min_c = interval.mean_outside_c;
    state.outside_max_c = interval.mean_outside_c;
    state.heat_min_w = interval.mean_heat_w;
    state.heat_max_w = interval.mean_heat_w;
  } else {
    state.residual_mean_k_per_h = (1.0 - alpha) * state.residual_mean_k_per_h + alpha * residual_k_per_h;
    state.residual_square_mean_k2_per_h2 =
        (1.0 - alpha) * state.residual_square_mean_k2_per_h2 + alpha * residual_k_per_h * residual_k_per_h;
    const double retained_outside_min =
        interval_forgetting_factor * state.outside_min_c + (1.0 - interval_forgetting_factor) * interval.mean_outside_c;
    const double retained_outside_max =
        interval_forgetting_factor * state.outside_max_c + (1.0 - interval_forgetting_factor) * interval.mean_outside_c;
    const double retained_heat_min =
        interval_forgetting_factor * state.heat_min_w + (1.0 - interval_forgetting_factor) * interval.mean_heat_w;
    const double retained_heat_max =
        interval_forgetting_factor * state.heat_max_w + (1.0 - interval_forgetting_factor) * interval.mean_heat_w;
    state.outside_min_c = fmin(retained_outside_min, interval.mean_outside_c);
    state.outside_max_c = fmax(retained_outside_max, interval.mean_outside_c);
    state.heat_min_w = fmin(retained_heat_min, interval.mean_heat_w);
    state.heat_max_w = fmax(retained_heat_max, interval.mean_heat_w);
  }
  state.effective_observation_hours = interval_forgetting_factor * state.effective_observation_hours + duration_h;
  if (interval.unmodeled_gain_bound_valid && isfinite(interval.unmodeled_gain_bound_w) &&
      interval.unmodeled_gain_bound_w >= 0.0) {
    state.maximum_observed_unmodeled_gain_bound_w =
        fmax(state.maximum_observed_unmodeled_gain_bound_w, interval.unmodeled_gain_bound_w);
    if (interval.unmodeled_gain_bound_w > config.max_unmodeled_gain_w)
      state.all_unmodeled_gain_bounds_known_and_acceptable = false;
  } else {
    state.all_unmodeled_gain_bounds_known_and_acceptable = false;
  }
  if (state.accepted_samples == UINT32_MAX) {
    reject_without_reset(state, interval);
    reset_state(state, config);
    result.status = ThermalUpdateStatus::RESET_NUMERIC_STATE;
    result.estimate = estimate_thermal_model(state, config, observation_now_ms);
    return result;
  }
  ++state.accepted_samples;
  state.recent_data_valid = true;
  state.last_interval_end_monotonic_ms = interval.end_monotonic_ms;
  state.last_observation_monotonic_ms = interval.end_monotonic_ms;
  state.source_generation = interval.source_generation;
  state.physical_context_generation = interval.physical_context_generation;
  state.control_generation = interval.control_generation;

  result.accepted = true;
  result.estimate = estimate_thermal_model(state, config, interval.end_monotonic_ms);
  result.status =
      result.estimate.ready ? ThermalUpdateStatus::ACCEPTED_READY : ThermalUpdateStatus::ACCEPTED_COLLECTING;
  return result;
}

inline ThermalUpdateResult invalidate_thermal_observation(ThermalModelState& state, uint64_t now_monotonic_ms,
                                                          const ThermalModelConfig& config) {
  using namespace thermal_detail;
  ThermalUpdateResult result;
  if (!valid_config(config)) {
    if (finite_state(state)) {
      ThermalInterval invalid;
      invalid.end_monotonic_ms = now_monotonic_ms;
      reject_without_reset(state, invalid);
    }
    result.status = ThermalUpdateStatus::INVALID_CONFIGURATION;
    return result;
  }
  if (!finite_state(state)) {
    reset_untrusted_state(state, config, now_monotonic_ms);
    result.status = ThermalUpdateStatus::RESET_NUMERIC_STATE;
    result.estimate = estimate_thermal_model(state, config, now_monotonic_ms);
    return result;
  }
  if (!same_config(state.bound_config, config)) {
    ThermalInterval invalid;
    invalid.end_monotonic_ms = now_monotonic_ms;
    reject_without_reset(state, invalid);
    reset_state(state, config);
    result.status = ThermalUpdateStatus::RESET_CONFIGURATION;
    result.estimate = estimate_thermal_model(state, config, now_monotonic_ms);
    return result;
  }
  if (now_monotonic_ms == 0 ||
      (state.last_observation_monotonic_ms != 0 && now_monotonic_ms < state.last_observation_monotonic_ms)) {
    reset_state(state, config);
    result.status = ThermalUpdateStatus::RESET_TIME;
    result.estimate = estimate_thermal_model(state, config, now_monotonic_ms);
    return result;
  }
  if (state.last_interval_end_monotonic_ms != 0 &&
      now_monotonic_ms - state.last_interval_end_monotonic_ms > config.max_model_gap_ms) {
    ThermalInterval invalid;
    invalid.end_monotonic_ms = now_monotonic_ms;
    reject_without_reset(state, invalid);
    reset_state(state, config);
    result.status = ThermalUpdateStatus::RESET_LONG_GAP;
    result.estimate = estimate_thermal_model(state, config, now_monotonic_ms);
    return result;
  }
  ThermalInterval invalid;
  invalid.end_monotonic_ms = now_monotonic_ms;
  reject_without_reset(state, invalid);
  result.status = ThermalUpdateStatus::REJECTED_STALE_OR_INCOMPLETE;
  result.estimate = estimate_thermal_model(state, config, now_monotonic_ms);
  return result;
}

inline ThermalModelCheckpoint make_thermal_checkpoint(const ThermalModelState& state,
                                                      const ThermalModelConfig& config) {
  ThermalModelCheckpoint checkpoint;
  checkpoint.config_fingerprint = thermal_detail::config_fingerprint(config);
  checkpoint.state = state;
  checkpoint.checksum = thermal_detail::checkpoint_checksum(checkpoint);
  return checkpoint;
}

inline ThermalCheckpointStatus restore_thermal_checkpoint(ThermalModelState& state,
                                                          const ThermalModelCheckpoint& checkpoint,
                                                          const ThermalModelConfig& config) {
  using namespace thermal_detail;
  if (!valid_config(config)) {
    state = {};
    return ThermalCheckpointStatus::INVALID_CONFIGURATION;
  }
  if (checkpoint.magic != kThermalCheckpointMagic || checkpoint.version != kThermalCheckpointVersion ||
      checkpoint.reserved != 0 || checkpoint.config_fingerprint != config_fingerprint(checkpoint.state.bound_config) ||
      !same_config(checkpoint.state.bound_config, config) || checkpoint.checksum != checkpoint_checksum(checkpoint) ||
      !checkpoint_state_valid(checkpoint.state)) {
    reset_untrusted_state(state, config);
    return ThermalCheckpointStatus::CORRUPT_RESET;
  }
  state = checkpoint.state;
  // Persistence cannot prove that live sources still own the saved generations after a reboot.
  state.recent_data_valid = false;
  return ThermalCheckpointStatus::OK;
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
