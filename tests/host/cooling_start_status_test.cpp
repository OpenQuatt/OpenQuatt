#include <assert.h>

#include "../../openquatt/includes/control/oq_cooling_dispatch_logic.h"
#include "../../openquatt/includes/control/oq_cooling_start_status.h"

using namespace oq_cooling;
using namespace oq_cooling_start_status;

static DispatchInput active_input(uint32_t now_ms = 1000000U) {
  DispatchInput in;
  in.now_ms = now_ms;
  in.cooling_mode = true;
  in.cadence_ms = 5000U;
  in.hp_min_off_ms = 240000U;
  in.raw_demand = in.demand_max = in.power_cap = 4;
  in.hp1.candidate = {0, true, false, false};
  in.hp1.has_allowed_level = true;
  return in;
}

static void test_single_restart_reports_minimum_unblock() {
  DispatchState state;
  auto in = active_input();
  // Stopped 190 s ago with a 240 s guard: 50 s to go.
  in.hp1.last_stop_ms = in.now_ms - 190000U;
  const auto out = update_dispatch(in, state);
  assert(out.start_blocked);
  assert(out.hp1_restart_blocked);
  assert(out.start_status_reason == HP_RESTART);
  assert(out.start_status_remaining_s == 50);
}

static void test_confirmation_wins_over_running_countdown() {
  DispatchState state;
  auto in = active_input();
  // The exact remainder is unknown until the stop is confirmed, even though a
  // cooling countdown is also nonzero: never invent a countdown here.
  in.global_min_off_remaining_ms = 120000U;
  in.stop_confirmation_pending = true;
  const auto out = update_dispatch(in, state);
  assert(out.start_blocked);
  assert(out.start_status_reason == COOLING_CONFIRM);
  assert(out.start_status_remaining_s == 0);
}

static void test_global_cooling_guard_counts_down() {
  DispatchState state;
  auto in = active_input();
  in.global_min_off_remaining_ms = 190000U;
  const auto out = update_dispatch(in, state);
  assert(out.start_blocked);
  assert(out.start_status_reason == COOLING_MIN_OFF);
  assert(out.start_status_remaining_s == 190);
}

static void test_duo_reports_first_free_hp() {
  DispatchState state;
  auto in = active_input();
  in.duo = true;
  in.hp2.candidate = {0, true, false, false};
  in.hp2.has_allowed_level = true;
  // HP1 free in 30 s, HP2 in 190 s: cooling can start in 30 s.
  in.hp1.last_stop_ms = in.now_ms - 210000U;
  in.hp2.last_stop_ms = in.now_ms - 50000U;
  const auto out = update_dispatch(in, state);
  assert(out.start_blocked);
  assert(out.start_status_reason == HP_RESTART);
  assert(out.start_status_remaining_s == 30);
}

static void test_duo_with_one_free_hp_is_no_block() {
  DispatchState state;
  auto in = active_input();
  in.duo = true;
  in.hp2.candidate = {0, true, false, false};
  in.hp2.has_allowed_level = true;
  // HP1 free now, HP2 still 190 s out: the dispatch simply picks HP1.
  in.hp2.last_stop_ms = in.now_ms - 50000U;
  const auto out = update_dispatch(in, state);
  assert(!out.start_blocked);
  assert(out.owner == 1);
  assert(out.start_status_reason == NONE);
  assert(out.start_status_remaining_s == 0);
}

static void test_duo_without_candidates_reports_other_without_countdown() {
  DispatchState state;
  auto in = active_input();
  in.duo = true;
  in.hp1.candidate.available_for_start = false;
  in.hp2.candidate = {0, false, false, false};
  in.hp2.has_allowed_level = false;
  const auto out = update_dispatch(in, state);
  assert(out.start_blocked);
  assert(out.start_status_reason == OTHER);
  assert(out.start_status_remaining_s == 0);
}

static void test_running_hp_keeps_owner_and_reports_none() {
  DispatchState state;
  auto in = active_input();
  in.duo = true;
  in.hp1.candidate.previous_applied_level = 2;
  in.hp2.candidate = {0, true, false, false};
  in.hp2.has_allowed_level = true;
  // HP2 still in restart while HP1 cools: active operation, no global block.
  in.hp2.last_stop_ms = in.now_ms - 50000U;
  const auto out = update_dispatch(in, state);
  assert(!out.start_blocked);
  assert(out.owner == 1);
  assert(out.start_status_reason == NONE);
}

static void test_no_demand_reports_none() {
  DispatchState state;
  auto in = active_input();
  in.raw_demand = 0;
  const auto out = update_dispatch(in, state);
  assert(!out.start_blocked);
  assert(out.start_status_reason == NONE);
}

static void test_actuator_refuse_mapping() {
  assert(map_actuator_refuse(true, false, false, 190).reason == COOLING_MIN_OFF);
  assert(map_actuator_refuse(true, false, false, 190).remaining_s == 190);
  assert(map_actuator_refuse(true, false, false, 0).reason == COOLING_CONFIRM);
  assert(map_actuator_refuse(true, false, false, 0).remaining_s == 0);
  assert(map_actuator_refuse(false, true, false, 180).reason == HP_RESTART);
  assert(map_actuator_refuse(false, true, false, 180).remaining_s == 180);
  assert(map_actuator_refuse(false, false, true, 420).reason == START_LIMIT);
  assert(map_actuator_refuse(false, false, true, 420).remaining_s == 420);
  assert(map_actuator_refuse(false, false, false, 0).reason == OTHER);
  assert(map_actuator_refuse(false, false, false, 0).remaining_s == 0);
}

static void test_actuator_aggregate_keeps_first_refuse_regardless_of_order() {
  // HP1 refused, HP2 idle: the HP1 block survives HP2 being processed after it.
  const Status hp1_blocked{HP_RESTART, 60};
  assert(aggregate_actuator_slot(hp1_blocked, Status{}).reason == HP_RESTART);
  assert(aggregate_actuator_slot(hp1_blocked, Status{}).remaining_s == 60);
  // Mirrored: HP1 idle, HP2 refused (HP2 as owner).
  const Status hp2_blocked{START_LIMIT, 420};
  assert(aggregate_actuator_slot(Status{}, hp2_blocked).reason == START_LIMIT);
  assert(aggregate_actuator_slot(Status{}, hp2_blocked).remaining_s == 420);
  assert(aggregate_actuator_slot(Status{}, Status{}).reason == NONE);
  // Single-HP builds aggregate the one slot they have.
  assert(aggregate_actuator_slot(hp1_blocked).reason == HP_RESTART);
}

static DispatchInput inhibited_input(uint32_t hp1_remaining_ms, bool hp1_must_stop = false) {
  DispatchInput in = active_input();
  // Inhibited HPs surface as unavailable candidates (requests are zeroed
  // upstream), so the candidate is dead while the inhibit flags name it.
  in.hp1.candidate = {0, false, hp1_must_stop, false};
  in.hp1_startup_inhibited = true;
  in.hp1_startup_remaining_ms = hp1_remaining_ms;
  return in;
}

static void test_owner_inhibited_reports_startup_without_rerouting() {
  // End-to-end pipeline contract: healthy, available HP whose live startup
  // guard is still active. Dispatch still picks it (routing untouched), but
  // thermal-request zeroes inhibited HPs downstream, so the published status
  // must be Startup inhibit with the guard's own remaining time.
  DispatchState state;
  auto in = active_input();
  in.hp1_startup_inhibited = true;
  in.hp1_startup_remaining_ms = 180000U;
  const auto out = update_dispatch(in, state);
  assert(!out.start_blocked);
  assert(out.owner == 1);
  assert(out.hp1_request > 0);
  assert(out.start_status_reason == STARTUP_INHIBIT);
  assert(out.start_status_remaining_s == 180);
}

static void test_owner_inhibited_duo_reports_inhibited_owner() {
  DispatchState state;
  auto in = active_input();
  in.duo = true;
  in.hp2.candidate = {0, true, false, false};
  in.hp2.has_allowed_level = true;
  in.stored_owner = 2;
  in.hp2_startup_inhibited = true;
  in.hp2_startup_remaining_ms = 210000U;
  const auto out = update_dispatch(in, state);
  assert(!out.start_blocked);
  assert(out.owner == 2);
  assert(out.start_status_reason == STARTUP_INHIBIT);
  assert(out.start_status_remaining_s == 210);
}

static void test_startup_inhibit_reports_real_remaining() {
  DispatchState state;
  auto in = inhibited_input(180000U);
  const auto out = update_dispatch(in, state);
  assert(out.start_blocked);
  assert(out.start_status_reason == STARTUP_INHIBIT);
  assert(out.start_status_remaining_s == 180);
}

static void test_startup_duo_reports_earliest_deployable_hp() {
  DispatchState state;
  auto in = inhibited_input(30000U);
  in.duo = true;
  in.hp2.candidate = {0, false, false, false};
  in.hp2.has_allowed_level = true;
  in.hp2_startup_inhibited = true;
  in.hp2_startup_remaining_ms = 190000U;
  // HP1 frees up after 30 s and can otherwise serve: no 3:10 display.
  const auto out = update_dispatch(in, state);
  assert(out.start_blocked);
  assert(out.start_status_reason == STARTUP_INHIBIT);
  assert(out.start_status_remaining_s == 30);
}

static void test_startup_skips_hp_that_could_never_serve() {
  DispatchState state;
  auto in = inhibited_input(30000U, true);
  in.duo = true;
  in.hp2.candidate = {0, false, false, false};
  in.hp2.has_allowed_level = true;
  in.hp2_startup_inhibited = true;
  in.hp2_startup_remaining_ms = 190000U;
  // HP1 inhibited but must-stop: HP2's 190 s is the honest countdown.
  const auto out = update_dispatch(in, state);
  assert(out.start_blocked);
  assert(out.start_status_reason == STARTUP_INHIBIT);
  assert(out.start_status_remaining_s == 190);
}

static void test_startup_without_deployable_hp_has_no_countdown() {
  DispatchState state;
  auto in = inhibited_input(30000U, true);
  const auto out = update_dispatch(in, state);
  assert(out.start_blocked);
  assert(out.start_status_reason == STARTUP_INHIBIT);
  assert(out.start_status_remaining_s == 0);
}

static void test_merge_prefers_actuator_refuse() {
  assert(merge(HP_RESTART, 30, NONE, 0).reason == HP_RESTART);
  assert(merge(HP_RESTART, 30, NONE, 0).remaining_s == 30);
  assert(merge(NONE, 0, START_LIMIT, 420).reason == START_LIMIT);
  assert(merge(COOLING_MIN_OFF, 190, HP_RESTART, 60).reason == HP_RESTART);
  assert(merge(NONE, 0, NONE, 0).reason == NONE);
}

int main() {
  test_single_restart_reports_minimum_unblock();
  test_confirmation_wins_over_running_countdown();
  test_global_cooling_guard_counts_down();
  test_duo_reports_first_free_hp();
  test_duo_with_one_free_hp_is_no_block();
  test_duo_without_candidates_reports_other_without_countdown();
  test_running_hp_keeps_owner_and_reports_none();
  test_no_demand_reports_none();
  test_actuator_refuse_mapping();
  test_actuator_aggregate_keeps_first_refuse_regardless_of_order();
  test_owner_inhibited_reports_startup_without_rerouting();
  test_owner_inhibited_duo_reports_inhibited_owner();
  test_startup_inhibit_reports_real_remaining();
  test_startup_duo_reports_earliest_deployable_hp();
  test_startup_skips_hp_that_could_never_serve();
  test_startup_without_deployable_hp_has_no_countdown();
  test_merge_prefers_actuator_refuse();
  return 0;
}
