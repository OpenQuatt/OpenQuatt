#pragma once

#include <array>
#include <cmath>
#include "hp_perf_frequency.h"

// Observation only. Same maps and domains as control; fixed water order HP1 -> HP2.
namespace oq_water_shadow {
struct Prediction {
  float thermal_w = NAN, electrical_w = NAN, outlet_c = NAN;
  bool valid() const {
    return std::isfinite(thermal_w) && thermal_w >= 0 && std::isfinite(electrical_w) && electrical_w >= 0 &&
           std::isfinite(outlet_c);
  }
};
inline Prediction observed(oq_odu::Variant variant, float hz, float outside_c, float outlet_c) {
  if (!std::isfinite(hz) || hz < 0 || hz > 90 || !std::isfinite(outside_c) || !std::isfinite(outlet_c)) return {};
  const auto p = oq_perf::predict_heating_hz(variant, hz, outside_c, outlet_c);
  return p.available ? Prediction{p.pth_w, p.pel_w, outlet_c} : Prediction{};
}

// Solve Q = map(hz, outside, Tin + Q/K). Unsupported map points stay unavailable.
inline Prediction solve(oq_odu::Variant variant, float hz, float outside_c, float inlet_c, float flow_lph) {
  if (!std::isfinite(flow_lph) || flow_lph < 300 || flow_lph > 3000) return {};
  const auto initial = observed(variant, hz, outside_c, inlet_c);
  if (!initial.valid() || hz == 0) return initial;
  const float capacity = flow_lph * 4186.0f / 3600.0f;
  float low = inlet_c, high = inlet_c + initial.thermal_w / capacity;
  auto p = observed(variant, hz, outside_c, high);
  if (!p.valid() || capacity * (high - inlet_c) - p.thermal_w < -1.0f) return {};
  for (unsigned step = 0; step < 16; ++step) {
    const float residual = capacity * (p.outlet_c - inlet_c) - p.thermal_w;
    if (std::fabs(residual) <= 1.0f) return p;
    if (residual > 0)
      high = p.outlet_c;
    else
      low = p.outlet_c;
    p = observed(variant, hz, outside_c, low + .5f * (high - low));
    if (!p.valid()) return {};
  }
  return {};
}
inline std::array<Prediction, 2> series(const std::array<oq_odu::Variant, 2>& variants, const std::array<float, 2>& hz,
                                        float outside_c, float inlet_c, float flow_lph) {
  const auto first = solve(variants[0], hz[0], outside_c, inlet_c, flow_lph);
  return {first, first.valid() ? solve(variants[1], hz[1], outside_c, first.outlet_c, flow_lph) : Prediction{}};
}
}  // namespace oq_water_shadow
