#include <assert.h>

#include "../../openquatt/includes/control/oq_hp_candidate_logic.h"

namespace {

enum class FakeLinkState {
  HEALTHY,
  SUSPECT,
};

struct FakeIncidentOutputs {
  FakeLinkState link_state = FakeLinkState::HEALTHY;
  bool available_for_start = false;
  bool must_stop = false;
};

}  // namespace

int main() {
  using oq_hp_candidate::HpCandidateState;
  using oq_hp_candidate::may_serve_candidate;
  using oq_hp_candidate::may_start;
  using oq_hp_candidate::minimum_off_ready;
  using oq_hp_candidate::preserve_active_topology_during_suspect;

  assert(may_start(true, false));
  assert(!may_start(false, false));
  assert(!may_start(true, true));

  assert(may_serve_candidate(true, false, 0));
  // SUSPECT blocks a new start but retains an already active owner.
  assert(may_serve_candidate(false, false, 4));
  assert(!may_serve_candidate(false, false, 0));
  assert(!may_serve_candidate(true, true, 4));
  assert(!may_serve_candidate(true, false, 0, false));
  assert(may_serve_candidate(true, false, 4, false));
  assert(!minimum_off_ready(239999U, 1U, 240000U, 0));
  assert(minimum_off_ready(240001U, 1U, 240000U, 0));
  assert(minimum_off_ready(25U, UINT32_MAX - 50U, 75U, 0));
  assert(minimum_off_ready(100U, 99U, 240000U, 4));

  // A running SUSPECT HP remains the owner; a healthy idle HP is not started
  // merely because the short link dip blocks new starts on the owner.
  const auto keep_hp1 = preserve_active_topology_during_suspect(0, 6, 4, 0, true, false, false, false, true);
  assert(keep_hp1.active);
  assert(keep_hp1.hp1_level == 4);
  assert(keep_hp1.hp2_level == 0);

  // Level regulation may continue without changing the active topology.
  const auto regulate_hp1 = preserve_active_topology_during_suspect(3, 0, 4, 0, true, false, false, false, true);
  assert(regulate_hp1.active);
  assert(regulate_hp1.hp1_level == 3);
  assert(regulate_hp1.hp2_level == 0);

  // Duo membership remains stable while either running member is SUSPECT.
  const auto keep_duo = preserve_active_topology_during_suspect(0, 5, 4, 5, true, false, false, false, true);
  assert(keep_duo.active);
  assert(keep_duo.hp1_level == 4);
  assert(keep_duo.hp2_level == 5);

  // Independent safety and demand removal still win immediately.
  const auto must_stop = preserve_active_topology_during_suspect(0, 5, 4, 5, true, true, true, false, true);
  assert(must_stop.active);
  assert(must_stop.hp1_level == 0);
  assert(must_stop.hp2_level == 5);
  const auto demand_ended = preserve_active_topology_during_suspect(0, 0, 4, 0, true, false, false, false, false);
  assert(!demand_ended.active);
  assert(demand_ended.hp1_level == 0);
  assert(demand_ended.hp2_level == 0);

  FakeIncidentOutputs hp1_outputs;
  hp1_outputs.link_state = FakeLinkState::SUSPECT;
  const auto hp1_state = oq_hp_candidate::candidate_state(hp1_outputs, 4);
  assert(hp1_state.link_suspect);
  assert(may_serve_candidate(hp1_state));

  // The compact strategy-facing form also derives owner and capacity mode.
  const auto compact_single = preserve_active_topology_during_suspect({0, 6, hp1_state, HpCandidateState{}, true});
  assert(compact_single.active);
  assert(compact_single.hp1_level == 4);
  assert(compact_single.hp2_level == 0);
  assert(compact_single.owner_hp == 1);
  assert(compact_single.capacity_mode == 1);

  FakeIncidentOutputs hp2_outputs;
  hp2_outputs.link_state = FakeLinkState::SUSPECT;
  const auto hp2_state = oq_hp_candidate::candidate_state(hp2_outputs, 5);
  const auto compact_duo = preserve_active_topology_during_suspect({3, 5, hp1_state, hp2_state, true});
  assert(compact_duo.active);
  assert(compact_duo.hp1_level == 3);
  assert(compact_duo.hp2_level == 5);
  assert(compact_duo.owner_hp == 0);
  assert(compact_duo.capacity_mode == 2);

  hp1_outputs.must_stop = true;
  const auto hp1_stop_state = oq_hp_candidate::candidate_state(hp1_outputs, 4);
  const auto compact_must_stop = preserve_active_topology_during_suspect({3, 5, hp1_stop_state, hp2_state, true});
  assert(compact_must_stop.active);
  assert(compact_must_stop.hp1_level == 0);
  assert(compact_must_stop.hp2_level == 5);
  assert(compact_must_stop.owner_hp == 2);
  assert(compact_must_stop.capacity_mode == 1);
  return 0;
}
