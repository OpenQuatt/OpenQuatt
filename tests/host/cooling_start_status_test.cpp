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

static void test_startup_override_names_inhibit_with_real_remaining() {
  // After a reboot the inhibited HP never reaches the actuator preflight, so
  // the dispatch OTHER verdict is resolved from the manager's own flags.
  assert(resolve_startup_override(true, false, 180000U, 0U).reason == STARTUP_INHIBIT);
  assert(resolve_startup_override(true, false, 180000U, 0U).remaining_s == 180);
  assert(resolve_startup_override(false, true, 0U, 210000U).reason == STARTUP_INHIBIT);
  assert(resolve_startup_override(false, true, 0U, 210000U).remaining_s == 210);
  // No inhibit: OTHER stands, without an invented countdown.
  assert(resolve_startup_override(false, false, 0U, 0U).reason == OTHER);
  assert(resolve_startup_override(false, false, 0U, 0U).remaining_s == 0);
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
  test_startup_override_names_inhibit_with_real_remaining();
  test_merge_prefers_actuator_refuse();
  return 0;
}
