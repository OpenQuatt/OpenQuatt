#include <assert.h>
#include <math.h>
#include <string.h>

#include "../../openquatt/includes/control/oq_aux_relay_logic.h"

namespace {

using namespace oq_aux_relay;

Inputs base_inputs(Function function, int control_mode) {
  Inputs inputs;
  inputs.function = function;
  inputs.control_mode = control_mode;
  inputs.heating_start_c = 30.0f;
  inputs.cooling_start_c = 18.0f;
  inputs.hysteresis_c = 2.0f;
  return inputs;
}

void test_control_modes_and_cm1_destination() {
  State state;
  auto result = update(state, base_inputs(Function::HEATING, 2));
  assert(result.relay_on);
  assert(result.status == Status::HEATING_ACTIVE);

  auto inputs = base_inputs(Function::COOLING, 1);
  inputs.cm1_next_mode = 5;
  result = update(state, inputs);
  assert(result.relay_on);
  assert(result.thermal_mode == ThermalMode::COOLING);

  result = update(state, base_inputs(Function::HEATING_OR_COOLING, 98));
  assert(!result.relay_on);
  assert(result.status == Status::NO_THERMAL);
}

void test_heating_gate_hysteresis_and_missing_supply() {
  State state;
  auto inputs = base_inputs(Function::HEATING, 2);
  inputs.temperature_gate_enabled = true;
  inputs.supply_c = 29.0f;
  auto result = update(state, inputs);
  assert(!result.relay_on);
  assert(result.status == Status::WAITING_WARM);

  inputs.supply_c = 30.0f;
  result = update(state, inputs);
  assert(result.relay_on);
  inputs.supply_c = 29.0f;
  result = update(state, inputs);
  assert(result.relay_on);
  inputs.supply_c = 28.0f;
  result = update(state, inputs);
  assert(!result.relay_on);

  inputs.supply_c = NAN;
  result = update(state, inputs);
  assert(!result.relay_on);
  assert(result.status == Status::SUPPLY_UNAVAILABLE);
}

void test_cooling_gate_and_mode_change_reset() {
  State state;
  auto cooling = base_inputs(Function::HEATING_OR_COOLING, 5);
  cooling.temperature_gate_enabled = true;
  cooling.supply_c = 18.0f;
  assert(update(state, cooling).relay_on);
  cooling.supply_c = 19.0f;
  assert(update(state, cooling).relay_on);

  auto heating = base_inputs(Function::HEATING_OR_COOLING, 2);
  heating.temperature_gate_enabled = true;
  heating.supply_c = 29.0f;
  const auto result = update(state, heating);
  assert(!result.relay_on);
  assert(result.status == Status::WAITING_WARM);
}

void test_external_control_is_explicit_and_retained() {
  State state;
  assert(!apply_external_command(state, Function::HEATING, true));
  assert(!state.relay_on);
  assert(apply_external_command(state, Function::EXTERNAL, true));
  assert(state.relay_on);

  auto result = update(state, base_inputs(Function::EXTERNAL, 0));
  assert(result.relay_on);
  assert(!result.changed);
  assert(strcmp(status_text(result.status), "External control") == 0);

  result = update(state, base_inputs(Function::DISABLED, 0));
  assert(!result.relay_on);
  assert(result.changed);
}

void test_command_outside_external_control_cannot_override_owner() {
  State state;
  assert(update(state, base_inputs(Function::HEATING, 2)).relay_on);
  assert(!apply_external_command(state, Function::HEATING, false));
  assert(state.relay_on);
}

Inputs run_on_inputs(Function function, int control_mode, RunOnMode run_on_mode, uint32_t now_ms) {
  auto inputs = base_inputs(function, control_mode);
  inputs.run_on_mode = run_on_mode;
  inputs.run_on_s = 300.0f;
  inputs.now_ms = now_ms;
  return inputs;
}

void test_fixed_time_run_on_after_demand_ends() {
  State state;
  assert(update(state, run_on_inputs(Function::HEATING, 2, RunOnMode::FIXED_TIME, 0)).relay_on);

  auto result = update(state, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, 1000));
  assert(result.relay_on);
  assert(!result.changed);
  assert(result.status == Status::RUN_ON);
  assert(strcmp(status_text(result.status), "Run-on active") == 0);

  result = update(state, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, 300999));
  assert(result.relay_on);
  result = update(state, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, 301000));
  assert(!result.relay_on);
  assert(result.changed);
  assert(result.status == Status::NO_HEATING);

  result = update(state, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, 302000));
  assert(!result.relay_on);
}

void test_run_on_until_supply_temp_is_bounded_by_time() {
  State state;
  auto inputs = run_on_inputs(Function::HEATING, 2, RunOnMode::UNTIL_SUPPLY_TEMP, 0);
  inputs.supply_c = 35.0f;
  assert(update(state, inputs).relay_on);

  // Run-on ends at the gate's switch-off threshold: heating start 30 °C minus 2 °C hysteresis.
  inputs.control_mode = 0;
  inputs.now_ms = 5000;
  assert(update(state, inputs).relay_on);
  inputs.supply_c = 28.5f;
  assert(update(state, inputs).relay_on);
  inputs.supply_c = 28.0f;
  assert(!update(state, inputs).relay_on);

  // Supply that never cools down still ends at the run-on time.
  State capped;
  inputs = run_on_inputs(Function::HEATING, 2, RunOnMode::UNTIL_SUPPLY_TEMP, 0);
  inputs.supply_c = 35.0f;
  assert(update(capped, inputs).relay_on);
  inputs.control_mode = 0;
  inputs.now_ms = 1000;
  assert(update(capped, inputs).relay_on);
  inputs.now_ms = 300999;
  assert(update(capped, inputs).relay_on);
  inputs.now_ms = 301000;
  assert(!update(capped, inputs).relay_on);

  State missing;
  inputs = run_on_inputs(Function::HEATING, 2, RunOnMode::UNTIL_SUPPLY_TEMP, 0);
  inputs.supply_c = 35.0f;
  assert(update(missing, inputs).relay_on);
  inputs.control_mode = 0;
  inputs.supply_c = NAN;
  assert(!update(missing, inputs).relay_on);
}

void test_cooling_run_on_until_supply_warms_up() {
  State state;
  auto inputs = run_on_inputs(Function::COOLING, 5, RunOnMode::UNTIL_SUPPLY_TEMP, 0);
  inputs.supply_c = 16.0f;
  assert(update(state, inputs).relay_on);

  inputs.control_mode = 0;
  inputs.supply_c = 19.5f;
  const auto result = update(state, inputs);
  assert(result.relay_on);
  assert(result.status == Status::RUN_ON);
  inputs.supply_c = 20.0f;
  assert(!update(state, inputs).relay_on);
}

void test_run_on_is_cancelled_by_demand_return_and_owner_changes() {
  State state;
  assert(update(state, run_on_inputs(Function::HEATING, 2, RunOnMode::FIXED_TIME, 0)).relay_on);
  assert(update(state, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, 1000)).relay_on);

  // Returning demand owns the relay again and a later stop restarts the run-on timer.
  auto result = update(state, run_on_inputs(Function::HEATING, 2, RunOnMode::FIXED_TIME, 200000));
  assert(result.relay_on);
  assert(result.status == Status::HEATING_ACTIVE);
  assert(update(state, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, 250000)).relay_on);
  assert(update(state, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, 549000)).relay_on);
  assert(!update(state, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, 550000)).relay_on);

  State disabled;
  assert(update(disabled, run_on_inputs(Function::HEATING, 2, RunOnMode::FIXED_TIME, 0)).relay_on);
  assert(update(disabled, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, 1000)).relay_on);
  assert(!update(disabled, run_on_inputs(Function::HEATING, 0, RunOnMode::OFF, 2000)).relay_on);

  State opposite;
  assert(update(opposite, run_on_inputs(Function::HEATING, 2, RunOnMode::FIXED_TIME, 0)).relay_on);
  assert(update(opposite, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, 1000)).relay_on);
  result = update(opposite, run_on_inputs(Function::HEATING, 5, RunOnMode::FIXED_TIME, 2000));
  assert(!result.relay_on);
  assert(result.status == Status::NO_HEATING);

  State external;
  assert(update(external, run_on_inputs(Function::HEATING, 2, RunOnMode::FIXED_TIME, 0)).relay_on);
  assert(update(external, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, 1000)).relay_on);
  assert(update(external, run_on_inputs(Function::EXTERNAL, 0, RunOnMode::FIXED_TIME, 2000)).relay_on);
  assert(apply_external_command(external, Function::EXTERNAL, false));
  assert(!update(external, run_on_inputs(Function::EXTERNAL, 0, RunOnMode::FIXED_TIME, 3000)).relay_on);
}

void test_run_on_only_extends_a_relay_that_was_on() {
  State gated;
  auto inputs = run_on_inputs(Function::HEATING, 2, RunOnMode::FIXED_TIME, 0);
  inputs.temperature_gate_enabled = true;
  inputs.supply_c = 29.0f;
  assert(!update(gated, inputs).relay_on);
  inputs.control_mode = 0;
  inputs.now_ms = 1000;
  assert(!update(gated, inputs).relay_on);

  // A relay held on by external control is not extended after switching to a demand function.
  State external;
  assert(apply_external_command(external, Function::EXTERNAL, true));
  assert(update(external, run_on_inputs(Function::EXTERNAL, 0, RunOnMode::FIXED_TIME, 0)).relay_on);
  assert(!update(external, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, 1000)).relay_on);

  State invalid_time;
  inputs = run_on_inputs(Function::HEATING, 2, RunOnMode::FIXED_TIME, 0);
  assert(update(invalid_time, inputs).relay_on);
  inputs.control_mode = 0;
  inputs.run_on_s = NAN;
  assert(!update(invalid_time, inputs).relay_on);
}

void test_run_on_timer_is_rollover_safe() {
  State state;
  const uint32_t start_ms = UINT32_MAX - 1000U;
  assert(update(state, run_on_inputs(Function::HEATING, 2, RunOnMode::FIXED_TIME, start_ms - 5000U)).relay_on);
  assert(update(state, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, start_ms)).relay_on);
  assert(update(state, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, start_ms + 299999U)).relay_on);
  assert(!update(state, run_on_inputs(Function::HEATING, 0, RunOnMode::FIXED_TIME, start_ms + 300000U)).relay_on);
}

}  // namespace

int main() {
  test_control_modes_and_cm1_destination();
  test_heating_gate_hysteresis_and_missing_supply();
  test_cooling_gate_and_mode_change_reset();
  test_external_control_is_explicit_and_retained();
  test_command_outside_external_control_cannot_override_owner();
  test_fixed_time_run_on_after_demand_ends();
  test_run_on_until_supply_temp_is_bounded_by_time();
  test_cooling_run_on_until_supply_warms_up();
  test_run_on_is_cancelled_by_demand_return_and_owner_changes();
  test_run_on_only_extends_a_relay_that_was_on();
  test_run_on_timer_is_rollover_safe();
  return 0;
}
