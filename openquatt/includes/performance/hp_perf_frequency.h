#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include "../odu/oq_odu_generation.h"
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

struct HeatingPrediction {
  float pth_w{NAN};
  float cop{NAN};
  float pel_w{NAN};
  bool available{false};
  bool low_supply_boundary_estimate{false};
};

struct CandidatePrediction {
  int runtime_frequency_hz{-1};
  bool runtime_frequency_known{false};
  bool frequency_policy_allowed{false};
  HeatingPrediction performance{};

  bool usable_for_running_optimization() const {
    return runtime_frequency_known && frequency_policy_allowed && performance.available &&
           std::isfinite(performance.pth_w) && performance.pth_w > 0.0f && std::isfinite(performance.pel_w) &&
           performance.pel_w >= 0.0f;
  }
};

inline HeatingPrediction predict_heating_hz(oq_odu::Variant variant, float frequency_hz, float Tamb, float Tsup,
                                            bool allow_low_supply_boundary_estimate = false) {
  if (variant == oq_odu::Variant::V2_OLD_MODEL || variant == oq_odu::Variant::V2_NEW_MODEL) {
    const auto prediction =
        oq_v2_model::predict_heating_for_control(frequency_hz, Tamb, Tsup, allow_low_supply_boundary_estimate);
    return {prediction.pth_w, prediction.cop, prediction.pel_w,
            prediction.status == oq_v2_model::Status::VALID || prediction.status == oq_v2_model::Status::OFF,
            prediction.low_supply_boundary_estimate};
  }
  if (variant != oq_odu::Variant::V1 && variant != oq_odu::Variant::V1_5) return {};
  const float pth_w = interp_frequency_axis(V1_HEATING_FREQUENCIES_HZ, frequency_hz, [=](int level) {
    return oq_perf_v1::interp_power_th_w(level, Tamb, Tsup);
  });
  const float cop = interp_frequency_axis(V1_HEATING_FREQUENCIES_HZ, frequency_hz,
                                          [=](int level) { return oq_perf_v1::interp_cop(level, Tamb, Tsup); });
  const float pel_w = interp_frequency_axis(V1_HEATING_FREQUENCIES_HZ, frequency_hz, [=](int level) {
    return oq_perf_v1::interp_power_el_w(level, Tamb, Tsup);
  });
  return {pth_w, cop, pel_w,
          std::isfinite(pth_w) && pth_w >= 0.0f && std::isfinite(cop) && cop >= 0.0f && std::isfinite(pel_w) &&
              pel_w >= 0.0f,
          false};
}

inline HeatingPrediction predict_heating_hz(float frequency_hz, float Tamb, float Tsup) {
  return predict_heating_hz(uses_v2_map() ? oq_odu::Variant::V2_OLD_MODEL : oq_odu::Variant::V1, frequency_hz, Tamb,
                            Tsup);
}

inline float interp_power_th_w_hz(float frequency_hz, float Tamb, float Tsup) {
  return predict_heating_hz(frequency_hz, Tamb, Tsup).pth_w;
}

inline float interp_cop_hz(float frequency_hz, float Tamb, float Tsup) {
  return predict_heating_hz(frequency_hz, Tamb, Tsup).cop;
}

inline float interp_power_el_w_hz(float frequency_hz, float Tamb, float Tsup, float cop_fallback = 3.0f) {
  if (uses_v2_map()) return predict_heating_hz(frequency_hz, Tamb, Tsup).pel_w;
  return interp_frequency_axis(V1_HEATING_FREQUENCIES_HZ, frequency_hz, [=](int level) {
    return oq_perf_v1::interp_power_el_w(level, Tamb, Tsup, cop_fallback);
  });
}

template <typename FrequencyContext>
inline CandidatePrediction predict_candidate(const FrequencyContext& frequency, oq_odu::Variant variant, bool hp1,
                                             int control_level, float Tamb, float Tsup,
                                             bool allow_low_supply_boundary_estimate = false) {
  CandidatePrediction result;
  if (variant == oq_odu::Variant::UNKNOWN) return result;
  if (control_level < 0 || control_level > 10) return result;
  if (control_level == 0) {
    result.runtime_frequency_hz = 0;
    result.runtime_frequency_known = true;
    result.frequency_policy_allowed = true;
    result.performance = predict_heating_hz(variant, 0.0f, Tamb, Tsup);
    return result;
  }
  const int frequency_hz = frequency.automatic_frequency_hz(hp1, 2, control_level);
  if (frequency_hz <= 0) return result;
  result.runtime_frequency_hz = frequency_hz;
  result.runtime_frequency_known = true;
  result.frequency_policy_allowed = frequency.frequency_allowed(hp1, 2, control_level);
  result.performance =
      predict_heating_hz(variant, static_cast<float>(frequency_hz), Tamb, Tsup, allow_low_supply_boundary_estimate);
  return result;
}

}  // namespace oq_perf
