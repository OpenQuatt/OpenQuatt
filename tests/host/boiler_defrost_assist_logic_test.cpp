#include <assert.h>
#include <initializer_list>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/control/oq_boiler_defrost_assist_logic.h"

namespace {

using oq_boiler_defrost_assist::Config;
using oq_boiler_defrost_assist::Input;
using oq_boiler_defrost_assist::Output;
using oq_boiler_defrost_assist::State;

// Timing-free policy for the session rules; the anti-cycling timers have their
// own cases below with kTimed.
constexpr Config kConfig{900000UL, 3.0f, 0UL, 0UL, 0UL, 0UL, 0UL};
constexpr Config kTimed{900000UL, 3.0f, 10000UL, 60000UL, 120000UL, 60000UL, 300000UL};

Input heating(uint32_t now_ms, float supply_c, bool defrost) {
  Input in;
  in.now_ms = now_ms;
  in.enabled = true;
  in.heating_owned = true;
  in.defrost_active = defrost;
  in.supply_reference_valid = true;
  in.supply_c = supply_c;
  return in;
}

// Runs normal heating long enough to learn a supply reference.
State primed(float supply_c) {
  const Output out = oq_boiler_defrost_assist::update(heating(1000U, supply_c, false), State{}, kConfig);
  assert(!out.requested);
  return out.state;
}

void test_idle_without_defrost_only_tracks_the_reference() {
  auto out = oq_boiler_defrost_assist::update(heating(1000U, 34.0f, false), State{}, kConfig);
  assert(!out.requested);
  assert(!out.started);
  assert(!out.released);
  assert(isnan(out.target_c));
  assert(out.state.reference_supply_c == 34.0f);

  out = oq_boiler_defrost_assist::update(heating(6000U, 35.5f, false), out.state, kConfig);
  assert(!out.requested);
  assert(out.state.reference_supply_c == 35.5f);

  // An unusable measurement keeps the last good reference.
  out = oq_boiler_defrost_assist::update(heating(11000U, NAN, false), out.state, kConfig);
  assert(out.state.reference_supply_c == 35.5f);
}

void test_defrost_starts_assist_at_the_pre_defrost_supply() {
  auto out = oq_boiler_defrost_assist::update(heating(6000U, 30.0f, true), primed(36.0f), kConfig);
  assert(out.requested);
  assert(out.started);
  // The target sits the configured margin above the pre-defrost supply.
  assert(out.target_c == 39.0f);

  // The dip itself must not lower the reference or the target.
  out = oq_boiler_defrost_assist::update(heating(11000U, 24.0f, true), out.state, kConfig);
  assert(out.requested);
  assert(!out.started);
  assert(out.target_c == 39.0f);
  assert(out.state.reference_supply_c == 36.0f);
}

void test_unusable_margin_never_raises_the_target() {
  for (const float margin_c : {0.0f, -2.0f, NAN, INFINITY}) {
    const Config config{900000UL, margin_c, 0UL, 0UL, 0UL, 0UL, 0UL};
    const auto out = oq_boiler_defrost_assist::update(heating(6000U, 30.0f, true), primed(36.0f), config);
    assert(out.requested);
    assert(out.target_c == 36.0f);
  }
}

void test_disabled_setting_never_requests_assist() {
  auto in = heating(6000U, 30.0f, true);
  in.enabled = false;
  const auto out = oq_boiler_defrost_assist::update(in, primed(36.0f), kConfig);
  assert(!out.requested);
  assert(!out.started);
  assert(isnan(out.target_c));
}

void test_disabling_during_a_defrost_releases_at_once() {
  auto out = oq_boiler_defrost_assist::update(heating(6000U, 30.0f, true), primed(36.0f), kConfig);
  assert(out.requested);
  auto in = heating(11000U, 28.0f, true);
  in.enabled = false;
  out = oq_boiler_defrost_assist::update(in, out.state, kConfig);
  assert(!out.requested);
  assert(out.released);
  assert(isnan(out.target_c));

  // One session per defrost report: switching it back on does not restart.
  out = oq_boiler_defrost_assist::update(heating(16000U, 28.0f, true), out.state, kConfig);
  assert(!out.requested);
  assert(!out.started);
}

void test_interrupted_session_cannot_extend_a_stuck_defrost_report() {
  // Ten minutes of assist, a short loss of heating ownership, then heating
  // again while the report never cleared: no second bounded session.
  auto out = oq_boiler_defrost_assist::update(heating(6000U, 30.0f, true), primed(36.0f), kConfig);
  assert(out.requested);
  out = oq_boiler_defrost_assist::update(heating(606000U, 30.0f, true), out.state, kConfig);
  assert(out.requested);
  auto in = heating(611000U, 30.0f, true);
  in.heating_owned = false;
  out = oq_boiler_defrost_assist::update(in, out.state, kConfig);
  assert(!out.requested);
  for (uint32_t now_ms = 616000U; now_ms < 2000000U; now_ms += 5000U) {
    out = oq_boiler_defrost_assist::update(heating(now_ms, 30.0f, true), out.state, kConfig);
    assert(!out.requested);
    assert(!out.started);
  }
}

void test_stuck_report_across_a_heating_stop_has_no_reference() {
  // The report stays set while heating stops and resumes much later. The old
  // supply value must not become a boiler target.
  State state = primed(36.0f);
  auto in = heating(6000U, 30.0f, true);
  in.enabled = false;
  auto out = oq_boiler_defrost_assist::update(in, state, kConfig);
  in = heating(11000U, 25.0f, true);
  in.heating_owned = false;
  out = oq_boiler_defrost_assist::update(in, out.state, kConfig);
  assert(isnan(out.state.reference_supply_c));
  out = oq_boiler_defrost_assist::update(heating(7200000U, 22.0f, true), out.state, kConfig);
  assert(!out.requested);
  assert(!out.started);
}

void test_heat_pump_outlet_fallback_is_never_a_reference() {
  // Without a real supply measurement after the boiler nothing is learned.
  auto in = heating(1000U, 36.0f, false);
  in.supply_reference_valid = false;
  auto out = oq_boiler_defrost_assist::update(in, State{}, kConfig);
  assert(isnan(out.state.reference_supply_c));
  in = heating(6000U, 30.0f, true);
  in.supply_reference_valid = false;
  out = oq_boiler_defrost_assist::update(in, out.state, kConfig);
  assert(!out.requested);

  // A good reference is dropped as soon as the measurement falls back.
  in = heating(11000U, 36.0f, false);
  in.supply_reference_valid = false;
  out = oq_boiler_defrost_assist::update(in, primed(36.0f), kConfig);
  assert(isnan(out.state.reference_supply_c));

  // Falling back during a session releases it for the rest of that report.
  out = oq_boiler_defrost_assist::update(heating(6000U, 30.0f, true), primed(36.0f), kConfig);
  assert(out.requested);
  in = heating(11000U, 12.0f, true);
  in.supply_reference_valid = false;
  out = oq_boiler_defrost_assist::update(in, out.state, kConfig);
  assert(!out.requested);
  assert(out.released);
  out = oq_boiler_defrost_assist::update(heating(16000U, 30.0f, true), out.state, kConfig);
  assert(!out.requested);
}

void test_no_reference_means_no_assist() {
  // A defrost seen before any heating supply was measured has nothing to hold.
  const auto out = oq_boiler_defrost_assist::update(heating(6000U, 30.0f, true), State{}, kConfig);
  assert(!out.requested);
  assert(!out.started);
}

void test_reference_is_dropped_outside_heating() {
  auto in = heating(6000U, 36.0f, false);
  in.heating_owned = false;
  auto out = oq_boiler_defrost_assist::update(in, primed(36.0f), kConfig);
  assert(isnan(out.state.reference_supply_c));

  // A defrost right after heating resumes cannot reuse the old reference.
  out = oq_boiler_defrost_assist::update(heating(11000U, 30.0f, true), out.state, kConfig);
  assert(!out.requested);
}

void test_losing_heating_ownership_releases() {
  auto out = oq_boiler_defrost_assist::update(heating(6000U, 30.0f, true), primed(36.0f), kConfig);
  assert(out.requested);
  auto in = heating(11000U, 28.0f, true);
  in.heating_owned = false;
  out = oq_boiler_defrost_assist::update(in, out.state, kConfig);
  assert(!out.requested);
  assert(out.released);
}

void test_defrost_end_releases_and_resumes_tracking() {
  auto out = oq_boiler_defrost_assist::update(heating(6000U, 30.0f, true), primed(36.0f), kConfig);
  assert(out.requested);
  out = oq_boiler_defrost_assist::update(heating(11000U, 33.0f, false), out.state, kConfig);
  assert(!out.requested);
  assert(out.released);
  assert(!out.expired_now);
  assert(isnan(out.target_c));
  // The reference resumes once the session has been rearmed.
  assert(out.state.reference_supply_c == 36.0f);
  out = oq_boiler_defrost_assist::update(heating(16000U, 33.0f, false), out.state, kConfig);
  assert(out.state.reference_supply_c == 33.0f);
}

// Steps the policy every 5 s, like the supervisory loop, and counts starts.
struct Run {
  State state;
  uint32_t now_ms = 0;
  int starts = 0;
  uint32_t last_start_ms = 0;
  uint32_t shortest_start_gap_ms = UINT32_MAX;
  uint32_t shortest_session_ms = UINT32_MAX;

  Output step(bool defrost, float supply_c) {
    now_ms += 5000U;
    const Output out = oq_boiler_defrost_assist::update(heating(now_ms, supply_c, defrost), state, kTimed);
    if (out.started) {
      if (starts > 0 && now_ms - last_start_ms < shortest_start_gap_ms) shortest_start_gap_ms = now_ms - last_start_ms;
      last_start_ms = now_ms;
      ++starts;
    }
    if (out.released && now_ms - last_start_ms < shortest_session_ms) shortest_session_ms = now_ms - last_start_ms;
    state = out.state;
    return out;
  }
};

void test_a_single_sample_report_does_not_start_a_session() {
  Run run;
  run.step(false, 36.0f);
  // One 5 s sample with the bit set, then clear again.
  assert(!run.step(true, 36.0f).requested);
  assert(!run.step(false, 36.0f).requested);
  // A report that persists for the confirm time does start.
  assert(!run.step(true, 36.0f).requested);
  assert(!run.step(true, 35.0f).requested);
  const auto out = run.step(true, 33.0f);
  assert(out.requested);
  assert(out.started);
  assert(out.target_c == 39.0f);
}

void test_started_session_runs_its_minimum_and_release_time() {
  Run run;
  run.step(false, 36.0f);
  run.step(true, 36.0f);
  run.step(true, 35.0f);
  assert(run.step(true, 33.0f).started);
  const uint32_t started_ms = run.now_ms;
  // The report clears 5 s into the session. The session is held for the
  // minimum run and until the report has been clear for the release time.
  while (run.now_ms - started_ms < 60000U) {
    const auto out = run.step(false, 37.0f);
    assert(out.requested);
    assert(!out.released);
  }
  const auto out = run.step(false, 37.0f);
  assert(!out.requested);
  assert(out.released);
  assert(run.now_ms - started_ms == 65000U);
}

void test_a_single_clear_sample_does_not_end_a_session() {
  Run run;
  run.step(false, 36.0f);
  run.step(true, 36.0f);
  run.step(true, 35.0f);
  assert(run.step(true, 33.0f).started);
  for (int tick = 0; tick < 24; ++tick) assert(run.step(true, 34.0f).requested);
  // One 5 s sample without the bit, past the minimum run.
  assert(run.step(false, 34.0f).requested);
  assert(run.step(true, 34.0f).requested);
  assert(run.starts == 1);
}

void test_release_time_bridges_a_second_outdoor_unit() {
  // Duo: the second unit starts its defrost 45 s after the first one ended.
  // The session carries over instead of leaving the second dip unassisted.
  Run run;
  run.step(false, 36.0f);
  run.step(true, 36.0f);
  run.step(true, 35.0f);
  assert(run.step(true, 33.0f).started);
  for (int tick = 0; tick < 36; ++tick) assert(run.step(true, 34.0f).requested);
  for (int tick = 0; tick < 9; ++tick) assert(run.step(false, 37.0f).requested);
  for (int tick = 0; tick < 36; ++tick) assert(run.step(true, 34.0f).requested);
  // The second defrost ends: release after the release time.
  for (int tick = 0; tick < 12; ++tick) assert(run.step(false, 37.0f).requested);
  const auto out = run.step(false, 37.0f);
  assert(!out.requested);
  assert(out.released);
  assert(run.starts == 1);
}

void test_losing_permission_overrides_the_minimum_time() {
  Run run;
  run.step(false, 36.0f);
  run.step(true, 36.0f);
  run.step(true, 35.0f);
  assert(run.step(true, 33.0f).started);
  run.now_ms += 5000U;
  auto in = heating(run.now_ms, 33.0f, true);
  in.enabled = false;
  const auto out = oq_boiler_defrost_assist::update(in, run.state, kTimed);
  assert(!out.requested);
  assert(out.released);
}

void test_flickering_report_cannot_cycle_the_session() {
  // The defrost bit toggles in several fast patterns for an hour each. The
  // normal CM2/CM3 timers are bypassed, so the session itself must bound the
  // cycling: every session lasts at least the minimum run, and two starts are
  // at least minimum run plus rearm time apart.
  const int patterns[][2] = {{1, 1}, {2, 1}, {3, 1}, {3, 3}, {4, 2}, {6, 6}, {3, 24}, {12, 26}};
  for (const auto& pattern : patterns) {
    Run run;
    run.step(false, 36.0f);
    for (int tick = 0; tick < 720; ++tick) {
      const bool defrost = tick % (pattern[0] + pattern[1]) < pattern[0];
      run.step(defrost, defrost ? 30.0f : 36.0f);
    }
    if (run.starts > 0) assert(run.shortest_session_ms >= 60000U);
    if (run.starts > 1) assert(run.shortest_start_gap_ms >= 180000U);
    assert(run.starts <= 20);
  }

  // A bit that flickers faster than the rearm time gets one session at most.
  Run fast;
  fast.step(false, 36.0f);
  for (int tick = 0; tick < 720; ++tick) fast.step(tick % 4 < 3, 30.0f);
  assert(fast.starts == 1);

  // A bit that never holds for the confirm time gets none.
  Run glitch;
  glitch.step(false, 36.0f);
  for (int tick = 0; tick < 720; ++tick) glitch.step(tick % 2 == 0, 30.0f);
  assert(glitch.starts == 0);
}

void test_assist_heat_does_not_raise_the_next_reference() {
  // Boiler heat still in the water after a session must not become the
  // reference of the next defrost, or the target would ratchet up.
  Run run;
  run.step(false, 36.0f);
  run.step(true, 36.0f);
  run.step(true, 35.0f);
  assert(run.step(true, 33.0f).started);
  for (int tick = 0; tick < 30; ++tick) run.step(true, 38.5f);
  // Defrost over; supply still lifted by the boiler during release and rearm.
  for (int tick = 0; tick < 35; ++tick) {
    run.step(false, 38.5f);
    assert(run.state.reference_supply_c == 36.0f);
  }
  // Rearmed: the reference follows the heat-pump supply again.
  run.step(false, 35.5f);
  run.step(false, 35.5f);
  assert(run.state.reference_supply_c == 35.5f);
}

void test_boiler_heat_is_never_a_reference() {
  // Normal assist lifts the supply in CM3. That supply is no reference, also
  // not during the settle time after the boiler stopped.
  const Config config{900000UL, 3.0f, 0UL, 0UL, 0UL, 0UL, 300000UL};
  auto out = oq_boiler_defrost_assist::update(heating(1000U, 36.0f, false), State{}, config);
  assert(out.state.reference_supply_c == 36.0f);
  auto in = heating(6000U, 48.0f, false);
  in.boiler_heat_active = true;
  out = oq_boiler_defrost_assist::update(in, out.state, config);
  assert(isnan(out.state.reference_supply_c));

  // A defrost while the boiler heats has nothing to hold.
  in = heating(11000U, 44.0f, true);
  in.boiler_heat_active = true;
  out = oq_boiler_defrost_assist::update(in, out.state, config);
  assert(!out.requested);

  // Normal assist has ended, but the water still holds boiler heat.
  out = oq_boiler_defrost_assist::update(heating(16000U, 46.0f, false), out.state, config);
  assert(isnan(out.state.reference_supply_c));
  out = oq_boiler_defrost_assist::update(heating(216000U, 41.0f, true), out.state, config);
  assert(!out.requested);

  // After the settle time the heat-pump supply is the reference again.
  out = oq_boiler_defrost_assist::update(heating(311000U, 38.0f, false), out.state, config);
  assert(out.state.reference_supply_c == 38.0f);
  out = oq_boiler_defrost_assist::update(heating(316000U, 31.0f, true), out.state, config);
  assert(out.requested);
  assert(out.target_c == 41.0f);
}

void test_running_session_keeps_its_target_while_the_boiler_heats() {
  auto out = oq_boiler_defrost_assist::update(heating(6000U, 30.0f, true), primed(36.0f), kConfig);
  assert(out.requested);
  auto in = heating(11000U, 34.0f, true);
  in.boiler_heat_active = true;
  out = oq_boiler_defrost_assist::update(in, out.state, kConfig);
  assert(out.requested);
  assert(out.target_c == 39.0f);
  assert(isnan(out.state.reference_supply_c));
}

void test_maximum_duration_releases_until_the_defrost_clears() {
  auto out = oq_boiler_defrost_assist::update(heating(6000U, 30.0f, true), primed(36.0f), kConfig);
  assert(out.requested);

  out = oq_boiler_defrost_assist::update(heating(6000U + 899999U, 30.0f, true), out.state, kConfig);
  assert(out.requested);

  out = oq_boiler_defrost_assist::update(heating(6000U + 900000U, 30.0f, true), out.state, kConfig);
  assert(!out.requested);
  assert(out.released);
  assert(out.expired_now);

  // A defrost report that never clears cannot restart the boiler.
  out = oq_boiler_defrost_assist::update(heating(6000U + 905000U, 30.0f, true), out.state, kConfig);
  assert(!out.requested);
  assert(!out.started);
  assert(!out.expired_now);

  // Once it clears, the next defrost may be assisted again.
  out = oq_boiler_defrost_assist::update(heating(6000U + 910000U, 35.0f, false), out.state, kConfig);
  assert(!out.requested);
  out = oq_boiler_defrost_assist::update(heating(6000U + 915000U, 31.0f, true), out.state, kConfig);
  assert(out.requested);
  assert(out.target_c == 38.0f);
}

void test_duration_survives_millis_wraparound() {
  State state = primed(36.0f);
  auto out = oq_boiler_defrost_assist::update(heating(0xFFFFF000U, 30.0f, true), state, kConfig);
  assert(out.requested);
  out = oq_boiler_defrost_assist::update(heating(0x00001000U, 30.0f, true), out.state, kConfig);
  assert(out.requested);
  out = oq_boiler_defrost_assist::update(heating(0xFFFFF000U + 900000U, 30.0f, true), out.state, kConfig);
  assert(!out.requested);
  assert(out.expired_now);
}

// Flame confirmation. With feedback from the heat source (OpenTherm) the
// session raises its target until the source actually fires. The cases use the
// recorded night of 22 November: supply reference 31.4 C, boiler water 31.0 C.
constexpr Config kFlame{900000UL, 3.0f, 10000UL, 60000UL, 120000UL, 60000UL, 300000UL, 45000UL, 2.0f, 30000UL, 20000UL};

bool near(float value, float expected) { return fabsf(value - expected) < 0.01f; }

struct Source {
  State state;
  uint32_t now_ms = 0;
  uint32_t session_start_ms = 0;
  bool in_session = false;
  int raises = 0;
  int confirmations = 0;
  int ceiling_reports = 0;
  uint32_t last_raise_ms = 0;
  uint32_t confirmed_ms = 0;
  float target_c = NAN;
  // Inputs the supervisory derives from the transport.
  bool enabled = true;
  bool feedback = true;
  bool applied = true;
  bool dhw = false;
  float boiler_water_c = 31.0f;
  float ceiling_c = 42.0f;
  float supply_c = 31.4f;

  Output step(bool defrost, bool firing) {
    now_ms += 5000U;
    Input in = heating(now_ms, supply_c, defrost);
    in.enabled = enabled;
    in.feedback_available = feedback;
    in.command_applied = applied;
    in.firing = firing;
    in.dhw_active = dhw;
    in.boiler_water_c = boiler_water_c;
    in.target_ceiling_c = ceiling_c;
    const Output out = oq_boiler_defrost_assist::update(in, state, kFlame);
    if (out.started) {
      session_start_ms = now_ms;
      in_session = true;
    }
    if (out.released) in_session = false;
    if (out.raised) {
      ++raises;
      last_raise_ms = now_ms;
    }
    if (out.flame_confirmed_now) {
      ++confirmations;
      confirmed_ms = now_ms;
    }
    if (out.ceiling_reached_now) ++ceiling_reports;
    target_c = out.target_c;
    state = out.state;
    return out;
  }

  // Learns the reference and runs the defrost report until the session starts.
  void start_session() {
    step(false, false);
    while (!in_session) step(true, false);
    assert(near(target_c, 34.4f));
  }

  // A source that enables its elements below "setpoint minus hysteresis", like
  // the recorded e-boiler (P02).
  bool fires_below(float hysteresis_c) const { return in_session && target_c - hysteresis_c > boiler_water_c; }
};

void test_recorded_ecv_starts_after_one_step() {
  Source source;
  source.start_session();
  for (int tick = 0; tick < 60; ++tick) source.step(true, source.fires_below(5.0f));
  assert(source.raises == 1);
  assert(source.last_raise_ms - source.session_start_ms == 45000U);
  assert(source.confirmations == 1);
  assert(source.confirmed_ms - source.session_start_ms <= 50000U);
  // The target stays where the source started, for the rest of the session.
  assert(near(source.target_c, 36.4f));
  assert(source.state.flame_confirmed);
}

void test_factory_hysteresis_needs_three_steps() {
  Source source;
  source.start_session();
  for (int tick = 0; tick < 60; ++tick) source.step(true, source.fires_below(8.0f));
  assert(source.raises == 3);
  assert(near(source.target_c, 40.4f));
  assert(source.confirmations == 1);
  assert(source.confirmed_ms - source.session_start_ms <= 110000U);
}

void test_source_that_ignites_in_time_is_not_raised() {
  Source source;
  source.start_session();
  for (int tick = 0; tick < 60; ++tick) source.step(true, source.now_ms - source.session_start_ms >= 30000U);
  assert(source.raises == 0);
  assert(source.confirmations == 1);
  assert(near(source.target_c, 34.4f));
}

void test_blocked_command_is_never_raised() {
  // The boiler chain did not apply the command (start guard, minimum off-time,
  // inhibit). No flame then says nothing about the target.
  Source source;
  source.applied = false;
  source.start_session();
  for (int tick = 0; tick < 120; ++tick) source.step(true, false);
  assert(source.raises == 0);
  assert(source.ceiling_reports == 0);
  assert(near(source.target_c, 34.4f));
}

void test_dhw_pauses_search_and_flame_does_not_count() {
  Source source;
  source.start_session();
  source.dhw = true;
  for (int tick = 0; tick < 24; ++tick) source.step(true, true);
  assert(source.raises == 0);
  assert(source.confirmations == 0);
  assert(!source.state.flame_confirmed);
  source.dhw = false;
  const uint32_t dhw_end_ms = source.now_ms;
  for (int tick = 0; tick < 8; ++tick) source.step(true, false);
  assert(source.raises == 0);
  source.step(true, false);
  source.step(true, false);
  assert(source.raises == 1);
  assert(source.last_raise_ms - dhw_end_ms >= 45000U);
}

// Runs the recorded e-boiler until it fires at 36.4 C.
Source confirmed_source() {
  Source source;
  source.start_session();
  while (source.confirmations == 0) source.step(true, source.fires_below(5.0f));
  assert(source.raises == 1);
  assert(near(source.target_c, 36.4f));
  return source;
}

void test_flame_loss_below_target_gets_one_extra_step() {
  Source source = confirmed_source();
  source.boiler_water_c = 33.0f;
  const uint32_t lost_ms = source.now_ms;
  for (int tick = 0; tick < 60; ++tick) source.step(true, false);
  assert(source.raises == 2);
  assert(source.last_raise_ms - lost_ms >= 20000U);
  assert(source.last_raise_ms - lost_ms <= 25000U);
  assert(near(source.target_c, 38.4f));
  assert(source.ceiling_reports == 0);
}

void test_flame_loss_when_satisfied_is_not_raised() {
  Source source = confirmed_source();
  source.boiler_water_c = 37.0f;
  for (int tick = 0; tick < 60; ++tick) source.step(true, false);
  assert(source.raises == 1);
  assert(near(source.target_c, 36.4f));
}

void test_satisfied_uses_supply_without_boiler_water() {
  Source source = confirmed_source();
  source.boiler_water_c = NAN;
  source.supply_c = 37.0f;
  for (int tick = 0; tick < 60; ++tick) source.step(true, false);
  assert(source.raises == 1);
  assert(near(source.target_c, 36.4f));
}

void test_target_never_exceeds_ceiling() {
  Source source;
  source.ceiling_c = 37.0f;
  source.start_session();
  for (int tick = 0; tick < 60; ++tick) {
    source.step(true, false);
    assert(source.target_c <= 37.0f);
  }
  assert(source.raises == 2);
  assert(near(source.target_c, 37.0f));
  assert(source.ceiling_reports == 1);
  // The command stays until the session ends the normal way.
  assert(source.in_session);
  for (int tick = 0; tick < 20; ++tick) source.step(false, false);
  assert(!source.in_session);
}

void test_ceiling_below_base_target_caps_without_raising() {
  Source source;
  source.ceiling_c = 33.0f;
  source.step(false, false);
  while (!source.in_session) source.step(true, false);
  for (int tick = 0; tick < 60; ++tick) {
    source.step(true, false);
    assert(near(source.target_c, 33.0f));
  }
  assert(source.raises == 0);
  assert(source.ceiling_reports == 1);
}

void test_no_ceiling_means_no_raise() {
  Source source;
  source.ceiling_c = NAN;
  source.start_session();
  for (int tick = 0; tick < 60; ++tick) source.step(true, false);
  assert(source.raises == 0);
  assert(near(source.target_c, 34.4f));
}

void test_without_feedback_behaves_like_before() {
  // R1, or OpenTherm telemetry that is not fresh: nothing but the old target.
  Source source;
  source.feedback = false;
  Run plain;
  plain.step(false, 31.4f);
  source.step(false, false);
  for (int tick = 0; tick < 200; ++tick) {
    const bool defrost = tick < 100;
    const Output old_out = plain.step(defrost, 31.4f);
    const Output new_out = source.step(defrost, tick % 3 == 0);
    assert(old_out.requested == new_out.requested);
    assert(old_out.started == new_out.started);
    assert(old_out.released == new_out.released);
    assert((isnan(old_out.target_c) && isnan(new_out.target_c)) || old_out.target_c == new_out.target_c);
  }
  assert(source.raises == 0);
  assert(source.confirmations == 0);
}

void test_feedback_lost_keeps_raised_target() {
  Source source;
  source.start_session();
  while (source.raises == 0) source.step(true, false);
  assert(near(source.target_c, 36.4f));
  source.feedback = false;
  for (int tick = 0; tick < 60; ++tick) {
    source.step(true, false);
    assert(near(source.target_c, 36.4f));
  }
  assert(source.raises == 1);
}

void test_flickering_flame_is_bounded() {
  Source source;
  source.start_session();
  for (int tick = 0; tick < 120; ++tick) source.step(true, tick % 2 == 0);
  assert(source.confirmations == 1);
  assert(source.raises <= 2);
}

void test_wait_survives_millis_rollover() {
  Source source;
  source.now_ms = UINT32_MAX - 40000U;
  source.start_session();
  const uint32_t start_ms = source.session_start_ms;
  while (source.raises == 0) {
    source.step(true, false);
    assert(static_cast<uint32_t>(source.now_ms - start_ms) <= 45000U);
  }
  assert(static_cast<uint32_t>(source.last_raise_ms - start_ms) == 45000U);
  assert(source.now_ms < start_ms);
}

void test_raise_is_cleared_at_session_end() {
  Source source = confirmed_source();
  while (source.in_session) source.step(false, false);
  assert(source.state.raise_c == 0.0f);
  assert(!source.state.flame_confirmed);
  assert(!source.state.extra_step_used);
  // A later session searches again from its own base target.
  for (int tick = 0; tick < 80; ++tick) source.step(false, false);
  while (!source.in_session) source.step(true, false);
  assert(near(source.target_c, 34.4f));
}

void test_unusable_step_never_raises() {
  for (const float step_c : {0.0f, -2.0f, NAN, INFINITY}) {
    Config config = kFlame;
    config.raise_step_c = step_c;
    State state;
    uint32_t now_ms = 0;
    bool raised = false;
    for (int tick = 0; tick < 80; ++tick) {
      now_ms += 5000U;
      Input in = heating(now_ms, 31.4f, tick > 0);
      in.feedback_available = true;
      in.command_applied = true;
      in.boiler_water_c = 31.0f;
      in.target_ceiling_c = 42.0f;
      const Output out = oq_boiler_defrost_assist::update(in, state, config);
      raised = raised || out.raised;
      state = out.state;
    }
    assert(!raised);
  }
}

void test_disabled_never_raises() {
  Source source;
  source.enabled = false;
  source.step(false, false);
  for (int tick = 0; tick < 60; ++tick) {
    const Output out = source.step(true, false);
    assert(!out.requested);
  }
  assert(source.raises == 0);
}

}  // namespace

int main() {
  test_idle_without_defrost_only_tracks_the_reference();
  test_defrost_starts_assist_at_the_pre_defrost_supply();
  test_unusable_margin_never_raises_the_target();
  test_disabled_setting_never_requests_assist();
  test_disabling_during_a_defrost_releases_at_once();
  test_interrupted_session_cannot_extend_a_stuck_defrost_report();
  test_stuck_report_across_a_heating_stop_has_no_reference();
  test_heat_pump_outlet_fallback_is_never_a_reference();
  test_no_reference_means_no_assist();
  test_reference_is_dropped_outside_heating();
  test_losing_heating_ownership_releases();
  test_defrost_end_releases_and_resumes_tracking();
  test_a_single_sample_report_does_not_start_a_session();
  test_started_session_runs_its_minimum_and_release_time();
  test_a_single_clear_sample_does_not_end_a_session();
  test_release_time_bridges_a_second_outdoor_unit();
  test_losing_permission_overrides_the_minimum_time();
  test_flickering_report_cannot_cycle_the_session();
  test_assist_heat_does_not_raise_the_next_reference();
  test_boiler_heat_is_never_a_reference();
  test_running_session_keeps_its_target_while_the_boiler_heats();
  test_maximum_duration_releases_until_the_defrost_clears();
  test_duration_survives_millis_wraparound();
  test_recorded_ecv_starts_after_one_step();
  test_factory_hysteresis_needs_three_steps();
  test_source_that_ignites_in_time_is_not_raised();
  test_blocked_command_is_never_raised();
  test_dhw_pauses_search_and_flame_does_not_count();
  test_flame_loss_below_target_gets_one_extra_step();
  test_flame_loss_when_satisfied_is_not_raised();
  test_satisfied_uses_supply_without_boiler_water();
  test_target_never_exceeds_ceiling();
  test_ceiling_below_base_target_caps_without_raising();
  test_no_ceiling_means_no_raise();
  test_without_feedback_behaves_like_before();
  test_feedback_lost_keeps_raised_target();
  test_flickering_flame_is_bounded();
  test_wait_survives_millis_rollover();
  test_raise_is_cleared_at_session_end();
  test_unusable_step_never_raises();
  test_disabled_never_raises();
  return 0;
}
