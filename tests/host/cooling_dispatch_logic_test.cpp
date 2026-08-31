#include <assert.h>
#include "../../openquatt/includes/control/oq_cooling_dispatch_logic.h"
using namespace oq_cooling;
DispatchInput active_input(uint32_t now_ms = 10000) {
  DispatchInput in;
  in.now_ms = now_ms;
  in.cooling_mode = true;
  in.raw_demand = in.demand_max = in.power_cap = 4;
  in.hp1.candidate = {0, true, false, false};
  in.hp1.has_allowed_level = true;
  return in;
}
void test_timing() {
  DispatchState state;
  auto in = active_input(UINT32_MAX - 1999U);
  assert(update_dispatch(in, state).evaluated);
  in.now_ms = 1999U;
  assert(!update_dispatch(in, state).evaluated);
  in.now_ms = 3000U;
  assert(update_dispatch(in, state).evaluated);
  assert(!hp_minimum_off_blocks_start(100, 0, 0, 600000));
  assert(!hp_minimum_off_blocks_start(100, UINT32_MAX - 99U, 1, 600000));
  assert(hp_minimum_off_blocks_start(599899U, UINT32_MAX - 99U, 0, 600000));
  assert(!hp_minimum_off_blocks_start(599900U, UINT32_MAX - 99U, 0, 600000));
  assert(hp_minimum_off_remaining_s(100, UINT32_MAX - 99U, 0, 600000) == 600);
}
void test_single() {
  DispatchState state;
  auto in = active_input();
  in.raw_demand = 9;
  in.demand_max = 6;
  in.power_cap = 3;
  auto out = update_dispatch(in, state);
  assert(out.raw_demand == 6 && out.demand == 3 && out.owner == 1 && out.hp1_request == 3);
  in.now_ms += in.cadence_ms;
  in.hp1.last_stop_ms = in.now_ms - 239999U;
  in.hp_min_off_ms = 240000U;
  out = update_dispatch(in, state);
  assert(out.hp1_restart_blocked && out.start_blocked && out.owner == 0);
  in.now_ms++;
  assert(!update_dispatch(in, state).evaluated);
  in.now_ms += in.cadence_ms;
  in.stop_confirmation_pending = true;
  out = update_dispatch(in, state);
  assert(out.start_blocked && out.owner == 0);
  in.hp1.candidate.previous_applied_level = 2;
  in.now_ms += in.cadence_ms;
  out = update_dispatch(in, state);
  assert(!out.start_blocked && out.owner == 1 && out.hp1_request == 3);
}
void test_duo() {
  DispatchInput in = active_input(200);
  in.duo = true;
  in.hp2.candidate = {0, true, false, false};
  in.hp2.has_allowed_level = true;
  in.hp1.last_start_ms = UINT32_MAX - 999U;
  in.hp2.last_stop_ms = UINT32_MAX - 1999U;
  assert(recent_activity_owner(in) == 1);
  DispatchState state;
  in.lead_is_hp1 = false;
  auto out = update_dispatch(in, state);
  assert(out.owner == 1);
  in.now_ms += in.cadence_ms;
  in.hp1.last_start_ms = in.hp2.last_stop_ms = 0;
  in.stored_owner = 2;
  out = update_dispatch(in, state);
  assert(out.owner == 2);
  in.now_ms += in.cadence_ms;
  in.stored_owner = 0;
  in.hp1.candidate.previous_applied_level = 2;
  out = update_dispatch(in, state);
  assert(out.owner == 1);
  in.now_ms += in.cadence_ms;
  in.hp1.candidate.previous_applied_level = 0;
  in.hp1.candidate.available_for_start = false;
  out = update_dispatch(in, state);
  assert(out.owner == 2);
}
void test_stops() {
  DispatchState state;
  auto in = active_input();
  in.duo = true;
  in.hp2.candidate = {2, false, false, true};
  in.hp2.has_allowed_level = true;
  in.stored_owner = 2;
  auto out = update_dispatch(in, state);
  assert(out.owner_before_hold == 2 && out.owner == 2 && out.hp2_request == 4);
  in.now_ms += in.cadence_ms;
  in.hp2.candidate.must_stop = true;
  out = update_dispatch(in, state);
  assert(out.owner == 1 && out.hp1_request == 4 && out.hp2_request == 0);
  in.now_ms += in.cadence_ms;
  in.stop_confirmation_pending = true;
  out = update_dispatch(in, state);
  assert(out.start_blocked && out.owner == 0 && out.hp1_request == 0 && out.hp2_request == 0);
  in.now_ms += in.cadence_ms;
  in.stop_confirmation_pending = false;
  out = update_dispatch(in, state);
  assert(out.owner == 1 && out.hp1_request == 4 && out.hp2_request == 0);
  in.now_ms += in.cadence_ms;
  in.raw_demand = 0;
  in.hp2.candidate.must_stop = false;
  out = update_dispatch(in, state);
  assert(out.owner == 0 && out.hp1_request == 0 && out.hp2_request == 0);
  in.cooling_mode = false;
  assert(update_dispatch(in, state).evaluated && !state.loop_seen);
}
int main() { return (test_timing(), test_single(), test_duo(), test_stops(), 0); }
