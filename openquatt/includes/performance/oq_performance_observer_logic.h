#pragma once

#include <cstring>

#include "oq_performance_observer_context.h"
#include "../experimental/oq_water_performance_grid.h"

namespace oq_performance_observer {
using oq_water_shadow::Snapshot;
using oq_water_shadow::Status;

struct Policy {
  std::array<std::array<uint8_t, 21>, 2> hz{};
  std::array<std::array<bool, 21>, 2> allowed{};
  std::array<oq_odu::Variant, 2> variant{};
  std::array<float, 4> offsets{};
  int cap_hz = -1;
  bool operator==(const Policy& other) const {
    return hz == other.hz && allowed == other.allowed && variant == other.variant && offsets == other.offsets &&
           cap_hz == other.cap_hz;
  }
};

// A stable window consists of distinct complete receipt groups. Observer ticks
// cannot turn repeated/held samples into fresh measurements or fill a window.
class Stability {
 public:
  Status observe(const Snapshot& frame, const Policy& policy, uint64_t eligible_after_ms) {
    const auto context = oq_water_shadow::predict_series(
        frame, {0, 0}, {}, [](unsigned, float, float, float) { return oq_water_shadow::ModelPoint{}; });
    if (!context.valid()) {
      reset();
      return context.status;
    }
    for (unsigned hp = 0; hp < 2; ++hp) {
      const float hz = frame.frequency[hp].value;
      if ((hz > 0) != frame.heating[hp]) {
        reset();
        return Status::TRANSIENT;
      }
    }
    uint64_t group_ms = frame.flow.received_ms;
    for (unsigned hp = 0; hp < 2; ++hp) {
      group_ms = std::min(
          {group_ms, frame.inlet[hp].received_ms, frame.outlet[hp].received_ms, frame.frequency[hp].received_ms});
    }
    if (!initialized_ || !(policy_ == policy) || changed(frame)) {
      anchor_ = frame;
      policy_ = policy;
      initialized_ = true;
      first_ms_ = last_ms_ = group_ms;
      samples_ = 1;
    } else if (group_ms > last_ms_) {
      last_ms_ = group_ms;
      ++samples_;
    } else if (group_ms < last_ms_) {
      reset();
      return Status::TIME_SKEW;
    }
    if (frame.now_ms < eligible_after_ms || samples_ < 6 || last_ms_ - first_ms_ < 60000) return Status::SETTLING;
    return Status::VALID;
  }
  void reset() {
    initialized_ = false;
    samples_ = 0;
    first_ms_ = last_ms_ = 0;
  }
  bool changed(const Snapshot& frame) const {
    if (!initialized_) return true;
    if (frame.online != anchor_.online || frame.heating != anchor_.heating || frame.transient != anchor_.transient ||
        std::fabs(frame.flow.value - anchor_.flow.value) > std::max(20.0f, .02f * anchor_.flow.value) ||
        std::fabs(frame.outside.value - anchor_.outside.value) > .25f)
      return true;
    for (unsigned hp = 0; hp < 2; ++hp)
      if (std::fabs(frame.inlet[hp].value - anchor_.inlet[hp].value) > .2f ||
          std::fabs(frame.outlet[hp].value - anchor_.outlet[hp].value) > .2f ||
          std::fabs(frame.frequency[hp].value - anchor_.frequency[hp].value) > .5f)
        return true;
    return false;
  }
  unsigned samples() const { return samples_; }

 private:
  Snapshot anchor_;
  Policy policy_;
  bool initialized_ = false;
  uint64_t first_ms_ = 0, last_ms_ = 0;
  unsigned samples_ = 0;
};

enum class Mode : uint8_t { NONE, POWER_HOUSE, CURVE };
// Exact canonical input comparison, not a probabilistic policy fingerprint.
// Padding and NaN payloads cannot change equality; clock progress is excluded.
struct ContextKey {
  std::array<uint32_t, 256> words{};
  unsigned count = 0;
  bool valid = true;
  void add(uint32_t value) {
    if (count >= words.size()) {
      valid = false;
      return;
    }
    words[count++] = value;
  }
  void real(float value) {
    uint32_t bits = UINT32_MAX;
    if (!std::isnan(value)) std::memcpy(&bits, &value, sizeof(bits));
    add(bits);
  }
  void hold(float value, int threshold) {
    if (!std::isfinite(value)) {
      valid = false;
      return;
    }
    // Beyond the hold threshold the selector only observes >=. Continued
    // accumulation must not cancel an otherwise unchanged long-running grid.
    real(std::min(value, static_cast<float>(std::max(1, threshold))));
  }
  void hp(const oq_hp_candidate::HpCandidateState& value) {
    add(value.previous_applied_level);
    add(value.available_for_start);
    add(value.must_stop);
    add(value.link_suspect);
    add(value.minimum_off_ready);
  }
  bool operator==(const ContextKey& other) const {
    return valid && other.valid && count == other.count && words == other.words;
  }
};
inline ContextKey key(const oq_power_house_dispatch::DispatchInput& in,
                      const oq_power_house_dispatch::DispatchTuning& tune,
                      const oq_power_house_dispatch::DispatchState& state, Reference ref) {
  ContextKey out;
  out.add(1);
  out.add(in.demand_level);
  out.real(in.requested_w);
  out.add(in.duo);
  out.add(in.performance_valid);
  out.add(in.lead_hp1);
  out.real(ref.outside_c);
  out.real(ref.water_c);
  for (const auto* hp : {&in.hp1, &in.hp2}) {
    out.hp(hp->candidate);
    out.add(hp->defrost);
    out.add(hp->valve_defrost);
    for (const auto& level : hp->levels) {
      out.add(level.allowed | (level.thermal_valid << 1) | (level.electrical_valid << 2));
      out.add(level.frequency_hz);
      out.real(level.thermal_w);
      out.real(level.electrical_w);
    }
  }
  out.real(tune.soft_limit_w);
  out.real(tune.peak_limit_w);
  out.real(tune.over_soft_penalty_per_w);
  out.real(tune.topology_power_margin_w);
  out.real(tune.topology_heat_advantage_w);
  out.add(tune.defrost_boost_min_level);
  out.add(tune.defrost_boost_steps);
  for (const auto* tail : {&state.hp1_defrost, &state.hp2_defrost, &state.oil_return}) {
    out.add(tail->previous_active);
    out.add(tail->armed);
    out.add(tail->started_ms);
  }
  out.add(state.topology_initialized);
  out.add(state.topology_hold_armed);
  out.add(state.applied_topology);
  out.add(state.topology_hold_started_ms);
  return out;
}
inline ContextKey key(const oq_curve_duo::Input& in, const oq_curve_duo::State& state, const Powers& power,
                      Reference ref) {
  ContextKey out;
  out.add(2);
  out.add(in.lead_last_start_ms);
  out.add(in.loop_ms);
  out.add(in.capped);
  out.add(in.previous_capped);
  out.add(in.previous_hp1);
  out.add(in.previous_hp2);
  out.add(in.single_owner);
  out.real(in.demand_u);
  out.real(in.dispatch_u);
  out.real(in.temperature_error_c);
  out.real(ref.outside_c);
  out.real(ref.water_c);
  out.add(in.demand_active);
  out.add(in.heat_phase);
  out.add(in.lead_hp1);
  out.hp(in.hp1);
  out.hp(in.hp2);
  out.add(in.hp1_model_available);
  out.add(in.hp2_model_available);
  out.add(in.hp1_valve_defrost);
  out.add(in.hp2_valve_defrost);
  out.add(in.dual_on_hold_min);
  out.add(in.dual_off_hold_min);
  out.add(in.tuning.dual_startup_grace_s);
  out.add(in.tuning.dual_emergency_hold_min);
  out.real(in.tuning.dual_emergency_temp_err_c);
  out.real(in.tuning.dual_disable_temp_err_max_c);
  out.add(state.dual_enabled);
  out.hold(state.on_min, in.dual_on_hold_min);
  out.hold(state.off_min, in.dual_off_hold_min);
  out.hold(state.emergency_min, in.tuning.dual_emergency_hold_min);
  for (unsigned hp = 0; hp < 2; ++hp)
    for (unsigned level = 0; level <= 20; ++level) {
      out.add(in.hz[hp][level]);
      out.real(power[hp][level]);
    }
  return out;
}
struct Context {
  Mode mode = Mode::NONE;
  uint64_t captured_ms = 0;
  float reference_outside_c = NAN, reference_water_c = NAN;
  oq_power_house_dispatch::DispatchInput ph;
  oq_power_house_dispatch::DispatchTuning ph_tuning;
  oq_power_house_dispatch::DispatchState ph_state;
  oq_power_house_dispatch::DispatchDecision ph_actual;
  oq_curve_duo::Input curve;
  oq_curve_duo::State curve_state;
  Powers curve_power;
  oq_curve_duo::Decision curve_actual;
};
struct Choice {
  int first = 0, second = 0;
  float thermal_w = NAN, electrical_w = NAN, target_w = NAN, capacity_w = NAN;
  int reason = 0;
};
struct Comparison {
  bool valid = false;
  Choice old, series;
  std::array<oq_water_shadow::UnitPrediction, 2> own;
  std::array<float, 2> shared_w{NAN, NAN};
  std::array<float, 2> predicted_outlet_c{NAN, NAN};
  uint32_t sequence = 0;
  uint64_t context_ms = 0;
};

class Job {
 public:
  Context context;
  Snapshot frame;
  Policy policy;
  Comparison result;
  Status status = Status::DISABLED;
  uint32_t sequence = 0;
  unsigned unavailable = 0;
  bool running = false;

  void invalidate(Status why) {
    running = false;
    grid_.cancel();
    result = {};
    status = why;
  }
  void begin(const Snapshot& snapshot, const Policy& next_policy) {
    invalidate(Status::SETTLING);
    frame = snapshot;
    policy = next_policy;
    // Fill in place: an aggregate assignment may need a 5 KiB stack temporary.
    for (auto& row : cache_)
      for (auto& point : row) point = oq_power_house_dispatch::PairPower{};
    oq_water_shadow::GridAxes axes;
    for (unsigned hp = 0; hp < 2; ++hp) {
      axes.count[hp] = 1;
      levels_[hp][0] = 0;
      for (int level = 1; level <= 20; ++level)
        if (policy.allowed[hp][level]) {
          const auto index = axes.count[hp]++;
          axes.hz[hp][index] = policy.hz[hp][level];
          levels_[hp][index] = level;
        }
    }
    grid_.begin(frame, axes);
    status = grid_.status;
    running = grid_.running;
    unavailable = 0;
    ++sequence;
  }
  void step(uint64_t now_ms) {
    if (!running) return;
    const auto model = [&](unsigned hp, float hz, float outside, float outlet) {
      const auto p = oq_perf::predict_heating_hz(policy.variant[hp], hz, outside, outlet);
      return oq_water_shadow::ModelPoint{p.pth_w, p.pel_w, p.available, p.low_supply_boundary_estimate};
    };
    const auto p = grid_.step(now_ms, model);
    if (grid_.status != Status::VALID) {
      invalidate(grid_.status);
      return;
    }
    const unsigned first = levels_[0][grid_.last_indices[0]], second = levels_[1][grid_.last_indices[1]];
    cache_[first][second] = {p.thermal_w, p.electrical_w, p.valid()};
    unavailable += !p.valid();
    if (!grid_.complete) return;
    // The matrix is complete before either winner is published. Selection uses
    // the same frozen request/holds and the shared production comparators.
    Comparison next;
    for (unsigned hp = 0; hp < 2; ++hp) {
      next.own[hp] = oq_water_shadow::predict_observed(frame, hp, {}, model);
      if (!next.own[hp].valid()) {
        invalidate(next.own[hp].status);
        return;
      }
      const auto old = model(hp, frame.frequency[hp].value, frame.outside.value, context.reference_water_c);
      if (oq_water_shadow::valid_model(old)) next.shared_w[hp] = old.thermal_w;
    }
    if (context.mode == Mode::POWER_HOUSE) {
      auto input = context.ph;
      // Series availability is determined by complete solved pairs, independent
      // of holes at the old shared reference temperature.
      input.performance_valid = true;
      const auto baseline = oq_power_house_dispatch::decide_dispatch(context.ph, context.ph_tuning, context.ph_state);
      if (baseline.hp1_level != context.ph_actual.hp1_level || baseline.hp2_level != context.ph_actual.hp2_level ||
          baseline.reason != context.ph_actual.reason) {
        invalidate(Status::BASELINE_MISMATCH);
        return;
      }
      const auto selected = oq_power_house_dispatch::decide_dispatch_using(
          input, context.ph_tuning, context.ph_state, [&](int first, int second, float target) {
            return oq_power_house_dispatch::make_candidate(input, context.ph_tuning, first, second, target,
                                                           &cache_[first][second]);
          });
      if (!baseline.output_valid || !selected.output_valid) {
        invalidate(Status::MODEL_UNAVAILABLE);
        return;
      }
      next.old = {baseline.hp1_level,
                  baseline.hp2_level,
                  baseline.expected_w,
                  NAN,
                  context.ph.requested_w,
                  baseline.capacity_w,
                  static_cast<int>(baseline.reason)};
      const auto old = oq_power_house_dispatch::make_candidate(context.ph, context.ph_tuning, baseline.hp1_level,
                                                               baseline.hp2_level, context.ph.requested_w);
      next.old.electrical_w = old.valid ? old.electrical_w : NAN;
      const auto chosen = cache_[selected.hp1_level][selected.hp2_level];
      if (!chosen.valid) {
        invalidate(Status::MODEL_UNAVAILABLE);
        return;
      }
      next.series = {selected.hp1_level,
                     selected.hp2_level,
                     selected.expected_w,
                     chosen.electrical_w,
                     context.ph.requested_w,
                     selected.capacity_w,
                     static_cast<int>(selected.reason)};
    } else if (context.mode == Mode::CURVE) {
      const auto baseline = oq_curve_duo::decide(context.curve, context.curve_state, [&](int first, int second) {
        const float p = context.curve_power[0][first] + context.curve_power[1][second];
        return oq_power_house_dispatch::PairPower{p, NAN, std::isfinite(p)};
      });
      if (baseline.hp1_level != context.curve_actual.hp1_level ||
          baseline.hp2_level != context.curve_actual.hp2_level ||
          baseline.capacity_mode != context.curve_actual.capacity_mode) {
        invalidate(Status::BASELINE_MISMATCH);
        return;
      }
      auto input = context.curve;
      input.hp1_model_available = input.hp2_model_available = false;
      for (unsigned first = 0; first <= 20; ++first)
        for (unsigned second = 0; second <= 20; ++second) {
          input.hp1_model_available |= first > 0 && cache_[first][second].valid;
          input.hp2_model_available |= second > 0 && cache_[first][second].valid;
        }
      const auto selected = oq_curve_duo::decide(
          input, context.curve_state, [&](int first, int second) { return cache_[first][second]; }, false);
      const auto chosen = cache_[selected.hp1_level][selected.hp2_level];
      if (!chosen.valid || !std::isfinite(baseline.expected_w)) {
        invalidate(Status::MODEL_UNAVAILABLE);
        return;
      }
      next.old = {baseline.hp1_level, baseline.hp2_level,  baseline.expected_w,   NAN,
                  baseline.target_w,  baseline.capacity_w, baseline.capacity_mode};
      const auto old1 = model(0, policy.hz[0][baseline.hp1_level], frame.outside.value, context.reference_water_c);
      const auto old2 = model(1, policy.hz[1][baseline.hp2_level], frame.outside.value, context.reference_water_c);
      next.old.electrical_w =
          (baseline.hp1_level > 0 ? old1.electrical_w : 0) + (baseline.hp2_level > 0 ? old2.electrical_w : 0);
      next.series = {selected.hp1_level, selected.hp2_level,  selected.expected_w,   chosen.electrical_w,
                     selected.target_w,  selected.capacity_w, selected.capacity_mode};
    } else {
      invalidate(Status::DISABLED);
      return;
    }
    const auto selected = oq_water_shadow::predict_series(
        frame,
        {static_cast<float>(policy.hz[0][next.series.first]), static_cast<float>(policy.hz[1][next.series.second])}, {},
        model);
    if (!selected.valid()) {
      invalidate(selected.status);
      return;
    }
    next.predicted_outlet_c = {selected.unit[0].outlet_c, selected.unit[1].outlet_c};
    next.sequence = sequence;
    next.context_ms = context.captured_ms;
    next.valid = true;
    result = next;
    status = Status::VALID;
    running = false;
  }

 private:
  std::array<std::array<oq_power_house_dispatch::PairPower, 21>, 21> cache_{};
  std::array<std::array<uint8_t, 21>, 2> levels_{};
  oq_water_shadow::GridJob grid_;
};

static_assert(sizeof(Job) + sizeof(Stability) <= 10000, "Observer state requires bounded external memory");
}  // namespace oq_performance_observer
