#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_boiler_commissioning {

struct OperatingPoint {
  bool feasible = false;
  float target_temperature_c = NAN;
  float required_flow_lph = NAN;
  float headroom_c = NAN;
  const char* reason = nullptr;
};

inline OperatingPoint compute_operating_point(float rated_power_w, float inlet_c, float max_c, float flow_lph,
                                              float cp_j_per_kgk, float headroom_c = 5.0f) {
  OperatingPoint out{};
  out.headroom_c = headroom_c;

  if (isnan(rated_power_w) || isnan(inlet_c) || isnan(max_c) || isnan(flow_lph) || isnan(cp_j_per_kgk) ||
      rated_power_w <= 0.0f || flow_lph <= 0.0f || cp_j_per_kgk <= 0.0f) {
    out.reason = "invalid input";
    return out;
  }

  if (inlet_c >= max_c) {
    out.reason = "inlet at or above max";
    return out;
  }

  const float available_headroom_c = max_c - headroom_c - inlet_c;
  if (available_headroom_c <= 0.0f) {
    out.reason = "insufficient thermal headroom for boiler power test";
    return out;
  }

  // Required flow to deliver rated power within headroom
  const float required_flow_kgps = rated_power_w / (cp_j_per_kgk * available_headroom_c);
  const float required_flow_lph = required_flow_kgps * 3600.0f;
  out.required_flow_lph = required_flow_lph;

  // If current flow is sufficient, use it and set target with headroom
  // Otherwise, signal needed flow increase or infeasibility
  if (required_flow_lph > 1500.0f) {
    out.reason = "insufficient thermal headroom for boiler power test";
    return out;
  }

  // Target is inlet + deltaT at rated power, capped at max - headroom
  const float thermal_conductance = (flow_lph / 3600.0f) * cp_j_per_kgk;
  const float delta_t_at_rated = rated_power_w / thermal_conductance;
  float target = inlet_c + delta_t_at_rated;
  const float max_target = max_c - headroom_c;
  if (target > max_target) target = max_target;
  if (target <= inlet_c) target = inlet_c + 1.0f;
  if (target >= max_c) target = max_c - headroom_c;

  out.target_temperature_c = target;
  out.feasible = true;
  out.reason = nullptr;
  return out;
}

inline OperatingPoint compute_opentherm_operating_point(bool opentherm_selected, float otb_max_capacity_w,
                                                        float rated_power_w, float inlet_c, float max_c, float flow_lph,
                                                        float cp_j_per_kgk, float headroom_c = 5.0f) {
  if (!opentherm_selected) {
    // R1: keep existing behavior, no dynamic flow, just headroom-capped target
    (void)otb_max_capacity_w;
    (void)rated_power_w;
    (void)cp_j_per_kgk;
    OperatingPoint out{};
    out.feasible = true;
    out.headroom_c = headroom_c;
    out.required_flow_lph = flow_lph;
    float max_target = max_c - headroom_c;
    if (isnan(max_target) || max_target <= inlet_c) max_target = inlet_c + 1.0f;
    out.target_temperature_c = max_target;
    out.reason = nullptr;
    return out;
  }
  if (isnan(otb_max_capacity_w) || otb_max_capacity_w <= 0.0f) {
    // OT max capacity unavailable (ID15 not supported): use conservative high flow
    // Do not refuse; let thermal guard and measurement plateau determine reliability
    (void)rated_power_w;
    (void)flow_lph;
    (void)cp_j_per_kgk;
    OperatingPoint out{};
    out.feasible = true;
    out.headroom_c = headroom_c;
    out.required_flow_lph = 1500.0f;
    float max_target = max_c - headroom_c;
    if (isnan(max_target) || max_target <= inlet_c) max_target = inlet_c + 1.0f;
    out.target_temperature_c = max_target;
    out.reason = nullptr;
    return out;
  }
  return compute_operating_point(otb_max_capacity_w, inlet_c, max_c, flow_lph, cp_j_per_kgk, headroom_c);
}

inline bool has_sufficient_headroom(float rated_power_w, float inlet_c, float max_c, float flow_lph, float cp_j_per_kgk,
                                    float headroom_c = 5.0f) {
  return compute_operating_point(rated_power_w, inlet_c, max_c, flow_lph, cp_j_per_kgk, headroom_c).feasible;
}

}  // namespace oq_boiler_commissioning
