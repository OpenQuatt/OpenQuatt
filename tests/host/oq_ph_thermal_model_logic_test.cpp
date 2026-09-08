#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/learning/oq_ph_thermal_model_logic.h"

namespace {
using namespace oq_power_house::learning;

constexpr double kTrueHeatLossWPerK = 200.0;
constexpr double kTrueCapacityWhPerK = 6000.0;

ThermalModelConfig test_config() {
  ThermalModelConfig config;
  config.forgetting_factor_per_hour = 1.0;
  config.initial_heat_loss_w_per_k = 350.0;
  config.initial_thermal_capacity_wh_per_k = 12000.0;
  config.initial_covariance = 1000.0;
  config.min_ready_observation_hours = 16.0;
  config.min_information_eigenvalue = 0.05;
  config.max_information_condition = 100000.0;
  config.residual_ewma_alpha_per_hour = 0.15;
  config.max_residual_rms_k_per_h = 0.20;
  config.max_abs_residual_bias_k_per_h = 0.10;
  return config;
}

ThermalInterval exact_interval(uint64_t start_ms, double duration_h, double indoor_start_c, double outside_c,
                               double heat_w, double unmodeled_drift_k_per_h = 0.0) {
  const double loss_factor = kTrueHeatLossWPerK / kTrueCapacityWhPerK * duration_h;
  const double heat_term = heat_w / kTrueCapacityWhPerK * duration_h;
  const double indoor_delta_c =
      (loss_factor * (outside_c - indoor_start_c) + heat_term + unmodeled_drift_k_per_h * duration_h) /
      (1.0 + 0.5 * loss_factor);
  ThermalInterval interval;
  interval.start_monotonic_ms = start_ms;
  interval.end_monotonic_ms = start_ms + static_cast<uint64_t>(duration_h * 3600000.0);
  interval.context_revision = 11;
  interval.complete = true;
  interval.inputs_fresh = true;
  interval.generations_consistent = true;
  interval.operational_gates_passed = true;
  interval.hidden_heat_exclusion_valid = true;
  interval.hidden_heat_excluded = true;
  interval.indoor_start_c = indoor_start_c;
  interval.indoor_end_c = indoor_start_c + indoor_delta_c;
  interval.mean_indoor_c = indoor_start_c + 0.5 * indoor_delta_c;
  interval.mean_outside_c = outside_c;
  interval.mean_heat_w = heat_w;
  return interval;
}

ThermalModelEstimate train_synthetic(ThermalModelState& state, const ThermalModelConfig& config, uint32_t count) {
  assert(initialize_thermal_model(state, config));
  uint64_t time_ms = 1000;
  double indoor_c = 20.0;
  ThermalUpdateResult result;
  constexpr double durations_h[] = {0.25, 0.5, 0.75, 1.0, 0.5};
  constexpr double excitation_w[] = {-1600.0, 800.0, -400.0, 1800.0, 0.0, 1200.0, -900.0};
  for (uint32_t index = 0; index < count; ++index) {
    const double duration_h = durations_h[index % 5U];
    const double outside_c = -5.0 + static_cast<double>((index * 7U) % 23U) * 0.65;
    const double equilibrium_heat_w = kTrueHeatLossWPerK * (indoor_c - outside_c);
    const double heat_w = fmax(0.0, equilibrium_heat_w + excitation_w[index % 7U]);
    auto interval = exact_interval(time_ms, duration_h, indoor_c, outside_c, heat_w);
    result = observe_thermal_interval(state, interval, config);
    assert(result.accepted);
    indoor_c = interval.indoor_end_c;
    time_ms = interval.end_monotonic_ms;
  }
  return result.estimate;
}

void test_synthetic_recovery_and_irregular_intervals() {
  const auto config = test_config();
  ThermalModelState state;
  const auto estimate = train_synthetic(state, config, 240);
  assert(estimate.parameters_valid);
  assert(estimate.ready);
  assert(fabs(estimate.heat_loss_w_per_k - kTrueHeatLossWPerK) < 1.0);
  assert(fabs(estimate.thermal_capacity_wh_per_k - kTrueCapacityWhPerK) < 20.0);
  assert(estimate.information_min_eigenvalue >= config.min_information_eigenvalue);
  assert(thermal_detail::covariance_is_spd(state.covariance_00, state.covariance_01, state.covariance_11));
}

void test_temperature_change_is_information() {
  const auto config = test_config();
  ThermalModelState state;
  assert(initialize_thermal_model(state, config));
  const auto interval = exact_interval(1000, 0.5, 20.0, 0.0, 6000.0);
  assert(fabs(interval.indoor_end_c - interval.indoor_start_c) > 0.01);
  const auto result = observe_thermal_interval(state, interval, config);
  assert(result.accepted);
  assert(state.accepted_samples == 1);
}

void test_stationary_rank_one_data_never_becomes_ready() {
  auto config = test_config();
  config.initial_heat_loss_w_per_k = kTrueHeatLossWPerK;
  config.initial_thermal_capacity_wh_per_k = kTrueCapacityWhPerK;
  ThermalModelState state;
  assert(initialize_thermal_model(state, config));
  uint64_t time_ms = 1000;
  ThermalUpdateResult result;
  for (uint32_t index = 0; index < 100; ++index) {
    const auto interval = exact_interval(time_ms, 0.5, 20.0, 5.0, 3000.0);
    assert(fabs(interval.indoor_end_c - interval.indoor_start_c) < 1e-12);
    result = observe_thermal_interval(state, interval, config);
    assert(result.accepted);
    time_ms = interval.end_monotonic_ms;
  }
  assert(result.estimate.parameters_valid);
  assert(!result.estimate.ready);
  assert((result.estimate.readiness_reasons & THERMAL_READY_UNOBSERVABLE) != 0);
  assert((result.estimate.readiness_reasons & THERMAL_READY_OUTSIDE_SPAN) != 0);
  assert((result.estimate.readiness_reasons & THERMAL_READY_HEAT_SPAN) != 0);
}

void test_missing_stale_and_context_changes_preserve_learning() {
  const auto config = test_config();
  ThermalModelState state;
  train_synthetic(state, config, 10);
  const uint32_t prior_resets = state.reset_count;
  auto missing = exact_interval(state.last_interval_end_monotonic_ms, 0.5, 20.0, 5.0, 3000.0);
  missing.inputs_fresh = false;
  auto result = observe_thermal_interval(state, missing, config);
  assert(result.status == ThermalUpdateStatus::REJECTED_STALE_OR_INCOMPLETE);
  assert(!result.accepted && state.accepted_samples == 10 && state.reset_count == prior_resets);
  assert(!state.recent_data_valid && !result.estimate.ready);

  auto resumed = exact_interval(missing.end_monotonic_ms, 0.5, 20.0, 5.0, 3000.0);
  result = observe_thermal_interval(state, resumed, config);
  assert(result.accepted && state.accepted_samples == 11 && state.recent_data_valid);

  train_synthetic(state, config, 10);
  const uint32_t accepted_before_context_change = state.accepted_samples;
  const double theta_loss_before_context_change = state.theta_loss_scaled;
  const double covariance_before_context_change = state.covariance_00;
  auto changed = exact_interval(state.last_interval_end_monotonic_ms, 0.5, 20.0, 5.0, 3000.0);
  ++changed.context_revision;
  result = observe_thermal_interval(state, changed, config);
  assert(result.accepted && state.accepted_samples == accepted_before_context_change + 1U);
  assert(state.theta_loss_scaled != theta_loss_before_context_change ||
         state.covariance_00 != covariance_before_context_change);
  assert(state.context_revision == changed.context_revision);
  const uint64_t context_watermark_ms = state.last_observation_monotonic_ms;

  auto late_previous_context = exact_interval(context_watermark_ms, 0.5, 20.0, 5.0, 3000.0);
  result = observe_thermal_interval(state, late_previous_context, config);
  assert(result.status == ThermalUpdateStatus::REJECTED_STALE_CONTEXT);
  assert(!result.accepted && state.accepted_samples == accepted_before_context_change + 1U);
  assert(state.context_revision == changed.context_revision);

  auto current_context = exact_interval(late_previous_context.end_monotonic_ms, 0.5, 20.0, 5.0, 3000.0);
  current_context.context_revision = changed.context_revision;
  result = observe_thermal_interval(state, current_context, config);
  assert(result.accepted && state.accepted_samples == accepted_before_context_change + 2U);
  assert(state.context_revision == changed.context_revision);

  train_synthetic(state, config, 10);
  const uint32_t accepted_before_inconsistent = state.accepted_samples;
  auto inconsistent = exact_interval(state.last_interval_end_monotonic_ms, 0.5, 20.0, 5.0, 3000.0);
  inconsistent.generations_consistent = false;
  result = observe_thermal_interval(state, inconsistent, config);
  assert(result.status == ThermalUpdateStatus::REJECTED_STALE_CONTEXT);
  assert(!result.accepted && state.accepted_samples == accepted_before_inconsistent);

  train_synthetic(state, config, 10);
  const uint32_t accepted_before_long_gap = state.accepted_samples;
  auto long_gap =
      exact_interval(state.last_interval_end_monotonic_ms + config.max_model_gap_ms + 1U, 0.5, 20.0, 5.0, 3000.0);
  result = observe_thermal_interval(state, long_gap, config);
  assert(result.accepted && state.accepted_samples == accepted_before_long_gap + 1U);
}

void test_interval_bounds_and_hidden_heat() {
  const auto config = test_config();
  ThermalModelState state;
  train_synthetic(state, config, 10);
  const uint32_t accepted_before_invalid = state.accepted_samples;

  auto short_interval = exact_interval(state.last_interval_end_monotonic_ms, 0.25, 20.0, 5.0, 3000.0);
  --short_interval.end_monotonic_ms;
  auto result = observe_thermal_interval(state, short_interval, config);
  assert(result.status == ThermalUpdateStatus::REJECTED_DURATION && !result.accepted);
  assert(state.accepted_samples == accepted_before_invalid);

  auto hidden_heat = exact_interval(short_interval.end_monotonic_ms, 0.5, 20.0, 5.0, 3000.0);
  hidden_heat.hidden_heat_exclusion_valid = false;
  result = observe_thermal_interval(state, hidden_heat, config);
  assert(result.status == ThermalUpdateStatus::REJECTED_HIDDEN_HEAT &&
         state.accepted_samples == accepted_before_invalid);

  auto nan_interval = exact_interval(hidden_heat.end_monotonic_ms, 0.5, 20.0, 5.0, 3000.0);
  nan_interval.mean_heat_w = NAN;
  result = observe_thermal_interval(state, nan_interval, config);
  assert(result.status == ThermalUpdateStatus::REJECTED_INVALID_INTERVAL && !result.accepted);
  assert(state.accepted_samples == accepted_before_invalid);
}

void test_invalidation_revokes_readiness_without_erasing_history() {
  const auto config = test_config();
  ThermalModelState state;
  const auto ready = train_synthetic(state, config, 240);
  assert(ready.ready);
  const uint32_t accepted_before = state.accepted_samples;
  const uint64_t invalid_time_ms = state.last_interval_end_monotonic_ms + 10000U;
  const auto invalidated = invalidate_thermal_observation(state, invalid_time_ms, config);
  assert(invalidated.status == ThermalUpdateStatus::REJECTED_STALE_OR_INCOMPLETE);
  assert(!invalidated.accepted && !invalidated.estimate.ready);
  assert((invalidated.estimate.readiness_reasons & THERMAL_READY_RECENT_DATA_INVALID) != 0);
  assert(state.accepted_samples == accepted_before);

  const auto stale =
      estimate_thermal_model(state, config, state.last_interval_end_monotonic_ms + config.max_estimate_age_ms + 1U);
  assert(!stale.ready);
  assert((stale.readiness_reasons & THERMAL_READY_STALE_MODEL) != 0);

  auto overlaps_invalid_observation = exact_interval(state.last_interval_end_monotonic_ms, 0.5, 20.0, 5.0, 3000.0);
  const auto overlap = observe_thermal_interval(state, overlaps_invalid_observation, config);
  assert(overlap.status == ThermalUpdateStatus::REJECTED_INVALID_INTERVAL);
  assert(state.accepted_samples == accepted_before);

  const auto ready_again = train_synthetic(state, config, 240);
  assert(ready_again.ready);
  const uint64_t long_gap_end = state.last_interval_end_monotonic_ms + config.max_model_gap_ms + 1U;
  const auto long_gap = invalidate_thermal_observation(state, long_gap_end, config);
  assert(long_gap.status == ThermalUpdateStatus::REJECTED_STALE_OR_INCOMPLETE);
  assert(state.accepted_samples == 240);
  assert(state.last_observation_monotonic_ms == long_gap_end);
  const auto late = exact_interval(long_gap_end - 1800000U, 0.5, 20.0, 5.0, 3000.0);
  assert(!observe_thermal_interval(state, late, config).accepted);
}

void test_forgetting_factor_is_elapsed_time_based() {
  auto config = test_config();
  config.forgetting_factor_per_hour = 0.96;
  ThermalModelState hourly;
  ThermalModelState quarter_hourly;
  assert(initialize_thermal_model(hourly, config));
  assert(initialize_thermal_model(quarter_hourly, config));

  auto one_hour = exact_interval(1000, 1.0, 20.0, 20.0, 0.0);
  assert(observe_thermal_interval(hourly, one_hour, config).accepted);

  uint64_t time_ms = 1000;
  for (uint32_t index = 0; index < 4; ++index) {
    auto quarter = exact_interval(time_ms, 0.25, 20.0, 20.0, 0.0);
    assert(observe_thermal_interval(quarter_hourly, quarter, config).accepted);
    time_ms = quarter.end_monotonic_ms;
  }
  assert(fabs(hourly.covariance_00 - quarter_hourly.covariance_00) < 1e-9);
  assert(fabs(hourly.covariance_11 - quarter_hourly.covariance_11) < 1e-9);
  assert(fabs(hourly.information_00 - quarter_hourly.information_00) < 1e-9);
  assert(fabs(hourly.information_01 - quarter_hourly.information_01) < 1e-9);
  assert(fabs(hourly.information_11 - quarter_hourly.information_11) < 1e-9);
  assert(fabs(hourly.effective_observation_hours - quarter_hourly.effective_observation_hours) < 0.02);
}

void test_readiness_evidence_is_duration_normalized() {
  auto config = test_config();
  config.forgetting_factor_per_hour = 1.0;
  ThermalModelState hourly;
  ThermalModelState quarter_hourly;
  assert(initialize_thermal_model(hourly, config));
  assert(initialize_thermal_model(quarter_hourly, config));

  const auto one_hour = exact_interval(1000, 1.0, 20.0, 5.0, 3000.0);
  assert(observe_thermal_interval(hourly, one_hour, config).accepted);
  uint64_t time_ms = 1000;
  for (uint32_t index = 0; index < 4; ++index) {
    const auto quarter = exact_interval(time_ms, 0.25, 20.0, 5.0, 3000.0);
    assert(observe_thermal_interval(quarter_hourly, quarter, config).accepted);
    time_ms = quarter.end_monotonic_ms;
  }
  assert(fabs(hourly.effective_observation_hours - quarter_hourly.effective_observation_hours) < 1e-12);
  assert(fabs(hourly.information_00 - quarter_hourly.information_00) < 1e-12);
  assert(fabs(hourly.information_01 - quarter_hourly.information_01) < 1e-12);
  assert(fabs(hourly.information_11 - quarter_hourly.information_11) < 1e-12);
}

void test_residual_diagnostics_use_rate_units() {
  auto config = test_config();
  config.initial_heat_loss_w_per_k = kTrueHeatLossWPerK;
  config.initial_thermal_capacity_wh_per_k = kTrueCapacityWhPerK;
  ThermalModelState hourly;
  ThermalModelState quarter_hourly;
  assert(initialize_thermal_model(hourly, config));
  assert(initialize_thermal_model(quarter_hourly, config));

  constexpr double drift_k_per_h = 0.005;
  const auto hour = exact_interval(1000, 1.0, 20.0, 5.0, 3000.0, drift_k_per_h);
  const auto quarter = exact_interval(1000, 0.25, 20.0, 5.0, 3000.0, drift_k_per_h);
  assert(observe_thermal_interval(hourly, hour, config).accepted);
  assert(observe_thermal_interval(quarter_hourly, quarter, config).accepted);
  assert(fabs(hourly.residual_mean_k_per_h - quarter_hourly.residual_mean_k_per_h) < 1e-12);
  assert(fabs(hourly.residual_mean_k_per_h - drift_k_per_h) < 1e-12);
}

void test_only_observed_duration_ages_old_evidence() {
  auto config = test_config();
  config.forgetting_factor_per_hour = 0.96;
  ThermalModelState state;
  train_synthetic(state, config, 20);
  const double prior_information = state.information_00;
  const uint64_t prior_end_ms = state.last_interval_end_monotonic_ms;
  const auto zero_feature = exact_interval(prior_end_ms + 5ULL * 3600000ULL, 1.0, 20.0, 20.0, 0.0);
  const auto result = observe_thermal_interval(state, zero_feature, config);
  assert(result.accepted);
  assert(fabs(state.information_00 - prior_information * pow(config.forgetting_factor_per_hour, 1.0)) < 1e-9);
}

void test_old_excitation_cannot_keep_readiness_alive() {
  auto config = test_config();
  config.forgetting_factor_per_hour = 0.95;
  config.min_ready_observation_hours = 4.0;
  ThermalModelState state;
  auto estimate = train_synthetic(state, config, 240);
  assert(estimate.ready);

  uint64_t time_ms = state.last_interval_end_monotonic_ms;
  for (uint32_t index = 0; index < 400; ++index) {
    const auto stationary = exact_interval(time_ms, 1.0, 20.0, 5.0, 3000.0);
    const auto result = observe_thermal_interval(state, stationary, config);
    assert(result.accepted);
    estimate = result.estimate;
    time_ms = stationary.end_monotonic_ms;
  }
  assert(!estimate.ready);
  assert((estimate.readiness_reasons &
          (THERMAL_READY_UNOBSERVABLE | THERMAL_READY_OUTSIDE_SPAN | THERMAL_READY_HEAT_SPAN)) != 0);
}

void test_parameter_bounds_and_covariance_corruption() {
  const auto config = test_config();
  ThermalModelState state;
  assert(initialize_thermal_model(state, config));
  state.theta_heat_scaled = -1.0;
  auto estimate = estimate_thermal_model(state, config, 1000);
  assert(!estimate.parameters_valid && !estimate.ready);
  assert((estimate.readiness_reasons & THERMAL_READY_PARAMETER_BOUNDS) != 0);

  assert(initialize_thermal_model(state, config));
  state.covariance_01 = state.covariance_00 * 2.0;
  const auto interval = exact_interval(1000, 0.5, 20.0, 5.0, 3000.0);
  const auto result = observe_thermal_interval(state, interval, config);
  assert(result.status == ThermalUpdateStatus::RESET_NUMERIC_STATE);
  assert(!result.accepted && state.accepted_samples == 0);
  assert(thermal_detail::covariance_is_spd(state.covariance_00, state.covariance_01, state.covariance_11));
  const auto overlapping = exact_interval(interval.start_monotonic_ms, 0.5, 20.0, 5.0, 3000.0);
  assert(observe_thermal_interval(state, overlapping, config).status == ThermalUpdateStatus::REJECTED_INVALID_INTERVAL);
}

void test_corrupt_ordering_metadata_is_not_retained() {
  const auto config = test_config();
  ThermalModelState state;
  assert(initialize_thermal_model(state, config));
  state.covariance_00 = NAN;
  state.last_interval_end_monotonic_ms = UINT64_MAX - 1U;
  state.last_observation_monotonic_ms = UINT64_MAX;
  state.context_revision = UINT32_MAX;

  const auto discarded = exact_interval(1000, 0.5, 20.0, 5.0, 3000.0);
  const auto reset = observe_thermal_interval(state, discarded, config);
  assert(reset.status == ThermalUpdateStatus::RESET_NUMERIC_STATE && !reset.accepted);
  assert(state.last_interval_end_monotonic_ms == 0 &&
         state.last_observation_monotonic_ms == discarded.end_monotonic_ms);
  assert(state.context_revision == 0);

  const auto recovered = exact_interval(discarded.end_monotonic_ms, 0.5, 20.0, 5.0, 3000.0);
  const auto accepted = observe_thermal_interval(state, recovered, config);
  assert(accepted.accepted && state.accepted_samples == 1);
  assert(state.context_revision == recovered.context_revision);

  state.covariance_00 = NAN;
  state.last_observation_monotonic_ms = UINT64_MAX;
  state.context_revision = UINT32_MAX;
  const uint64_t invalidated_at_ms = recovered.end_monotonic_ms + 1000U;
  const auto invalidated = invalidate_thermal_observation(state, invalidated_at_ms, config);
  assert(invalidated.status == ThermalUpdateStatus::RESET_NUMERIC_STATE);
  assert(state.last_observation_monotonic_ms == invalidated_at_ms && state.context_revision == 0);
  const auto after_invalidation = exact_interval(invalidated_at_ms, 0.5, 20.0, 5.0, 3000.0);
  assert(observe_thermal_interval(state, after_invalidation, config).accepted);
}

void test_sample_counter_overflow_consumes_interval() {
  const auto config = test_config();
  ThermalModelState state;
  train_synthetic(state, config, 2);
  state.accepted_samples = UINT32_MAX;
  const auto interval = exact_interval(state.last_interval_end_monotonic_ms, 0.5, 20.0, 5.0, 3000.0);
  const auto result = observe_thermal_interval(state, interval, config);
  assert(result.status == ThermalUpdateStatus::RESET_NUMERIC_STATE);
  assert(state.accepted_samples == 0);
  assert(state.last_observation_monotonic_ms == interval.end_monotonic_ms);

  const auto overlapping = exact_interval(interval.start_monotonic_ms, 0.5, 20.0, 5.0, 3000.0);
  assert(observe_thermal_interval(state, overlapping, config).status == ThermalUpdateStatus::REJECTED_INVALID_INTERVAL);
}

void test_live_configuration_is_bound_to_state() {
  const auto config = test_config();
  ThermalModelState state;
  const auto ready = train_synthetic(state, config, 240);
  assert(ready.ready);

  auto changed = config;
  changed.max_residual_rms_k_per_h *= 1.5;
  const auto interval = exact_interval(state.last_interval_end_monotonic_ms, 0.5, 20.0, 5.0, 3000.0);
  const auto result = observe_thermal_interval(state, interval, changed);
  assert(result.accepted && state.accepted_samples == 241);
  assert(thermal_detail::same_config(state.bound_config, changed));
  assert(result.estimate.parameters_valid);

  auto incompatible = changed;
  incompatible.loss_feature_scale_kh *= 2.0;
  const uint32_t accepted_before_incompatible = state.accepted_samples;
  const double theta_before_incompatible = state.theta_loss_scaled;
  const double covariance_before_incompatible = state.covariance_00;
  const auto rejected = observe_thermal_interval(
      state, exact_interval(state.last_interval_end_monotonic_ms, 0.5, 20.0, 5.0, 3000.0), incompatible);
  assert(rejected.status == ThermalUpdateStatus::REJECTED_INCOMPATIBLE_CONFIGURATION && !rejected.accepted);
  assert(state.accepted_samples == accepted_before_incompatible &&
         state.theta_loss_scaled == theta_before_incompatible && state.covariance_00 == covariance_before_incompatible);
  assert(thermal_detail::same_config(state.bound_config, changed));
  assert(initialize_thermal_model(state, incompatible));
  assert(state.accepted_samples == 0 && thermal_detail::same_config(state.bound_config, incompatible));
}

void test_invalid_configuration_never_seeds_ready_model() {
  auto invalid = test_config();
  invalid.min_interval_ms = 1;
  ThermalModelState state;
  assert(!initialize_thermal_model(state, invalid));
  assert(!state.initialized);

  const auto config = test_config();
  assert(initialize_thermal_model(state, config));
  const auto seed_estimate = estimate_thermal_model(state, config, 1000);
  assert(seed_estimate.parameters_valid);
  assert(!seed_estimate.ready);
  assert((seed_estimate.readiness_reasons & THERMAL_READY_NOT_ENOUGH_SAMPLES) != 0);
}

}  // namespace

int main() {
  test_synthetic_recovery_and_irregular_intervals();
  test_temperature_change_is_information();
  test_stationary_rank_one_data_never_becomes_ready();
  test_missing_stale_and_context_changes_preserve_learning();
  test_interval_bounds_and_hidden_heat();
  test_invalidation_revokes_readiness_without_erasing_history();
  test_forgetting_factor_is_elapsed_time_based();
  test_readiness_evidence_is_duration_normalized();
  test_residual_diagnostics_use_rate_units();
  test_only_observed_duration_ages_old_evidence();
  test_old_excitation_cannot_keep_readiness_alive();
  test_parameter_bounds_and_covariance_corruption();
  test_corrupt_ordering_metadata_is_not_retained();
  test_sample_counter_overflow_consumes_interval();
  test_live_configuration_is_bound_to_state();
  test_invalid_configuration_never_seeds_ready_model();
  return 0;
}
