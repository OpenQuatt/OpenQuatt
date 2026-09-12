#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "detail/hp_perf_map_v2_data.h"

// Pure V2 heating-performance model reconstructed from the CiC 4.2.0
// controller ELF. This module performs no I/O, allocation, or control action.
namespace oq_v2_model {

enum class Status : uint8_t {
  VALID = 0,
  OFF = 1,
  INVALID_INPUT = 2,
  OUTSIDE_DOMAIN = 3,
  MASKED = 4,
};

inline constexpr float UNAVAILABLE = std::numeric_limits<float>::quiet_NaN();

struct Prediction {
  float pth_w{UNAVAILABLE};
  float cop{UNAVAILABLE};
  float pel_w{UNAVAILABLE};
  Status status{Status::INVALID_INPUT};
  bool low_supply_boundary_estimate{false};

  bool valid_running_point() const { return status == Status::VALID; }
};

struct Blend {
  size_t lo;
  size_t hi;
  double weight;
};

template <size_t N>
inline Blend bracket(const double (&axis)[N], double value) {
  // Exact nodes do not depend on a masked neighbour.
  for (size_t i = 0; i < N; ++i) {
    if (value == axis[i]) return {i, i, 0.0};
    if (value < axis[i]) return {i - 1, i, (value - axis[i - 1]) / (axis[i] - axis[i - 1])};
  }
  return {N - 1, N - 1, 0.0};
}

inline double reference_frequency_hz(int level) {
  if (level <= 0) return 0.0;
  return data::FREQUENCY_HZ[level > 10 ? 9 : level - 1];
}

inline Prediction predict_heating(double frequency_hz, double ambient_c, double supply_c) {
  if (!std::isfinite(frequency_hz) || frequency_hz < 0.0) return {};
  // OFF is an incremental optimizer point, not whole-ODU standby power.
  if (frequency_hz == 0.0) return {0.0f, UNAVAILABLE, 0.0f, Status::OFF, false};
  if (!std::isfinite(ambient_c) || !std::isfinite(supply_c)) return {};
  if (frequency_hz < data::FREQUENCY_HZ[0] || frequency_hz > data::FREQUENCY_HZ[9] || ambient_c < data::AMBIENT_C[0] ||
      ambient_c > data::AMBIENT_C[16] || supply_c < data::SUPPLY_C[0] || supply_c > data::SUPPLY_C[7]) {
    return {UNAVAILABLE, UNAVAILABLE, UNAVAILABLE, Status::OUTSIDE_DOMAIN, false};
  }

  const Blend supply = bracket(data::SUPPLY_C, supply_c);
  const Blend ambient = bracket(data::AMBIENT_C, ambient_c);
  const Blend frequency = bracket(data::FREQUENCY_HZ, frequency_hz);
  double pth_w = 0.0;
  double cop = 0.0;
  for (size_t supply_side = 0; supply_side < 2; ++supply_side) {
    const double supply_weight = supply_side ? supply.weight : 1.0 - supply.weight;
    if (supply_weight == 0.0) continue;
    for (size_t ambient_side = 0; ambient_side < 2; ++ambient_side) {
      const double ambient_weight = ambient_side ? ambient.weight : 1.0 - ambient.weight;
      if (ambient_weight == 0.0) continue;
      for (size_t frequency_side = 0; frequency_side < 2; ++frequency_side) {
        const double frequency_weight = frequency_side ? frequency.weight : 1.0 - frequency.weight;
        if (frequency_weight == 0.0) continue;
        const size_t supply_index = supply_side ? supply.hi : supply.lo;
        const size_t ambient_index = ambient_side ? ambient.hi : ambient.lo;
        const size_t frequency_index = frequency_side ? frequency.hi : frequency.lo;
        const float cell_pth_w = data::PTH_W[supply_index][ambient_index][frequency_index];
        const float cell_cop = data::COP[supply_index][ambient_index][frequency_index];
        if (!std::isfinite(cell_pth_w) || !std::isfinite(cell_cop) || cell_pth_w <= 0.0f || cell_cop <= 0.0f) {
          return {UNAVAILABLE, UNAVAILABLE, UNAVAILABLE, Status::MASKED, false};
        }
        const double weight = supply_weight * ambient_weight * frequency_weight;
        pth_w += weight * cell_pth_w;
        cop += weight * cell_cop;
      }
    }
  }

  return {static_cast<float>(pth_w), static_cast<float>(cop), static_cast<float>(pth_w / cop), Status::VALID, false};
}

inline Prediction predict_heating_for_control(double frequency_hz, double ambient_c, double supply_c,
                                              bool existing_startup_guards_allow_boundary_estimate = false) {
  // This optional adapter cannot authorize a compressor start. The caller
  // remains responsible for the existing hydraulic, frost, and current gates.
  const bool clamp_supply = existing_startup_guards_allow_boundary_estimate && std::isfinite(supply_c) &&
                            supply_c < data::SUPPLY_C[0] && frequency_hz > 0.0;
  auto result = predict_heating(frequency_hz, ambient_c, clamp_supply ? data::SUPPLY_C[0] : supply_c);
  result.low_supply_boundary_estimate = clamp_supply;
  return result;
}

}  // namespace oq_v2_model
