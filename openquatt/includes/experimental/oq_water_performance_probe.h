#pragma once

#include "../performance/hp_perf_frequency.h"
#include "../performance/oq_water_performance_shadow.h"

namespace oq_water_shadow {

struct ProbeResult {
  std::array<UnitPrediction, 2> observed;
  std::array<float, 2> shared_thermal_w{NAN, NAN};
  PairPrediction series;
  Movement transition;
};

// HIL/experimental adapter. The caller supplies one immutable main-loop
// snapshot and the shared-water reference used for the baseline comparison.
inline ProbeResult probe(const Snapshot& snapshot, const std::array<float, 2>& candidate_hz, float shared_water_c,
                         const std::array<oq_odu::Variant, 2>& variants, const Limits& limits = {}) {
  ProbeResult result;
  const auto model = [&](unsigned hp, float hz, float outside, float outlet) {
    const auto prediction = oq_perf::predict_heating_hz(variants[hp], hz, outside, outlet);
    return ModelPoint{prediction.pth_w, prediction.pel_w, prediction.available,
                      prediction.low_supply_boundary_estimate};
  };
  for (unsigned hp = 0; hp < 2; ++hp) {
    result.observed[hp] = predict_observed(snapshot, hp, limits, model);
    if (result.observed[hp].valid() && std::isfinite(shared_water_c)) {
      const auto shared = model(hp, snapshot.frequency[hp].value, snapshot.outside.value, shared_water_c);
      if (valid_model(shared)) result.shared_thermal_w[hp] = shared.thermal_w;
    }
  }
  result.series = predict_series(snapshot, candidate_hz, limits, model);
  if (result.observed[0].valid() && result.observed[1].valid())
    result.transition = movement({snapshot.frequency[0].value, snapshot.frequency[1].value}, candidate_hz);
  return result;
}

struct ProbeState {
  Snapshot captured;
  ProbeResult result;
  uint32_t sequence = 0;
  uint32_t calculation_us = 0;
  uint32_t loop_stack_free_bytes = 0;
  uint32_t grid_us = 0;
  unsigned grid_pairs = 0;
  unsigned grid_valid_pairs = 0;
  bool captured_once = false;
};

static_assert(sizeof(ProbeState) <= 512, "Water-performance probe state must remain small and bounded");

// Only the HIL overlay calls this accessor. All its readers/writers run on
// the ESPHome main loop; production entrypoints do not instantiate this state.
inline ProbeState& probe_state() {
  static ProbeState state;
  return state;
}

}  // namespace oq_water_shadow
