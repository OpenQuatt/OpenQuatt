#pragma once

#include "oq_boiler_dispatch_logic.h"

namespace oq_boiler_dispatch_runtime {

inline void tick(const oq_boiler_dispatch::Config& config, float boiler_inlet_c) {
  oq_boiler_dispatch::Inputs in;
  in.control_mode = id(oq_control_mode_code);
  in.now_ms = (uint32_t)millis();
  in.cold_start_assist = id(oq_cold_start_assist_active);
  in.heat_request = id(oq_strategy_heat_request_active);
  in.rated_boiler_power_w = id(oq_boiler_rated_heat_power).state;
  in.maximum_water_temperature_c = id(max_water_temp_limit_c).state;
  in.strategy = (uint8_t)id(oq_strategy_active_code);
  in.strategy_output_valid = id(oq_strategy_output_valid);
  in.strategy_output_source = id(oq_strategy_output_source_code);
  in.strategy_updated_ms = id(oq_strategy_output_updated_ms);
  in.strategy_requested_power_w = id(oq_strategy_requested_power_w);
  in.hp_expected_power_w = id(oq_strategy_hp_expected_power_w);
  in.strategy_supply_target_c = id(oq_strategy_supply_target_temp);
  in.flow_lph = id(flow_rate_selected).state;
  in.boiler_inlet_c = boiler_inlet_c;
  in.commissioning_active = id(oq_commissioning_active);
  in.commissioning_boiler_task = id(oq_commissioning_task_code) == oq_commissioning::TASK_BOILER_POWER_TEST;
  in.commissioning_heat_request = id(oq_commissioning_boiler_request);
  in.commissioning_updated_ms = id(oq_commissioning_boiler_request_updated_ms);
  in.commissioning_target_c =
      oq_boiler_commissioning::commissioning_target_temperature_c(in.maximum_water_temperature_c);

  const auto command = oq_boiler_dispatch::dispatch(in, config);
  id(oq_boiler_command_valid) = command.valid;
  id(oq_boiler_command_demand_present) = command.demand_present;
  id(oq_boiler_command_heat_request) = command.heat_request;
  id(oq_boiler_command_requested_power_w) = command.requested_power_w;
  id(oq_boiler_command_target_temperature_c) = command.target_temperature_c;
  id(oq_boiler_command_source_code) = command.source;
  id(oq_boiler_command_updated_ms) = command.updated_at_ms;
}

}  // namespace oq_boiler_dispatch_runtime
