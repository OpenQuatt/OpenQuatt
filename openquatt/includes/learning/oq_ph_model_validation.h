#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include "oq_ph_learning_fit.h"
#include "oq_ph_thermal_model_logic.h"

namespace oq_power_house::learning {

struct ModelValidationConfig {
  // Development thresholds; field validation is required before enabling apply.
  double max_heat_loss_difference_fraction = 0.20;
  double min_shared_outside_span_c = 4.0;
  double max_storage_power_w = 250.0;
  double max_storage_fraction = 0.10;
};

enum class ModelValidationStatus : uint8_t {
  INVALID_CONFIGURATION = 0,
  BATCH_UNAVAILABLE,
  THERMAL_UNAVAILABLE,
  CONTEXT_MISMATCH,
  INSUFFICIENT_SHARED_RANGE,
  MODEL_DISAGREEMENT,
  THERMAL_STORAGE_ACTIVE,
  MODELS_CONSISTENT,
  WAITING_FOR_VALID_OBSERVATION,
};

struct ModelValidationResult {
  ModelValidationStatus status = ModelValidationStatus::INVALID_CONFIGURATION;
  bool batch_advice_ready = false;
  bool thermal_model_ready = false;
  bool cross_validated_advice_ready = false;
  // This layer has no consent, persistence, HIL or trial evidence and cannot
  // authorize a control write. Model consistency is one future apply gate.
  bool auto_apply_allowed = false;
  double heat_loss_difference_fraction = NAN;
  double shared_outside_min_c = NAN;
  double shared_outside_max_c = NAN;
  double max_estimated_storage_power_w = NAN;
  double max_estimated_storage_fraction = NAN;
  ThermalModelEstimate thermal;
};

inline const char* model_validation_status_name(ModelValidationStatus status) {
  switch (status) {
    case ModelValidationStatus::INVALID_CONFIGURATION:
      return "invalid_model_validation_configuration";
    case ModelValidationStatus::WAITING_FOR_VALID_OBSERVATION:
      return "waiting_for_valid_observation";
    case ModelValidationStatus::BATCH_UNAVAILABLE:
      return "batch_model_unavailable";
    case ModelValidationStatus::THERMAL_UNAVAILABLE:
      return "insufficient_thermal_model_confidence";
    case ModelValidationStatus::CONTEXT_MISMATCH:
      return "model_context_mismatch";
    case ModelValidationStatus::INSUFFICIENT_SHARED_RANGE:
      return "insufficient_shared_temperature_range";
    case ModelValidationStatus::MODEL_DISAGREEMENT:
      return "model_disagreement";
    case ModelValidationStatus::THERMAL_STORAGE_ACTIVE:
      return "thermal_storage_not_stationary";
    case ModelValidationStatus::MODELS_CONSISTENT:
      return "models_consistent";
  }
  return "invalid_model_validation_configuration";
}

inline ModelValidationResult validate_house_models(const AdviceResult& batch, const ThermalModelState& thermal,
                                                   const ThermalModelConfig& thermal_config, uint64_t now_monotonic_ms,
                                                   uint32_t context_revision,
                                                   const ModelValidationConfig& config = {}) {
  ModelValidationResult result;
  if (!valid_thermal_model_config(thermal_config) || !isfinite(config.max_heat_loss_difference_fraction) ||
      config.max_heat_loss_difference_fraction < 0.0 || config.max_heat_loss_difference_fraction > 0.50 ||
      !isfinite(config.min_shared_outside_span_c) || config.min_shared_outside_span_c <= 0.0 ||
      config.min_shared_outside_span_c > 40.0 || now_monotonic_ms == 0 || context_revision == 0 ||
      !isfinite(config.max_storage_power_w) || config.max_storage_power_w < 0.0 ||
      config.max_storage_power_w > 5000.0 || !isfinite(config.max_storage_fraction) ||
      config.max_storage_fraction < 0.0 || config.max_storage_fraction > 1.0)
    return result;
  result.thermal = estimate_thermal_model(thermal, thermal_config, now_monotonic_ms);
  result.thermal_model_ready = result.thermal.ready;
  result.batch_advice_ready = batch.status == LearningStatus::ADVICE_READY && batch.candidate_available &&
                              batch.advice_ready && valid_house_line(batch.candidate) &&
                              isfinite(batch.validated_temp_min_c) && isfinite(batch.validated_temp_max_c) &&
                              batch.validated_temp_max_c > batch.validated_temp_min_c;
  // Even a prior is useful diagnostic output, but only evidence-backed models
  // may pass. Both estimators share metrology; agreement is not independence.
  if (batch.candidate_available && valid_house_line(batch.candidate) && result.thermal.parameters_valid)
    result.heat_loss_difference_fraction =
        fabs(result.thermal.heat_loss_w_per_k - batch.candidate.heat_loss_w_per_k) / batch.candidate.heat_loss_w_per_k;
  if (!result.batch_advice_ready) {
    result.status = ModelValidationStatus::BATCH_UNAVAILABLE;
    return result;
  }
  if (batch.context_revision != context_revision || thermal.context_revision != context_revision) {
    result.status = ModelValidationStatus::CONTEXT_MISMATCH;
    return result;
  }
  if (!result.thermal_model_ready) {
    result.status = ModelValidationStatus::THERMAL_UNAVAILABLE;
    return result;
  }
  if (!isfinite(batch.max_abs_room_trend_k_per_h) || batch.max_abs_room_trend_k_per_h < 0.0f ||
      !isfinite(batch.max_abs_room_trend_per_heat_k_per_h_w) || batch.max_abs_room_trend_per_heat_k_per_h_w < 0.0f) {
    result.status = ModelValidationStatus::BATCH_UNAVAILABLE;
    return result;
  }
  // C [Wh/K] * room trend [K/h] estimates energy going into/out of storage,
  // which must not be mistaken for structural heat loss by the batch fit.
  result.max_estimated_storage_power_w = result.thermal.thermal_capacity_wh_per_k * batch.max_abs_room_trend_k_per_h;
  result.max_estimated_storage_fraction =
      result.thermal.thermal_capacity_wh_per_k * batch.max_abs_room_trend_per_heat_k_per_h_w;
  if (!isfinite(result.max_estimated_storage_power_w) || !isfinite(result.max_estimated_storage_fraction) ||
      result.max_estimated_storage_power_w > config.max_storage_power_w ||
      result.max_estimated_storage_fraction > config.max_storage_fraction) {
    result.status = ModelValidationStatus::THERMAL_STORAGE_ACTIVE;
    return result;
  }
  result.shared_outside_min_c = fmax(static_cast<double>(batch.validated_temp_min_c), thermal.outside_min_c);
  result.shared_outside_max_c = fmin(static_cast<double>(batch.validated_temp_max_c), thermal.outside_max_c);
  if (!isfinite(thermal.outside_min_c) || !isfinite(thermal.outside_max_c) ||
      result.shared_outside_max_c - result.shared_outside_min_c < config.min_shared_outside_span_c) {
    result.status = ModelValidationStatus::INSUFFICIENT_SHARED_RANGE;
    return result;
  }
  if (!isfinite(result.heat_loss_difference_fraction) ||
      result.heat_loss_difference_fraction > config.max_heat_loss_difference_fraction) {
    result.status = ModelValidationStatus::MODEL_DISAGREEMENT;
    return result;
  }
  result.status = ModelValidationStatus::MODELS_CONSISTENT;
  result.cross_validated_advice_ready = true;
  return result;
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
