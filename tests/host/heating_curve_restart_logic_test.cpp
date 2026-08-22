#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/control/oq_heating_curve_logic.h"

namespace {

using oq_curve::evaluate_restart;
using oq_curve::reset_control_state;
using oq_curve::RESTART_NONE;
using oq_curve::RESTART_ROOM_DEMAND;
using oq_curve::RESTART_WATER_BAND;

constexpr float ROOM_SETPOINT_C = 20.5f;
constexpr float ROOM_OVERHEAT_OFF_C = 0.3f;
constexpr float ROOM_RESUME_HEAT_C = 0.05f;

void test_fresh_warm_room_blocks_water_restart() {
  const auto decision =
      evaluate_restart(true, false, false, true, 23.4f, ROOM_SETPOINT_C, ROOM_OVERHEAT_OFF_C, ROOM_RESUME_HEAT_C);

  assert(!decision.restart);
  assert(decision.blocked_by_room);
  assert(!decision.blocked_by_off_lock);
  assert(decision.reason == RESTART_NONE);
}

void test_fresh_warm_room_also_blocks_deep_undershoot() {
  const auto decision =
      evaluate_restart(true, true, true, true, 23.4f, ROOM_SETPOINT_C, ROOM_OVERHEAT_OFF_C, ROOM_RESUME_HEAT_C);

  assert(!decision.restart);
  assert(decision.blocked_by_room);
  assert(!decision.blocked_by_off_lock);
}

void test_fresh_room_demand_has_highest_priority() {
  const auto decision =
      evaluate_restart(false, false, true, true, 20.0f, ROOM_SETPOINT_C, ROOM_OVERHEAT_OFF_C, ROOM_RESUME_HEAT_C);

  assert(decision.restart);
  assert(!decision.blocked_by_room);
  assert(!decision.blocked_by_off_lock);
  assert(decision.reason == RESTART_ROOM_DEMAND);
}

void test_stale_warm_room_fails_open_to_water_restart() {
  const auto decision =
      evaluate_restart(true, false, false, false, 23.4f, ROOM_SETPOINT_C, ROOM_OVERHEAT_OFF_C, ROOM_RESUME_HEAT_C);

  assert(decision.restart);
  assert(!decision.blocked_by_room);
  assert(decision.reason == RESTART_WATER_BAND);
}

void test_stale_room_does_not_bypass_off_lock() {
  const auto decision =
      evaluate_restart(true, false, true, false, 19.0f, ROOM_SETPOINT_C, ROOM_OVERHEAT_OFF_C, ROOM_RESUME_HEAT_C);

  assert(!decision.restart);
  assert(!decision.blocked_by_room);
  assert(decision.blocked_by_off_lock);
}

void test_deep_undershoot_keeps_existing_off_lock_bypass_without_room_veto() {
  const auto decision =
      evaluate_restart(true, true, true, true, 20.5f, ROOM_SETPOINT_C, ROOM_OVERHEAT_OFF_C, ROOM_RESUME_HEAT_C);

  assert(decision.restart);
  assert(!decision.blocked_by_room);
  assert(!decision.blocked_by_off_lock);
  assert(decision.reason == RESTART_WATER_BAND);
}

void test_no_water_restart_does_not_report_a_room_block() {
  const auto decision =
      evaluate_restart(false, false, false, true, 23.4f, ROOM_SETPOINT_C, ROOM_OVERHEAT_OFF_C, ROOM_RESUME_HEAT_C);

  assert(!decision.restart);
  assert(!decision.blocked_by_room);
  assert(!decision.blocked_by_off_lock);
}

void test_non_finite_room_values_cannot_block_restart() {
  const auto decision =
      evaluate_restart(true, false, false, true, NAN, ROOM_SETPOINT_C, ROOM_OVERHEAT_OFF_C, ROOM_RESUME_HEAT_C);

  assert(decision.restart);
  assert(!decision.blocked_by_room);
  assert(decision.reason == RESTART_WATER_BAND);
}

void test_control_reset_clears_restart_diagnostics() {
  float demand_continuous = 12.0f;
  int demand_curve = 8;
  int demand_pre_guardrail = 7;
  bool heat_request_active = true;
  uint32_t stop_arm_ms = 100U;
  uint32_t off_since_ms = 200U;
  bool restart_inhibit_active = true;
  bool restart_blocked_by_room = true;
  int regime_code = 2;

  reset_control_state(demand_continuous, demand_curve, demand_pre_guardrail, heat_request_active, stop_arm_ms,
                      off_since_ms, restart_inhibit_active, restart_blocked_by_room, regime_code);

  assert(isnan(demand_continuous));
  assert(demand_curve == 0);
  assert(demand_pre_guardrail == 0);
  assert(!heat_request_active);
  assert(stop_arm_ms == 0U);
  assert(off_since_ms == 0U);
  assert(!restart_inhibit_active);
  assert(!restart_blocked_by_room);
  assert(regime_code == 0);
}

}  // namespace

int main() {
  test_fresh_warm_room_blocks_water_restart();
  test_fresh_warm_room_also_blocks_deep_undershoot();
  test_fresh_room_demand_has_highest_priority();
  test_stale_warm_room_fails_open_to_water_restart();
  test_stale_room_does_not_bypass_off_lock();
  test_deep_undershoot_keeps_existing_off_lock_bypass_without_room_veto();
  test_no_water_restart_does_not_report_a_room_block();
  test_non_finite_room_values_cannot_block_restart();
  test_control_reset_clears_restart_diagnostics();
  return 0;
}
