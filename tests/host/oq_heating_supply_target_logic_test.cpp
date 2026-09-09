#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/control/oq_heating_supply_target_logic.h"

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
}  // namespace

int main() {
  test_external_replaces_local_curve();
  test_external_clamps_to_heating_range();
  test_fallback_to_local_curve();
  test_broken_limits_fall_back_to_local();
  return 0;
}
