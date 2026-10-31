#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include "hp_perf_frequency.h"

// Water-side performance model; fixed flow order HP1 -> HP2.
namespace oq_water_performance {
struct Prediction {
  float thermal_w = NAN, electrical_w = NAN, outlet_c = NAN;
  bool valid() const {
    return std::isfinite(thermal_w) && thermal_w >= 0 && std::isfinite(electrical_w) && electrical_w >= 0 &&
           std::isfinite(outlet_c);
  }
};
inline Prediction observed(oq_odu::Variant variant, float hz, float outside_c, float outlet_c, bool boundary = false) {
  if (!std::isfinite(hz) || hz < 0 || hz > 90 || !std::isfinite(outside_c) || !std::isfinite(outlet_c)) return {};
  if (hz == 0) return {0, 0, outlet_c};
  const auto p = oq_perf::predict_heating_hz(variant, hz, outside_c, outlet_c, boundary);
  return p.available ? Prediction{p.pth_w, p.pel_w, outlet_c} : Prediction{};
}

// Solve Q = map(hz, outside, Tin + Q/K). Unsupported map points stay unavailable.
inline Prediction solve(oq_odu::Variant variant, float hz, float outside_c, float inlet_c, float flow_lph,
                        bool boundary = false) {
  if (!std::isfinite(flow_lph) || flow_lph < 300 || flow_lph > 3000) return {};
  if (hz == 0) return observed(variant, hz, outside_c, inlet_c, boundary);
  const bool v2 = variant == oq_odu::Variant::V2_OLD_MODEL || variant == oq_odu::Variant::V2_NEW_MODEL;
  float low = v2 && !boundary ? std::max(inlet_c, static_cast<float>(oq_v2_model::data::SUPPLY_C[0])) : inlet_c;
  const auto initial = observed(variant, hz, outside_c, low, boundary);
  if (!initial.valid()) return {};
  const float capacity = flow_lph * 4186.0f / 3600.0f;
  float low_residual = capacity * (low - inlet_c) - initial.thermal_w;
  if (std::fabs(low_residual) <= 1) return initial;
  if (low_residual > 0) return {};  // The root would lie below the supported outlet domain.
  const float maximum_outlet =
      v2 ? static_cast<float>(oq_v2_model::data::SUPPLY_C[oq_v2_model::data::N_SUPPLY - 1]) : INFINITY;
  const auto upper_prediction = [&](float target) {
    auto result = observed(variant, hz, outside_c, target, boundary);
    if (result.valid() || !v2) return result;
    // A first guess may cross a frequency-specific water boundary while the
    // energy-balance root still lies below it. Try the valid side of each edge.
    for (size_t index = oq_v2_model::data::N_SUPPLY; index > 0; --index) {
      const float edge = std::nextafter(static_cast<float>(oq_v2_model::data::SUPPLY_C[index - 1]), -INFINITY);
      if (edge <= low || edge >= target) continue;
      result = observed(variant, hz, outside_c, edge, boundary);
      if (result.valid()) return result;
    }
    return Prediction{};
  };
  float high = std::min(maximum_outlet, inlet_c + initial.thermal_w / capacity);
  auto p = upper_prediction(high);
  if (!p.valid()) return {};
  high = p.outlet_c;
  float high_residual = capacity * (high - inlet_c) - p.thermal_w;
  // V1 capacity can rise with Tout; extend the bracket within supported map points.
  for (unsigned step = 0; high_residual < -1.0f && step < 4; ++step) {
    high = std::min(maximum_outlet, inlet_c + 2 * (high - inlet_c));
    p = upper_prediction(high);
    if (!p.valid()) return {};
    high = p.outlet_c;
    high_residual = capacity * (high - inlet_c) - p.thermal_w;
  }
  if (high_residual < -1.0f) return {};
  for (unsigned step = 0; step < 8; ++step) {
    const float residual = capacity * (p.outlet_c - inlet_c) - p.thermal_w;
    if (std::fabs(residual) <= 1.0f) return p;
    if (residual > 0)
      high = p.outlet_c, high_residual = residual;
    else
      low = p.outlet_c, low_residual = residual;
    p = observed(variant, hz, outside_c, low - low_residual * (high - low) / (high_residual - low_residual), boundary);
    if (!p.valid()) return {};
  }
  return {};
}
inline std::array<Prediction, 2> series(const std::array<oq_odu::Variant, 2>& variants, const std::array<float, 2>& hz,
                                        float outside_c, float inlet_c, float flow_lph) {
  const auto first = solve(variants[0], hz[0], outside_c, inlet_c, flow_lph);
  return {first, first.valid() ? solve(variants[1], hz[1], outside_c, first.outlet_c, flow_lph) : Prediction{}};
}
// One control-tick snapshot. Only HP1 rows are reused; no pair matrix or heap allocation.
struct CandidateModel {
  std::array<oq_odu::Variant, 2> variants;
  std::array<float, 2> outlet;
  float outside_c, inlet_c, flow_lph;
  bool coupled, boundary;
  std::array<std::array<int, 21>, 2> hz{};
  std::array<Prediction, 21> upstream{};
  template <typename Frequency>
  CandidateModel(const Frequency& frequency, float outside, float inlet, float flow, std::array<float, 2> own_outlet,
                 bool use_series, bool cold_start)
      : variants{frequency.performance_variant(true), frequency.performance_variant(false)},
        outlet(own_outlet),
        outside_c(outside),
        inlet_c(inlet),
        flow_lph(flow),
        coupled(use_series),
        boundary(cold_start) {
    for (int level = 0; level <= 20; ++level) {
      for (unsigned hp = 0; hp < 2; ++hp)
        hz[hp][level] = level == 0 ? 0 : frequency.automatic_frequency_hz(hp == 0, 2, level);
      if (coupled) upstream[level] = solve(variants[0], hz[0][level], outside_c, inlet_c, flow_lph, boundary);
    }
  }
  Prediction unit(bool hp1, int level) const {
    if (level < 0 || level > 20) return {};
    if (coupled)
      return hp1 ? upstream[level] : solve(variants[1], hz[1][level], outside_c, inlet_c, flow_lph, boundary);
    const unsigned hp = hp1 ? 0 : 1;
    return observed(variants[hp], hz[hp][level], outside_c, outlet[hp], boundary);
  }
  Prediction pair(int first, int second) const {
    if (first < 0 || first > 20 || second < 0 || second > 20) return {};
    const auto a = unit(true, first);
    const auto b = coupled && a.valid() ? solve(variants[1], hz[1][second], outside_c, a.outlet_c, flow_lph, boundary)
                                        : unit(false, second);
    return a.valid() && b.valid() ? Prediction{a.thermal_w + b.thermal_w, a.electrical_w + b.electrical_w, b.outlet_c}
                                  : Prediction{};
  }
  static std::array<float, 2> dispatch(const void* context, int first, int second) {
    const auto p = static_cast<const CandidateModel*>(context)->pair(first, second);
    return {p.thermal_w, p.electrical_w};
  }
};
}  // namespace oq_water_performance
