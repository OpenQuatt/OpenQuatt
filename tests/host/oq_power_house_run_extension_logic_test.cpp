#include <assert.h>
#include <math.h>
#include <initializer_list>

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

void test_wait_suppresses_base_above_restart_threshold() {
  // Comfort stop at 21.0, compressor stopped, room 20.9 with 2500 W of modelled
  // house demand: WAIT must hold the normal request at 0 instead of passing
  // base through, otherwise the normal start logic restarts above the 20.8 C
  // hysteresis. At 20.8 C with house need the warm restart may go ahead.
  State wait;
  wait.phase = Phase::WAIT_WARM_RESTART;
  wait.cycle_armed = true;
  wait.last_setpoint_c = 20.5f;
  Input idle = base_input();
  idle.cycle_active = false;
  idle.actual_heating_active = false;
  idle.room_c = 20.9f;
  idle.base_requested_w = 2500.0f;
  const auto held = evaluate(idle, tuning(), wait);
  assert(held.next.phase == Phase::WAIT_WARM_RESTART);
  assert(held.force_comfort_stop);
  assert(!held.floor_active);
  assert(!held.warm_restart_intent);
  // An unexpectedly running compressor at 20.9 must not lift the hysteresis.
  Input running = idle;
  running.cycle_active = true;
  running.actual_heating_active = true;
  const auto held_running = evaluate(running, tuning(), wait);
  assert(held_running.next.phase == Phase::WAIT_WARM_RESTART);
  assert(held_running.force_comfort_stop);
  // Room at the restart threshold with house need: warm restart.
  Input cooled = idle;
  cooled.room_c = 20.8f;
  const auto restart = evaluate(cooled, tuning(), wait);
  assert(restart.next.phase == Phase::WARM_RESTART);
  assert(restart.warm_restart_intent);
  // A run already going at/below the restart threshold is a normal takeover.
  Input takeover = running;
  takeover.room_c = 20.7f;
  assert(evaluate(takeover, tuning(), wait).next.phase == Phase::RUNNING);
  // Room back above the stop with the compressor running re-latches the stop.
  Input overrun = running;
  overrun.room_c = 21.0f;
  const auto relatch = evaluate(overrun, tuning(), wait);
  assert(relatch.next.phase == Phase::COMFORT_STOP);
  assert(relatch.force_comfort_stop);
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

void test_setpoint_drop_comfort_stop_does_not_arm_restart() {
  State extending;
  extending.phase = Phase::EXTENDING;
  extending.cycle_armed = true;
  extending.last_setpoint_c = 20.5f;
  Input lowered = base_input();
  lowered.setpoint_c = 19.5f;
  lowered.room_c = 20.4f;
  lowered.base_requested_w = 700.0f;
  const auto stopping = evaluate(lowered, tuning(), extending);
  assert(stopping.next.phase == Phase::COMFORT_STOP);
  assert(stopping.force_comfort_stop);
  assert(!stopping.next.cycle_armed);

  lowered.actual_heating_active = false;
  lowered.cycle_active = false;
  const auto stopped = evaluate(lowered, tuning(), stopping.next);
  assert(stopped.next.phase == Phase::INACTIVE);
  assert(!stopped.warm_restart_intent);
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

void test_configurable_restart_and_cold_edge() {
  Input in = base_input();
  in.setpoint_c = 21.0f;
  in.cycle_active = false;
  in.actual_heating_active = false;
  State wait{Phase::WAIT_WARM_RESTART, 21.0f, true};
  for (const auto settings :
       {Tuning{0.7f, 0.2f, 0.2f}, Tuning{0.7f, 0.7f, 0.2f}, Tuning{0.7f, 0.9f, 0.2f}, Tuning{0.7f, 1.2f, 0.2f}}) {
    const float expected = std::max(21.7f - settings.restart_cooldown_c, 20.8f);
    in.room_c = expected + 0.01f;
    in.base_requested_w = 2500.0f;
    const auto held = evaluate(in, settings, wait);
    assert(fabsf(held.warm_restart_c - expected) < 0.001f);
    assert(held.force_comfort_stop && !held.warm_restart_intent);
    in.room_c = held.warm_restart_c;
    in.base_requested_w = 0.0f;
    assert(!evaluate(in, settings, wait).warm_restart_intent);
    in.base_requested_w = 900.0f;
    const auto restart = evaluate(in, settings, wait);
    assert(restart.warm_restart_intent && !restart.force_comfort_stop);
    assert(restart.floor_active);
  }
  // The new comfort guard intentionally wins even with the default cooldown.
  in.room_c = 21.0f;
  assert(evaluate(in, {0.1f, 0.2f, 0.0f}, wait).warm_restart_c == 21.0f);
}

void test_cold_room_intent_releases_wait() {
  Input in = base_input();
  in.cycle_active = false;
  in.actual_heating_active = false;
  in.room_c = oq_heat_intent::room_cold_edge_c(in.setpoint_c, 0.2f);
  in.base_requested_w = 0.0f;
  const Tuning settings{0.7f, 1.2f, 0.2f};
  State wait{Phase::WAIT_WARM_RESTART, in.setpoint_c, true};
  oq_heat_intent::Input room;
  room.now_ms = 1;
  room.strategy_active = room.heating_enable_valid = room.heating_enabled = true;
  room.room_fresh = room.setpoint_fresh = true;
  room.setpoint_source = 1;
  room.room_c = in.room_c;
  room.setpoint_c = in.setpoint_c;
  room.room_resume_delta_c = settings.room_resume_delta_c;
  room.room_confirm_ms = 10000;
  auto intent = oq_heat_intent::evaluate(room, {});
  assert(intent.room_condition && !intent.fast_start);
  assert(!evaluate(in, settings, wait).floor_active);
  room.now_ms += 10000;
  intent = oq_heat_intent::evaluate(room, intent.next);
  assert(intent.fast_start);
  // Runtime promotes normal confirmed room demand before capturing base demand.
  in.base_requested_w = in.minimum_viable_w;
  const auto out = evaluate(in, settings, wait);
  assert(out.warm_restart_intent && !out.force_comfort_stop);
  assert(!out.floor_active);
}

void test_restart_setting_changes_and_failure_boundaries() {
  Input in = base_input();
  in.cycle_active = false;
  in.actual_heating_active = false;
  in.room_c = 20.7f;
  State wait{Phase::WAIT_WARM_RESTART, in.setpoint_c, true};
  assert(evaluate(in, {0.5f, 0.2f}, wait).warm_restart_intent);
  assert(evaluate(in, {0.5f, 0.7f}, wait).force_comfort_stop);
  State restart{Phase::WARM_RESTART, in.setpoint_c, true};
  // A permitted restart keeps its existing latch across temperature/settings jitter.
  assert(evaluate(in, {0.5f, 0.7f}, restart).warm_restart_intent);
  for (const auto phase : {Phase::EXTENDING, Phase::WAIT_WARM_RESTART, Phase::WARM_RESTART}) {
    State state{phase, in.setpoint_c, true};
    Input invalid = in;
    invalid.inputs_valid = false;
    const auto stale = evaluate(invalid, tuning(), state);
    assert(!stale.floor_active && !stale.warm_restart_intent && !stale.next.cycle_armed);
    invalid = in;
    invalid.heating_allowed = false;
    assert(!evaluate(invalid, tuning(), state).next.cycle_armed);
    invalid = in;
    invalid.enabled = false;
    const auto disabled = evaluate(invalid, tuning(), state);
    assert(disabled.next.phase == Phase::INACTIVE && !disabled.floor_active && !disabled.warm_restart_intent);
  }
  for (const bool water_limited : {false, true}) {
    Input limited = in;
    if (water_limited)
      limited.water_limit_factor = 0.8f;
    else
      limited.minimum_viable_w = NAN;
    assert(!evaluate(limited, tuning(), wait).floor_active);
    assert(!evaluate(limited, tuning(), restart).floor_active);
  }
}

void test_same_logic_with_three_emitter_lags() {
  // Synthetic thermal smoke test, not calibrated emitter-specific control modes.
  for (const float lag_s : {300.0f, 1200.0f, 3600.0f}) {
    for (const float cooldown_c : {0.2f, 0.9f, 1.2f}) {
      const Tuning settings{0.7f, cooldown_c, 0.2f};
      State state;
      float room_c = 20.8f;
      float emitted_w = 0.0f;
      bool running = true;
      int stopped_s = 240;
      bool saw_stop = false, saw_restart = false;
      for (int tick = 0; tick < 2880; ++tick) {
        Input in = base_input();
        in.setpoint_c = 21.0f;
        in.room_c = room_c;
        in.cycle_active = in.actual_heating_active = running;
        in.base_requested_w = std::max(0.0f, 1400.0f + 3000.0f * (21.0f - room_c));
        const auto out = evaluate(in, settings, state);
        assert(out.warm_restart_c >= oq_heat_intent::room_cold_edge_c(21.0f, 0.2f));
        if (state.phase == Phase::WAIT_WARM_RESTART && room_c <= 20.8f && in.base_requested_w > 0.0f)
          assert(!out.force_comfort_stop);
        saw_stop |= out.next.phase == Phase::COMFORT_STOP;
        saw_restart |= out.warm_restart_intent;
        const float request_w = out.force_comfort_stop ? 0.0f : std::max(in.base_requested_w, out.floor_w);
        if (request_w == 0.0f)
          running = false;
        else if (running || stopped_s >= 240)
          running = true;
        stopped_s = running ? 0 : stopped_s + 60;
        emitted_w += (running ? std::max(request_w, 1700.0f) - emitted_w : -emitted_w) * (60.0f / lag_s);
        room_c += (emitted_w - 1400.0f) * 60.0f / 10000000.0f;
        assert(std::isfinite(room_c) && room_c > 19.0f && room_c < 23.0f);
        state = out.next;
      }
      assert(saw_stop && saw_restart);
    }
  }
}
}  // namespace

int main() {
  test_configurable_restart_and_cold_edge();
  test_cold_room_intent_releases_wait();
  test_restart_setting_changes_and_failure_boundaries();
  test_same_logic_with_three_emitter_lags();
  test_disabled_keeps_old_behavior();
  test_enabled_while_idle_no_start();
  test_normal_run_above_pmin_no_floor();
  test_active_run_below_pmin_floor();
  test_extending_allows_zero_base();
  test_comfort_stop_at_threshold();
  test_comfort_stop_latched_while_compressor_active();
  test_comfort_stop_transitions_to_wait();
  test_wait_needs_cooldown_and_base();
  test_wait_suppresses_base_above_restart_threshold();
  test_warm_restart_keeps_intent_on_jitter();
  test_warm_restart_reverts_when_base_drops_to_zero();
  test_warm_restart_confirmed_run_allows_zero_base();
  test_warm_restart_hands_back_to_run();
  test_disable_clears_extending();
  test_stale_inputs_fail_closed();
  test_setpoint_drop_cancels_restart();
  test_setpoint_drop_comfort_stop_does_not_arm_restart();
  test_water_limit_blocks_floor();
  test_missing_pmin_no_floor();
  test_house_deficit_ignores_comfort_floor();
  test_no_restart_after_reboot_reset();
  return 0;
}
