#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/learning/oq_ph_model_validation.h"

using namespace oq_power_house;
using namespace oq_power_house::learning;

static AdviceResult batch_fit(float room_trend = 0.0f, float reference_room = 20.0f) {
  SegmentRecord records[9];
  for (unsigned day = 0; day < 9; ++day) {
    auto& record = records[day];
    record.start_epoch_s = (20000U + day) * 86400U;
    record.end_epoch_s = record.start_epoch_s + 86400U;
    record.duration_s = 86400;
    record.context_revision = 1;
    record.mean_room_c = record.mean_setpoint_c = reference_room;
    record.mean_outside_c = -5.0f + 2.0f * day + reference_room - 20.0f;
    for (auto& point : record.effective_outside_profile_centi)
      point = static_cast<int16_t>(lroundf(effective_outside_c(record) * 100.0f));
    record.mean_heat_w = 200.0f * (reference_room - record.mean_outside_c);
    record.room_trend_k_per_h = room_trend;
    record.room_range_k = fabsf(room_trend) * 24.0f;
    record.setpoint_range_c = 0.0f;
    record.water_start_c = record.water_end_c = 35.0f;
  }
  FitConfig config;
  AdviceFitWorkspace workspace;
  auto status =
      begin_advice_fit(records, 9, 20009U * 86400U, HouseLine{150.0f, 18.0f}, QualityConfig{}, config, workspace);
  while (status == LearningStatus::FIT_IN_PROGRESS) status = advance_advice_fit(workspace);
  assert(status == LearningStatus::ADVICE_READY);
  assert(fabs(workspace.result.candidate.heat_loss_w_per_k - 200.0f) < 0.01);
  assert(fabs(workspace.result.candidate.zero_power_temp_c - 20.0f) < 0.01);
  return workspace.result;
}

static ThermalModelState dynamic_fit(double heat_loss, const ThermalModelConfig& config, double reference_room = 20.0) {
  constexpr double capacity = 6000.0;
  ThermalModelState state;
  assert(initialize_thermal_model(state, config));
  double indoor = reference_room;
  uint64_t now = 1000;
  for (unsigned i = 0; i < 400; ++i) {
    const double hours = i % 2 == 0 ? 0.25 : 0.75;
    const double outside = 4.0 + 9.0 * sin(i * 0.13) + reference_room - 20.0;
    const double heat = heat_loss * (reference_room - outside) + 1200.0 * sin(i * 0.61);
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
    interval.indoor_start_c = indoor;
    interval.indoor_end_c = end_indoor;
    interval.mean_indoor_c = equilibrium + (indoor - equilibrium) * (1.0 - decay) / (heat_loss / capacity * hours);
    interval.mean_outside_c = outside;
    interval.mean_heat_w = heat;
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
  // Normalized daily coordinates differ from actual outdoor temperatures.
  // Comparing models must use the same actual outside range on both sides.
  ThermalModelConfig shifted_config;
  auto shifted_thermal = dynamic_fit(200.0, shifted_config, 17.0);
  const auto shifted_batch = batch_fit(0.0f, 17.0f);
  assert(shifted_batch.validated_temp_min_c == 4.0f && shifted_batch.validated_temp_max_c == 8.0f);
  assert(validate_house_models(shifted_batch, shifted_thermal, shifted_config,
                               shifted_thermal.last_interval_end_monotonic_ms, 1)
             .cross_validated_advice_ready);
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
  assert(validation.status == ModelValidationStatus::MODELS_CONSISTENT);
  assert(validation.max_estimated_storage_power_w > 290.0);
  assert(validation.batch_advice_ready && validation.cross_validated_advice_ready && !validation.auto_apply_allowed);

  assert(!validation.thermal.capacity_validated);
  auto biased_capacity = thermal;
  // Preserve U while doubling C: numerical readiness cannot prove physical C.
  biased_capacity.theta_heat_scaled *= 0.5;
  biased_capacity.theta_loss_scaled *= 0.5;
  const auto biased = validate_house_models(warming_batch, biased_capacity, config, now, 1);
  assert(biased.thermal.ready && !biased.thermal.capacity_validated);
  assert(biased.cross_validated_advice_ready && !biased.auto_apply_allowed);
  assert(fabs(biased.max_estimated_storage_power_w / validation.max_estimated_storage_power_w - 2.0) < 1e-6);

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
