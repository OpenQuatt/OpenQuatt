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

void test_post_dhw_window_allows_an_early_pulse_with_a_longer_cooling_tail() {
  constexpr uint64_t dhw_end_ms = 2000;
  assert(!post_dhw_window_open(2000, 0, false));
  assert(!post_dhw_window_open(1999, dhw_end_ms, false));
  assert(post_dhw_window_open(31999, dhw_end_ms, false));
  assert(!post_dhw_window_open(32000, dhw_end_ms, false));
  assert(post_dhw_window_open(52000, dhw_end_ms, true));
  assert(post_dhw_window_open(91999, dhw_end_ms, true));
  assert(!post_dhw_window_open(92000, dhw_end_ms, true));
}

void test_post_dhw_pulse_keeps_the_running_heat_pump_and_inhibits_the_boiler() {
  State state;
  update(state, {1000, 69.65f, 46.0f, 3, 200, 34.0f});
  assert(state.post_dhw_active);
  assert(state.boiler_inhibit);
  assert(!state.trip && !state.hard_trip);
  assert(state.limit_factor == 1.0f);

  update(state, {2000, 55.0f, 46.0f, 2, 200, 35.0f});
  assert(state.post_dhw_active);
  assert(state.boiler_inhibit);
  update(state, {3000, 43.0f, 46.0f, 2, 200, 35.0f});
  assert(!state.post_dhw_active && !state.boiler_inhibit);

  update(state, {4000, 65.6f, 46.0f, 2, 200, 35.0f});
  assert(!state.post_dhw_active && state.trip && state.hard_trip);
}

void test_post_dhw_window_never_clears_an_existing_trip() {
  State state;
  update(state, {1000, 65.0f, 46.0f, 2});
  update(state, {2000, 60.0f, 46.0f, 2, 1500, 32.0f});
  assert(!state.post_dhw_active && state.trip && state.hard_trip);
  assert(state.limit_factor == 0.0f);
}

void test_post_dhw_reverts_to_hard_stop_when_the_handover_is_no_longer_safe() {
  const Inputs guards[] = {{3000, 65.6f, 46.0f, 3, 0, 34.0f},
                           {3000, 65.6f, 46.0f, 3, 200, 52.0f},
                           {3000, 65.6f, 46.0f, 3, 200, NAN},
                           {3000, 70.0f, 46.0f, 3, 200, 34.0f},
                           {3000, 65.6f, 46.0f, 3, 2500, 34.0f}};
  for (const Inputs& next : guards) {
    State state;
    update(state, {1000, 65.6f, 46.0f, 3, 200, 34.0f});
    update(state, next);
    assert(!state.post_dhw_active && state.trip && state.hard_trip);
    assert(state.limit_factor == 0.0f);
  }
}

void test_post_dhw_requires_an_observed_cool_supply_to_clear_after_pt1000_loss() {
  State state;
  update(state, {1000, 65.6f, 46.0f, 3, 200, 34.0f});
  update(state, {2000, 34.0f, 46.0f, 3, 0, NAN, false});
  assert(state.awaiting_supply && state.hard_trip && state.boiler_inhibit);
  update(state, {3000, 34.0f, 46.0f, 2, 0, NAN, false});
  assert(state.awaiting_supply && state.hard_trip && state.boiler_inhibit);
  update(state, {4000, 55.0f, 46.0f, 2, 200, 34.0f});
  assert(state.awaiting_supply && state.hard_trip && state.boiler_inhibit);
  update(state, {5000, 40.0f, 46.0f, 2});
  assert(!state.hard_trip && !state.boiler_inhibit);
}

void test_post_dhw_odu_overheating_stops_even_when_the_pt1000_is_cool() {
  State state;
  update(state, {1000, 65.6f, 46.0f, 3, 200, 34.0f});
  update(state, {2000, 42.0f, 46.0f, 3, 0, 53.0f});
  assert(!state.post_dhw_active && state.awaiting_heat_pump_water && state.heat_pump_hard_trip());
  assert(!state.hard_trip && !state.boiler_inhibit);

  update(state, {3000, 41.0f, 46.0f, 1, 0, NAN, true, false, false});
  assert(state.awaiting_heat_pump_water && state.heat_pump_hard_trip());
  update(state, {4000, 41.0f, 46.0f, 1, 0, 48.0f, true, true, false});
  assert(state.awaiting_heat_pump_water && state.heat_pump_hard_trip());
  update(state, {5000, 41.0f, 46.0f, 1, 0, 46.0f, true, true, false});
  assert(!state.heat_pump_hard_trip());
}

void test_post_dhw_derates_against_the_odu_at_a_35_degree_maximum() {
  State state;
  update(state, {1000, 35.2f, 35.0f, 3, 200, 34.0f});
  assert(!state.post_dhw_active);
  update(state, {2000, 65.6f, 35.0f, 3, 200, 34.0f});
  assert(state.post_dhw_active && state.boiler_inhibit && !state.heat_pump_hard_trip());
  assert(fabsf(state.limit_factor - 0.5f) < 0.001f);
  update(state, {3000, 65.6f, 35.0f, 3, 200, 35.0f});
  assert(!state.post_dhw_active && state.heat_pump_hard_trip());
}

void test_post_dhw_keeps_the_independent_system_ceiling_at_a_high_configured_limit() {
  State state;
  update(state, {1000, 70.0f, 75.0f, 3, 200, 40.0f});
  assert(state.awaiting_supply && state.hard_trip);
  update(state, {2000, 69.5f, 75.0f, 1});
  assert(state.awaiting_supply && state.hard_trip);
  update(state, {3000, 69.0f, 75.0f, 1});
  assert(!state.awaiting_supply && !state.hard_trip);
}

}  // namespace

int main() {
  test_soft_limit_and_boiler_hysteresis();
  test_non_cm3_trip_is_immediate_and_latched();
  test_cm3_trip_requires_hold_and_survives_rollover();
  test_leaving_cm3_escalates_an_armed_trip_immediately();
  test_missing_supply_fails_closed_only_after_trip();
  test_invalid_max_uses_bounded_default();
  test_post_dhw_window_allows_an_early_pulse_with_a_longer_cooling_tail();
  test_post_dhw_pulse_keeps_the_running_heat_pump_and_inhibits_the_boiler();
  test_post_dhw_window_never_clears_an_existing_trip();
  test_post_dhw_reverts_to_hard_stop_when_the_handover_is_no_longer_safe();
  test_post_dhw_requires_an_observed_cool_supply_to_clear_after_pt1000_loss();
  test_post_dhw_odu_overheating_stops_even_when_the_pt1000_is_cool();
  test_post_dhw_derates_against_the_odu_at_a_35_degree_maximum();
  test_post_dhw_keeps_the_independent_system_ceiling_at_a_high_configured_limit();
  return 0;
}
