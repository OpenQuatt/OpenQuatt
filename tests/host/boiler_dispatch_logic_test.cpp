#include <assert.h>
#include <math.h>

#include "../../openquatt/includes/control/oq_boiler_dispatch_logic.h"

namespace {

using oq_boiler_dispatch::Config;
using oq_boiler_dispatch::Inputs;

Inputs base_inputs() {
  Inputs in;
  in.now_ms = 5000;
  in.heat_request = true;
  in.rated_boiler_power_w = 10000.0f;
  in.maximum_water_temperature_c = 60.0f;
  return in;
}

void test_idle_command_is_valid_but_inactive() {
  const auto command = oq_boiler_dispatch::dispatch(base_inputs());
  assert(command.valid);
  assert(!command.demand_present);
  assert(!command.heat_request);
  assert(command.source == oq_boiler::COMMAND_SOURCE_NONE);
  assert(command.updated_at_ms == 5000);
}

void test_cold_start_owns_cm3_and_clamps_target() {
  auto in = base_inputs();
  in.control_mode = 3;
  in.cold_start_assist = true;
  in.maximum_water_temperature_c = 32.0f;
  const auto command = oq_boiler_dispatch::dispatch(in, {35.0f, 250.0f, 4180.0f});
  assert(command.valid);
  assert(command.demand_present);
  assert(command.heat_request);
  assert(command.source == oq_boiler::COMMAND_SOURCE_COLD_START);
  assert(command.target_temperature_c == 32.0f);
}

void test_power_house_uses_strategy_timestamp_and_hydraulic_target() {
  auto in = base_inputs();
  in.control_mode = 3;
  in.strategy = 3;
  in.strategy_output_valid = true;
  in.strategy_output_source = 3;
  in.strategy_updated_ms = 4000;
  in.strategy_requested_power_w = 9000.0f;
  in.hp_expected_power_w = 5000.0f;
  in.flow_lph = 720.0f;
  in.boiler_inlet_c = 30.0f;
  const auto command = oq_boiler_dispatch::dispatch(in);
  assert(command.valid);
  assert(command.heat_request);
  assert(command.source == oq_boiler::COMMAND_SOURCE_POWER_HOUSE);
  assert(command.updated_at_ms == 4000);
  assert(fabsf(command.requested_power_w - 4000.0f) < 0.01f);
  assert(command.target_temperature_c > 34.7f && command.target_temperature_c < 34.9f);
}

void test_power_house_rejects_stale_or_wrong_strategy_output() {
  auto in = base_inputs();
  in.control_mode = 3;
  in.strategy = 3;
  in.strategy_output_valid = true;
  in.strategy_output_source = 2;
  in.strategy_updated_ms = 4000;
  in.strategy_requested_power_w = 9000.0f;
  in.hp_expected_power_w = 5000.0f;
  auto command = oq_boiler_dispatch::dispatch(in);
  assert(!command.valid);
  assert(command.updated_at_ms == 4000);

  in.strategy_output_source = 3;
  in.strategy_updated_ms = 0;
  command = oq_boiler_dispatch::dispatch(in);
  assert(!command.valid);
  assert(command.updated_at_ms == 0);
}

void test_heating_curve_forwards_request_and_target() {
  auto in = base_inputs();
  in.control_mode = 3;
  in.strategy = 2;
  in.strategy_output_valid = true;
  in.strategy_output_source = 2;
  in.strategy_updated_ms = 4200;
  in.strategy_supply_target_c = 41.5f;
  const auto command = oq_boiler_dispatch::dispatch(in);
  assert(command.valid);
  assert(command.source == oq_boiler::COMMAND_SOURCE_HEATING_CURVE);
  assert(command.target_temperature_c == 41.5f);
  assert(command.updated_at_ms == 4200);
}

void test_fallback_and_commissioning_keep_authorization_provenance() {
  auto in = base_inputs();
  in.control_mode = 4;
  auto command = oq_boiler_dispatch::dispatch(in);
  assert(command.source == oq_boiler::COMMAND_SOURCE_FALLBACK);
  assert(command.updated_at_ms == 5000);

  in.control_mode = 100;
  in.commissioning_active = true;
  in.commissioning_boiler_task = true;
  in.commissioning_heat_request = true;
  in.commissioning_updated_ms = 1234;
  in.commissioning_target_c = 45.0f;
  command = oq_boiler_dispatch::dispatch(in);
  assert(command.source == oq_boiler::COMMAND_SOURCE_COMMISSIONING);
  assert(command.heat_request);
  assert(command.updated_at_ms == 1234);
  assert(command.target_temperature_c == 45.0f);
}

void test_power_house_without_flow_keeps_r1_power_but_no_ot_target() {
  auto in = base_inputs();
  in.control_mode = 3;
  in.strategy = 3;
  in.strategy_output_valid = true;
  in.strategy_output_source = 3;
  in.strategy_updated_ms = 4000;
  in.strategy_requested_power_w = 9000.0f;
  in.hp_expected_power_w = 5000.0f;
  in.flow_lph = NAN;
  const auto command = oq_boiler_dispatch::dispatch(in);
  assert(command.valid);
  assert(command.heat_request);
  assert(command.requested_power_w == 4000.0f);
  assert(isnan(command.target_temperature_c));
}

}  // namespace

int main() {
  test_idle_command_is_valid_but_inactive();
  test_cold_start_owns_cm3_and_clamps_target();
  test_power_house_uses_strategy_timestamp_and_hydraulic_target();
  test_power_house_rejects_stale_or_wrong_strategy_output();
  test_heating_curve_forwards_request_and_target();
  test_fallback_and_commissioning_keep_authorization_provenance();
  test_power_house_without_flow_keeps_r1_power_but_no_ot_target();
  return 0;
}
