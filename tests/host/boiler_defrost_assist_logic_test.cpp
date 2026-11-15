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

constexpr Config kConfig{900000UL, 3.0f};

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
    const Config config{900000UL, margin_c};
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
  assert(out.state.reference_supply_c == 33.0f);
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
  test_maximum_duration_releases_until_the_defrost_clears();
  test_duration_survives_millis_wraparound();
  return 0;
}
