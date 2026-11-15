#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/control/oq_boiler_defrost_assist_logic.h"

namespace {

using oq_boiler_defrost_assist::Config;
using oq_boiler_defrost_assist::Input;
using oq_boiler_defrost_assist::Output;
using oq_boiler_defrost_assist::State;

constexpr Config kConfig{900000UL};

Input heating(uint32_t now_ms, float supply_c, bool defrost) {
  Input in;
  in.now_ms = now_ms;
  in.enabled = true;
  in.heating_owned = true;
  in.defrost_active = defrost;
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
  assert(out.target_c == 36.0f);

  // The dip itself must not lower the reference or the target.
  out = oq_boiler_defrost_assist::update(heating(11000U, 24.0f, true), out.state, kConfig);
  assert(out.requested);
  assert(!out.started);
  assert(out.target_c == 36.0f);
  assert(out.state.reference_supply_c == 36.0f);
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
  assert(out.target_c == 35.0f);
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
  test_disabled_setting_never_requests_assist();
  test_disabling_during_a_defrost_releases_at_once();
  test_no_reference_means_no_assist();
  test_reference_is_dropped_outside_heating();
  test_losing_heating_ownership_releases();
  test_defrost_end_releases_and_resumes_tracking();
  test_maximum_duration_releases_until_the_defrost_clears();
  test_duration_survives_millis_wraparound();
  return 0;
}
