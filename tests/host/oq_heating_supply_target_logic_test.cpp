#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/control/oq_heating_supply_target_logic.h"
#include "../../openquatt/includes/control/oq_input_source_logic.h"

namespace {
using namespace oq_heating_supply;
bool near(float actual, float expected) { return fabsf(actual - expected) < 0.001f; }

void test_external_replaces_local_curve() {
  // A valid external target wins as-is; the local curve (stooklijn +
  // room-trim) must not run again on top of it.
  const auto result = select_effective_target(35.0f, 42.0f, true, 20.0f, 55.0f);
  assert(result.external && near(result.supply_target_c, 42.0f));
}

void test_external_clamps_to_heating_range() {
  const auto low = select_effective_target(35.0f, 5.0f, true, 20.0f, 55.0f);
  assert(low.external && near(low.supply_target_c, 20.0f));
  const auto high = select_effective_target(35.0f, 80.0f, true, 20.0f, 55.0f);
  assert(high.external && near(high.supply_target_c, 55.0f));
}

void test_fallback_to_local_curve() {
  // Invalid, stale (valid=false) or non-finite external input falls back to
  // the local heating curve rather than to no heat at all.
  struct Case {
    float external_c;
    bool valid;
  };
  const Case cases[] = {{42.0f, false}, {NAN, true}, {INFINITY, true}};
  for (const auto& test : cases) {
    const auto result = select_effective_target(35.0f, test.external_c, test.valid, 20.0f, 55.0f);
    assert(!result.external && near(result.supply_target_c, 35.0f));
  }
  // A broken local curve stays visible as NAN so downstream fails closed.
  const auto broken = select_effective_target(NAN, NAN, false, 20.0f, 55.0f);
  assert(!broken.external && isnan(broken.supply_target_c));
}

void test_broken_limits_fall_back_to_local() {
  // Without a usable [min, max] heating range there is no scale to clamp an
  // external target against, so the local curve stays in charge.
  const float bad[] = {NAN, INFINITY};
  for (float limit : bad) {
    assert(!select_effective_target(35.0f, 42.0f, true, limit, 55.0f).external);
    assert(!select_effective_target(35.0f, 42.0f, true, 20.0f, limit).external);
  }
  assert(!select_effective_target(35.0f, 42.0f, true, 55.0f, 55.0f).external);
  assert(!select_effective_target(35.0f, 42.0f, true, 55.0f, 20.0f).external);
}

void test_unusable_control_setpoint_never_reaches_selection() {
  // A fresh but unusable TSet (0 = thermostat without heat demand, or any
  // out-of-range value) must fall back to the curve instead of being clamped
  // onto an external minimum target (issue #649, P1).
  assert(external_target_in_range(20.0f));
  assert(external_target_in_range(42.0f));
  assert(external_target_in_range(70.0f));
  assert(!external_target_in_range(0.0f));
  assert(!external_target_in_range(19.9f));
  assert(!external_target_in_range(70.1f));
  assert(!external_target_in_range(NAN));
  assert(!external_target_in_range(INFINITY));
}

void test_explicit_validity_off_revokes_ha_hold() {
  // Switching the HA validity flag off must drop the cached target at once
  // instead of replaying it for the hold window (issue #649, P1). A missing
  // flag (for example during an HA reload) keeps bridging.
  assert(ha_hold_revoked(true, false));
  assert(!ha_hold_revoked(true, true));
  assert(!ha_hold_revoked(false, false));
  assert(!ha_hold_revoked(false, true));
}

void test_explicit_off_skips_ha_hold_replay() {
  // End to end over the shared selection primitive: with an explicit off the
  // seeded hold is revoked first, so select_direct no longer replays it.
  // Without revocation the same dropout still bridges (reload protection).
  const oq_input_source::NumericSources empty{};
  oq_input_source::HoldState revoked;
  revoked.remember(42.0f, 1000U, oq_input_source::Source::HA);
  if (ha_hold_revoked(true, false)) revoked.reset();
  const auto dropped =
      oq_input_source::select_direct(oq_input_source::Source::HA, empty, true, 2000U, 300000U, revoked);
  assert(!dropped.valid);

  oq_input_source::HoldState bridged;
  bridged.remember(42.0f, 1000U, oq_input_source::Source::HA);
  if (ha_hold_revoked(false, false)) bridged.reset();
  const auto held = oq_input_source::select_direct(oq_input_source::Source::HA, empty, true, 2000U, 300000U, bridged);
  assert(held.valid && held.held && near(held.value, 42.0f));
}
}  // namespace

int main() {
  test_external_replaces_local_curve();
  test_external_clamps_to_heating_range();
  test_fallback_to_local_curve();
  test_broken_limits_fall_back_to_local();
  test_unusable_control_setpoint_never_reaches_selection();
  test_explicit_validity_off_revokes_ha_hold();
  test_explicit_off_skips_ha_hold_replay();
  return 0;
}
