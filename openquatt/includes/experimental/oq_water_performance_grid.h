#pragma once

#include "oq_water_performance_probe.h"

namespace oq_water_shadow {

struct GridAxes {
  std::array<std::array<uint8_t, 21>, 2> hz{};
  std::array<uint8_t, 2> count{};

  bool valid() const {
    for (unsigned hp = 0; hp < 2; ++hp) {
      if (count[hp] == 0 || count[hp] > hz[hp].size() || hz[hp][0] != 0) return false;
      for (unsigned item = 1; item < count[hp]; ++item)
        if (hz[hp][item] == 0 || !valid_frequency(hz[hp][item])) return false;
    }
    return true;
  }
};

// One immutable receipt set per job, one pair per step, no matrix or allocation.
// The upstream solution is independent of downstream Hz and is cached for its
// complete inner loop, including unavailable results. A new job clears it.
class GridJob {
 public:
  void begin(const Snapshot& snapshot, const GridAxes& axes, const Limits& limits = {}) {
    *this = {};
    frame_ = snapshot;
    axes_ = axes;
    limits_ = limits;
    if (!axes.valid()) {
      status = Status::INVALID_INPUT;
      return;
    }
    const auto context =
        predict_series(frame_, {0, 0}, limits_, [](unsigned, float, float, float) { return ModelPoint{}; });
    if (!context.valid()) {
      status = context.status;
      return;
    }
    status = Status::VALID;
    running = true;
  }

  void cancel() {
    running = false;
    complete = false;
    status = Status::DISABLED;
  }

  template <typename Model>
  PairPrediction step(uint64_t now_ms, Model model) {
    PairPrediction result;
    if (!running) return result;
    frame_.now_ms = now_ms;
    const unsigned first = frame_.hp1_upstream ? 0U : 1U, second = 1U - first;
    std::array<float, 2> candidates{};
    candidates[first] = axes_.hz[first][first_index_];
    candidates[second] = axes_.hz[second][second_index_];
    last_hz = {static_cast<uint8_t>(candidates[0]), static_cast<uint8_t>(candidates[1])};
    last_indices[first] = first_index_;
    last_indices[second] = second_index_;
    const auto cached_solver = [&](unsigned hp, float hz, float outside, float inlet, float capacity,
                                   const Limits& bounds, auto evaluate) {
      if (hp == first && cached_hz_ == hz) return upstream_;
      ++unit_solves;
      const auto point = solve_unit(hp, hz, outside, inlet, capacity, bounds, evaluate);
      if (hp == first) {
        cached_hz_ = hz;
        upstream_ = point;
      }
      return point;
    };
    result = predict_series_using(frame_, candidates, limits_, model, cached_solver);
    // Receipt/context expiration invalidates the whole unfinished job.
    if (result.status == Status::STALE) {
      running = false;
      complete = false;
      status = result.status;
      return result;
    }
    ++pairs;
    valid_pairs += result.valid();
    if (++second_index_ >= axes_.count[second]) {
      second_index_ = 0;
      if (++first_index_ >= axes_.count[first]) {
        running = false;
        complete = true;
      }
    }
    return result;
  }

  bool running = false, complete = false;
  Status status = Status::DISABLED;
  unsigned pairs = 0, valid_pairs = 0, unit_solves = 0;
  std::array<uint8_t, 2> last_hz{};
  std::array<uint8_t, 2> last_indices{};

 private:
  Snapshot frame_;
  GridAxes axes_;
  Limits limits_;
  UnitPrediction upstream_;
  float cached_hz_ = NAN;
  uint8_t first_index_ = 0, second_index_ = 0;
};

static_assert(sizeof(GridJob) <= 512, "Grid observer must have bounded small state");

struct GridProbeState {
  GridJob job;
  std::array<oq_odu::Variant, 2> variants{};
  uint64_t started_us = 0;
  uint32_t work_us = 0, maximum_step_us = 0, elapsed_us = 0;
  uint32_t frame_sequence = 0;
  uint32_t policy_fingerprint = 0;
  bool hp1_upstream = true;
};

template <typename FrequencyContext>
inline uint32_t grid_policy_fingerprint(const FrequencyContext& context) {
  uint32_t hash = 2166136261U;
  const auto mix = [&](int value) { hash = (hash ^ static_cast<uint32_t>(value)) * 16777619U; };
  mix(context.cap_hz);
  for (const bool hp1 : {true, false}) {
    mix(static_cast<int>(context.performance_variant(hp1)));
    const int maximum = context.automatic_level_limit(hp1, 2);
    mix(maximum);
    for (int level = 1; level <= maximum; ++level) {
      mix(context.automatic_frequency_hz(hp1, 2, level));
      mix(context.frequency_allowed(hp1, 2, level));
    }
  }
  return hash;
}

static_assert(sizeof(ProbeState) + sizeof(GridProbeState) <= 1024, "Observers must keep small bounded state");

inline GridProbeState& grid_probe_state() {
  static GridProbeState state;
  return state;
}

}  // namespace oq_water_shadow
