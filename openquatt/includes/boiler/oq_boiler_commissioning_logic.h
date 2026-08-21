#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_boiler_commissioning {

struct OperatingPoint {
  bool feasible = false;
  bool flow_limited = false;
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

  const float max_target = max_c - headroom_c;
  const float available_headroom_c = max_target - inlet_c;
  if (available_headroom_c <= 0.0f) {
    out.reason = "insufficient thermal headroom for boiler power test";
    return out;
  }

  const float required_flow_kgps = rated_power_w / (cp_j_per_kgk * available_headroom_c);
  out.required_flow_lph = required_flow_kgps * 3600.0f;
  out.target_temperature_c = max_target;
  out.feasible = true;
  out.reason = nullptr;
  return out;
}

inline OperatingPoint compute_opentherm_operating_point(bool opentherm_selected, float otb_max_capacity_w,
                                                        float rated_power_w, float inlet_c, float max_c, float flow_lph,
                                                        float cp_j_per_kgk = 4180.0f, float headroom_c = 5.0f) {
  const float max_target = max_c - headroom_c;

  if (!opentherm_selected) {
    // R1: keep the configured flow and do not apply OT capacity-based flow selection.
    (void)otb_max_capacity_w;
    (void)rated_power_w;
    (void)cp_j_per_kgk;
    OperatingPoint out{};
    out.headroom_c = headroom_c;
    if (isnan(inlet_c) || isnan(max_c) || isnan(flow_lph) || flow_lph <= 0.0f) {
      out.reason = "invalid input";
      return out;
    }
    out.feasible = true;
    out.required_flow_lph = flow_lph;
    out.target_temperature_c = max_target;
    out.reason = nullptr;
    return out;
  }

  // Thermal headroom is required even when optional OT Data ID 15 is unavailable.
  if (isnan(inlet_c) || isnan(max_c) || isnan(flow_lph) || flow_lph <= 0.0f) {
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
    // ID15 is optional. Without it, keep the supplied base flow (normally 800 L/h)
    // and let the normal thermal guards plus plateau quality determine the outcome.
    (void)rated_power_w;
    (void)cp_j_per_kgk;
    OperatingPoint out{};
    out.feasible = true;
    out.headroom_c = headroom_c;
    out.required_flow_lph = flow_lph;
    out.target_temperature_c = max_target;
    out.reason = nullptr;
    return out;
  }

  auto op = compute_operating_point(otb_max_capacity_w, inlet_c, max_c, flow_lph, cp_j_per_kgk, headroom_c);
  if (!op.feasible) return op;

  // OpenQuatt installations should not be forced beyond the practical commissioning range.
  // A higher theoretical requirement is a valid but flow-limited test, not an infeasible one.
  if (op.required_flow_lph > 1000.0f) {
    op.flow_limited = true;
    op.required_flow_lph = 1000.0f;
  }
  return op;
}

inline bool has_sufficient_headroom(float rated_power_w, float inlet_c, float max_c, float flow_lph, float cp_j_per_kgk,
                                    float headroom_c = 5.0f) {
  return compute_operating_point(rated_power_w, inlet_c, max_c, flow_lph, cp_j_per_kgk, headroom_c).feasible;
}

}  // namespace oq_boiler_commissioning
