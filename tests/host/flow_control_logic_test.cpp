#include <assert.h>
#include <math.h>
#include <string.h>

#include "../../openquatt/includes/control/oq_flow_control_logic.h"

namespace {

using namespace oq_flow_control;

void test_stale_zero_should_not_cause_dip() {
  // Repro for #464: PV 0 during 20s hold (2 ticks dt=10), then 824 at first PI.
  // Old logic seeded sp_f=0 -> ramp to 250 -> e=-574 -> iPWM 400->452.
  // Fixed logic keeps sp_f=NAN during hold -> seeds to 824 -> small error.
  State s;
  s.startup_hold = 2;
  s.sp_f = NAN;
  s.integral = 0;
  s.last_e = 0;

  PiInputs in{};
  in.sp_target = 800.0f;
  in.kp = 0.09f;
  in.ki = 0.0012f;
  in.dt = 10.0f;
  in.pwm_seed = 400;

  // Two hold ticks with pv 0 (stale)
  in.pv = 0.0f;
  auto r1 = update_pi(s, in);
  assert(r1.in_startup_hold);
  assert(isnan(s.sp_f));  // fix: remains NAN
  assert(r1.pwm == 400);
  assert(s.startup_hold == 1);

  auto r2 = update_pi(s, in);
  assert(r2.in_startup_hold);
  assert(isnan(s.sp_f));
  assert(r2.pwm == 400);
  assert(s.startup_hold == 0);

  // First real PI with actual flow ~824
  in.pv = 824.0f;
  auto r3 = update_pi(s, in);
  assert(!r3.in_startup_hold);
  assert(!r3.failsafe);
  // sp_f should have been seeded to pv (824) then ramped towards 800 -> 800? or 824->800 ramp limited but close
  // With pv 824, sp_target 800, sp_f seeded to 824, then ramp down by 15*10=150 towards 800 => sp_f=800? Actually
  // d=800-824=-24, max_step=150, so sp_f=800 Then e = 800-824 = -24 -> deadband? 24 outside deadband 10 -> e=-24 u =
  // kp*e = -2.16 plus small integral, limited to -80 (u_down 80) -> pwm = 400 - (-2) = ~402 (slightly softer) Crucially
  // NOT 452.
  assert(r3.pwm < 430);  // no large 452 jump
  assert(r3.pwm > 380 && r3.pwm < 430);
  // Extra tick should stay stable around 400-420, not drift to 477
  in.pwm_seed = r3.pwm;
  in.pv = 816.0f;
  auto r4 = update_pi(s, in);
  assert(r4.pwm < 430);
}

void test_low_flow_should_pump_harder() {
  // Countercase 1: truly low flow at end of hold should pump harder (lower iPWM)
  State s;
  s.startup_hold = 2;
  s.sp_f = NAN;
  PiInputs in{};
  in.sp_target = 800;
  in.kp = 0.09f;
  in.ki = 0.0012f;
  in.dt = 10;
  in.pwm_seed = 400;
  in.pv = 0;
  update_pi(s, in);
  update_pi(s, in);
  // Now pv 350 (low)
  in.pv = 350.0f;
  auto r = update_pi(s, in);
  assert(!r.failsafe);
  // e = sp_f - pv, sp_f seeded to 350 then ramp to 600 (250 step) => e ~250 -> positive -> u positive -> pwm = 400 -
  // positive = <400 (harder)
  assert(r.pwm < 400);
  assert(r.pwm >= 50);
}

void test_pv_around_target_no_big_step() {
  State s;
  s.startup_hold = 0;
  s.sp_f = 800;  // already at target
  s.integral = 0;
  PiInputs in{};
  in.sp_target = 800;
  in.pv = 805;
  in.kp = 0.09f;
  in.ki = 0.0012f;
  in.dt = 10;
  in.pwm_seed = 410;
  auto r = update_pi(s, in);
  // e = 800-805 = -5 -> deadband -> 0 -> u ~0 -> pwm unchanged
  assert(r.pwm == 410 || r.pwm == 409 || r.pwm == 411);
}

void test_nan_flow_failsafe() {
  State s;
  s.startup_hold = 0;
  s.sp_f = 400;
  PiInputs in{};
  in.sp_target = 800;
  in.pv = NAN;
  in.kp = 0.09f;
  in.ki = 0.0012f;
  in.dt = 10;
  in.pwm_seed = 400;
  auto r = update_pi(s, in);
  assert(r.failsafe);
  assert(r.pwm == 850);
  assert(isnan(s.sp_f));
}

void test_compute_start_pwm() {
  // CM100 commissioning always uses commissioning PWM (400), not last_good
  int start = compute_start_pwm(true, 400, 440, false, 460);
  assert(start == 400);
  int start_cooling = compute_start_pwm(true, 400, 440, true, 460);
  assert(start_cooling == 400);
  // Outside CM100 uses last_good per bank
  int start_auto = compute_start_pwm(false, 400, 440, false, 460);
  assert(start_auto == 440);
  int start_auto_cooling = compute_start_pwm(false, 400, 440, true, 460);
  assert(start_auto_cooling == 460);
  // Invalid last_good falls back to the fixed C++ default.
  int start_fallback = compute_start_pwm(false, 400, 0, false, 460);
  assert(start_fallback == kAutoStartFallbackIpwm);
  int start_fallback_cooling = compute_start_pwm(false, 400, 440, true, 900);
  assert(start_fallback_cooling == kAutoStartFallbackIpwm);
  int start_fallback_comm = compute_start_pwm(true, 400, 0, false, 460);
  assert(start_fallback_comm == 400);
}

void test_local_flow_selection() {
  assert(select_local_flow(false, 720.0f, 400.0f, 150.0f) == 720.0f);
  assert(isnan(select_local_flow(false, NAN, 400.0f, 150.0f)));
  assert(select_local_flow(true, 700.0f, 720.0f, 150.0f) == 710.0f);
  assert(select_local_flow(true, 0.0f, 720.0f, 150.0f) == 720.0f);
  assert(select_local_flow(true, NAN, 720.0f, 150.0f) == 720.0f);
  assert(select_local_flow(true, 700.0f, NAN, 150.0f) == 700.0f);
  assert(isnan(select_local_flow(true, NAN, NAN, 150.0f)));
}

void test_normal_cooling_and_manual_flow_setpoint_selection() {
  assert(!uses_cooling_setpoint(2, false, 0, 0));
  assert(uses_cooling_setpoint(5, false, 0, 0));
  assert(uses_cooling_setpoint(100, true, 1, 0));
  assert(uses_cooling_setpoint(100, true, 0, 1));
  assert(!uses_cooling_setpoint(100, true, 2, 2));

  assert(select_flow_setpoint(false, 500.0f, false, 900.0f, 800.0f) == 800.0f);
  assert(select_flow_setpoint(false, 500.0f, true, 900.0f, 800.0f) == 900.0f);
  assert(select_flow_setpoint(true, 500.0f, true, 900.0f, 800.0f) == 500.0f);
}

void test_flow_mismatch_hold_hysteresis_and_pump_gate() {
  const oq_flow::PumpRelayState running{true, true};
  const oq_flow::PumpRelayState stopped{true, false};
  MismatchState state;
  MismatchInputs inputs{true, running, running, 500.0f, 800.0f, 150.0f, 25.0f, 1000, 30000};

  assert(!update_mismatch(state, inputs));
  inputs.now_ms = 30999;
  assert(!update_mismatch(state, inputs));
  inputs.now_ms = 31000;
  assert(update_mismatch(state, inputs));

  inputs.hp2_flow_lph = 630.0f;  // Difference 130: above off threshold 125.
  assert(update_mismatch(state, inputs));
  inputs.hp2_flow_lph = 620.0f;  // Difference 120: below off threshold.
  assert(!update_mismatch(state, inputs));

  inputs.hp1_pump = stopped;
  inputs.hp2_pump = stopped;
  inputs.hp2_flow_lph = 800.0f;
  assert(!update_mismatch(state, inputs));
  assert(!state.timer_running);
}

void test_flow_mismatch_hold_is_rollover_safe() {
  const oq_flow::PumpRelayState running{true, true};
  MismatchState state;
  MismatchInputs inputs{true, running, running, 500.0f, 800.0f, 150.0f, 25.0f, 0xFFFFFF00UL, 1000};
  assert(!update_mismatch(state, inputs));
  inputs.now_ms = 0x000002E7UL;
  assert(!update_mismatch(state, inputs));
  inputs.now_ms = 0x000002E8UL;
  assert(update_mismatch(state, inputs));
}

void test_execution_mode_labels_are_stable() {
  assert(strcmp(execution_mode_text(ExecutionMode::AUTO), "AUTO") == 0);
  assert(strcmp(execution_mode_text(ExecutionMode::AUTO_FAILSAFE), "AUTO (failsafe)") == 0);
  assert(strcmp(execution_mode_text(ExecutionMode::MANUAL_FLOW), "MANUAL FLOW") == 0);
  assert(strcmp(execution_mode_text(ExecutionMode::CM100_IDLE), "CM100 idle") == 0);
}

}  // namespace

int main() {
  test_stale_zero_should_not_cause_dip();
  test_low_flow_should_pump_harder();
  test_pv_around_target_no_big_step();
  test_nan_flow_failsafe();
  test_compute_start_pwm();
  test_local_flow_selection();
  test_normal_cooling_and_manual_flow_setpoint_selection();
  test_flow_mismatch_hold_hysteresis_and_pump_gate();
  test_flow_mismatch_hold_is_rollover_safe();
  test_execution_mode_labels_are_stable();
  return 0;
}
