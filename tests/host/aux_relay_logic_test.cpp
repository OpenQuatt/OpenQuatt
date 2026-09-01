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

}  // namespace

int main() {
  test_control_modes_and_cm1_destination();
  test_heating_gate_hysteresis_and_missing_supply();
  test_cooling_gate_and_mode_change_reset();
  test_external_control_is_explicit_and_retained();
  test_command_outside_external_control_cannot_override_owner();
  return 0;
}
