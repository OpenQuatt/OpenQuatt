#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_boiler_commissioning {

static constexpr float kDefaultMaxWaterTemperatureC = 60.0f;
static constexpr float kMinMaxWaterTemperatureC = 25.0f;
static constexpr float kMaxMaxWaterTemperatureC = 75.0f;
static constexpr float kBoilerTestHeadroomC = 5.0f;
static constexpr float kBoilerTestMaxFlowLph = 1000.0f;
static constexpr float kBoilerTestMinimumResultW = 1000.0f;
static constexpr float kBoilerTestMaximumResultW = 50000.0f;
static constexpr float kBoilerTestMinimumFlowStabilityRatio = 0.90f;
static constexpr float kBoilerTestMinimumApplyConfidence = 80.0f;
static constexpr int kBoilerTestMinimumPowerSamples = 8;
static constexpr uint32_t kEmpiricalApplyConfirmationWindowMs = 30000UL;

inline bool boiler_test_dhw_interferes(bool opentherm_selected, bool dhw_has_state, bool dhw_active) {
  return opentherm_selected && dhw_has_state && dhw_active;
}

inline float normalize_max_water_temperature_c(float max_c) {
  if (isnan(max_c)) max_c = kDefaultMaxWaterTemperatureC;
  return fmaxf(kMinMaxWaterTemperatureC, fminf(max_c, kMaxMaxWaterTemperatureC));
}

inline float commissioning_target_temperature_c(float max_c, float headroom_c = kBoilerTestHeadroomC) {
  if (isnan(headroom_c) || headroom_c < 0.0f) return NAN;
  return normalize_max_water_temperature_c(max_c) - headroom_c;
}

struct OperatingPoint {
  bool feasible = false;
  bool flow_limited = false;
  float target_temperature_c = NAN;
  float theoretical_flow_lph = NAN;
  float target_flow_lph = NAN;
  float headroom_c = NAN;
  const char* reason = nullptr;
};

inline OperatingPoint compute_operating_point(float rated_power_w, float inlet_c, float max_c, float flow_lph,
                                              float cp_j_per_kgk, float headroom_c = kBoilerTestHeadroomC) {
  OperatingPoint out{};
  out.headroom_c = headroom_c;

  if (isnan(rated_power_w) || isnan(inlet_c) || isnan(max_c) || isnan(flow_lph) || isnan(cp_j_per_kgk) ||
      isnan(headroom_c) || rated_power_w <= 0.0f || flow_lph <= 0.0f || cp_j_per_kgk <= 0.0f || headroom_c < 0.0f) {
    out.reason = "invalid input";
    return out;
  }

  const float max_target = commissioning_target_temperature_c(max_c, headroom_c);
  const float available_headroom_c = max_target - inlet_c;
  if (available_headroom_c <= 0.0f) {
    out.reason = "insufficient thermal headroom for boiler power test";
    return out;
  }

  const float required_flow_kgps = rated_power_w / (cp_j_per_kgk * available_headroom_c);
  out.theoretical_flow_lph = required_flow_kgps * 3600.0f;
  out.target_flow_lph = out.theoretical_flow_lph;
  out.target_temperature_c = max_target;
  out.feasible = true;
  out.reason = nullptr;
  return out;
}

inline OperatingPoint compute_opentherm_operating_point(bool opentherm_selected, float otb_max_capacity_w,
                                                        float rated_power_w, float inlet_c, float max_c,
                                                        float base_flow_lph, float cp_j_per_kgk = 4180.0f,
                                                        float headroom_c = kBoilerTestHeadroomC) {
  if (isnan(max_c) || isnan(headroom_c) || headroom_c < 0.0f) {
    OperatingPoint out{};
    out.headroom_c = headroom_c;
    out.reason = "invalid input";
    return out;
  }
  const float max_target = commissioning_target_temperature_c(max_c, headroom_c);

  if (!opentherm_selected) {
    (void)otb_max_capacity_w;
    (void)rated_power_w;
    (void)cp_j_per_kgk;
    OperatingPoint out{};
    out.headroom_c = headroom_c;
    if (isnan(inlet_c) || isnan(base_flow_lph) || base_flow_lph <= 0.0f) {
      out.reason = "invalid input";
      return out;
    }
    out.feasible = true;
    out.target_flow_lph = base_flow_lph;
    out.target_temperature_c = max_target;
    out.reason = nullptr;
    return out;
  }

  if (isnan(inlet_c) || isnan(base_flow_lph) || base_flow_lph <= 0.0f) {
    OperatingPoint out{};
    out.headroom_c = headroom_c;
    out.reason = "invalid input";
    return out;
  }
  if (max_target - inlet_c <= 0.0f) {
    OperatingPoint out{};
    out.headroom_c = headroom_c;
    out.reason = "insufficient thermal headroom for boiler power test";
    return out;
  }

  if (isnan(otb_max_capacity_w) || otb_max_capacity_w <= 0.0f) {
    (void)rated_power_w;
    (void)cp_j_per_kgk;
    OperatingPoint out{};
    out.feasible = true;
    out.headroom_c = headroom_c;
    out.target_flow_lph = base_flow_lph;
    out.target_temperature_c = max_target;
    out.reason = nullptr;
    return out;
  }

  auto op = compute_operating_point(otb_max_capacity_w, inlet_c, max_c, base_flow_lph, cp_j_per_kgk, headroom_c);
  if (!op.feasible) return op;

  const float selected_flow_lph = fmaxf(base_flow_lph, fminf(op.theoretical_flow_lph, kBoilerTestMaxFlowLph));
  op.target_flow_lph = selected_flow_lph;
  op.flow_limited = op.theoretical_flow_lph > kBoilerTestMaxFlowLph;
  return op;
}

class FlowReachabilityMonitor {
 public:
  void reset() {
    saturated_since_ms_ = 0;
    reference_flow_lph_ = NAN;
    best_flow_lph_ = NAN;
  }

  bool update(uint32_t now_ms, float flow_lph, float target_flow_lph, float flow_band_lph, float output_ipwm,
              uint32_t hold_ms = 60000UL, float saturation_ipwm = 60.0f, float progress_lph = 20.0f) {
    if (isnan(flow_lph) || isnan(target_flow_lph) || isnan(flow_band_lph) || isnan(output_ipwm) || flow_lph <= 0.0f ||
        target_flow_lph <= 0.0f || flow_band_lph < 0.0f || hold_ms == 0 || progress_lph < 0.0f) {
      reset();
      return false;
    }

    const float lower_flow_lph = target_flow_lph - flow_band_lph;
    if (flow_lph >= lower_flow_lph || output_ipwm > saturation_ipwm) {
      reset();
      return false;
    }

    if (saturated_since_ms_ == 0) {
      saturated_since_ms_ = now_ms;
      reference_flow_lph_ = flow_lph;
      best_flow_lph_ = flow_lph;
      return false;
    }

    if (isnan(best_flow_lph_) || flow_lph > best_flow_lph_) best_flow_lph_ = flow_lph;
    if (best_flow_lph_ >= reference_flow_lph_ + progress_lph) {
      saturated_since_ms_ = now_ms;
      reference_flow_lph_ = best_flow_lph_;
      return false;
    }

    return (uint32_t)(now_ms - saturated_since_ms_) >= hold_ms;
  }

  float best_flow_lph() const { return best_flow_lph_; }

  uint32_t saturated_duration_ms(uint32_t now_ms) const {
    return saturated_since_ms_ == 0 ? 0UL : (uint32_t)(now_ms - saturated_since_ms_);
  }

 private:
  uint32_t saturated_since_ms_{0};
  float reference_flow_lph_{NAN};
  float best_flow_lph_{NAN};
};

class BoilerActivationSettleMonitor {
 public:
  void reset() {
    active_ = false;
    active_since_ms_ = 0;
  }

  bool update(uint32_t now_ms, bool boiler_active, uint32_t settle_ms) {
    if (!boiler_active) {
      reset();
      return false;
    }
    if (!active_) {
      active_ = true;
      active_since_ms_ = now_ms;
    }
    return (uint32_t)(now_ms - active_since_ms_) >= settle_ms;
  }

 private:
  bool active_{false};
  uint32_t active_since_ms_{0};
};

enum PowerPlateauUpdate : uint8_t {
  POWER_PLATEAU_WAITING = 0,
  POWER_PLATEAU_STABLE,
  POWER_PLATEAU_LOST,
};

class PowerPlateauMonitor {
 public:
  static constexpr int kMaxWindowSamples = 8;

  void reset() {
    sample_count_ = 0;
    next_sample_ = 0;
    reference_w_ = NAN;
    stable_ = false;
  }

  PowerPlateauUpdate update(float power_w, float plateau_ratio, int required_samples) {
    if (!isfinite(power_w) || power_w <= 0.0f || !isfinite(plateau_ratio) || plateau_ratio <= 0.0f ||
        plateau_ratio >= 1.0f || required_samples < 2 || required_samples > kMaxWindowSamples) {
      const bool was_stable = stable_;
      reset();
      return was_stable ? POWER_PLATEAU_LOST : POWER_PLATEAU_WAITING;
    }

    if (stable_) {
      const float tolerance = 1.0f - plateau_ratio;
      const float lower_w = reference_w_ * (1.0f - tolerance);
      const float upper_w = reference_w_ * (1.0f + tolerance);
      if (power_w >= lower_w && power_w <= upper_w) return POWER_PLATEAU_STABLE;

      reset();
      push_sample(power_w, required_samples);
      return POWER_PLATEAU_LOST;
    }

    push_sample(power_w, required_samples);
    if (sample_count_ < required_samples) return POWER_PLATEAU_WAITING;

    float min_w = samples_[0];
    float max_w = samples_[0];
    float sum_w = 0.0f;
    for (int i = 0; i < required_samples; i++) {
      min_w = fminf(min_w, samples_[i]);
      max_w = fmaxf(max_w, samples_[i]);
      sum_w += samples_[i];
    }
    const float average_w = sum_w / (float)required_samples;
    const float allowed_spread_w = average_w * (1.0f - plateau_ratio);
    if (max_w - min_w > allowed_spread_w) return POWER_PLATEAU_WAITING;

    reference_w_ = average_w;
    stable_ = true;
    return POWER_PLATEAU_STABLE;
  }

  bool stable() const { return stable_; }
  float reference_w() const { return reference_w_; }

 private:
  void push_sample(float power_w, int required_samples) {
    if (sample_count_ < required_samples) {
      samples_[sample_count_++] = power_w;
      next_sample_ = sample_count_ % required_samples;
      return;
    }
    samples_[next_sample_] = power_w;
    next_sample_ = (next_sample_ + 1) % required_samples;
  }

  float samples_[kMaxWindowSamples]{};
  int sample_count_{0};
  int next_sample_{0};
  float reference_w_{NAN};
  bool stable_{false};
};

enum ResultQuality : uint8_t {
  RESULT_QUALITY_NONE = 0,
  RESULT_QUALITY_OPENTHERM_ID15_AVAILABLE = 1,
  RESULT_QUALITY_EMPIRICAL_UNVERIFIED = 2,
  RESULT_QUALITY_RELAY_EMPIRICAL = 3,
  RESULT_QUALITY_FLOW_LIMITED = 4,
  RESULT_QUALITY_INVALID = 5,
};

enum ResultApplyMode : uint8_t {
  RESULT_APPLY_DENIED = 0,
  RESULT_APPLY_DIRECT = 1,
  RESULT_APPLY_CONFIRMATION_REQUIRED = 2,
};

struct MeasurementQualityEvidence {
  bool completed = false;
  bool opentherm_selected = false;
  bool capacity_verified = false;
  bool flow_limited = false;
  bool transport_clean = false;
  bool boiler_active_throughout = false;
  bool thermal_safe = false;
  bool dhw_clear = false;
  uint32_t measurement_ticks = 0;
  uint32_t stable_flow_ticks = 0;
  int valid_power_samples = 0;
  float result_w = NAN;
  float confidence_percent = 0.0f;
};

inline bool result_is_in_safe_range(float result_w) {
  return isfinite(result_w) && result_w >= kBoilerTestMinimumResultW && result_w <= kBoilerTestMaximumResultW;
}

inline float measurement_flow_stability_ratio(uint32_t measurement_ticks, uint32_t stable_flow_ticks) {
  if (measurement_ticks == 0 || stable_flow_ticks > measurement_ticks) return 0.0f;
  return (float)stable_flow_ticks / (float)measurement_ticks;
}

inline ResultQuality evaluate_result_quality(const MeasurementQualityEvidence& evidence) {
  const bool measurement_valid =
      evidence.completed && evidence.transport_clean && evidence.boiler_active_throughout && evidence.thermal_safe &&
      evidence.dhw_clear && evidence.valid_power_samples >= kBoilerTestMinimumPowerSamples &&
      result_is_in_safe_range(evidence.result_w) && isfinite(evidence.confidence_percent) &&
      evidence.confidence_percent >= kBoilerTestMinimumApplyConfidence &&
      measurement_flow_stability_ratio(evidence.measurement_ticks, evidence.stable_flow_ticks) >=
          kBoilerTestMinimumFlowStabilityRatio;
  if (!measurement_valid) return RESULT_QUALITY_INVALID;
  if (evidence.flow_limited) return RESULT_QUALITY_FLOW_LIMITED;
  if (!evidence.opentherm_selected) return RESULT_QUALITY_RELAY_EMPIRICAL;
  if (evidence.capacity_verified) return RESULT_QUALITY_OPENTHERM_ID15_AVAILABLE;
  return RESULT_QUALITY_EMPIRICAL_UNVERIFIED;
}

inline const char* result_quality_text(uint8_t quality) {
  switch (quality) {
    case RESULT_QUALITY_OPENTHERM_ID15_AVAILABLE:
      return "OpenTherm measurement, ID15 capacity available";
    case RESULT_QUALITY_EMPIRICAL_UNVERIFIED:
      return "empirical, ID15 unavailable";
    case RESULT_QUALITY_RELAY_EMPIRICAL:
      return "empirical relay measurement";
    case RESULT_QUALITY_FLOW_LIMITED:
      return "rejected: flow/headroom limited";
    case RESULT_QUALITY_INVALID:
      return "rejected: quality requirements not met";
    default:
      return "not available";
  }
}

inline ResultApplyMode result_apply_mode(uint8_t quality) {
  switch (quality) {
    case RESULT_QUALITY_OPENTHERM_ID15_AVAILABLE:
    case RESULT_QUALITY_RELAY_EMPIRICAL:
      return RESULT_APPLY_DIRECT;
    case RESULT_QUALITY_EMPIRICAL_UNVERIFIED:
      return RESULT_APPLY_CONFIRMATION_REQUIRED;
    default:
      return RESULT_APPLY_DENIED;
  }
}

enum ApplyConfirmationDecision : uint8_t {
  APPLY_CONFIRMATION_ARMED = 0,
  APPLY_CONFIRMATION_CONFIRMED = 1,
};

class ApplyConfirmationWindow {
 public:
  void reset() {
    armed_ = false;
    armed_at_ms_ = 0;
  }

  ApplyConfirmationDecision confirm_or_arm(uint32_t now_ms, uint32_t window_ms = kEmpiricalApplyConfirmationWindowMs) {
    if (armed_ && window_ms > 0 && (uint32_t)(now_ms - armed_at_ms_) <= window_ms) {
      reset();
      return APPLY_CONFIRMATION_CONFIRMED;
    }
    armed_ = true;
    armed_at_ms_ = now_ms;
    return APPLY_CONFIRMATION_ARMED;
  }

  bool active(uint32_t now_ms, uint32_t window_ms = kEmpiricalApplyConfirmationWindowMs) const {
    return armed_ && window_ms > 0 && (uint32_t)(now_ms - armed_at_ms_) <= window_ms;
  }

  bool expire(uint32_t now_ms, uint32_t window_ms = kEmpiricalApplyConfirmationWindowMs) {
    if (!armed_ || (window_ms > 0 && (uint32_t)(now_ms - armed_at_ms_) <= window_ms)) return false;
    reset();
    return true;
  }

 private:
  bool armed_{false};
  uint32_t armed_at_ms_{0};
};

inline bool has_sufficient_headroom(float rated_power_w, float inlet_c, float max_c, float flow_lph, float cp_j_per_kgk,
                                    float headroom_c = kBoilerTestHeadroomC) {
  return compute_operating_point(rated_power_w, inlet_c, max_c, flow_lph, cp_j_per_kgk, headroom_c).feasible;
}

}  // namespace oq_boiler_commissioning
