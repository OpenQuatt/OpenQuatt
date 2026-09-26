#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string_view>

#include "../../openquatt/includes/control/oq_input_source_logic.h"

using namespace oq_input_source;

struct StubValidEntity {
  bool present = false;
  bool state = false;
  bool has_state() const { return present; }
};

struct StubValueEntity {
  bool present = false;
  float state = NAN;
  bool has_state() const { return present; }
};

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

void test_room_setpoint_contract_is_signal_specific() {
  assert(!room_setpoint_usable(NAN));
  assert(!room_setpoint_usable(INFINITY));
  assert(!room_setpoint_usable(0.0f));
  assert(!room_setpoint_usable(4.9f));
  assert(room_setpoint_usable(5.0f));
  assert(room_setpoint_usable(20.0f));
  assert(room_setpoint_usable(35.0f));
  assert(!room_setpoint_usable(35.1f));

  assert(!room_setpoint_sample(true, true, 0.0f).valid);
  assert(room_setpoint_sample(true, true, 20.0f).valid);
  assert(!room_setpoint_sample(false, true, 20.0f).valid);

  // Zero remains a valid generic numeric value. The restriction belongs only
  // to Room Setpoint, so flow, power and other zero-valued signals are not
  // accidentally filtered by the shared input-source layer.
  assert(numeric_sample(true, true, 0.0f).valid);
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
  assert(std::string_view(flow_route_name(selected.route)) == "Aggregate");

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

void test_ha_live_constant_value_survives_on_heartbeat() {
  // Room temperature 20.5 °C never changes, but the HA ingress heartbeat
  // keeps arriving every minute: the live input must stay valid well beyond
  // its 600 s stale window (issue #698).
  StubValidEntity valid{true, true};
  StubValueEntity value{true, 20.5f};
  TimedState ingress;
  ingress.observe(0);
  for (uint32_t now_ms = 60000; now_ms <= 3600000; now_ms += 60000) {
    ingress.observe(now_ms);
    assert(ha_live_valid(valid, value, ingress, now_ms, 600));
  }
}

void test_ha_live_goes_stale_without_heartbeat_and_recovers() {
  // Heating supply target 40.0 °C with a heartbeat every minute stays at
  // 40.0 without falling back; once the heartbeat stops for more than the
  // 900 s window the HA input goes invalid, and it becomes usable again as
  // soon as the heartbeat resumes (issue #698).
  StubValidEntity valid{true, true};
  StubValueEntity value{true, 40.0f};
  TimedState ingress;
  ingress.observe(0);
  for (uint32_t now_ms = 60000; now_ms <= 1800000; now_ms += 60000) {
    ingress.observe(now_ms);
    assert(ha_live_valid(valid, value, ingress, now_ms, 900));
  }
  assert(!ha_live_valid(valid, value, ingress, 1800000 + 900001, 900));
  ingress.observe(2700001);
  assert(ha_live_valid(valid, value, ingress, 2700001, 900));
}

void test_ha_live_validity_off_overrides_fresh_heartbeat() {
  // An explicitly switched-off validity flag invalidates the input at once,
  // no matter how fresh the heartbeat is; a missing or non-finite value does
  // the same.
  StubValidEntity valid{true, true};
  StubValueEntity value{true, 40.0f};
  TimedState ingress;
  ingress.observe(1000);
  assert(ha_live_valid(valid, value, ingress, 1000, 900));

  StubValidEntity switched_off{true, false};
  assert(!ha_live_valid(switched_off, value, ingress, 1000, 900));

  StubValidEntity missing{false, false};
  assert(!ha_live_valid(missing, value, ingress, 1000, 900));

  StubValueEntity no_value{false, NAN};
  assert(!ha_live_valid(valid, no_value, ingress, 1000, 900));

  StubValueEntity nan_value{true, NAN};
  assert(!ha_live_valid(valid, nan_value, ingress, 1000, 900));

  StubValueEntity inf_value{true, INFINITY};
  assert(!ha_live_valid(valid, inf_value, ingress, 1000, 900));
}

void test_ha_live_zero_timeout_never_expires() {
  // Pure helper boundary: stale_s = 0 means "never expire" once an ingress
  // clock exists. Stateful HA inputs bypass this helper entirely and keep
  // plain entity validity.
  StubValidEntity valid{true, true};
  StubValueEntity value{true, 20.0f};
  TimedState ingress;
  assert(!ha_live_valid(valid, value, ingress, 100000, 0));
  ingress.observe(0);
  assert(ha_live_valid(valid, value, ingress, 0, 0));
  assert(ha_live_valid(valid, value, ingress, 3600000, 0));
}

void test_ha_live_legacy_without_heartbeat() {
  // Backward compatibility with pre-heartbeat HA packages and custom
  // proxies: a valid HA proxy stays usable while this boot never received
  // a heartbeat, so an OTA never suddenly rejects existing HA ingress.
  // After the first heartbeat, freshness gating is permanent for that boot.
  TimedState ingress;
  assert(ha_live_valid_with_legacy(true, ingress, 3600000, 600));
  assert(!ha_live_valid_with_legacy(false, ingress, 3600000, 600));

  ingress.observe(3600000);
  assert(ha_live_valid_with_legacy(true, ingress, 3600000, 600));
  assert(!ha_live_valid_with_legacy(true, ingress, 3600000 + 600001, 600));
  assert(!ha_live_valid_with_legacy(false, ingress, 3600000, 600));
}

void test_ha_live_legacy_freshness_preserves_supply_target_timeout() {
  // Heating Supply Target had a per-value freshness timer before the shared
  // heartbeat. With an old HA package (no heartbeat), keep that 900 s timeout
  // instead of accepting the retained proxy state forever.
  TimedState ingress;
  TimedState legacy;
  assert(!ha_live_valid_with_legacy_freshness(true, ingress, legacy, 1000, 900));

  legacy.observe(1000);
  assert(ha_live_valid_with_legacy_freshness(true, ingress, legacy, 1000, 900));
  assert(!ha_live_valid_with_legacy_freshness(true, ingress, legacy, 901002, 900));

  // After the first shared heartbeat it is authoritative for the rest of the
  // boot, even if legacy target publishes continue to arrive.
  ingress.observe(1000000);
  assert(ha_live_valid_with_legacy_freshness(true, ingress, legacy, 1000000, 900));
  legacy.observe(1800000);
  assert(!ha_live_valid_with_legacy_freshness(true, ingress, legacy, 1900001, 900));
}

void test_ha_live_millis_rollover() {
  StubValidEntity valid{true, true};
  StubValueEntity value{true, 20.5f};
  TimedState ingress;
  ingress.observe(UINT32_MAX - 30000U);
  assert(ha_live_valid(valid, value, ingress, 60000, 600));
  assert(!ha_live_valid(valid, value, ingress, 600000 + 60000, 600));
}

int main() {
  test_freshness_accepts_timestamp_zero_and_rollover();
  test_hold_is_bound_to_selected_source();
  test_non_finite_samples_fail_closed();
  test_room_setpoint_contract_is_signal_specific();
  test_outside_lowest_valid_selection();
  test_enable_source_selection();
  test_flow_source_routes();
  test_ha_live_constant_value_survives_on_heartbeat();
  test_ha_live_goes_stale_without_heartbeat_and_recovers();
  test_ha_live_validity_off_overrides_fresh_heartbeat();
  test_ha_live_zero_timeout_never_expires();
  test_ha_live_legacy_without_heartbeat();
  test_ha_live_legacy_freshness_preserves_supply_target_timeout();
  test_ha_live_millis_rollover();
  return 0;
}
