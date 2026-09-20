#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/control/oq_power_house_run_extension_logic.h"

namespace {
using namespace oq_power_house_run_extension;

Input base_input() {
  Input in;
  in.enabled = true;
  in.cycle_active = true;
  in.actual_heating_active = true;
  in.inputs_valid = true;
  in.heating_allowed = true;
  in.room_c = 20.4f;
  in.setpoint_c = 20.5f;
  in.base_requested_w = 900.0f;
  in.minimum_viable_w = 1700.0f;
  in.water_limit_factor = 1.0f;
  return in;
}

Tuning tuning() { return {0.5f, 0.2f}; }

void test_disabled_keeps_old_behavior() {
  Input in = base_input();
  in.enabled = false;
  const auto out = evaluate(in, tuning(), {});
  assert(out.next.phase == Phase::INACTIVE);
  assert(!out.floor_active && !out.force_comfort_stop && !out.warm_restart_intent);
}

void test_enabled_while_idle_no_start() {
  Input in = base_input();
  in.cycle_active = false;
  in.actual_heating_active = false;
  in.base_requested_w = 0.0f;
  const auto out = evaluate(in, tuning(), {});
  assert(out.next.phase == Phase::INACTIVE);
  assert(!out.floor_active);
}

void test_normal_run_above_pmin_no_floor() {
  Input in = base_input();
  in.base_requested_w = 2500.0f;
  State state;
  state.phase = Phase::RUNNING;
  state.cycle_armed = true;
  state.last_setpoint_c = 20.5f;
  const auto out = evaluate(in, tuning(), state);
  assert(out.next.phase == Phase::RUNNING);
  assert(!out.floor_active);
}

void test_active_run_below_pmin_floor() {
  Input in = base_input();
  State armed;
  armed.phase = Phase::RUNNING;
  armed.cycle_armed = true;
  armed.last_setpoint_c = 20.5f;
  const auto out = evaluate(in, tuning(), armed);
  assert(out.next.phase == Phase::EXTENDING);
  assert(out.floor_active && fabsf(out.floor_w - 1700.0f) < 0.01f);
}

void test_extending_allows_zero_base() {
  Input in = base_input();
  in.base_requested_w = 0.0f;
  State state;
  state.phase = Phase::EXTENDING;
  state.cycle_armed = true;
  state.last_setpoint_c = 20.5f;
  const auto out = evaluate(in, tuning(), state);
  assert(out.next.phase == Phase::EXTENDING);
  assert(out.floor_active);
}

void test_comfort_stop_at_threshold() {
  Input in = base_input();
  in.room_c = 21.0f;
  State state;
  state.phase = Phase::EXTENDING;
  state.cycle_armed = true;
  state.last_setpoint_c = 20.5f;
  const auto out = evaluate(in, tuning(), state);
  assert(out.next.phase == Phase::COMFORT_STOP);
  assert(out.force_comfort_stop);
}

void test_comfort_stop_latched_while_compressor_active() {
  // 21.0 comfort stop, compressor still spinning down, quantised dip to 20.9:
  // the stop must not be aborted and the 0.2 K hysteresis must not be bypassed.
  State state;
  state.phase = Phase::COMFORT_STOP;
  state.cycle_armed = true;
  state.last_setpoint_c = 20.5f;
  Input in = base_input();
  in.room_c = 20.9f;
  in.base_requested_w = 700.0f;
  const auto out = evaluate(in, tuning(), state);
  assert(out.next.phase == Phase::COMFORT_STOP);
  assert(out.force_comfort_stop);
  assert(!out.warm_restart_intent);
}

void test_comfort_stop_transitions_to_wait() {
  State state;
  state.phase = Phase::COMFORT_STOP;
  state.cycle_armed = true;
  state.last_setpoint_c = 20.5f;
  Input in = base_input();
  in.actual_heating_active = false;
  in.cycle_active = false;
  const auto out = evaluate(in, tuning(), state);
  assert(out.next.phase == Phase::WAIT_WARM_RESTART);
  assert(out.force_comfort_stop);
}

void test_wait_needs_cooldown_and_base() {
  State wait;
  wait.phase = Phase::WAIT_WARM_RESTART;
  wait.cycle_armed = true;
  wait.last_setpoint_c = 20.5f;
  Input warm = base_input();
  warm.cycle_active = false;
  warm.actual_heating_active = false;
  warm.room_c = 20.9f;
  warm.base_requested_w = 700.0f;
  assert(evaluate(warm, tuning(), wait).next.phase == Phase::WAIT_WARM_RESTART);
  warm.room_c = 20.8f;
  warm.base_requested_w = 0.0f;
  assert(evaluate(warm, tuning(), wait).next.phase == Phase::WAIT_WARM_RESTART);
  warm.base_requested_w = 700.0f;
  const auto out = evaluate(warm, tuning(), wait);
  assert(out.next.phase == Phase::WARM_RESTART);
  assert(out.warm_restart_intent);
  assert(out.floor_active && fabsf(out.floor_w - 1700.0f) < 0.01f);
}

void test_warm_restart_keeps_intent_on_jitter() {
  State restart;
  restart.phase = Phase::WARM_RESTART;
  restart.cycle_armed = true;
  restart.last_setpoint_c = 20.5f;
  Input in = base_input();
  in.cycle_active = false;
  in.actual_heating_active = false;
  in.room_c = 20.85f;
  in.base_requested_w = 700.0f;
  const auto out = evaluate(in, tuning(), restart);
  assert(out.next.phase == Phase::WARM_RESTART);
  assert(out.warm_restart_intent);
}

void test_warm_restart_hands_back_to_run() {
  State restart;
  restart.phase = Phase::WARM_RESTART;
  restart.cycle_armed = true;
  restart.last_setpoint_c = 20.5f;
  Input in = base_input();
  in.room_c = 20.7f;
  in.base_requested_w = 700.0f;
  const auto out = evaluate(in, tuning(), restart);
  assert(out.next.phase == Phase::EXTENDING);
}

void test_warm_restart_reverts_when_base_drops_to_zero() {
  // Requested restart during minimum off-time: if house need falls to zero
  // before the compressor runs again, drop the intent and the floor instead of
  // forcing Pmin into a start.
  State restart;
  restart.phase = Phase::WARM_RESTART;
  restart.cycle_armed = true;
  restart.last_setpoint_c = 20.5f;
  Input in = base_input();
  in.cycle_active = false;
  in.actual_heating_active = false;
  in.room_c = 20.7f;
  in.base_requested_w = 0.0f;
  const auto out = evaluate(in, tuning(), restart);
  assert(out.next.phase == Phase::WAIT_WARM_RESTART);
  assert(!out.warm_restart_intent);
  assert(!out.floor_active);
}

void test_warm_restart_confirmed_run_allows_zero_base() {
  // Once the new run is actually going, base == 0 may extend again.
  State restart;
  restart.phase = Phase::WARM_RESTART;
  restart.cycle_armed = true;
  restart.last_setpoint_c = 20.5f;
  Input in = base_input();
  in.room_c = 20.7f;
  in.base_requested_w = 0.0f;
  const auto out = evaluate(in, tuning(), restart);
  assert(out.next.phase == Phase::EXTENDING);
  assert(out.floor_active);
}

void test_house_deficit_ignores_comfort_floor() {
  // CM3 invariant: the #608 floor must never count as house deficit.
  assert(compute_house_deficit_w(900.0f, 1500.0f, true, 800.0f) == 0.0f);
  assert(compute_house_deficit_w(4000.0f, 3000.0f, true, 0.0f) == 1000.0f);
  assert(compute_house_deficit_w(NAN, 1500.0f, true, 42.0f) == 42.0f);
  assert(compute_house_deficit_w(900.0f, NAN, true, 42.0f) == 42.0f);
  assert(compute_house_deficit_w(900.0f, 1500.0f, false, 42.0f) == 42.0f);
  assert(compute_house_saturated(5, 1000.0f));
  assert(!compute_house_saturated(5, 0.0f));
  assert(!compute_house_saturated(0, 1000.0f));
}

void test_disable_clears_extending() {
  Input in = base_input();
  in.enabled = false;
  State state;
  state.phase = Phase::EXTENDING;
  state.cycle_armed = true;
  const auto out = evaluate(in, tuning(), state);
  assert(out.next.phase == Phase::INACTIVE);
  assert(!out.floor_active);
}

void test_stale_inputs_fail_closed() {
  State state;
  state.phase = Phase::EXTENDING;
  state.cycle_armed = true;
  Input in = base_input();
  in.inputs_valid = false;
  assert(evaluate(in, tuning(), state).next.phase == Phase::INACTIVE);
  State wait;
  wait.phase = Phase::WAIT_WARM_RESTART;
  wait.cycle_armed = true;
  assert(evaluate(in, tuning(), wait).next.phase == Phase::INACTIVE);
}

void test_setpoint_drop_cancels_restart() {
  State wait;
  wait.phase = Phase::WAIT_WARM_RESTART;
  wait.cycle_armed = true;
  wait.last_setpoint_c = 20.5f;
  Input in = base_input();
  in.cycle_active = false;
  in.actual_heating_active = false;
  in.setpoint_c = 19.5f;
  in.room_c = 19.3f;
  in.base_requested_w = 700.0f;
  assert(evaluate(in, tuning(), wait).next.phase == Phase::INACTIVE);
}

void test_water_limit_blocks_floor() {
  Input in = base_input();
  in.water_limit_factor = 0.8f;
  State state;
  state.phase = Phase::RUNNING;
  state.cycle_armed = true;
  state.last_setpoint_c = 20.5f;
  const auto out = evaluate(in, tuning(), state);
  assert(out.next.phase == Phase::RUNNING);
  assert(!out.floor_active);
}

void test_missing_pmin_no_floor() {
  Input in = base_input();
  in.minimum_viable_w = NAN;
  State state;
  state.phase = Phase::RUNNING;
  state.cycle_armed = true;
  const auto out = evaluate(in, tuning(), state);
  assert(!out.floor_active);
}

void test_no_restart_after_reboot_reset() {
  Input in = base_input();
  in.cycle_active = false;
  in.actual_heating_active = false;
  in.base_requested_w = 700.0f;
  in.room_c = 20.7f;
  const auto out = evaluate(in, tuning(), reset_state());
  assert(out.next.phase == Phase::INACTIVE);
  assert(!out.warm_restart_intent);
}
}  // namespace

int main() {
  test_disabled_keeps_old_behavior();
  test_enabled_while_idle_no_start();
  test_normal_run_above_pmin_no_floor();
  test_active_run_below_pmin_floor();
  test_extending_allows_zero_base();
  test_comfort_stop_at_threshold();
  test_comfort_stop_latched_while_compressor_active();
  test_comfort_stop_transitions_to_wait();
  test_wait_needs_cooldown_and_base();
  test_warm_restart_keeps_intent_on_jitter();
  test_warm_restart_reverts_when_base_drops_to_zero();
  test_warm_restart_confirmed_run_allows_zero_base();
  test_warm_restart_hands_back_to_run();
  test_disable_clears_extending();
  test_stale_inputs_fail_closed();
  test_setpoint_drop_cancels_restart();
  test_water_limit_blocks_floor();
  test_missing_pmin_no_floor();
  test_house_deficit_ignores_comfort_floor();
  test_no_restart_after_reboot_reset();
  return 0;
}
