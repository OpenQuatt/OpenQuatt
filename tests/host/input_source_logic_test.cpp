#include <assert.h>
#include <limits.h>
#include <math.h>

#include "../../openquatt/includes/control/oq_input_source_logic.h"

using namespace oq_input_source;

void test_freshness_accepts_timestamp_zero_and_rollover() {
  TimedState state;
  assert(!evaluate_freshness(state, 1000, 10, true).valid);

  state.observe(0);
  auto result = evaluate_freshness(state, 0, 10, true);
  assert(result.valid);
  assert(result.age_s == 0.0f);
  assert(evaluate_freshness(state, 10000, 10, true).valid);
  assert(!evaluate_freshness(state, 10001, 10, true).valid);
  assert(evaluate_freshness(state, 100000, 0, true).valid);
  result = evaluate_freshness(state, 1000, 10, false);
  assert(!result.valid && isnan(result.age_s));

  state.observe(UINT32_MAX - 4U);
  result = evaluate_freshness(state, 5, 10, true);
  assert(result.valid);
  assert(result.age_s == 0.010f);
  assert(seconds_to_millis(UINT32_MAX / 1000U) == (UINT32_MAX / 1000U) * 1000U);
  assert(seconds_to_millis(UINT32_MAX) == UINT32_MAX);
}

void test_hold_is_bound_to_selected_source() {
  NumericSources sources;
  HoldState hold;

  sources.ha = numeric_sample(true, true, 20.0f);
  auto selected = select_direct(Source::HA, sources, true, 0, 300000, hold);
  assert(selected.valid && !selected.held && selected.value == 20.0f);

  sources.ha = {};
  selected = select_direct(Source::HA, sources, true, 1000, 300000, hold);
  assert(selected.valid && selected.held && selected.value == 20.0f);
  selected = select_direct(Source::HA, sources, true, 300000, 300000, hold);
  assert(!selected.valid);

  // Switching away clears the HA cache, so switching back cannot replay another source.
  sources.ha = numeric_sample(true, true, 21.0f);
  assert(select_direct(Source::HA, sources, true, 400000, 300000, hold).valid);
  sources.opentherm = numeric_sample(true, true, 19.0f);
  assert(select_direct(Source::OPENTHERM, sources, true, 401000, 300000, hold).value == 19.0f);
  sources.ha = {};
  assert(!select_direct(Source::HA, sources, true, 402000, 300000, hold).valid);

  // API inputs expire instead of inheriting the HA-only reload hold.
  sources.api = numeric_sample(true, true, 22.0f);
  assert(select_direct(Source::API, sources, true, 500000, 300000, hold).valid);
  sources.api = {};
  assert(!select_direct(Source::API, sources, true, 501000, 300000, hold).valid);
}

void test_non_finite_samples_fail_closed() {
  assert(!numeric_sample(true, true, INFINITY).valid);
  assert(!numeric_sample(true, true, NAN).valid);
  assert(!numeric_sample(false, true, 20.0f).valid);
}

void test_outside_lowest_valid_selection() {
  NumericSources sources;
  sources.ha = numeric_sample(true, true, 8.0f);
  sources.outdoor = numeric_sample(true, true, 5.0f);
  sources.api = numeric_sample(true, true, 6.0f);
  sources.mqtt = numeric_sample(true, true, 7.0f);

  HoldState hold;
  auto selected = select_outside(Source::AUTO, sources, 1000, 300000, hold);
  assert(selected.valid && selected.route == Source::OUTDOOR && selected.value == 5.0f);
  sources.outdoor = {};
  selected = select_outside(Source::AUTO, sources, 2000, 300000, hold);
  assert(selected.valid && selected.route == Source::API && selected.value == 6.0f);
}

void test_enable_source_selection() {
  EnableSources sources;
  sources.cic = {false, true};
  sources.ha = {true, true};
  sources.api = {true, false};
  sources.schedule = {false, true};

  auto heating = select_heating_enable(Source::DISABLED, sources);
  assert(heating.valid && heating.value);
  heating = select_heating_enable(Source::CIC, sources);
  assert(heating.valid && !heating.value);
  heating = select_heating_enable(Source::API, sources);
  assert(!heating.valid && !heating.value);

  auto cooling = select_cooling_enable(Source::CIC_OR_HA, sources, false);
  assert(cooling.valid && cooling.value);
  cooling = select_cooling_enable(Source::DISABLED, sources, false);
  assert(cooling.valid && !cooling.value);
  cooling = select_cooling_enable(Source::API, sources, true);
  assert(cooling.valid && cooling.value);
  sources.ha = {false, false};
  cooling = select_cooling_enable(Source::CIC_OR_HA, sources, false);
  assert(cooling.valid && !cooling.value);
  sources.cic = {false, false};
  cooling = select_cooling_enable(Source::CIC_OR_HA, sources, false);
  assert(!cooling.valid && !cooling.value);
  cooling = select_cooling_enable(Source::SCHEDULE, sources, false);
  assert(cooling.valid && !cooling.value);
  sources.schedule = {true, true};
  cooling = select_cooling_enable(Source::SCHEDULE, sources, false);
  assert(cooling.valid && cooling.value);
  sources.schedule = {false, false};
  cooling = select_cooling_enable(Source::SCHEDULE, sources, false);
  assert(!cooling.valid && !cooling.value);
  cooling = select_cooling_enable(Source::SCHEDULE, sources, true);
  assert(cooling.valid && cooling.value);
}

void test_flow_source_routes() {
  FlowInputs input;
  input.selected = Source::OUTDOOR;
  input.aggregate = numeric_sample(true, true, 900.0f);
  auto selected = select_flow(input);
  assert(selected.valid && selected.route == FlowRoute::AGGREGATE && selected.value == 900.0f);

  input.all_relevant_pumps_stopped = true;
  selected = select_flow(input);
  assert(selected.valid && selected.route == FlowRoute::PUMPS_STOPPED && selected.value == 0.0f);

  input.q_hardware = true;
  input.controller_mode = ControllerFlowMode::LOCAL;
  input.controller = numeric_sample(true, true, 850.0f);
  selected = select_flow(input);
  assert(selected.valid && selected.route == FlowRoute::CONTROLLER && selected.value == 850.0f);

  // Q Single V1 Auto also resolves exclusively to the controller flowmeter.
  input.controller_mode = ControllerFlowMode::AUTO;
  input.hp_generation_v1 = true;
  selected = select_flow(input);
  assert(selected.valid && selected.route == FlowRoute::CONTROLLER && selected.value == 850.0f);

  // Duo Auto keeps using the selected outdoor-unit route.
  input.duo = true;
  input.all_relevant_pumps_stopped = false;
  selected = select_flow(input);
  assert(selected.valid && selected.route == FlowRoute::AGGREGATE && selected.value == 900.0f);

  input.q_hardware = false;
  input.all_relevant_pumps_stopped = false;
  input.outdoor_mode = OutdoorFlowMode::HP2;
  input.hp2 = numeric_sample(true, true, 700.0f);
  selected = select_flow(input);
  assert(selected.valid && selected.route == FlowRoute::HP2 && selected.value == 700.0f);

  input.selected = Source::CIC;
  input.cic = numeric_sample(true, true, 600.0f);
  selected = select_flow(input);
  assert(selected.valid && selected.route == FlowRoute::CIC && selected.value == 600.0f);
}

int main() {
  test_freshness_accepts_timestamp_zero_and_rollover();
  test_hold_is_bound_to_selected_source();
  test_non_finite_samples_fail_closed();
  test_outside_lowest_valid_selection();
  test_enable_source_selection();
  test_flow_source_routes();
  return 0;
}
