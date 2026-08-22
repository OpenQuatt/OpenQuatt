#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_boiler_commissioning {

static constexpr float kDefaultMaxWaterTemperatureC = 60.0f;
static constexpr float kMinMaxWaterTemperatureC = 25.0f;
static constexpr float kMaxMaxWaterTemperatureC = 75.0f;
static constexpr float kBoilerTestHeadroomC = 5.0f;

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

  const float selected_flow_lph = fmaxf(base_flow_lph, fminf(op.theoretical_flow_lph, 1000.0f));
  op.target_flow_lph = selected_flow_lph;
  op.flow_limited = op.theoretical_flow_lph > 1000.0f;
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

inline bool result_apply_allowed(bool opentherm_selected, bool capacity_verified, bool flow_limited) {
  return !opentherm_selected || (capacity_verified && !flow_limited);
}

inline bool has_sufficient_headroom(float rated_power_w, float inlet_c, float max_c, float flow_lph, float cp_j_per_kgk,
                                    float headroom_c = kBoilerTestHeadroomC) {
  return compute_operating_point(rated_power_w, inlet_c, max_c, flow_lph, cp_j_per_kgk, headroom_c).feasible;
}

}  // namespace oq_boiler_commissioning
