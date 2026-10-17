#include <cassert>

#include "includes/control/oq_heating_curve_logic.h"
#include "includes/control/oq_power_house_dispatch_logic.h"

namespace {
namespace ph = oq_power_house_dispatch;

ph::DispatchInput fixture(int current, int nearby, int distant) {
  ph::DispatchInput in;
  in.duo = true;
  in.demand_level = 10;
  in.requested_w = 6000;
  in.performance_valid = true;
  in.hp1.candidate = {current, true, false, false};
  in.hp2.candidate = {2, true, false, false};
  in.hp1.levels[current] = {true, true, true, 2000, 400, 40};
  in.hp1.levels[nearby] = {true, true, true, 4000, 500, 35};
  in.hp1.levels[distant] = {true, true, true, 4000, 500, 70};
  in.hp2.levels[2] = {true, true, true, 2000, 400, 60};
  return in;
}

void physical_frequency_beats_level_distance() {
  const ph::DispatchTuning tuning{9000, 10000};
  // A monotone table: two steps down are 5 Hz, one step up is 30 Hz.
  const auto in = fixture(3, 1, 4);
  const auto chosen = ph::decide_dispatch(in, tuning, {});
  assert(chosen.hp1_level == 1 && chosen.hp2_level == 2);
  const auto near = ph::make_candidate(in, tuning, 1, 2, 6000);
  const auto far = ph::make_candidate(in, tuning, 4, 2, 6000);
  assert(near.level_moves == 2 && far.level_moves == 1);
  assert(near.modulation_hz == 5 && far.modulation_hz == 30);
  assert(near.balance_gap > far.balance_gap);
  assert(ph::better_candidate(near, far));

  // Equivalent frequencies on a differently numbered 20-level table.
  const auto remapped = fixture(18, 15, 20);
  const auto remapped_chosen = ph::decide_dispatch(remapped, tuning, {});
  assert(remapped_chosen.hp1_level == 15 && remapped_chosen.hp2_level == 2);
  assert(remapped.hp1.levels[remapped_chosen.hp1_level].frequency_hz == in.hp1.levels[chosen.hp1_level].frequency_hz);

  const oq_curve::DispatchCandidate curve_near{true, 1, 2, 6000, 0, 2, 25, 5};
  const oq_curve::DispatchCandidate curve_far{true, 4, 2, 6000, 0, 2, 10, 30};
  assert(oq_curve::better_dispatch_candidate(curve_near, curve_far, 3, 2, true));
  auto curve_remapped = curve_near;
  curve_remapped.hp1_level = 15;
  auto curve_far_remapped = curve_far;
  curve_far_remapped.hp1_level = 20;
  assert(oq_curve::better_dispatch_candidate(curve_remapped, curve_far_remapped, 18, 2, true));
}

void events_and_primary_objectives() {
  const ph::DispatchTuning tuning{9000, 10000};
  auto in = fixture(3, 1, 4);
  in.hp2.candidate.previous_applied_level = 0;
  in.hp2.levels[2] = {true, true, true, 4000, 500, 35};
  const auto retained = ph::make_candidate(in, tuning, 1, 0, 4000);
  auto owner_swap = ph::make_candidate(in, tuning, 0, 2, 4000);
  assert(retained.starts == 0 && retained.stops == 0 && retained.modulation_hz == 5);
  assert(owner_swap.starts == 1 && owner_swap.stops == 1 && owner_swap.modulation_hz == 0);
  assert(ph::better_candidate(retained, owner_swap));
  owner_swap.electrical_w = retained.electrical_w - 100;
  assert(ph::better_candidate(owner_swap, retained));

  auto near = ph::make_candidate(in, tuning, 1, 2, 8000);
  auto far = ph::make_candidate(in, tuning, 4, 2, 8000);
  far.error_w = near.error_w - 10;
  assert(ph::better_candidate(far, near));
  near.over_soft_w = 20;
  assert(ph::better_candidate(far, near));

  const oq_curve::DispatchCandidate curve_retained{true, 1, 0, 4000, 0, 1, 35, 5};
  auto curve_swap = oq_curve::DispatchCandidate{true, 0, 2, 4000, 0, 1, 35, 0};
  assert(oq_curve::better_dispatch_candidate(curve_retained, curve_swap, 3, 0, true));
  curve_swap.error_w = 0;
  auto heat_mismatch = curve_retained;
  heat_mismatch.error_w = 51;
  assert(oq_curve::better_dispatch_candidate(curve_swap, heat_mismatch, 3, 0, true));

  // Single retains the old preference for the current/nearby physical level.
  const oq_curve::DispatchCandidate single_current{true, 3, 0, 3030, 30, 1, 0};
  const oq_curve::DispatchCandidate single_lower{true, 2, 0, 3000, 0, 1, 0};
  assert(oq_curve::better_dispatch_candidate(single_current, single_lower, 3, 0));

  // The candidate change does not authorize an unavailable/minimum-off HP.
  in.hp2.candidate = {0, false, false, true};
  assert(!ph::make_candidate(in, tuning, 1, 2, 8000).valid);
  in.hp1.candidate.must_stop = true;
  assert(!ph::make_candidate(in, tuning, 1, 0, 4000).valid);
}

void unknown_frequency_is_not_zero_cost() {
  using namespace oq_compressor_movement;
  assert(frequency(0, -1) == 0 && frequency(2, 0) == -1 && frequency(2, 255) == -1);
  assert(modulation_hz({30, 30}, {90, 90}) == 120);
  assert(modulation_hz({30, 0}, {0, 90}) == 0);
  assert(modulation_hz({-1, 30}, {35, 30}) == -1);
  assert(modulation_hz({120, 30}, {90, 30}) == 30);
  const ph::DispatchTuning tuning{9000, 10000};
  auto in = fixture(3, 1, 4);
  in.hp1.levels[3].frequency_hz = 0;
  auto near = ph::make_candidate(in, tuning, 1, 2, 6000);
  auto far = ph::make_candidate(in, tuning, 4, 2, 6000);
  assert(near.modulation_hz == -1 && far.modulation_hz == -1);
  // Missing frequency metadata never falls back to a misleading level cost.
  assert(ph::better_candidate(far, near));  // Known Hz balance remains 10 vs 25.
}
}  // namespace

int main() {
  physical_frequency_beats_level_distance();
  events_and_primary_objectives();
  unknown_frequency_is_not_zero_cost();
}
