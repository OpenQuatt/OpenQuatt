#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/control/oq_flow_delta_t_logic.h"

namespace {

using namespace oq_flow_delta_t;

constexpr uint32_t kTickMs = 10000u;

bool near(float a, float b, float tolerance = 0.5f) { return fabsf(a - b) <= tolerance; }

// One running heat pump in CM2 with a given temperature rise.
Input heating(uint32_t now_ms, float flow_lph, float rise_k) {
  Input in;
  in.now_ms = now_ms;
  in.enabled = true;
  in.heating_mode = true;
  in.unit_count = 1;
  in.units[0].heating = true;
  in.units[0].temps_fresh = true;
  in.units[0].in_c = 30.0f;
  in.units[0].out_c = 30.0f + rise_k;
  in.units[0].level = 3;
  in.flow_lph = flow_lph;
  in.base_setpoint_lph = 800.0f;
  in.target_k = 3.0f;
  in.min_lph = 500.0f;
  in.max_lph = 1200.0f;
  in.lowflow_threshold_lph = 400.0f;
  // The behaviour tests step every two minutes by up to 100 L/h; the defaults
  // (five minutes, 50 L/h) are covered by test_pacing_defaults_and_configuration.
  in.step_interval_min = 2.0f;
  in.max_step_lph = 100.0f;
  return in;
}

// Runs ticks while the measured flow follows the setpoint, as the PI would.
Output run(State& state, uint32_t& now_ms, int ticks, float rise_k, float flow_lph = NAN) {
  Output out;
  for (int i = 0; i < ticks; ++i) {
    const float flow = isfinite(flow_lph) ? flow_lph : (isfinite(state.setpoint_lph) ? state.setpoint_lph : 800.0f);
    out = decide(heating(now_ms, flow, rise_k), state);
    state = out.next;
    now_ms += kTickMs;
  }
  return out;
}

void test_off_and_not_heating_pass_the_fixed_setpoint_through() {
  State state;
  Input in = heating(0, 800.0f, 1.5f);
  in.enabled = false;
  Output out = decide(in, state);
  assert(!out.controlling && out.next.reason == Reason::OFF && out.setpoint_lph == 800.0f);

  in.enabled = true;
  in.heating_mode = false;  // cooling, preflow, boiler or service: untouched
  out = decide(in, state);
  assert(!out.controlling && out.next.reason == Reason::NOT_HEATING && out.setpoint_lph == 800.0f);

  in.heating_mode = true;
  in.units[0].heating = false;  // CM2 but the compressor is not running yet
  out = decide(in, state);
  assert(!out.controlling && out.next.reason == Reason::NOT_HEATING);
}

void test_new_cycle_starts_at_fixed_flow_and_settles() {
  State state;
  uint32_t now = 1000;
  Output out = run(state, now, 1, 1.5f);
  assert(out.controlling && out.next.reason == Reason::SETTLING);
  assert(out.setpoint_lph == 800.0f);
  // Five minutes of settling: no step, however far off the rise is.
  out = run(state, now, 29, 1.5f);
  assert(out.next.reason == Reason::SETTLING && out.setpoint_lph == 800.0f);
}

void test_low_rise_lowers_the_flow_in_bounded_steps_down_to_the_minimum() {
  State state;
  uint32_t now = 1000;
  run(state, now, 30, 1.5f);  // settle
  // First step: desired = 800 * 1.5 / 3 = 400, half the gap is -200, capped at -100.
  Output out = run(state, now, 12, 1.5f);
  assert(near(out.setpoint_lph, 700.0f));
  assert(out.next.reason == Reason::ADJUSTING);
  // Keeps stepping at most 100 L/h per two minutes and stops at the minimum (500).
  out = run(state, now, 12 * 10, 1.5f);
  assert(near(out.setpoint_lph, 500.0f));
  assert(out.next.reason == Reason::AT_MINIMUM);
}

void test_high_rise_raises_the_flow_up_to_the_maximum() {
  State state;
  uint32_t now = 1000;
  run(state, now, 30, 6.0f);
  Output out = run(state, now, 12, 6.0f);
  assert(near(out.setpoint_lph, 900.0f));  // +100 per step
  out = run(state, now, 12 * 10, 6.0f);
  assert(near(out.setpoint_lph, 1200.0f));
  assert(out.next.reason == Reason::AT_MAXIMUM);
}

void test_rise_within_deadband_holds_the_flow() {
  State state;
  uint32_t now = 1000;
  run(state, now, 30, 3.2f);
  Output out = run(state, now, 24, 3.2f);
  assert(out.setpoint_lph == 800.0f);
  assert(out.next.reason == Reason::ON_TARGET);
}

void test_maximum_can_never_sit_below_the_fixed_setpoint() {
  Input in = heating(0, 800.0f, 6.0f);
  in.max_lph = 500.0f;  // the dangerous setting: capped at 500 under full demand
  Band limits = band(in);
  assert(limits.valid && limits.max_lph == 800.0f);
  in.min_lph = 300.0f;  // below the low-flow fault threshold plus margin
  limits = band(in);
  assert(limits.min_lph == 450.0f);
  in.min_lph = 1400.0f;  // minimum above maximum is pulled down to it
  in.max_lph = 1000.0f;
  limits = band(in);
  assert(limits.min_lph == 1000.0f && limits.max_lph == 1000.0f);
}

void test_water_temperature_limit_goes_straight_to_maximum_flow() {
  State state;
  uint32_t now = 1000;
  run(state, now, 30, 1.5f);
  run(state, now, 12 * 4, 1.5f);  // walk the flow down first
  assert(state.setpoint_lph < 800.0f);
  Input in = heating(now, state.setpoint_lph, 1.5f);
  in.water_limit_active = true;
  Output out = decide(in, state);
  assert(out.controlling && out.next.reason == Reason::WATER_LIMIT);
  assert(out.setpoint_lph == 1200.0f);
  state = out.next;
  now += kTickMs;
  // Once the protection relaxes the flow stays up for a settle period.
  out = run(state, now, 29, 1.5f);
  assert(out.next.reason == Reason::SETTLING && out.setpoint_lph == 1200.0f);
}

// Ticks with a unit defrosting.
Output defrost(State& state, uint32_t& now_ms, int ticks) {
  Output out;
  for (int i = 0; i < ticks; ++i) {
    Input in = heating(now_ms, isfinite(state.setpoint_lph) ? state.setpoint_lph : 800.0f, -2.0f);
    in.units[0].defrost = true;
    out = decide(in, state);
    state = out.next;
    now_ms += kTickMs;
  }
  return out;
}

void test_defrost_never_runs_below_the_fixed_flow() {
  State state;
  uint32_t now = 1000;
  run(state, now, 30, 1.5f);
  run(state, now, 24, 1.5f);
  assert(state.setpoint_lph < 800.0f);
  // Low load sat below the fixed flow: defrost raises it to Flow Setpoint.
  Output out = defrost(state, now, 1);
  assert(out.controlling && out.next.reason == Reason::DEFROST && out.setpoint_lph == 800.0f);
  // Ten minutes after the defrost ends it still holds.
  out = run(state, now, 59, 1.5f);
  assert(out.next.reason == Reason::DEFROST && out.setpoint_lph == 800.0f);
  // Then a fresh cycle from that flow, settling first.
  out = run(state, now, 2, 1.5f);
  assert(out.controlling && out.next.reason == Reason::SETTLING && out.setpoint_lph == 800.0f);
}

void test_defrost_keeps_a_higher_flow_through_the_hold_and_restarts_from_it() {
  State state;
  uint32_t now = 1000;
  run(state, now, 30, 6.0f);
  run(state, now, 12 * 2, 6.0f);  // high load: 800 -> 1000
  assert(near(state.setpoint_lph, 1000.0f));
  // Lowering the flow would cool the heat exchanger further: it stays at 1000,
  // through a defrost that lasts five minutes and the hold after it.
  Output out = defrost(state, now, 30);
  assert(out.next.reason == Reason::DEFROST && near(out.setpoint_lph, 1000.0f));
  out = run(state, now, 59, 6.0f);
  assert(out.next.reason == Reason::DEFROST && near(out.setpoint_lph, 1000.0f));
  // The new cycle starts from 1000, not from the fixed 800.
  out = run(state, now, 2, 6.0f);
  assert(out.next.reason == Reason::SETTLING && near(out.setpoint_lph, 1000.0f));
  // A later cycle that starts without a defrost uses the fixed flow again.
  Input off = heating(now, 1000.0f, 6.0f);
  off.units[0].heating = false;
  state = decide(off, state).next;
  out = run(state, now, 1, 6.0f);
  assert(out.next.reason == Reason::SETTLING && out.setpoint_lph == 800.0f);
}

void test_leaving_heating_drops_a_defrost_hold() {
  // A hold left over from the last defrost of the season must not come back
  // after a long pause, when its deadline has long gone stale.
  State state;
  uint32_t now = 1000;
  run(state, now, 31, 3.0f);
  defrost(state, now, 1);
  assert(state.defrost_hold);
  Input in = heating(now, 800.0f, 3.0f);
  in.heating_mode = false;
  state = decide(in, state).next;
  assert(!state.defrost_hold);
  now += 0x80000000u;  // the season later
  Output out = run(state, now, 1, 3.0f);
  assert(out.next.reason == Reason::SETTLING);
}

void test_missing_temperatures_and_flow_fault_fall_back() {
  State state;
  Input in = heating(1000, 800.0f, 1.5f);
  in.units[0].temps_fresh = false;
  Output out = decide(in, state);
  assert(!out.controlling && out.next.reason == Reason::NO_DATA && out.setpoint_lph == 800.0f);
  in = heating(1000, 800.0f, 1.5f);
  in.flow_fault = true;
  out = decide(in, state);
  assert(!out.controlling && out.next.reason == Reason::FLOW_FAULT);
  in = heating(1000, NAN, 1.5f);
  out = decide(in, state);
  assert(!out.controlling && out.next.reason == Reason::NO_DATA);
}

void test_duo_averages_the_rise_per_running_unit() {
  State state;
  Input in = heating(1000, 800.0f, 2.0f);
  in.unit_count = 2;
  in.units[1].heating = true;
  in.units[1].temps_fresh = true;
  in.units[1].in_c = 32.0f;  // series: HP2 gets HP1's outlet
  in.units[1].out_c = 36.0f;
  in.units[1].level = 3;
  uint32_t now = 1000;
  Output out;
  for (int i = 0; i < 30 + 12; ++i) {
    in.now_ms = now;
    in.flow_lph = isfinite(state.setpoint_lph) ? state.setpoint_lph : 800.0f;
    out = decide(in, state);
    state = out.next;
    now += kTickMs;
  }
  assert(out.counted_units == 2);
  // Average rise (2 + 4) / 2 = 3 K: on target, not 6 K over both together.
  assert(out.next.reason == Reason::ON_TARGET && out.setpoint_lph == 800.0f);
  assert(near(out.next.last_rise_k, 3.0f, 0.01f));
}

void test_a_unit_joining_resettles_but_a_level_change_does_not() {
  State state;
  uint32_t now = 1000;
  run(state, now, 31, 3.0f);
  assert(!state.settling);
  // A level change only restarts the averaging window.
  Input in = heating(now, state.setpoint_lph, 3.0f);
  in.units[0].level = 5;
  Output out = decide(in, state);
  assert(!out.next.settling && out.next.rise_count <= 1);
  state = out.next;
  now += kTickMs;
  // The second unit joining settles again.
  in = heating(now, state.setpoint_lph, 3.0f);
  in.units[0].level = 5;
  in.unit_count = 2;
  in.units[1].heating = true;
  in.units[1].temps_fresh = true;
  in.units[1].in_c = 33.0f;
  in.units[1].out_c = 36.0f;
  out = decide(in, state);
  assert(out.next.settling && out.next.reason == Reason::SETTLING);
}

// Ticks where the pump delivers at most max_flow_lph, as a real pump would:
// the PI follows the setpoint below that, and is at full drive at it.
Output run_pump(State& state, uint32_t& now_ms, int ticks, float rise_k, float max_flow_lph) {
  Output out;
  for (int i = 0; i < ticks; ++i) {
    const float wanted = isfinite(state.setpoint_lph) ? state.setpoint_lph : 800.0f;
    Input in = heating(now_ms, fminf(wanted, max_flow_lph), rise_k);
    in.pump_at_full_drive = wanted > max_flow_lph;
    out = decide(in, state);
    state = out.next;
    now_ms += kTickMs;
  }
  return out;
}

void test_pump_limit_stops_asking_for_more() {
  State state;
  uint32_t now = 1000;
  run(state, now, 30, 6.0f, 800.0f);
  // The setpoint climbs, but the pump tops out at 900 L/h at full drive.
  Output out = run_pump(state, now, 12 * 6, 6.0f, 900.0f);
  assert(isfinite(out.next.ceiling_lph));
  assert(out.setpoint_lph <= 900.0f + kCeilingMarginLph + 0.5f);
  assert(out.next.reason == Reason::PUMP_LIMIT);
  // Valves open: the PI backs off from full drive and the ceiling lifts at once.
  Input in = heating(now, 950.0f, 6.0f);
  in.pump_at_full_drive = false;
  out = decide(in, state);
  assert(!isfinite(out.next.ceiling_lph));
}

void test_a_lagging_flow_with_drive_to_spare_is_not_a_pump_limit() {
  // HIL regression: driven to the minimum, then asked for more, the flow lags
  // the setpoint by 100 L/h while the pump still has drive to spare. That is
  // the PI catching up, not the limit of the installation.
  State state;
  uint32_t now = 1000;
  run(state, now, 30, 9.0f);
  run(state, now, 12 * 3, 1.0f);  // 800 -> 500
  assert(near(state.setpoint_lph, 500.0f));
  run(state, now, 12, 9.0f);  // 500 -> 600
  assert(near(state.setpoint_lph, 600.0f));
  Output out;
  for (int i = 0; i < 12 * 3; ++i) {
    Input in = heating(now, state.setpoint_lph - 100.0f, 9.0f);
    out = decide(in, state);
    state = out.next;
    now += kTickMs;
  }
  // No ceiling, and no further rise while the flow is still behind.
  assert(!isfinite(out.next.ceiling_lph) && near(out.setpoint_lph, 600.0f));
  // Once the flow catches up the climb resumes, all the way to the maximum.
  out = run(state, now, 12 * 8, 9.0f);
  assert(near(out.setpoint_lph, 1200.0f) && out.next.reason == Reason::AT_MAXIMUM);
}

void test_standing_aside_ends_the_cycle() {
  // Manual PWM, frost protection, idle or a service task took the tick: the
  // next heating tick must start afresh, not resume on old samples, setpoint
  // and ceiling.
  State state;
  uint32_t now = 1000;
  run(state, now, 30, 6.0f, 800.0f);
  run_pump(state, now, 12 * 6, 6.0f, 900.0f);  // ends a minute after a step: samples in the window
  assert(state.active && isfinite(state.ceiling_lph) && state.rise_count > 0);
  Output out = stand_aside(state, false);  // Manual PWM selected
  assert(!out.controlling && out.next.reason == Reason::OFF);
  assert(!out.next.active && !isfinite(out.next.ceiling_lph) && out.next.rise_count == 0);
  out = stand_aside(state, true);  // Delta T selected, frost protection or idle
  assert(out.next.reason == Reason::NOT_HEATING && !out.next.active);
  state = out.next;
  out = run(state, now, 1, 6.0f);
  assert(out.next.reason == Reason::SETTLING && out.setpoint_lph == 800.0f);
}

void test_the_first_minute_after_a_step_is_not_measured() {
  State state;
  uint32_t now = 1000;
  run(state, now, 30, 1.5f);
  run(state, now, 6, 1.5f);  // the first step lands on the sixth sample
  assert(near(state.setpoint_lph, 700.0f) && state.rise_count == 0);
  // The transition to the new flow: five more ticks, nothing counted.
  run(state, now, 5, 1.5f);
  assert(state.rise_count == 0);
  // From a minute after the step the samples count again.
  run(state, now, 1, 1.5f);
  assert(state.rise_count == 1);
  // Six samples later the next step lands, two minutes after the last one.
  Output out = run(state, now, 6, 1.5f);
  assert(near(out.setpoint_lph, 600.0f));
}

void test_long_uptime_does_not_freeze_the_controller() {
  // A cycle that starts just before millis() passes 2^31 and keeps running for
  // more than 24.8 days must keep stepping: a stale deadline compared with a
  // signed difference would otherwise read as "still settling".
  State state;
  uint32_t now = 0x7FFF0000u;
  run(state, now, 31, 3.0f);
  assert(!state.settling);
  now += 0x80000000u;  // jump ahead by 24.8 days without a unit change
  Output out = run(state, now, 13, 1.5f);
  assert(out.next.reason != Reason::SETTLING);
  assert(out.setpoint_lph < 800.0f);
}

void test_maximum_follows_the_configured_quatt_version() {
  // V2 runs 1000 L/h from the factory: a maximum of 900 is raised to it even
  // though the configured Flow Setpoint is only 800.
  Input in = heating(0, 800.0f, 6.0f);
  in.max_lph = 900.0f;
  in.standard_flow_lph = 1000.0f;
  Band limits = band(in);
  assert(limits.valid && limits.max_lph == 1000.0f);
  // V1.5 (800 L/h) leaves the same 900 maximum alone.
  in.standard_flow_lph = 800.0f;
  limits = band(in);
  assert(limits.max_lph == 900.0f);
  // A missing standard flow never invalidates the band.
  in.standard_flow_lph = NAN;
  limits = band(in);
  assert(limits.valid && limits.max_lph == 900.0f);
}

void test_warning_when_the_water_limit_persists_at_maximum_flow() {
  State state;
  uint32_t now = 1000;
  run(state, now, 31, 3.0f);
  Output out;
  // Four minutes of the protection cutting power: maximum flow, no warning yet.
  for (int i = 0; i < 24; ++i) {
    Input in = heating(now, 1200.0f, 3.0f);
    in.water_limit_active = true;
    out = decide(in, state);
    state = out.next;
    now += kTickMs;
  }
  assert(out.setpoint_lph == 1200.0f && !out.warning && out.next.reason == Reason::WATER_LIMIT);
  // Still cutting power after five minutes at maximum: more water did not help.
  for (int i = 0; i < 7; ++i) {
    Input in = heating(now, 1200.0f, 3.0f);
    in.water_limit_active = true;
    out = decide(in, state);
    state = out.next;
    now += kTickMs;
  }
  assert(out.warning && out.next.reason == Reason::LIMIT_AT_MAX);
  // The protection relaxes: the warning clears at once.
  out = decide(heating(now, 1200.0f, 3.0f), state);
  assert(!out.warning && !out.next.warning);
}

void test_duo_reports_the_rise_per_unit() {
  State state;
  Input in = heating(1000, 800.0f, 2.0f);
  in.unit_count = 2;
  in.units[1].heating = true;
  in.units[1].temps_fresh = true;
  in.units[1].in_c = 32.0f;
  in.units[1].out_c = 36.0f;
  in.units[1].level = 3;
  uint32_t now = 1000;
  Output out;
  for (int i = 0; i < 30 + 12; ++i) {
    in.now_ms = now;
    out = decide(in, state);
    state = out.next;
    now += kTickMs;
  }
  assert(near(out.next.last_unit_rise_k[0], 2.0f, 0.01f));
  assert(near(out.next.last_unit_rise_k[1], 4.0f, 0.01f));
  assert(near(out.next.last_rise_k, 3.0f, 0.01f));
  // HP2 stops: only HP1 is reported after the next step.
  in.units[1].heating = false;
  for (int i = 0; i < 30 + 12; ++i) {
    in.now_ms = now;
    out = decide(in, state);
    state = out.next;
    now += kTickMs;
  }
  assert(near(out.next.last_unit_rise_k[0], 2.0f, 0.01f) && isnan(out.next.last_unit_rise_k[1]));
}

// Ticks with the default pacing (5 min, 50 L/h) and a given dead band.
Output run_default(State& state, uint32_t& now_ms, int ticks, float rise_k, float deadband_k = kDefaultDeadbandK) {
  Output out;
  for (int i = 0; i < ticks; ++i) {
    Input in = heating(now_ms, isfinite(state.setpoint_lph) ? state.setpoint_lph : 800.0f, rise_k);
    in.step_interval_min = kDefaultStepIntervalMin;
    in.max_step_lph = kDefaultMaxStepLph;
    in.deadband_k = deadband_k;
    out = decide(in, state);
    state = out.next;
    now_ms += kTickMs;
  }
  return out;
}

void test_pacing_defaults_and_configuration() {
  // Defaults: one step per five minutes, at most 50 L/h, a 0.3 K dead band.
  Input in;
  Pacing pace = pacing(in);
  assert(pace.step_interval_ms == 300000u && pace.max_step_lph == 50.0f && near(pace.deadband_k, 0.3f, 0.001f));
  // Out-of-range values are held to bounds; a missing one falls back to its default.
  in.step_interval_min = 0.0f;
  in.max_step_lph = 1000.0f;
  in.deadband_k = NAN;
  pace = pacing(in);
  assert(pace.step_interval_ms == 120000u && pace.max_step_lph == 200.0f && near(pace.deadband_k, 0.3f, 0.001f));

  // With the defaults the first step lands five minutes in, once a minute of
  // samples is gathered after settling, and moves 50 L/h.
  State state;
  uint32_t now = 1000;
  Output out = run_default(state, now, 36, 1.5f);
  assert(near(out.setpoint_lph, 750.0f));
  // Nothing more until the next five minutes have passed...
  out = run_default(state, now, 29, 1.5f);
  assert(near(out.setpoint_lph, 750.0f));
  // ...then the next 50 L/h step.
  out = run_default(state, now, 1, 1.5f);
  assert(near(out.setpoint_lph, 700.0f));

  // A wider dead band holds where the default would adjust: 3.4 K is 0.4 K off.
  State held;
  now = 1000;
  out = run_default(held, now, 36, 3.4f, 0.5f);
  assert(out.next.reason == Reason::ON_TARGET && out.setpoint_lph == 800.0f);
  State moved;
  now = 1000;
  out = run_default(moved, now, 36, 3.4f);
  assert(out.next.reason != Reason::ON_TARGET && out.setpoint_lph > 800.0f);
}

void test_oil_return_holds_the_flow_and_discards_its_samples() {
  State state;
  uint32_t now = 1000;
  run(state, now, 30, 1.5f);  // settle
  run(state, now, 12, 1.5f);  // first step: 800 -> 700
  assert(near(state.setpoint_lph, 700.0f));
  // Three minutes of oil return with a rise that would otherwise drive the flow up hard.
  Output out;
  for (int i = 0; i < 18; ++i) {
    Input in = heating(now, state.setpoint_lph, 9.0f);
    in.units[0].oil_return = true;
    out = decide(in, state);
    state = out.next;
    now += kTickMs;
    assert(out.controlling && out.next.reason == Reason::OIL_RETURN && near(out.setpoint_lph, 700.0f));
  }
  // The two-minute hold afterwards: still held, still discarding.
  out = run(state, now, 11, 9.0f);
  assert(out.next.reason == Reason::OIL_RETURN && near(out.setpoint_lph, 700.0f) && out.next.rise_count == 0);
  // Then normal samples again: the flow carries on down from 700, untouched by
  // the 9 K the oil return showed.
  out = run(state, now, 12, 1.5f);
  assert(out.setpoint_lph < 700.0f);
}

void test_oil_return_on_either_duo_unit_holds_but_never_overrides_a_water_limit() {
  State state;
  uint32_t now = 1000;
  run(state, now, 31, 3.0f);
  // HP2 runs the oil return: the water runs through both, so the flow is held.
  Input in = heating(now, state.setpoint_lph, 3.0f);
  in.unit_count = 2;
  in.units[1].heating = true;
  in.units[1].temps_fresh = true;
  in.units[1].in_c = 33.0f;
  in.units[1].out_c = 36.0f;
  in.units[1].level = 3;
  in.units[1].oil_return = true;
  Output out = decide(in, state);
  assert(out.next.reason == Reason::OIL_RETURN);
  // The water temperature protection still wins: flow before power.
  in.water_limit_active = true;
  out = decide(in, out.next);
  assert(out.next.reason == Reason::WATER_LIMIT && out.setpoint_lph == 1200.0f);
}

void test_reason_text_is_defined_for_every_reason() {
  for (int r = 0; r <= static_cast<int>(Reason::OIL_RETURN); ++r) {
    const char* text = reason_text(static_cast<Reason>(r));
    assert(text != nullptr && text[0] != '\0');
  }
}

}  // namespace

int main() {
  test_off_and_not_heating_pass_the_fixed_setpoint_through();
  test_new_cycle_starts_at_fixed_flow_and_settles();
  test_low_rise_lowers_the_flow_in_bounded_steps_down_to_the_minimum();
  test_high_rise_raises_the_flow_up_to_the_maximum();
  test_rise_within_deadband_holds_the_flow();
  test_maximum_can_never_sit_below_the_fixed_setpoint();
  test_water_temperature_limit_goes_straight_to_maximum_flow();
  test_defrost_never_runs_below_the_fixed_flow();
  test_defrost_keeps_a_higher_flow_through_the_hold_and_restarts_from_it();
  test_leaving_heating_drops_a_defrost_hold();
  test_missing_temperatures_and_flow_fault_fall_back();
  test_duo_averages_the_rise_per_running_unit();
  test_a_unit_joining_resettles_but_a_level_change_does_not();
  test_pump_limit_stops_asking_for_more();
  test_a_lagging_flow_with_drive_to_spare_is_not_a_pump_limit();
  test_standing_aside_ends_the_cycle();
  test_the_first_minute_after_a_step_is_not_measured();
  test_long_uptime_does_not_freeze_the_controller();
  test_maximum_follows_the_configured_quatt_version();
  test_warning_when_the_water_limit_persists_at_maximum_flow();
  test_duo_reports_the_rise_per_unit();
  test_pacing_defaults_and_configuration();
  test_oil_return_holds_the_flow_and_discards_its_samples();
  test_oil_return_on_either_duo_unit_holds_but_never_overrides_a_water_limit();
  test_reason_text_is_defined_for_every_reason();
  return 0;
}
