#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "openquatt/includes/boiler/oq_boiler_logic.h"

namespace {

oq_boiler::BoilerCommand active_command(uint32_t updated_at_ms) {
  return oq_boiler::BoilerCommand{
      true, true, true, 5000.0f, 45.0f, oq_boiler::COMMAND_SOURCE_POWER_HOUSE, updated_at_ms,
  };
}

oq_boiler::ControllerInput safe_input(uint32_t now_ms) {
  return oq_boiler::ControllerInput{
      true,   true,  true,  true,   true,  true, true,
      false,  false, false, true,   true,  true, oq_boiler::BOILER_START_THERMAL_SAFE,
      true,   true,  false, now_ms, 15000, 0,    30000,
      120000,
  };
}

void assert_decision(const oq_boiler::ControllerDecision& decision, bool output_active, bool force_off,
                     uint8_t block_reason) {
  assert(decision.output_active == output_active);
  assert(decision.force_off == force_off);
  assert(decision.block_reason == block_reason);
}

void test_strategy_inputs() {
  const auto power_on = oq_boiler::power_house_assist(5000.0f, 4000.0f, 2000.0f);
  assert(power_on.need_on);
  assert(!power_on.okay_off);

  const auto power_off = oq_boiler::power_house_assist(1000.0f, 4000.0f, 2000.0f);
  assert(!power_off.need_on);
  assert(power_off.okay_off);

  const auto power_invalid = oq_boiler::power_house_assist(NAN, 4000.0f, 2000.0f);
  assert(!power_invalid.need_on);
  assert(power_invalid.okay_off);

  const auto curve_on = oq_boiler::heating_curve_assist(true, true, 45.0f, 40.0f, 3.0f, 1.0f);
  assert(curve_on.need_on);
  assert(!curve_on.okay_off);

  const auto curve_hold = oq_boiler::heating_curve_assist(true, true, 45.0f, 43.0f, 3.0f, 1.0f);
  assert(!curve_hold.need_on);
  assert(!curve_hold.okay_off);

  const auto curve_off = oq_boiler::heating_curve_assist(true, true, 45.0f, 44.5f, 3.0f, 1.0f);
  assert(!curve_off.need_on);
  assert(curve_off.okay_off);

  const auto curve_invalid = oq_boiler::heating_curve_assist(true, true, 45.0f, NAN, 3.0f, 1.0f);
  assert(!curve_invalid.need_on);
  assert(curve_invalid.okay_off);

  assert(oq_boiler::cm3_should_hold(false, false, false));
  assert(oq_boiler::cm3_should_hold(true, false, false));
  assert(oq_boiler::cm3_should_hold(true, true, false));
  assert(!oq_boiler::cm3_should_hold(true, true, true));
}

void test_power_target() {
  const auto target = oq_boiler::target_from_power(5000.0f, 4000.0f, 30.0f, 720.0f, 4180.0f, 50.0f);
  assert(target.valid);
  assert(fabsf(target.requested_power_w - 4000.0f) < 0.01f);
  assert(fabsf(target.target_temperature_c - 34.78469f) < 0.001f);

  const auto hydraulically_limited = oq_boiler::target_from_power(20000.0f, 20000.0f, 49.0f, 720.0f, 4180.0f, 50.0f);
  assert(hydraulically_limited.valid);
  assert(fabsf(hydraulically_limited.requested_power_w - 836.0f) < 0.01f);
  assert(fabsf(hydraulically_limited.target_temperature_c - 50.0f) < 0.001f);

  assert(!oq_boiler::target_from_power(5000.0f, 10000.0f, 30.0f, 0.0f, 4180.0f, 50.0f).valid);
  assert(!oq_boiler::target_from_power(5000.0f, 10000.0f, 30.0f, NAN, 4180.0f, 50.0f).valid);
  assert(!oq_boiler::target_from_power(5000.0f, 10000.0f, 50.0f, 720.0f, 4180.0f, 50.0f).valid);
}

void test_command_ownership_and_time() {
  const auto command = active_command(1000);

  assert(oq_boiler::strategy_output_is_current(true, 3, 3, 1000));
  assert(!oq_boiler::strategy_output_is_current(false, 3, 3, 1000));
  assert(!oq_boiler::strategy_output_is_current(true, 2, 3, 1000));
  assert(!oq_boiler::strategy_output_is_current(true, 3, 3, 0));

  assert(!oq_boiler::command_satisfies_rearm(true, command, 1000));
  assert(oq_boiler::command_satisfies_rearm(true, active_command(1001), 1000));
  assert(!oq_boiler::command_satisfies_rearm(true, active_command(999), 1000));
  assert(oq_boiler::timestamp_is_strictly_newer(5, UINT32_MAX - 5));
  assert(!oq_boiler::settle_period_elapsed(true, 5, UINT32_MAX - 5, 20));
  assert(oq_boiler::settle_period_elapsed(true, 20, UINT32_MAX - 5, 20));
  assert(oq_boiler::minimum_time_active(5, UINT32_MAX - 5, 20));
  assert(!oq_boiler::minimum_time_active(20, UINT32_MAX - 5, 20));

  // The command lease must accommodate the slowest (60 s) strategy cadence
  // with scheduler margin, while still expiring if that strategy stops.
  assert(oq_boiler::command_is_fresh(command, 76000, 75000));
  assert(!oq_boiler::command_is_fresh(command, 76001, 75000));
  assert(oq_boiler::command_is_fresh(active_command(UINT32_MAX - 5), 5, 20));

  const auto cm3 = oq_boiler::make_legacy_command(3, false, false, false, 100);
  assert(cm3.valid);
  assert(cm3.demand_present);
  assert(cm3.heat_request);
  assert(cm3.source == oq_boiler::COMMAND_SOURCE_CM3);

  const auto commissioning_waiting = oq_boiler::make_legacy_command(100, true, true, false, 100);
  assert(commissioning_waiting.demand_present);
  assert(!commissioning_waiting.heat_request);
  assert(commissioning_waiting.source == oq_boiler::COMMAND_SOURCE_COMMISSIONING);

  const auto no_owner = oq_boiler::make_legacy_command(5, false, false, false, 100);
  assert(!no_owner.demand_present);
  assert(!no_owner.heat_request);
  assert(no_owner.source == oq_boiler::COMMAND_SOURCE_NONE);
}

void test_effective_output_target() {
  assert(isnan(oq_boiler::effective_output_target(false, true, false, false, NAN, 45.0f)));
  assert(isnan(oq_boiler::effective_output_target(true, false, true, true, 50.0f, 45.0f)));
  assert(fabsf(oq_boiler::effective_output_target(true, true, true, true, 50.0f, 45.0f) - 50.0f) < 0.001f);
  assert(fabsf(oq_boiler::effective_output_target(true, true, false, false, NAN, 45.0f) - 45.0f) < 0.001f);
}

void test_boiler_start_thermal_policy() {
  auto decision = oq_boiler::evaluate_boiler_start_thermal_state(false, false, NAN, 23.0f, 30.0f, 35.0f);
  assert(decision.state == oq_boiler::BOILER_START_THERMAL_NOT_APPLICABLE);

  decision = oq_boiler::evaluate_boiler_start_thermal_state(true, true, 23.0f, 23.0f, 30.0f, 35.0f);
  assert(decision.state == oq_boiler::BOILER_START_THERMAL_SAFE);
  assert(fabsf(decision.safe_ceiling_c - 32.0f) < 0.001f);

  decision = oq_boiler::evaluate_boiler_start_thermal_state(true, true, 48.0f, 23.0f, 30.0f, 35.0f);
  assert(decision.state == oq_boiler::BOILER_START_THERMAL_HOT);
  assert(fabsf(decision.safe_ceiling_c - 32.0f) < 0.001f);

  decision = oq_boiler::evaluate_boiler_start_thermal_state(true, true, 34.0f, 34.0f, 30.0f, 35.0f);
  assert(decision.state == oq_boiler::BOILER_START_THERMAL_SAFE);
  assert(fabsf(decision.safe_ceiling_c - 35.0f) < 0.001f);

  assert(oq_boiler::evaluate_boiler_start_thermal_state(true, false, 23.0f, 23.0f, 30.0f, 35.0f).state ==
         oq_boiler::BOILER_START_THERMAL_UNKNOWN);
  assert(oq_boiler::evaluate_boiler_start_thermal_state(true, true, NAN, 23.0f, 30.0f, 35.0f).state ==
         oq_boiler::BOILER_START_THERMAL_UNKNOWN);
  assert(oq_boiler::evaluate_boiler_start_thermal_state(true, true, INFINITY, 23.0f, 30.0f, 35.0f).state ==
         oq_boiler::BOILER_START_THERMAL_UNKNOWN);
}

void test_warm_start_controller_interlock() {
  auto command = active_command(1000);
  auto input = safe_input(1500);
  input.boiler_start_thermal_state = oq_boiler::BOILER_START_THERMAL_HOT;
  auto decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_BOILER_TOO_HOT_FOR_START);

  command.source = oq_boiler::COMMAND_SOURCE_FALLBACK;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_BOILER_TOO_HOT_FOR_START);

  command.source = oq_boiler::COMMAND_SOURCE_COMMISSIONING;
  input.boiler_start_thermal_state = oq_boiler::BOILER_START_THERMAL_UNKNOWN;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_BOILER_TEMPERATURE_UNAVAILABLE);

  command.source = oq_boiler::COMMAND_SOURCE_POWER_HOUSE;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, true, false, oq_boiler::BLOCK_NONE);

  command.source = oq_boiler::COMMAND_SOURCE_FALLBACK;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, true, false, oq_boiler::BLOCK_NONE);

  // The warm-start guard applies only to the OFF -> ON edge. Once the boiler
  // is active, normal supply inhibit and hard-trip guards remain authoritative.
  command.source = oq_boiler::COMMAND_SOURCE_COMMISSIONING;
  input.output_active = true;
  input.output_last_change_ms = 1499;
  input.boiler_start_thermal_state = oq_boiler::BOILER_START_THERMAL_HOT;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, true, false, oq_boiler::BLOCK_NONE);
}

void test_fail_safe_priority() {
  const auto command = active_command(1000);
  auto input = safe_input(1500);
  input.output_active = true;
  input.output_last_change_ms = 1499;

  input.hard_trip_active = true;
  input.boiler_inhibit_active = true;
  auto decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_WATER_TEMP_HARD_TRIP);

  input = safe_input(1500);
  input.output_active = true;
  input.output_last_change_ms = 1499;
  input.boiler_inhibit_active = true;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_WATER_TEMP_INHIBIT);

  input = safe_input(1500);
  input.source_present = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_SOURCE_NOT_CONNECTED);

  input = safe_input(1500);
  input.assist_enabled = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_ASSIST_DISABLED);

  auto invalid_command = command;
  invalid_command.valid = false;
  input = safe_input(1500);
  decision = oq_boiler::evaluate(invalid_command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_COMMAND_INVALID);

  input = safe_input(76001);
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_COMMAND_STALE);

  input = safe_input(1500);
  input.supply_temperature_valid = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_SUPPLY_UNAVAILABLE);

  input = safe_input(1500);
  input.transport_settled = false;
  input.transport_available = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_TRANSPORT_SETTLING);

  input = safe_input(1500);
  input.connection_mismatch = true;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_CONNECTION_MISMATCH);

  input = safe_input(1500);
  input.transport_available = false;
  input.command_rearmed = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_TRANSPORT_UNAVAILABLE);

  input = safe_input(1500);
  input.command_rearmed = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_AWAITING_FRESH_COMMAND);

  input = safe_input(1500);
  input.target_valid = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_TARGET_INVALID);

  input.target_required = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, true, false, oq_boiler::BLOCK_NONE);
}

void test_transport_selection_guard() {
  assert(oq_boiler::connection_guard_active(true, false));
  assert(oq_boiler::connection_guard_active(false, true));
  assert(!oq_boiler::connection_guard_active(false, false));

  assert(!oq_boiler::transport_available_for_selection(true, false, true, false, true, false));
  assert(!oq_boiler::transport_available_for_selection(true, false, true, false, false, true));
  assert(oq_boiler::transport_available_for_selection(true, false, true, false, false, false));
  assert(!oq_boiler::transport_available_for_selection(false, false, true, false, false, false));
  assert(oq_boiler::transport_available_for_selection(true, true, true, true, false, false));
  assert(!oq_boiler::transport_available_for_selection(true, true, true, false, false, false));

  assert(oq_boiler::relay_must_be_off(true, false, false));
  assert(oq_boiler::relay_must_be_off(false, true, false));
  assert(oq_boiler::relay_must_be_off(false, false, true));
  assert(!oq_boiler::relay_must_be_off(false, false, false));
}

void test_fallback_and_flow_guards() {
  auto command = active_command(1000);
  command.source = oq_boiler::COMMAND_SOURCE_FALLBACK;

  auto input = safe_input(1500);
  input.assist_enabled = false;
  auto decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, true, false, oq_boiler::BLOCK_NONE);

  input = safe_input(1500);
  input.fallback_enabled = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_FALLBACK_DISABLED);

  input = safe_input(1500);
  input.flow_valid = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_FLOW_UNAVAILABLE);

  input = safe_input(1500);
  input.flow_sufficient = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_FLOW_INSUFFICIENT);

  input = safe_input(1500);
  input.fallback_outputs_safe = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_HP_STOP_UNCONFIRMED);
}

void test_cold_start_requires_assist_permission() {
  auto command = active_command(1500);
  command.source = oq_boiler::COMMAND_SOURCE_COLD_START;
  auto input = safe_input(1500);

  auto decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, true, false, oq_boiler::BLOCK_NONE);

  input.assist_enabled = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_ASSIST_DISABLED);
}

void test_minimum_times_and_ownership_loss() {
  const auto command = active_command(1000);
  auto no_heat_command = command;
  no_heat_command.heat_request = false;

  auto input = safe_input(1500);
  input.command_max_age_ms = 0;
  input.output_active = true;
  input.output_last_change_ms = 1400;
  auto decision = oq_boiler::evaluate(no_heat_command, input);
  assert_decision(decision, true, false, oq_boiler::BLOCK_MIN_ON_TIME);

  input.now_ms = 31400;
  decision = oq_boiler::evaluate(no_heat_command, input);
  assert_decision(decision, false, false, oq_boiler::BLOCK_NO_HEAT_REQUEST);
  assert(decision.blocked);

  input = safe_input(1500);
  input.command_max_age_ms = 0;
  input.output_active = false;
  input.output_last_change_ms = 1400;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, false, oq_boiler::BLOCK_MIN_OFF_TIME);
  assert(decision.blocked);

  input.now_ms = 121400;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, true, false, oq_boiler::BLOCK_NONE);

  // Losing the owning strategy must override minimum on-time. Otherwise an
  // OTB request could continue after entering cooling or an off mode.
  auto no_owner_command = command;
  no_owner_command.demand_present = false;
  input = safe_input(1500);
  input.output_active = true;
  input.output_last_change_ms = 1499;
  decision = oq_boiler::evaluate(no_owner_command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_NO_HEAT_REQUEST);
  assert(!decision.blocked);

  // Transport and thermal failures are also hard overrides while minimum
  // on-time is active.
  input = safe_input(1500);
  input.output_active = true;
  input.output_last_change_ms = 1499;
  input.transport_available = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_TRANSPORT_UNAVAILABLE);

  input = safe_input(1500);
  input.output_active = true;
  input.output_last_change_ms = 1499;
  input.hard_trip_active = true;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_WATER_TEMP_HARD_TRIP);

  // An invalid OpenTherm target also represents an unavailable/insufficient
  // flow measurement in the YAML adapter. It must override minimum on-time so
  // CH Enable and TSet are withdrawn on the next safety evaluation.
  input = safe_input(1500);
  input.output_active = true;
  input.output_last_change_ms = 1499;
  input.target_valid = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, true, oq_boiler::BLOCK_TARGET_INVALID);
}

void test_commissioning_wait_state() {
  auto command = active_command(1000);
  command.source = oq_boiler::COMMAND_SOURCE_COMMISSIONING;
  command.heat_request = false;

  auto input = safe_input(1500);
  input.assist_enabled = false;
  auto decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, false, false, oq_boiler::BLOCK_COMMISSIONING_WAITING);
  assert(decision.blocked);

  input.output_active = true;
  input.output_last_change_ms = 1400;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, true, false, oq_boiler::BLOCK_MIN_ON_TIME);

  command.heat_request = true;
  input = safe_input(1500);
  input.assist_enabled = false;
  decision = oq_boiler::evaluate(command, input);
  assert_decision(decision, true, false, oq_boiler::BLOCK_NONE);
}

void test_commissioning_start_failure_reason() {
  assert(
      strcmp(oq_boiler::commissioning_start_failure_reason(oq_boiler::BLOCK_TRANSPORT_UNAVAILABLE, true, false, false),
             oq_boiler::block_reason_text(oq_boiler::BLOCK_TRANSPORT_UNAVAILABLE)) == 0);
  assert(strcmp(oq_boiler::commissioning_start_failure_reason(oq_boiler::BLOCK_NONE, true, true, false),
                "OpenTherm link unavailable") == 0);
  assert(strcmp(oq_boiler::commissioning_start_failure_reason(oq_boiler::BLOCK_NONE, true, false, true),
                "boiler request not applied") == 0);
  assert(strcmp(oq_boiler::commissioning_start_failure_reason(oq_boiler::BLOCK_NONE, true, true, true),
                "OpenTherm CH active not confirmed") == 0);
  assert(strcmp(oq_boiler::commissioning_start_failure_reason(oq_boiler::BLOCK_NONE, false, true, false),
                "boiler active state not confirmed") == 0);
}

}  // namespace

int main() {
  test_strategy_inputs();
  test_power_target();
  test_command_ownership_and_time();
  test_effective_output_target();
  test_boiler_start_thermal_policy();
  test_warm_start_controller_interlock();
  test_fail_safe_priority();
  test_transport_selection_guard();
  test_fallback_and_flow_guards();
  test_cold_start_requires_assist_permission();
  test_minimum_times_and_ownership_loss();
  test_commissioning_wait_state();
  test_commissioning_start_failure_reason();
  return 0;
}
