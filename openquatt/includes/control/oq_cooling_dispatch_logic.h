#pragma once
#include <algorithm>
#include <stdint.h>
#include "oq_cooling_limiter_logic.h"
#include "oq_hp_candidate_logic.h"
namespace oq_cooling {
struct DispatchHpInput {
  oq_hp_candidate::HpCandidateState candidate;
  uint32_t last_start_ms = 0, last_stop_ms = 0;
  bool has_allowed_level = false;
};
struct DispatchInput {
  uint32_t now_ms = 0, cadence_ms = 5000, hp_min_off_ms = 0, global_min_off_remaining_ms = 0;
  int raw_demand = 0, demand_max = 10, power_cap = 10, stored_owner = 0;
  bool cooling_mode = false, duo = false, lead_is_hp1 = true;
  bool stop_confirmation_pending = false;
  DispatchHpInput hp1, hp2;
};
struct DispatchState {
  bool loop_seen = false;
  uint32_t last_loop_ms = 0;
};
struct DispatchOutput {
  bool evaluated = false, hp1_restart_blocked = false, hp2_restart_blocked = false;
  bool start_blocked = false;
  int raw_demand = 0, demand = 0, hp1_request = 0, hp2_request = 0, owner_before_hold = 0, owner = 0;
};
inline bool hp_minimum_off_blocks_start(uint32_t now_ms, uint32_t last_stop_ms, int previous_applied_level,
                                        uint32_t minimum_off_ms) {
  return previous_applied_level <= 0 && last_stop_ms != 0 && minimum_off_ms > 0 &&
         now_ms - last_stop_ms < minimum_off_ms;
}
inline uint32_t hp_minimum_off_remaining_s(uint32_t now_ms, uint32_t last_stop_ms, int previous_applied_level,
                                           uint32_t minimum_off_ms) {
  if (!hp_minimum_off_blocks_start(now_ms, last_stop_ms, previous_applied_level, minimum_off_ms)) return 0;
  return (minimum_off_ms - (now_ms - last_stop_ms) + 999UL) / 1000UL;
}
inline uint32_t latest_activity_age(uint32_t now_ms, uint32_t last_start_ms, uint32_t last_stop_ms) {
  if (last_start_ms == 0 && last_stop_ms == 0) return UINT32_MAX;
  if (last_start_ms == 0) return now_ms - last_stop_ms;
  if (last_stop_ms == 0) return now_ms - last_start_ms;
  return std::min(now_ms - last_start_ms, now_ms - last_stop_ms);
}
inline int recent_activity_owner(const DispatchInput& in) {
  const uint32_t hp1_age = latest_activity_age(in.now_ms, in.hp1.last_start_ms, in.hp1.last_stop_ms);
  const uint32_t hp2_age = latest_activity_age(in.now_ms, in.hp2.last_start_ms, in.hp2.last_stop_ms);
  return hp1_age == hp2_age ? 0 : (hp1_age < hp2_age ? 1 : 2);
}
inline bool dispatch_hp_can_serve(const DispatchInput& in, const DispatchHpInput& hp, bool restart_blocked) {
  return oq_hp_candidate::may_serve_candidate(hp.candidate) && hp.has_allowed_level && !restart_blocked &&
         !global_minimum_off_time_blocks_start(in.global_min_off_remaining_ms, in.stop_confirmation_pending, false,
                                               hp.candidate.previous_applied_level);
}
inline DispatchOutput update_dispatch(const DispatchInput& in, DispatchState& state) {
  DispatchOutput out;
  if (!in.cooling_mode) {
    state = {};
    out.evaluated = true;
    return out;
  }
  if (state.loop_seen && in.now_ms - state.last_loop_ms < in.cadence_ms) return out;
  state.loop_seen = true;
  state.last_loop_ms = in.now_ms;
  out.evaluated = true;
  const int demand_max = std::max(0, std::min(10, in.demand_max));
  out.raw_demand = std::max(0, std::min(demand_max, in.raw_demand));
  out.demand = std::min(out.raw_demand, std::max(0, std::min(demand_max, in.power_cap)));
  out.hp1_restart_blocked = hp_minimum_off_blocks_start(in.now_ms, in.hp1.last_stop_ms,
                                                        in.hp1.candidate.previous_applied_level, in.hp_min_off_ms);
  out.hp2_restart_blocked =
      in.duo && hp_minimum_off_blocks_start(in.now_ms, in.hp2.last_stop_ms, in.hp2.candidate.previous_applied_level,
                                            in.hp_min_off_ms);
  const bool hp1_can_serve = dispatch_hp_can_serve(in, in.hp1, out.hp1_restart_blocked);
  const bool hp2_can_serve = in.duo && dispatch_hp_can_serve(in, in.hp2, out.hp2_restart_blocked);
  const bool demand_active = out.demand > 0;
  int owner = in.stored_owner;
  if (!demand_active)
    owner = 0;
  else if (in.duo && in.hp1.candidate.previous_applied_level > 0 && in.hp2.candidate.previous_applied_level <= 0 &&
           hp1_can_serve)
    owner = 1;
  else if (in.duo && in.hp2.candidate.previous_applied_level > 0 && in.hp1.candidate.previous_applied_level <= 0 &&
           hp2_can_serve)
    owner = 2;
  else if (!in.duo)
    owner = hp1_can_serve ? 1 : 0;
  else if (!((owner == 1 && hp1_can_serve) || (owner == 2 && hp2_can_serve))) {
    const int recent_owner = recent_activity_owner(in);
    if (recent_owner == 1 && hp1_can_serve)
      owner = 1;
    else if (recent_owner == 2 && hp2_can_serve)
      owner = 2;
    else if (in.lead_is_hp1 && hp1_can_serve)
      owner = 1;
    else if (!in.lead_is_hp1 && hp2_can_serve)
      owner = 2;
    else if (hp1_can_serve != hp2_can_serve)
      owner = hp1_can_serve ? 1 : 2;
    else
      owner = hp1_can_serve ? (in.lead_is_hp1 ? 1 : 2) : 0;
  }
  out.owner_before_hold = owner;
  int hp1_request = owner == 1 ? out.demand : 0;
  int hp2_request = owner == 2 ? out.demand : 0;
  const auto hold = oq_hp_candidate::preserve_active_topology_during_suspect(
      {hp1_request, hp2_request, in.hp1.candidate, in.hp2.candidate, demand_active});
  out.hp1_request = hold.hp1_level;
  out.hp2_request = in.duo ? hold.hp2_level : 0;
  out.owner = in.duo ? hold.owner_hp : (out.hp1_request > 0 ? 1 : 0);
  out.start_blocked = demand_active && out.owner_before_hold == 0 && !hp1_can_serve && !hp2_can_serve;
  return out;
}
inline DispatchOutput dispatch_tick(const DispatchInput& input) {
  static DispatchState state;
  return update_dispatch(input, state);
}
}  // namespace oq_cooling
