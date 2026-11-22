#pragma once

#include <math.h>
#include <stdint.h>

#include "../boiler/oq_boiler_logic.h"

namespace oq_boiler_dispatch {

struct Config {
  float cold_start_target_c = 35.0f;
  float minimum_flow_lph = 250.0f;
  float water_cp_j_per_kgk = 4180.0f;
};

struct Inputs {
  int control_mode = 0;
  uint32_t now_ms = 0;
  bool cold_start_assist = false;
  bool heat_request = false;
  float rated_boiler_power_w = NAN;
  float maximum_water_temperature_c = NAN;
  uint8_t strategy = 0;
  bool strategy_output_valid = false;
  uint8_t strategy_output_source = 0;
  uint32_t strategy_updated_ms = 0;
  float strategy_requested_power_w = NAN;
  float hp_expected_power_w = NAN;
  float strategy_supply_target_c = NAN;
  float flow_lph = NAN;
  float boiler_inlet_c = NAN;
  bool commissioning_active = false;
  bool commissioning_boiler_task = false;
  bool commissioning_heat_request = false;
  uint32_t commissioning_updated_ms = 0;
  float commissioning_target_c = NAN;
};

inline float bounded_maximum_water_temperature(float configured_c) {
  if (isnan(configured_c)) return 60.0f;
  return fmaxf(25.0f, fminf(configured_c, 75.0f));
}

inline oq_boiler::BoilerCommand dispatch(const Inputs& in, const Config& config = {}) {
  oq_boiler::BoilerCommand command{
      true, false, false, NAN, NAN, oq_boiler::COMMAND_SOURCE_NONE, in.now_ms,
  };

  if (in.control_mode == 3 && in.cold_start_assist) {
    command.demand_present = true;
    command.heat_request = in.heat_request;
    command.requested_power_w = in.rated_boiler_power_w;
    command.target_temperature_c = isnan(in.maximum_water_temperature_c)
                                       ? config.cold_start_target_c
                                       : fminf(config.cold_start_target_c, in.maximum_water_temperature_c);
    command.source = oq_boiler::COMMAND_SOURCE_COLD_START;
    return command;
  }

  if (in.control_mode == 3) {
    command.demand_present = true;
    command.updated_at_ms = in.strategy_updated_ms;
    const bool strategy_current = oq_boiler::strategy_output_is_current(
        in.strategy_output_valid, in.strategy_output_source, in.strategy, in.strategy_updated_ms);
    if (in.strategy == 3) {
      command.source = oq_boiler::COMMAND_SOURCE_POWER_HOUSE;
      command.valid = strategy_current && !isnan(in.strategy_requested_power_w) && !isnan(in.hp_expected_power_w) &&
                      !isnan(in.rated_boiler_power_w) && in.rated_boiler_power_w > 0.0f;
      if (!command.valid) return command;

      const float remaining_power_w =
          fmaxf(0.0f, fminf(in.strategy_requested_power_w - in.hp_expected_power_w, in.rated_boiler_power_w));
      command.heat_request = in.heat_request && remaining_power_w > 0.5f;
      command.requested_power_w = remaining_power_w;
      if (!command.heat_request || isnan(in.flow_lph) || in.flow_lph < config.minimum_flow_lph) return command;

      const auto target = oq_boiler::target_from_power(
          remaining_power_w, in.rated_boiler_power_w, in.boiler_inlet_c, in.flow_lph, config.water_cp_j_per_kgk,
          bounded_maximum_water_temperature(in.maximum_water_temperature_c));
      if (target.valid) {
        command.requested_power_w = target.requested_power_w;
        command.target_temperature_c = target.target_temperature_c;
      }
    } else if (in.strategy == 2) {
      command.source = oq_boiler::COMMAND_SOURCE_HEATING_CURVE;
      command.valid = strategy_current;
      command.heat_request = in.heat_request;
      command.target_temperature_c = in.strategy_supply_target_c;
    } else {
      command.valid = false;
    }
    return command;
  }

  if (in.control_mode == 4) {
    command.demand_present = true;
    command.heat_request = in.heat_request;
    command.requested_power_w = in.rated_boiler_power_w;
    command.target_temperature_c = in.maximum_water_temperature_c;
    command.source = oq_boiler::COMMAND_SOURCE_FALLBACK;
    return command;
  }

  if (in.control_mode == 100 && in.commissioning_active && in.commissioning_boiler_task) {
    command.demand_present = true;
    command.heat_request = in.commissioning_heat_request;
    command.requested_power_w = in.rated_boiler_power_w;
    command.target_temperature_c = in.commissioning_target_c;
    command.source = oq_boiler::COMMAND_SOURCE_COMMISSIONING;
    command.updated_at_ms = in.commissioning_updated_ms;
  }
  return command;
}

}  // namespace oq_boiler_dispatch
