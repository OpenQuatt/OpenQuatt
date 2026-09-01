#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/control/oq_thermal_limits_logic.h"

namespace {

using namespace oq_thermal_limits;

void test_soft_limit_and_boiler_hysteresis() {
  State state;
  const auto result = update(state, {1000, 58.5f, 60.0f, 2});
  assert(result.soft_start_c == 57.0f);
  assert(fabsf(state.limit_factor - 0.625f) < 0.001f);
  assert(!state.boiler_inhibit);

  update(state, {2000, 60.0f, 60.0f, 2});
  assert(state.boiler_inhibit);
  assert(fabsf(state.limit_factor - 0.25f) < 0.001f);
  update(state, {3000, 58.0f, 60.0f, 2});
  assert(state.boiler_inhibit);
  update(state, {4000, 57.0f, 60.0f, 2});
  assert(!state.boiler_inhibit);
}

void test_non_cm3_trip_is_immediate_and_latched() {
  State state;
  update(state, {1000, 65.0f, 60.0f, 2});
  assert(state.trip);
  assert(state.hard_trip);
  assert(state.limit_factor == 0.0f);

  update(state, {2000, 62.0f, 60.0f, 2});
  assert(state.trip);
  assert(state.hard_trip);
  update(state, {3000, 60.0f, 60.0f, 2});
  assert(!state.trip);
  assert(!state.hard_trip);
}

void test_cm3_trip_requires_hold_and_survives_rollover() {
  State state;
  Config config;
  config.cm3_trip_hold_ms = 1000;
  update(state, {0xFFFFFF00UL, 65.0f, 60.0f, 3}, config);
  assert(state.trip);
  assert(!state.hard_trip);
  update(state, {0x000002E7UL, 65.0f, 60.0f, 3}, config);
  assert(!state.hard_trip);
  update(state, {0x000002E8UL, 65.0f, 60.0f, 3}, config);
  assert(state.hard_trip);
}

void test_leaving_cm3_escalates_an_armed_trip_immediately() {
  State state;
  update(state, {1000, 65.0f, 60.0f, 3});
  assert(state.trip);
  assert(state.trip_timer_running);
  assert(!state.hard_trip);

  update(state, {2000, 65.0f, 60.0f, 2});
  assert(state.hard_trip);
  assert(state.limit_factor == 0.0f);
}

void test_missing_supply_fails_closed_only_after_trip() {
  State state;
  update(state, {1000, NAN, 60.0f, 2});
  assert(state.limit_factor == 1.0f);
  assert(!state.trip);
  state.trip = true;
  state.hard_trip = true;
  state.limit_factor = 0.5f;
  update(state, {2000, NAN, 60.0f, 2});
  assert(state.limit_factor == 0.0f);
  assert(state.trip);
  assert(state.hard_trip);
}

void test_invalid_max_uses_bounded_default() {
  State state;
  auto result = update(state, {0, 50.0f, NAN, 0});
  assert(result.max_c == 60.0f);
  result = update(state, {0, 50.0f, 100.0f, 0});
  assert(result.max_c == 75.0f);
}

}  // namespace

int main() {
  test_soft_limit_and_boiler_hysteresis();
  test_non_cm3_trip_is_immediate_and_latched();
  test_cm3_trip_requires_hold_and_survives_rollover();
  test_leaving_cm3_escalates_an_armed_trip_immediately();
  test_missing_supply_fails_closed_only_after_trip();
  test_invalid_max_uses_bounded_default();
  return 0;
}
