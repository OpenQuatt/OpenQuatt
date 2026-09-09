#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "oq_hp_candidate_logic.h"

namespace oq_power_house_dispatch {
constexpr int kMaxLevel = 10;
constexpr uint32_t kTopologyHoldMs = 180000, kDefrostHoldMs = 180000, kOilReturnHoldMs = 120000;
// clang-format off
enum class Reason : uint8_t {
  IDLE = 0, FALLBACK_IDLE = 1, FALLBACK_HP1 = 2, FALLBACK_HP2 = 3,
  FALLBACK_DUO = 4, KEEP_CURRENT = 5, HOLD_ACTIVE = 6, DEFROST_HOLD = 7,
  BETTER_HEAT = 8, SOFT_GUARD = 9, LESS_POWER = 10, NO_CANDIDATE = 11,
  DEFROST_BOOST = 12, RUNTIME_LEAD = 13, SINGLE_TOPOLOGY = 14, OIL_RETURN_HOLD = 15,
};
// clang-format on
inline const char* request_reason_name(int code) {
  // clang-format off
  static constexpr const char* names[] = {
      "ph_idle", "ph_fallback_idle", "ph_fallback_single_hp1", "ph_fallback_single_hp2",
      "ph_fallback_duo", "keep_current", "hold_active", "defrost_hold", "better_heat", "soft_guard",
      "less_power", "no_candidate", "defrost_boost", "runtime_lead", "ph_single_topology", "oil_return_hold",
  };
  // clang-format on
  return code >= 0 && code < static_cast<int>(sizeof(names) / sizeof(names[0])) ? names[code] : names[0];
}
struct LevelEstimate {
  bool allowed = false, thermal_valid = false, electrical_valid = false;
  float thermal_w = 0.0f, electrical_w = 0.0f;
};
struct HpInput {
  oq_hp_candidate::HpCandidateState candidate;
  bool defrost = false, valve_defrost = false;
  std::array<LevelEstimate, kMaxLevel + 1> levels{};
};
struct TimedTail {
  bool previous_active = false, armed = false;
  uint32_t started_ms = 0;
};
struct DispatchState {
  TimedTail hp1_defrost;
  TimedTail hp2_defrost;
  TimedTail oil_return;
  bool topology_initialized = false, topology_hold_armed = false;
  int applied_topology = 0;
  uint32_t topology_hold_started_ms = 0;
};
struct ProtectionObservation {
  uint32_t now_ms = 0;
  bool hp1_valve_defrost = false, hp2_valve_defrost = false, oil_return = false;
  int hp1_applied_level = 0, hp2_applied_level = 0;
};
struct DispatchInput {
  uint32_t now_ms = 0;
  int demand_level = 0;
  float requested_w = NAN;
  bool duo = false, performance_valid = false, lead_hp1 = true;
  HpInput hp1, hp2;
};
struct DispatchTuning {
  float soft_limit_w = NAN, peak_limit_w = NAN;
  float over_soft_penalty_per_w = 5.0f;
  float topology_power_margin_w = 150.0f, topology_heat_advantage_w = 450.0f;
  int defrost_boost_min_level = 6, defrost_boost_steps = 1;
};
struct DispatchDecision {
  int hp1_level = 0, hp2_level = 0, owner_hp = 0;
  Reason reason = Reason::IDLE;
  float capacity_w = 0.0f, deficit_w = 0.0f;
  float expected_w = NAN;
  bool output_valid = false, saturated = false;
};
struct Candidate {
  bool valid = false;
  int hp1_level = 0, hp2_level = 0;
  float thermal_w = 0.0f, electrical_w = 0.0f, error_w = 0.0f, over_soft_w = 0.0f;
  int level_moves = 0, active_count = 0;
  bool single_on_lead = false;
};
inline int topology(int hp1_level, int hp2_level) { return (hp1_level > 0 ? 1 : 0) + (hp2_level > 0 ? 1 : 0); }
inline int owner(int hp1_level, int hp2_level) {
  return hp1_level > 0 && hp2_level <= 0 ? 1 : hp2_level > 0 && hp1_level <= 0 ? 2 : 0;
}
inline int applied_level(const HpInput& hp) {
  const int level = hp.candidate.previous_applied_level;
  return level >= 0 && level <= kMaxLevel ? level : 0;
}
inline oq_hp_candidate::HpCandidateState sanitized_candidate(const HpInput& hp) {
  auto candidate = hp.candidate;
  candidate.previous_applied_level = applied_level(hp);
  return candidate;
}
inline bool hp_may_serve(const HpInput& hp) { return oq_hp_candidate::may_serve_candidate(sanitized_candidate(hp)); }
inline void observe_tail(TimedTail& tail, bool active, uint32_t now_ms, uint32_t duration_ms) {
  if (active || tail.previous_active) {
    tail.armed = true;
    tail.started_ms = now_ms;
  } else if (tail.armed && static_cast<uint32_t>(now_ms - tail.started_ms) >= duration_ms)
    tail.armed = false;
  tail.previous_active = active;
}
inline bool tail_active(const TimedTail& tail, uint32_t now_ms, uint32_t duration_ms) {
  return tail.previous_active ||
         (tail.armed && duration_ms > 0 && static_cast<uint32_t>(now_ms - tail.started_ms) < duration_ms);
}
inline DispatchState observe_protection(DispatchState state, const ProtectionObservation& observation) {
  observe_tail(state.hp1_defrost, observation.hp1_valve_defrost, observation.now_ms, kDefrostHoldMs);
  observe_tail(state.hp2_defrost, observation.hp2_valve_defrost, observation.now_ms, kDefrostHoldMs);
  observe_tail(state.oil_return, observation.oil_return, observation.now_ms, kOilReturnHoldMs);
  if (state.topology_hold_armed &&
      static_cast<uint32_t>(observation.now_ms - state.topology_hold_started_ms) >= kTopologyHoldMs)
    state.topology_hold_armed = false;
  const int applied_topology = topology(observation.hp1_applied_level, observation.hp2_applied_level);
  if (state.topology_initialized && state.applied_topology > 0 && applied_topology > 0 &&
      state.applied_topology != applied_topology) {
    state.topology_hold_armed = true;
    state.topology_hold_started_ms = observation.now_ms;
  }
  state.topology_initialized = true;
  state.applied_topology = applied_topology;
  return state;
}
inline bool topology_hold_active(const DispatchState& state, uint32_t now_ms, uint32_t duration_ms) {
  return state.topology_hold_armed && duration_ms > 0 &&
         static_cast<uint32_t>(now_ms - state.topology_hold_started_ms) < duration_ms;
}
inline bool electrical_limits_valid(const DispatchTuning& tuning) {
  return std::isfinite(tuning.soft_limit_w) && std::isfinite(tuning.peak_limit_w) && tuning.soft_limit_w >= 0.0f &&
         tuning.peak_limit_w >= tuning.soft_limit_w;
}
inline Candidate make_candidate(const DispatchInput& in, const DispatchTuning& tuning, int hp1_level, int hp2_level,
                                float target_w) {
  Candidate result;
  result.hp1_level = hp1_level;
  result.hp2_level = in.duo ? hp2_level : 0;
  result.active_count = topology(result.hp1_level, result.hp2_level);
  result.single_on_lead =
      result.active_count == 1 && owner(result.hp1_level, result.hp2_level) == (in.lead_hp1 ? 1 : 2);
  if (!electrical_limits_valid(tuning) || !std::isfinite(target_w) || target_w < 0.0f || result.hp1_level < 0 ||
      result.hp2_level < 0 || result.hp1_level > kMaxLevel || result.hp2_level > kMaxLevel)
    return result;
  auto add_hp = [&](const HpInput& hp, int level) {
    if (level <= 0) return true;
    if (!hp_may_serve(hp)) return false;
    const auto& estimate = hp.levels[level];
    if (!estimate.allowed || !estimate.thermal_valid || !estimate.electrical_valid ||
        !std::isfinite(estimate.thermal_w) || !std::isfinite(estimate.electrical_w) || estimate.thermal_w < 0.0f ||
        estimate.electrical_w < 0.0f)
      return false;
    result.thermal_w += estimate.thermal_w;
    result.electrical_w += estimate.electrical_w;
    return true;
  };
  if (!add_hp(in.hp1, result.hp1_level) || !add_hp(in.hp2, result.hp2_level) || !std::isfinite(result.thermal_w) ||
      !std::isfinite(result.electrical_w) || result.electrical_w > tuning.peak_limit_w)
    return result;
  result.valid = true;
  result.error_w = std::fabs(result.thermal_w - target_w);
  result.over_soft_w = std::max(0.0f, result.electrical_w - tuning.soft_limit_w);
  result.level_moves =
      std::abs(result.hp1_level - applied_level(in.hp1)) + std::abs(result.hp2_level - applied_level(in.hp2));
  return result;
}
inline bool better_candidate(const Candidate& a, const Candidate& b) {
  if (std::fabs(a.over_soft_w - b.over_soft_w) > 1.0f) return a.over_soft_w < b.over_soft_w;
  if (std::fabs(a.electrical_w - b.electrical_w) > 1.0f) return a.electrical_w < b.electrical_w;
  if (std::fabs(a.error_w - b.error_w) > 1.0f) return a.error_w < b.error_w;
  if (a.level_moves != b.level_moves) return a.level_moves < b.level_moves;
  const int a_balance = std::abs(a.hp1_level - a.hp2_level);
  const int b_balance = std::abs(b.hp1_level - b.hp2_level);
  if (a.active_count == 2 && b.active_count == 2 && a_balance != b_balance) return a_balance < b_balance;
  if (a.active_count == 1 && b.active_count == 1 && a_balance == b_balance && a.single_on_lead != b.single_on_lead)
    return a.single_on_lead;
  return a.hp1_level != b.hp1_level ? a.hp1_level < b.hp1_level : a.hp2_level < b.hp2_level;
}
inline Candidate pick_topology(const DispatchInput& in, const DispatchTuning& tuning, float target_w, int active_count,
                               float tie_band_w) {
  Candidate chosen;
  float best_error_w = INFINITY;
  for (int hp1 = 0; hp1 <= kMaxLevel; ++hp1)
    for (int hp2 = 0; hp2 <= (in.duo ? kMaxLevel : 0); ++hp2) {
      const auto candidate = make_candidate(in, tuning, hp1, hp2, target_w);
      if (candidate.valid && candidate.active_count == active_count)
        best_error_w = std::min(best_error_w, candidate.error_w);
    }
  for (int hp1 = 0; hp1 <= kMaxLevel; ++hp1)
    for (int hp2 = 0; hp2 <= (in.duo ? kMaxLevel : 0); ++hp2) {
      const auto candidate = make_candidate(in, tuning, hp1, hp2, target_w);
      if (candidate.valid && candidate.active_count == active_count && candidate.error_w <= best_error_w + tie_band_w &&
          (!chosen.valid || better_candidate(candidate, chosen)))
        chosen = candidate;
    }
  return chosen;
}
inline float dispatch_capacity(const DispatchInput& in, const DispatchTuning& tuning) {
  if (!in.performance_valid || !electrical_limits_valid(tuning)) return 0.0f;
  float capacity_w = 0.0f;
  for (int hp1 = 0; hp1 <= kMaxLevel; ++hp1)
    for (int hp2 = 0; hp2 <= (in.duo ? kMaxLevel : 0); ++hp2) {
      const auto candidate = make_candidate(in, tuning, hp1, hp2, 0.0f);
      if (candidate.valid && candidate.thermal_w > capacity_w) capacity_w = candidate.thermal_w;
    }
  return capacity_w;
}
inline DispatchDecision decide_dispatch(const DispatchInput& in, const DispatchTuning& tuning,
                                        const DispatchState& state) {
  DispatchDecision out;
  const bool request_valid = std::isfinite(in.requested_w) && in.requested_w >= 0.0f;
  out.capacity_w = dispatch_capacity(in, tuning);
  out.deficit_w = request_valid ? std::max(0.0f, in.requested_w - out.capacity_w) : 0.0f;
  out.saturated = in.demand_level > 0 && out.deficit_w > 0.0f;
  if (in.demand_level <= 0) {
    out.output_valid = request_valid && in.performance_valid;
    if (out.output_valid) out.expected_w = 0.0f;
    return out;
  }
  auto finish = [&](const Candidate& candidate, Reason reason) {
    if (!candidate.valid) {
      out.reason = Reason::NO_CANDIDATE;
      return;
    }
    out.hp1_level = candidate.hp1_level;
    out.hp2_level = candidate.hp2_level;
    out.owner_hp = owner(out.hp1_level, out.hp2_level);
    out.reason = reason;
    out.expected_w = candidate.thermal_w;
    out.output_valid = request_valid;
  };
  if (!request_valid) {
    out.reason = Reason::FALLBACK_IDLE;
    return out;
  }
  if (!electrical_limits_valid(tuning)) {
    out.reason = Reason::NO_CANDIDATE;
    return out;
  }
  if (!in.performance_valid) {
    auto retained = [&](const HpInput& hp) {
      const int level = applied_level(hp);
      return level > 0 && level <= kMaxLevel && !hp.candidate.must_stop && hp.levels[level].allowed ? level : 0;
    };
    out.hp1_level = retained(in.hp1);
    out.hp2_level = in.duo ? retained(in.hp2) : 0;
    out.owner_hp = owner(out.hp1_level, out.hp2_level);
    out.reason = topology(out.hp1_level, out.hp2_level) == 0 ? Reason::FALLBACK_IDLE
                 : out.owner_hp == 1                         ? Reason::FALLBACK_HP1
                 : out.owner_hp == 2                         ? Reason::FALLBACK_HP2
                                                             : Reason::FALLBACK_DUO;
    return out;
  }
  const float target_w = std::max(0.0f, std::min(in.requested_w, out.capacity_w));
  if (!in.duo) {
    Candidate best;
    float best_cost = INFINITY;
    bool active_candidate = false;
    for (int level = 0; level <= kMaxLevel; ++level) {
      const auto candidate = make_candidate(in, tuning, level, 0, target_w);
      if (!candidate.valid) continue;
      active_candidate |= level > 0;
      const float penalty = std::isfinite(tuning.over_soft_penalty_per_w) && tuning.over_soft_penalty_per_w >= 0.0f
                                ? tuning.over_soft_penalty_per_w
                                : 0.0f;
      const float cost = candidate.error_w + 50.0f * candidate.level_moves + penalty * candidate.over_soft_w;
      if (cost < best_cost) {
        best = candidate;
        best_cost = cost;
      }
    }
    if (!active_candidate)
      out.reason = Reason::NO_CANDIDATE;
    else
      finish(best, best.hp1_level > 0 ? Reason::SINGLE_TOPOLOGY : Reason::IDLE);
  } else {
    const float tie_band_w = std::max(150.0f, 0.05f * std::max(target_w, 1000.0f));
    const float keep_margin_w = std::min(90.0f, tie_band_w * 0.5f);
    const float power_margin_w = std::isfinite(tuning.topology_power_margin_w) && tuning.topology_power_margin_w >= 0.0f
                                     ? tuning.topology_power_margin_w
                                     : 150.0f;
    const float heat_advantage_w =
        std::isfinite(tuning.topology_heat_advantage_w) && tuning.topology_heat_advantage_w >= 0.0f
            ? tuning.topology_heat_advantage_w
            : 450.0f;
    const Candidate single = pick_topology(in, tuning, target_w, 1, tie_band_w);
    const Candidate duo = pick_topology(in, tuning, target_w, 2, tie_band_w);
    Candidate best;
    const bool have_best = single.valid || duo.valid;
    if (single.valid && duo.valid) {
      const Candidate* preferred = duo.electrical_w < single.electrical_w ? &duo : &single;
      const Candidate* alternate = preferred == &duo ? &single : &duo;
      best = alternate->error_w + heat_advantage_w < preferred->error_w ? *alternate : *preferred;
    } else if (single.valid)
      best = single;
    else if (duo.valid)
      best = duo;
    const Candidate current = make_candidate(in, tuning, applied_level(in.hp1), applied_level(in.hp2), target_w);
    const bool valve_defrost = in.hp1.valve_defrost || in.hp2.valve_defrost;
    if (valve_defrost && topology(current.hp1_level, current.hp2_level) == 1 && single.valid && have_best &&
        best.active_count == 2)
      best = single;
    bool keep_current = false;
    Reason reason = Reason::BETTER_HEAT;
    if (have_best && current.valid) {
      const bool heat_ok = current.error_w <= best.error_w + keep_margin_w;
      const bool topology_change =
          current.active_count > 0 && best.active_count > 0 && current.active_count != best.active_count;
      const bool owner_swap =
          current.active_count == 1 && best.active_count == 1 && ((current.hp1_level > 0) != (best.hp1_level > 0));
      const bool change_allowed = !topology_change || best.error_w + heat_advantage_w < current.error_w ||
                                  best.electrical_w + power_margin_w < current.electrical_w;
      const bool defrost_hold = owner_swap && (tail_active(state.hp1_defrost, in.now_ms, kDefrostHoldMs) ||
                                               tail_active(state.hp2_defrost, in.now_ms, kDefrostHoldMs));
      const bool oil_hold =
          heat_ok && (topology_change || owner_swap) && tail_active(state.oil_return, in.now_ms, kOilReturnHoldMs);
      const bool ordinary_hold =
          topology_change && heat_ok && !change_allowed && topology_hold_active(state, in.now_ms, kTopologyHoldMs);
      if (defrost_hold)
        keep_current = true, reason = Reason::DEFROST_HOLD;
      else if (oil_hold)
        keep_current = true, reason = Reason::OIL_RETURN_HOLD;
      else if (ordinary_hold || (topology_change && !change_allowed && heat_ok))
        keep_current = true, reason = ordinary_hold && valve_defrost ? Reason::DEFROST_HOLD : Reason::HOLD_ACTIVE;
      else if (!heat_ok)
        reason = Reason::BETTER_HEAT;
      else if (current.over_soft_w > best.over_soft_w + 40.0f)
        reason = Reason::SOFT_GUARD;
      else if (current.electrical_w > best.electrical_w + (topology_change ? power_margin_w : 150.0f))
        reason = Reason::LESS_POWER;
      else
        keep_current = true, reason = Reason::KEEP_CURRENT;
    } else if (!have_best) {
      keep_current = current.valid && current.active_count > 0;
      reason = keep_current ? Reason::KEEP_CURRENT : Reason::NO_CANDIDATE;
    }
    Candidate chosen = keep_current && current.valid ? current : best;
    const int boost_steps = std::max(0, std::min(3, tuning.defrost_boost_steps));
    if (chosen.valid && chosen.active_count == 2 && boost_steps > 0 &&
        in.demand_level >= tuning.defrost_boost_min_level && chosen.thermal_w + tie_band_w < target_w &&
        (in.hp1.valve_defrost != in.hp2.valve_defrost)) {
      for (int step = boost_steps; step > 0; --step) {
        const int hp1 = in.hp2.valve_defrost ? std::min(kMaxLevel, chosen.hp1_level + step) : chosen.hp1_level;
        const int hp2 = in.hp1.valve_defrost ? std::min(kMaxLevel, chosen.hp2_level + step) : chosen.hp2_level;
        const auto boosted = make_candidate(in, tuning, hp1, hp2, target_w);
        if (boosted.valid && (hp1 > chosen.hp1_level || hp2 > chosen.hp2_level)) {
          chosen = boosted;
          reason = Reason::DEFROST_BOOST;
          break;
        }
      }
    }
    if (chosen.valid && chosen.active_count == 1 && applied_level(in.hp1) <= 0 && applied_level(in.hp2) <= 0 &&
        !in.hp1.defrost && !in.hp2.defrost && !valve_defrost) {
      const int level = chosen.hp1_level > 0 ? chosen.hp1_level : chosen.hp2_level;
      const auto lead = make_candidate(in, tuning, in.lead_hp1 ? level : 0, in.lead_hp1 ? 0 : level, target_w);
      const auto lag = make_candidate(in, tuning, in.lead_hp1 ? 0 : level, in.lead_hp1 ? level : 0, target_w);
      if (lead.valid && lag.valid) chosen = lead, reason = Reason::RUNTIME_LEAD;
    }
    if (chosen.valid) {
      const auto suspect = oq_hp_candidate::preserve_active_topology_during_suspect(
          {chosen.hp1_level, chosen.hp2_level, sanitized_candidate(in.hp1), sanitized_candidate(in.hp2), true});
      if (suspect.active) {
        auto retained_level = [](const HpInput& hp, int proposed) {
          const int previous = applied_level(hp);
          if (hp.candidate.must_stop || previous <= 0) return 0;
          return proposed > 0 ? proposed : previous;
        };
        const auto retained = make_candidate(in, tuning, retained_level(in.hp1, chosen.hp1_level),
                                             retained_level(in.hp2, chosen.hp2_level), target_w);
        if (retained.valid) chosen = retained, reason = Reason::KEEP_CURRENT;
      }
    }
    finish(chosen, reason);
  }
  return out;
}
}  // namespace oq_power_house_dispatch
