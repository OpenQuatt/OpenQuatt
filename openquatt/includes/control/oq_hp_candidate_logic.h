#pragma once

#include <cstdint>

namespace oq_hp_candidate {

inline bool may_start(bool available_for_start, bool must_stop) { return available_for_start && !must_stop; }

inline bool may_serve_candidate(bool available_for_start, bool must_stop, int previous_applied_level,
                                bool minimum_off_ready = true) {
  if (must_stop) return false;
  return previous_applied_level > 0 || (available_for_start && minimum_off_ready);
}

inline bool minimum_off_ready(uint32_t now_ms, uint32_t last_stop_ms, uint32_t minimum_off_ms,
                              int previous_applied_level) {
  if (previous_applied_level > 0 || last_stop_ms == 0 || minimum_off_ms == 0) return true;
  return static_cast<uint32_t>(now_ms - last_stop_ms) >= minimum_off_ms;
}

struct HpCandidateState {
  int previous_applied_level = 0;
  bool available_for_start = false;
  bool must_stop = false;
  bool link_suspect = false;
  bool minimum_off_ready = true;
};

template <typename IncidentOutputs>
inline HpCandidateState candidate_state(const IncidentOutputs& outputs, int previous_applied_level) {
  using LinkState = decltype(outputs.link_state);
  return HpCandidateState{
      previous_applied_level,
      outputs.available_for_start,
      outputs.must_stop,
      outputs.link_state == LinkState::SUSPECT,
      true,
  };
}

inline bool may_serve_candidate(const HpCandidateState& candidate) {
  return may_serve_candidate(candidate.available_for_start, candidate.must_stop, candidate.previous_applied_level,
                             candidate.minimum_off_ready);
}

struct SuspectTopologyHold {
  int hp1_level = 0;
  int hp2_level = 0;
  int owner_hp = 0;
  int capacity_mode = 0;
  bool active = false;
};

struct SuspectTopologyInputs {
  int proposed_hp1_level = 0;
  int proposed_hp2_level = 0;
  HpCandidateState hp1;
  HpCandidateState hp2;
  bool demand_active = false;
};

// A short link dip may block a new start, but must not by itself swap the
// active owner or change Single/Duo topology. Level control remains free while
// the proposed topology is unchanged. A must-stop decision is always allowed
// through and demand removal always wins.
inline SuspectTopologyHold preserve_active_topology_during_suspect(const SuspectTopologyInputs& inputs) {
  SuspectTopologyHold decision{
      inputs.proposed_hp1_level, inputs.proposed_hp2_level, 0, 0, false,
  };
  if (inputs.demand_active) {
    const bool hold_required =
        (inputs.hp1.link_suspect && inputs.hp1.previous_applied_level > 0 && !inputs.hp1.must_stop) ||
        (inputs.hp2.link_suspect && inputs.hp2.previous_applied_level > 0 && !inputs.hp2.must_stop);
    if (hold_required) {
      auto retained_level = [](int proposed_level, const HpCandidateState& candidate) {
        if (candidate.must_stop || candidate.previous_applied_level <= 0) {
          return 0;
        }
        return proposed_level > 0 ? proposed_level : candidate.previous_applied_level;
      };
      decision.hp1_level = retained_level(inputs.proposed_hp1_level, inputs.hp1);
      decision.hp2_level = retained_level(inputs.proposed_hp2_level, inputs.hp2);
      decision.active = true;
    }
  }

  decision.owner_hp = (decision.hp1_level > 0 && decision.hp2_level <= 0)   ? 1
                      : (decision.hp2_level > 0 && decision.hp1_level <= 0) ? 2
                                                                            : 0;
  decision.capacity_mode = (decision.hp1_level > 0 && decision.hp2_level > 0)
                               ? 2
                               : ((decision.hp1_level > 0 || decision.hp2_level > 0) ? 1 : 0);
  return decision;
}

inline SuspectTopologyHold preserve_active_topology_during_suspect(int proposed_hp1_level, int proposed_hp2_level,
                                                                   int previous_hp1_level, int previous_hp2_level,
                                                                   bool hp1_link_suspect, bool hp2_link_suspect,
                                                                   bool hp1_must_stop, bool hp2_must_stop,
                                                                   bool demand_active) {
  return preserve_active_topology_during_suspect(SuspectTopologyInputs{
      proposed_hp1_level,
      proposed_hp2_level,
      HpCandidateState{previous_hp1_level, false, hp1_must_stop, hp1_link_suspect},
      HpCandidateState{previous_hp2_level, false, hp2_must_stop, hp2_link_suspect},
      demand_active,
  });
}

}  // namespace oq_hp_candidate
