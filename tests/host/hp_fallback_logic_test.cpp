#include <assert.h>

#include "../../openquatt/includes/control/oq_boiler_control_logic.h"
#include "../../openquatt/includes/control/oq_boiler_output_logic.h"
#include "../../openquatt/includes/control/oq_hp_fallback_logic.h"

namespace {

struct FakeHeatOutput {
  void write_heat_enable(bool enabled) {
    assert(write_count < 8);
    writes[write_count++] = enabled;
  }

  bool writes[8] = {};
  int write_count = 0;
};

oq_hp_fallback::FallbackInputs eligible_fallback_inputs() {
  oq_hp_fallback::FallbackInputs inputs;
  inputs.current_mode = oq_hp_fallback::ControlMode::CM3;
  inputs.heating_demand = true;
  inputs.fallback_enabled = true;
  inputs.available_hp_count = 0;
  inputs.hp_availability_complete = true;
  inputs.confirmed_fallback_cause = true;
  inputs.hp_output_state_safe = true;
  inputs.flow_valid = true;
  inputs.flow_sufficient = true;
  inputs.supply_temperature_valid = true;
  inputs.boiler_guards_clear = true;
  return inputs;
}

oq_boiler::BoilerLogCodes boiler_log_codes() {
  return oq_boiler::BoilerLogCodes{
      {
          10,  // less power
          11,  // flow too low
          12,  // soft guard
          13,  // sensor fallback
          14,  // no candidate
          15,  // fallback blocked
          16,  // HP stop unconfirmed
          17,  // flow preflow
          18,  // boiler fallback
          19,  // HP recovered
          20,  // commissioning
          21,  // cooling request
          22,  // frost protection
          23,  // heating request cleared
          24,  // minimum rest active
      },
      {30, 31},
  };
}

oq_boiler::BoilerControllerLogInputs eligible_boiler_log_inputs() {
  oq_boiler::BoilerControllerLogInputs inputs;
  inputs.role = oq_boiler::BoilerRole::FALLBACK_CM4;
  inputs.controller_block_reason = oq_boiler::BLOCK_NONE;
  return inputs;
}

void test_cm3_cm4_cm3_keeps_heat_output_enabled() {
  using oq_boiler::BoilerOutputAction;
  using oq_boiler::BoilerOutputCommand;
  using oq_boiler::BoilerOutputController;
  using oq_boiler::BoilerRole;

  FakeHeatOutput output;
  BoilerOutputController controller;

  const auto cm3_start = controller.apply(BoilerOutputCommand{BoilerRole::ASSIST_CM3, true}, output);
  assert(cm3_start.output_action == BoilerOutputAction::ENABLE);
  assert(output.write_count == 1);
  assert(output.writes[0]);

  const auto cm4_handover = controller.apply(BoilerOutputCommand{BoilerRole::FALLBACK_CM4, true}, output);
  assert(cm4_handover.role_changed);
  assert(cm4_handover.output_action == BoilerOutputAction::KEEP);
  assert(cm4_handover.assist_fallback_handover_without_output_edge);
  assert(output.write_count == 1);
  assert(controller.heat_enable());

  const auto cm3_handover = controller.apply(BoilerOutputCommand{BoilerRole::ASSIST_CM3, true}, output);
  assert(cm3_handover.role_changed);
  assert(cm3_handover.output_action == BoilerOutputAction::KEEP);
  assert(cm3_handover.assist_fallback_handover_without_output_edge);
  assert(output.write_count == 1);
  assert(controller.heat_enable());

  const auto stop = controller.apply(BoilerOutputCommand{BoilerRole::OFF, false}, output);
  assert(stop.output_action == BoilerOutputAction::DISABLE);
  assert(output.write_count == 2);
  assert(!output.writes[1]);
}

void test_cm4_requires_every_guard() {
  using oq_hp_fallback::ControlMode;
  using oq_hp_fallback::decide_cm4;
  using oq_hp_fallback::FallbackAction;
  using oq_hp_fallback::FallbackBlockReason;

  auto inputs = eligible_fallback_inputs();
  auto decision = decide_cm4(inputs);
  assert(decision.cm4_allowed);
  assert(decision.action == FallbackAction::ENTER_CM4);
  assert(decision.block_reason == FallbackBlockReason::NONE);

  inputs.current_mode = ControlMode::CM4;
  decision = decide_cm4(inputs);
  assert(decision.cm4_allowed);
  assert(decision.action == FallbackAction::HOLD_CM4);

  inputs = eligible_fallback_inputs();
  inputs.heating_demand = false;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::NO_HEATING_DEMAND);
  inputs = eligible_fallback_inputs();
  inputs.fallback_enabled = false;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::FALLBACK_DISABLED);
  inputs = eligible_fallback_inputs();
  inputs.available_hp_count = 1;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::HP_AVAILABLE);
  inputs = eligible_fallback_inputs();
  inputs.hp_availability_complete = false;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::HP_AVAILABILITY_UNKNOWN);
  inputs = eligible_fallback_inputs();
  inputs.confirmed_fallback_cause = false;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::NO_CONFIRMED_FALLBACK_CAUSE);
  inputs = eligible_fallback_inputs();
  inputs.hp_output_state_safe = false;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::HP_OUTPUT_STATE_UNSAFE);
  inputs = eligible_fallback_inputs();
  inputs.flow_valid = false;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::FLOW_UNAVAILABLE);
  inputs = eligible_fallback_inputs();
  inputs.flow_sufficient = false;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::FLOW_INSUFFICIENT);
  inputs = eligible_fallback_inputs();
  inputs.supply_temperature_valid = false;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::SUPPLY_TEMPERATURE_UNAVAILABLE);
  inputs = eligible_fallback_inputs();
  inputs.boiler_guards_clear = false;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::BOILER_GUARD_BLOCKED);
}

void test_cm4_never_competes_with_other_control_owners() {
  using oq_hp_fallback::ControlMode;
  using oq_hp_fallback::decide_cm4;
  using oq_hp_fallback::FallbackAction;
  using oq_hp_fallback::FallbackBlockReason;

  auto inputs = eligible_fallback_inputs();
  inputs.override_active = true;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::OVERRIDE_ACTIVE);
  inputs = eligible_fallback_inputs();
  inputs.commissioning_active = true;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::COMMISSIONING_ACTIVE);
  inputs = eligible_fallback_inputs();
  inputs.current_mode = ControlMode::CM100;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::COMMISSIONING_ACTIVE);
  inputs = eligible_fallback_inputs();
  inputs.cooling_active = true;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::COOLING_ACTIVE);
  inputs = eligible_fallback_inputs();
  inputs.current_mode = ControlMode::CM5;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::COOLING_ACTIVE);
  inputs = eligible_fallback_inputs();
  inputs.frost_active = true;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::FROST_ACTIVE);
  inputs = eligible_fallback_inputs();
  inputs.current_mode = ControlMode::CM98;
  assert(decide_cm4(inputs).block_reason == FallbackBlockReason::FROST_ACTIVE);

  inputs = eligible_fallback_inputs();
  inputs.current_mode = ControlMode::CM4;
  inputs.cooling_active = true;
  const auto decision = decide_cm4(inputs);
  assert(!decision.cm4_allowed);
  assert(decision.action == FallbackAction::EXIT_CM4);
}

void test_off_role_fails_safe_even_with_heat_requested() {
  using oq_boiler::BoilerOutputAction;
  using oq_boiler::BoilerOutputCommand;
  using oq_boiler::BoilerOutputController;
  using oq_boiler::BoilerRole;

  FakeHeatOutput output;
  BoilerOutputController controller;
  controller.apply(BoilerOutputCommand{BoilerRole::ASSIST_CM3, true}, output);
  const auto transition = controller.apply(BoilerOutputCommand{BoilerRole::OFF, true}, output);

  assert(transition.output_action == BoilerOutputAction::DISABLE);
  assert(!transition.next_heat_enable);
  assert(!controller.heat_enable());
  assert(output.write_count == 2);
  assert(!output.writes[1]);
}

void test_boiler_role_and_log_classification() {
  using oq_boiler::boiler_role_for_source;
  using oq_boiler::BoilerLogReason;
  using oq_boiler::BoilerRole;
  using oq_boiler::classify_boiler_controller_log;

  const auto codes = boiler_log_codes();
  assert(boiler_role_for_source(oq_boiler::COMMAND_SOURCE_POWER_HOUSE) == BoilerRole::ASSIST_CM3);
  assert(boiler_role_for_source(oq_boiler::COMMAND_SOURCE_HEATING_CURVE) == BoilerRole::ASSIST_CM3);
  assert(boiler_role_for_source(oq_boiler::COMMAND_SOURCE_COLD_START) == BoilerRole::ASSIST_CM3);
  assert(boiler_role_for_source(oq_boiler::COMMAND_SOURCE_FALLBACK) == BoilerRole::FALLBACK_CM4);
  assert(boiler_role_for_source(oq_boiler::COMMAND_SOURCE_COMMISSIONING) == BoilerRole::COMMISSIONING_CM100);
  assert(boiler_role_for_source(oq_boiler::COMMAND_SOURCE_NONE) == BoilerRole::OFF);

  auto inputs = eligible_boiler_log_inputs();
  auto log = classify_boiler_controller_log(inputs, codes);
  assert(log.reason == BoilerLogReason::BOILER_FALLBACK);
  assert(log.reason_code == 18);
  assert(log.severity == 30);

  inputs.controller_block_reason = oq_boiler::BLOCK_FALLBACK_DISABLED;
  log = classify_boiler_controller_log(inputs, codes);
  assert(log.reason == BoilerLogReason::FALLBACK_BLOCKED);
  assert(log.reason_code == 15);
  assert(log.severity == 31);

  inputs.controller_block_reason = oq_boiler::BLOCK_FLOW_INSUFFICIENT;
  log = classify_boiler_controller_log(inputs, codes);
  assert(log.reason == BoilerLogReason::FLOW_TOO_LOW);
  assert(log.reason_code == 11);

  inputs.controller_block_reason = oq_boiler::BLOCK_WATER_TEMP_INHIBIT;
  log = classify_boiler_controller_log(inputs, codes);
  assert(log.reason == BoilerLogReason::SOFT_GUARD);
  assert(log.reason_code == 12);

  inputs.controller_block_reason = oq_boiler::BLOCK_MIN_OFF_TIME;
  log = classify_boiler_controller_log(inputs, codes);
  assert(log.reason == BoilerLogReason::MIN_REST_ACTIVE);
  assert(log.reason_code == 24);
  assert(log.severity == 31);
}

void test_boiler_stop_reason_preserves_safety_cause() {
  using oq_boiler::BoilerLogReason;
  using oq_boiler::BoilerRole;
  using oq_boiler::classify_boiler_stop_log;

  const auto codes = boiler_log_codes();
  oq_boiler::BoilerStopLogInputs inputs;
  inputs.stopped_role = BoilerRole::FALLBACK_CM4;
  inputs.current_mode = 4;
  inputs.heating_demand = true;
  inputs.fallback_enabled = true;
  inputs.fallback_outputs_safe = true;
  inputs.guard_reason = BoilerLogReason::SOFT_GUARD;

  auto log = classify_boiler_stop_log(inputs, codes);
  // A safety stop must retain its concrete cause, never "boiler fallback".
  assert(log.reason == BoilerLogReason::SOFT_GUARD);
  assert(log.reason_code == 12);
  assert(log.severity == 31);

  inputs.fallback_outputs_safe = false;
  inputs.guard_reason = BoilerLogReason::HP_STOP_UNCONFIRMED;
  log = classify_boiler_stop_log(inputs, codes);
  assert(log.reason == BoilerLogReason::HP_STOP_UNCONFIRMED);
  assert(log.reason_code == 16);

  inputs.continuous_handover = true;
  inputs.current_mode = 3;
  log = classify_boiler_stop_log(inputs, codes);
  assert(log.reason == BoilerLogReason::HP_RECOVERED);
  assert(log.reason_code == 19);
  assert(log.severity == 30);

  inputs.continuous_handover = false;
  inputs.current_mode = 0;
  inputs.heating_demand = false;
  inputs.fallback_outputs_safe = true;
  log = classify_boiler_stop_log(inputs, codes);
  assert(log.reason == BoilerLogReason::HEATING_REQUEST_CLEARED);
  assert(log.reason_code == 23);
  assert(log.severity == 30);

  inputs.stopped_role = BoilerRole::ASSIST_CM3;
  inputs.continuous_handover = true;
  inputs.current_mode = 4;
  inputs.heating_demand = true;
  inputs.guard_reason = BoilerLogReason::LESS_POWER;
  log = classify_boiler_stop_log(inputs, codes);
  assert(log.reason == BoilerLogReason::BOILER_FALLBACK);
  assert(log.reason_code == 18);
  assert(log.severity == 30);
}

}  // namespace

int main() {
  test_cm3_cm4_cm3_keeps_heat_output_enabled();
  test_cm4_requires_every_guard();
  test_cm4_never_competes_with_other_control_owners();
  test_off_role_fails_safe_even_with_heat_requested();
  test_boiler_role_and_log_classification();
  test_boiler_stop_reason_preserves_safety_cause();
  return 0;
}
