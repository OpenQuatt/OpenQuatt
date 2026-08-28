#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include "hp_perf_map.h"

namespace oq_perf {

// Performance-map axes are compressor frequencies, not physical ODU F-levels.
// V1 and V1.5 use the same original Quatt reference axis. Runtime-modified
// EEPROM tables must never change these measured performance anchors.
inline constexpr std::array<float, 10> V1_HEATING_FREQUENCIES_HZ = {
    30.0f, 39.0f, 49.0f, 55.0f, 61.0f, 67.0f, 72.0f, 79.0f, 85.0f, 90.0f,
};
inline constexpr std::array<float, 10> V2_HEATING_FREQUENCIES_HZ = {
    20.0f, 26.0f, 30.0f, 48.0f, 55.0f, 61.0f, 72.0f, 80.0f, 85.0f, 90.0f,
};

template <size_t N, typename LevelEvaluator>
inline float interp_frequency_axis(const std::array<float, N>& frequencies_hz, float frequency_hz,
                                   LevelEvaluator evaluate_level) {
  static_assert(N > 0U, "performance frequency axis must not be empty");
  if (frequency_hz <= 0.0f) return 0.0f;
  if (frequency_hz < frequencies_hz.front() || frequency_hz > frequencies_hz.back()) return NAN;

  for (size_t index = 0; index < frequencies_hz.size(); ++index) {
    if (frequency_hz == frequencies_hz[index]) return evaluate_level(static_cast<int>(index + 1U));
    if (index == 0U || frequency_hz > frequencies_hz[index]) continue;

    const size_t lower_index = index - 1U;
    const float lower_value = evaluate_level(static_cast<int>(lower_index + 1U));
    const float upper_value = evaluate_level(static_cast<int>(index + 1U));
    if (std::isnan(lower_value) || std::isnan(upper_value)) return NAN;

    const float span_hz = frequencies_hz[index] - frequencies_hz[lower_index];
    const float weight = span_hz <= 0.0f ? 0.0f : (frequency_hz - frequencies_hz[lower_index]) / span_hz;
    return lower_value + (upper_value - lower_value) * weight;
  }
  return NAN;
}

inline float model_frequency_hz(bool use_v2_map, int level) {
  if (level <= 0) return 0.0f;
  const size_t index = static_cast<size_t>(level > 10 ? 9 : level - 1);
  return use_v2_map ? V2_HEATING_FREQUENCIES_HZ[index] : V1_HEATING_FREQUENCIES_HZ[index];
}

inline float model_frequency_hz(int level) { return model_frequency_hz(uses_v2_map(), level); }

inline float interp_power_th_w_hz(float frequency_hz, float Tamb, float Tsup) {
  if (uses_v2_map()) {
    return interp_frequency_axis(V2_HEATING_FREQUENCIES_HZ, frequency_hz,
                                 [=](int level) { return interp_power_th_w(level, Tamb, Tsup); });
  }
  return interp_frequency_axis(V1_HEATING_FREQUENCIES_HZ, frequency_hz,
                               [=](int level) { return interp_power_th_w(level, Tamb, Tsup); });
}

inline float interp_cop_hz(float frequency_hz, float Tamb, float Tsup) {
  if (uses_v2_map()) {
    return interp_frequency_axis(V2_HEATING_FREQUENCIES_HZ, frequency_hz,
                                 [=](int level) { return interp_cop(level, Tamb, Tsup); });
  }
  return interp_frequency_axis(V1_HEATING_FREQUENCIES_HZ, frequency_hz,
                               [=](int level) { return interp_cop(level, Tamb, Tsup); });
}

inline float interp_power_el_w_hz(float frequency_hz, float Tamb, float Tsup, float cop_fallback = 3.0f) {
  if (uses_v2_map()) {
    return interp_frequency_axis(V2_HEATING_FREQUENCIES_HZ, frequency_hz,
                                 [=](int level) { return interp_power_el_w(level, Tamb, Tsup, cop_fallback); });
  }
  return interp_frequency_axis(V1_HEATING_FREQUENCIES_HZ, frequency_hz,
                               [=](int level) { return interp_power_el_w(level, Tamb, Tsup, cop_fallback); });
}

}  // namespace oq_perf
