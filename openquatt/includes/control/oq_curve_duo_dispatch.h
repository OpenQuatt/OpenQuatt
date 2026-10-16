#pragma once

#include "oq_heating_curve_logic.h"
#include "oq_heating_curve_duo_floor_logic.h"
#include "oq_hp_candidate_logic.h"
#include "oq_power_house_dispatch_logic.h"

namespace oq_curve_duo {
struct Input {
  uint32_t now_ms = 0, lead_last_start_ms = 0, loop_ms = 0;
  int capped = 0, previous_capped = 0, previous_hp1 = 0, previous_hp2 = 0, single_owner = 0;
  float demand_u = 0, dispatch_u = 0, temperature_error_c = NAN;
  bool demand_active = false, heat_phase = false, lead_hp1 = true;
  oq_hp_candidate::HpCandidateState hp1, hp2;
  bool hp1_model_available = false, hp2_model_available = false;
  bool hp1_valve_defrost = false, hp2_valve_defrost = false;
  int dual_on_hold_min = 1, dual_off_hold_min = 1;
  oq_curve::ControlProfileTuning tuning;
  std::array<std::array<uint8_t, 21>, 2> hz{};
};
struct State {
  bool dual_enabled = false;
  float on_min = 0, off_min = 0, emergency_min = 0;
};
struct Decision {
  int hp1_level = 0, hp2_level = 0, owner = 0, capacity_mode = 0, single_owner = 0;
  bool single_branch = false, regular_single_stop = false, duo_valid = false;
  float target_w = 0, capacity_w = 0, expected_w = NAN, electrical_w = NAN;
  State next;
};

// Pure shared selection, including topology holds. The production caller uses
// its existing independent estimates; the observer supplies frozen series pairs.
// Neither this function nor its pair provider can write an actuator or a global.
template <typename Power>
inline Decision decide(const Input& in, State state, Power power, bool independent_bounds = true) {
  if (in.previous_hp1 < 0 || in.previous_hp1 > 20 || in.previous_hp2 < 0 || in.previous_hp2 > 20) return {};
  const bool hp1_available = oq_hp_candidate::may_serve_candidate(in.hp1);
  const bool hp2_available = oq_hp_candidate::may_serve_candidate(in.hp2);
  const auto pair = [&](int first, int second) {
    if ((first > 0 && !hp1_available) || (second > 0 && !hp2_available)) return oq_power_house_dispatch::PairPower{};
    return power(first, second);
  };
  const auto level_power = [&](bool hp1, int level) {
    const auto p = pair(hp1 ? level : 0, hp1 ? 0 : level);
    return p.valid ? p.thermal_w : NAN;
  };
  const auto maximum_level = [&](bool hp1) {
    if (!(hp1 ? hp1_available : hp2_available)) return 0;
    for (int level = 20; level >= 1; --level) {
      const float p = level_power(hp1, level);
      if (std::isfinite(p) && p > 0) return level;
    }
    return 0;
  };
  const auto candidate = [&](int first, int second, float target) {
    const auto p = pair(first, second);
    if (!p.valid || !std::isfinite(p.thermal_w)) return oq_curve::invalid_dispatch_candidate();
    return oq_curve::DispatchCandidate{
        true,
        first,
        second,
        p.thermal_w,
        std::fabs(p.thermal_w - target),
        (first > 0) + (second > 0),
        std::abs(static_cast<int>(in.hz[0][first]) - in.hz[1][second]),
        oq_compressor_movement::modulation_hz(
            {oq_compressor_movement::frequency(in.previous_hp1, in.hz[0][in.previous_hp1]),
             oq_compressor_movement::frequency(in.previous_hp2, in.hz[1][in.previous_hp2])},
            {oq_compressor_movement::frequency(first, in.hz[0][first]),
             oq_compressor_movement::frequency(second, in.hz[1][second])})};
  };
  Decision out;
  out.single_owner = in.single_owner;
  int single_owner = oq_curve::pick_single_owner(in.demand_active, in.single_owner, in.previous_hp1 > 0,
                                                 in.previous_hp2 > 0, in.lead_hp1);
  if (single_owner == 1 && !hp1_available)
    single_owner = hp2_available ? 2 : 0;
  else if (single_owner == 2 && !hp2_available)
    single_owner = hp1_available ? 1 : 0;
  const int hp1_max = maximum_level(true), hp2_max = maximum_level(false);
  const int owner_max = single_owner == 2 ? hp2_max : hp1_max;
  const float owner_capacity_w = level_power(single_owner != 2, owner_max);
  float duo_capacity_w = std::isfinite(owner_capacity_w) ? owner_capacity_w : 0;
  float duo_floor_w = NAN;
  // A series pair can be valid although one standalone point is not. Baseline
  // providers reject those same holes, preserving the original iteration order.
  const int first_max = independent_bounds ? hp1_max : 20;
  const int second_max = independent_bounds ? hp2_max : 20;
  for (int first = 1; first <= first_max; ++first)
    for (int second = 1; second <= second_max; ++second) {
      const auto item = candidate(first, second, 0);
      if (!item.valid) continue;
      duo_capacity_w = std::max(duo_capacity_w, item.power_w);
      duo_floor_w = std::fmin(duo_floor_w, item.power_w);
    }
  out.capacity_w = duo_capacity_w;
  out.target_w = in.demand_active
                     ? oq_curve::phase_target_power_w(in.heat_phase, in.demand_u, owner_capacity_w, duo_capacity_w)
                     : 0;
  oq_curve::DispatchCandidate best_single, best_duo;
  if (in.demand_active) {
    for (int level = 1; level <= owner_max; ++level) {
      const auto item = candidate(single_owner == 1 ? level : 0, single_owner == 2 ? level : 0, out.target_w);
      if (oq_curve::better_dispatch_candidate(item, best_single, in.previous_hp1, in.previous_hp2, true))
        best_single = item;
    }
    for (int first = 1; first <= first_max; ++first)
      for (int second = 1; second <= second_max; ++second) {
        const auto item = candidate(first, second, out.target_w);
        if (oq_curve::better_dispatch_candidate(item, best_duo, in.previous_hp1, in.previous_hp2, true))
          best_duo = item;
      }
  }
  const bool demand_rundown = in.capped < in.previous_capped;
  const float dt_min = static_cast<float>(in.loop_ms) / 60000.0f;
  const bool startup_grace = oq_curve::elapsed_window_active(
      in.now_ms, in.lead_last_start_ms, static_cast<uint32_t>(std::max(0, in.tuning.dual_startup_grace_s)) * 1000UL);
  const float enable_margin = in.heat_phase ? 700.0f : 450.0f;
  const float enable_u = in.heat_phase ? 0.90f : 0.80f;
  const float disable_u = in.heat_phase ? 0.70f : 0.55f;
  const bool single_saturated =
      best_single.valid && owner_capacity_w > 0 && best_single.power_w >= .90f * owner_capacity_w;
  const bool duo_better = best_duo.valid && best_single.valid && best_duo.error_w + enable_margin < best_single.error_w;
  const bool single_sufficient =
      best_single.valid && (!best_duo.valid || best_single.error_w <= best_duo.error_w + 250.0f);
  const bool owner_hp1 = single_owner != 2;
  const bool floor_reached = oq_curve::duo_floor_reached(
      {owner_hp1 ? in.previous_hp1 : in.previous_hp2, owner_hp1 ? best_single.hp1_level : best_single.hp2_level,
       (owner_hp1 ? in.hp1 : in.hp2).available_for_start && !(owner_hp1 ? in.hp1_valve_defrost : in.hp2_valve_defrost),
       level_power(owner_hp1, owner_hp1 ? in.previous_hp1 : in.previous_hp2), duo_floor_w});
  const bool dual_defrost = in.demand_active && in.hp1_valve_defrost && in.hp2_valve_defrost;
  const bool emergency = in.demand_active && best_duo.valid && in.heat_phase && !startup_grace && !demand_rundown &&
                         single_saturated && duo_better && std::isfinite(in.temperature_error_c) &&
                         in.temperature_error_c >= in.tuning.dual_emergency_temp_err_c && in.dispatch_u >= .95f;
  state.emergency_min = emergency ? state.emergency_min + dt_min : 0;
  const bool forced = state.emergency_min >= static_cast<float>(std::max(1, in.tuning.dual_emergency_hold_min));
  const bool dual_on = in.demand_active && best_duo.valid && !startup_grace && !demand_rundown &&
                       (forced || dual_defrost ||
                        (duo_better && floor_reached && in.dispatch_u >= enable_u &&
                         (in.heat_phase ? single_saturated : !single_sufficient) &&
                         (!std::isfinite(in.temperature_error_c) || in.temperature_error_c >= -.05f)));
  const bool dual_off =
      !in.demand_active || !best_duo.valid || (!state.dual_enabled && !dual_on) ||
      (single_sufficient && in.dispatch_u <= disable_u &&
       (!std::isfinite(in.temperature_error_c) || in.temperature_error_c <= in.tuning.dual_disable_temp_err_max_c));
  if (!in.demand_active)
    state = {};
  else {
    state.on_min = dual_on ? state.on_min + dt_min : 0;
    state.off_min = dual_off ? state.off_min + dt_min : 0;
    if (!state.dual_enabled && state.on_min >= in.dual_on_hold_min) {
      state.dual_enabled = true;
      state.off_min = 0;
    } else if (state.dual_enabled && state.off_min >= in.dual_off_hold_min) {
      state.dual_enabled = false;
      state.on_min = 0;
    }
  }
  out.next = state;
  out.duo_valid = best_duo.valid;
  const bool dual_enabled = in.demand_active && best_duo.valid && (forced || dual_defrost || state.dual_enabled);
  if (in.demand_active) {
    const auto best = dual_enabled ? best_duo : best_single;
    if (best.valid) {
      out.hp1_level = best.hp1_level;
      out.hp2_level = best.hp2_level;
    }
    out.capacity_mode = dual_enabled ? 2 : best.valid && (out.hp1_level > 0 || out.hp2_level > 0) ? 1 : 0;
    out.single_owner = dual_enabled ? 0 : single_owner;
    out.owner = dual_enabled ? 0 : single_owner;
    out.single_branch = !dual_enabled;
    out.regular_single_stop =
        !dual_enabled && in.previous_hp1 > 0 && in.previous_hp2 > 0 && single_sufficient &&
        in.dispatch_u <= disable_u &&
        (!std::isfinite(in.temperature_error_c) || in.temperature_error_c <= in.tuning.dual_disable_temp_err_max_c) &&
        state.off_min >= in.dual_off_hold_min;
  }
  const bool active_missing = (in.previous_hp1 > 0 && !in.hp1.must_stop && !in.hp1_model_available) ||
                              (in.previous_hp2 > 0 && !in.hp2.must_stop && !in.hp2_model_available);
  const bool idle_missing =
      in.previous_hp1 <= 0 && in.previous_hp2 <= 0 && !in.hp1_model_available && !in.hp2_model_available;
  if (in.demand_active && (active_missing || (!best_single.valid && !best_duo.valid && idle_missing))) {
    const auto held = oq_hp_candidate::preserve_active_topology_without_model(true, in.hp1, in.hp2);
    out.hp1_level = held.hp1_level;
    out.hp2_level = held.hp2_level;
    out.owner = out.single_owner = held.owner_hp;
    out.capacity_mode = held.capacity_mode;
  }
  const auto suspect = oq_hp_candidate::preserve_active_topology_during_suspect(
      {out.hp1_level, out.hp2_level, in.hp1, in.hp2, in.demand_active});
  if (suspect.active) {
    out.hp1_level = suspect.hp1_level;
    out.hp2_level = suspect.hp2_level;
    out.owner = out.single_owner = suspect.owner_hp;
    out.capacity_mode = suspect.capacity_mode;
  }
  const auto chosen = pair(out.hp1_level, out.hp2_level);
  if (chosen.valid) {
    out.expected_w = chosen.thermal_w;
    out.electrical_w = chosen.electrical_w;
  }
  return out;
}
}  // namespace oq_curve_duo
