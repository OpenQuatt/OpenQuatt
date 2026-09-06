#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/learning/oq_ph_model_validation.h"

using namespace oq_power_house;
using namespace oq_power_house::learning;

static AdviceResult batch_fit(float room_trend = 0.0f) {
  SegmentRecord records[18];
  for (unsigned day = 0; day < 9; ++day) {
    for (unsigned slot = 0; slot < 2; ++slot) {
      auto& record = records[day * 2 + slot];
      record.start_epoch_s = (20000U + day) * 86400U + slot * 28800U;
      record.end_epoch_s = record.start_epoch_s + 14400U;
      record.duration_s = 14400;
      record.context_revision = 1;
      record.mean_room_c = record.mean_setpoint_c = 20.0f;
      record.mean_outside_c = -5.0f + 2.0f * day;
      record.mean_heat_w = 200.0f * (20.0f - record.mean_outside_c);
      record.mean_heat_uncertainty_w = 10.0f;
      record.room_trend_k_per_h = room_trend;
      record.room_range_k = fabsf(room_trend) * 4.0f;
      record.setpoint_range_c = 0.0f;
      record.water_start_c = record.water_end_c = 35.0f;
    }
  }
  FitConfig config;
  config.reference_room_c = config.reference_setpoint_c = 20.0f;
  AdviceFitWorkspace workspace;
  auto status =
      begin_advice_fit(records, 18, 20009U * 86400U, HouseLine{150.0f, 18.0f}, QualityConfig{}, config, workspace);
  while (status == LearningStatus::FIT_IN_PROGRESS) status = advance_advice_fit(workspace);
  assert(status == LearningStatus::ADVICE_READY);
  assert(fabs(workspace.result.candidate.heat_loss_w_per_k - 200.0f) < 0.01);
  assert(fabs(workspace.result.candidate.zero_power_temp_c - 20.0f) < 0.01);
  return workspace.result;
}

static ThermalModelState dynamic_fit(double heat_loss, const ThermalModelConfig& config) {
  constexpr double capacity = 6000.0;
  ThermalModelState state;
  assert(initialize_thermal_model(state, config));
  double indoor = 20.0;
  uint64_t now = 1000;
  for (unsigned i = 0; i < 400; ++i) {
    const double hours = i % 2 == 0 ? 0.25 : 0.75;
    const double outside = 4.0 + 9.0 * sin(i * 0.13);
    const double heat = heat_loss * (20.0 - outside) + 1200.0 * sin(i * 0.61);
    const double equilibrium = outside + heat / heat_loss;
    const double decay = exp(-heat_loss / capacity * hours);
    const double end_indoor = equilibrium + (indoor - equilibrium) * decay;
    ThermalInterval interval;
    interval.start_monotonic_ms = now;
    now += static_cast<uint64_t>(hours * 3600000.0);
    interval.end_monotonic_ms = now;
    interval.context_revision = 1;
    interval.complete = interval.inputs_fresh = interval.generations_consistent = true;
    interval.operational_gates_passed = interval.hidden_heat_exclusion_valid = interval.hidden_heat_excluded = true;
    interval.unmodeled_gain_bound_valid = true;
    interval.unmodeled_gain_bound_w = 0.0;  // Exact synthetic house has no internal/solar source.
    interval.indoor_start_c = indoor;
    interval.indoor_end_c = end_indoor;
    interval.mean_indoor_c = equilibrium + (indoor - equilibrium) * (1.0 - decay) / (heat_loss / capacity * hours);
    interval.mean_outside_c = outside;
    interval.mean_heat_w = heat;
    interval.heat_uncertainty_w = 10.0;
    assert(observe_thermal_interval(state, interval, config).accepted);
    indoor = end_indoor;
  }
  const auto estimate = estimate_thermal_model(state, config, now);
  assert(estimate.ready);
  assert(fabs(estimate.heat_loss_w_per_k - heat_loss) < 2.0);
  assert(fabs(estimate.thermal_capacity_wh_per_k - capacity) < 100.0);
  return state;
}

int main() {
  const auto batch = batch_fit();
  ThermalModelConfig config;
  config.initial_heat_loss_w_per_k = 150.0;
  config.initial_thermal_capacity_wh_per_k = 8000.0;
  auto thermal = dynamic_fit(200.0, config);
  const auto now = thermal.last_interval_end_monotonic_ms;
  auto validation = validate_house_models(batch, thermal, config, now, 1);
  assert(validation.status == ModelValidationStatus::MODELS_CONSISTENT);
  assert(validation.cross_validated_advice_ready && !validation.auto_apply_allowed);
  assert(validation.heat_loss_difference_fraction < 0.01);
  assert(batch.candidate.zero_power_temp_c == 20.0f);
  const auto warming_batch = batch_fit(0.049f);
  validation = validate_house_models(warming_batch, thermal, config, now, 1);
  assert(validation.status == ModelValidationStatus::THERMAL_STORAGE_ACTIVE);
  assert(validation.max_estimated_storage_power_w > 290.0);
  assert(!validation.cross_validated_advice_ready && !validation.auto_apply_allowed);

  const auto disagreeing = dynamic_fit(285.0, config);
  validation = validate_house_models(batch, disagreeing, config, now, 1);
  assert(validation.status == ModelValidationStatus::MODEL_DISAGREEMENT);
  assert(!validation.cross_validated_advice_ready && !validation.auto_apply_allowed);

  validation = validate_house_models(batch, thermal, config, now + config.max_estimate_age_ms + 1, 1);
  assert(validation.status == ModelValidationStatus::THERMAL_UNAVAILABLE);
  validation = validate_house_models(batch, thermal, config, now, 2);
  assert(validation.status == ModelValidationStatus::CONTEXT_MISMATCH);
  invalidate_thermal_observation(thermal, now + 1, config);
  validation = validate_house_models(batch, thermal, config, now + 1, 1);
  assert(validation.status == ModelValidationStatus::THERMAL_UNAVAILABLE);

  // A prior that happens to agree has no identification evidence.
  config.initial_heat_loss_w_per_k = 200.0;
  assert(initialize_thermal_model(thermal, config));
  validation = validate_house_models(batch, thermal, config, now, 1);
  assert(!validation.cross_validated_advice_ready && !validation.auto_apply_allowed);
  return 0;
}
