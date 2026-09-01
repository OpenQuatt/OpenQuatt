#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_aux_relay {

enum class Function : uint8_t { DISABLED, HEATING, COOLING, HEATING_OR_COOLING, EXTERNAL };
enum class ThermalMode : uint8_t { NONE, COOLING, HEATING };
enum class Status : uint8_t {
  DISABLED,
  EXTERNAL_CONTROL,
  HEATING_ACTIVE,
  NO_HEATING,
  COOLING_ACTIVE,
  NO_COOLING,
  NO_THERMAL,
  SUPPLY_UNAVAILABLE,
  WAITING_WARM,
  WAITING_COLD,
};

struct State {
  bool relay_on = false;
  bool temperature_gate_on = false;
  ThermalMode last_mode = ThermalMode::NONE;
};

struct Inputs {
  Function function = Function::DISABLED;
  int control_mode = 0;
  int cm1_next_mode = 0;
  bool temperature_gate_enabled = false;
  float supply_c = NAN;
  float heating_start_c = NAN;
  float cooling_start_c = NAN;
  float hysteresis_c = NAN;
};

struct Decision {
  bool relay_on = false;
  bool changed = false;
  bool external_control = false;
  ThermalMode thermal_mode = ThermalMode::NONE;
  Status status = Status::DISABLED;
};

inline const char* status_text(Status status) {
  switch (status) {
    case Status::EXTERNAL_CONTROL:
      return "External control";
    case Status::HEATING_ACTIVE:
      return "Heating demand active";
    case Status::NO_HEATING:
      return "No heating demand";
    case Status::COOLING_ACTIVE:
      return "Cooling demand active";
    case Status::NO_COOLING:
      return "No cooling demand";
    case Status::NO_THERMAL:
      return "No thermal demand";
    case Status::SUPPLY_UNAVAILABLE:
      return "Supply temperature unavailable";
    case Status::WAITING_WARM:
      return "Waiting for warm water";
    case Status::WAITING_COLD:
      return "Waiting for cold water";
    default:
      return "Disabled";
  }
}

inline ThermalMode effective_mode(int control_mode, int cm1_next_mode) {
  const bool heating = control_mode == 2 || control_mode == 3 || control_mode == 4 ||
                       (control_mode == 1 && (cm1_next_mode == 2 || cm1_next_mode == 4));
  const bool cooling = control_mode == 5 || (control_mode == 1 && cm1_next_mode == 5);
  if (cooling) return ThermalMode::COOLING;
  if (heating) return ThermalMode::HEATING;
  return ThermalMode::NONE;
}

inline Decision update(State& state, const Inputs& inputs) {
  Decision result;
  result.external_control = inputs.function == Function::EXTERNAL;
  result.thermal_mode = effective_mode(inputs.control_mode, inputs.cm1_next_mode);
  if (result.thermal_mode != state.last_mode) {
    state.temperature_gate_on = false;
    state.last_mode = result.thermal_mode;
  }

  bool desired = false;
  if (result.external_control) {
    desired = state.relay_on;
    result.status = Status::EXTERNAL_CONTROL;
    state.temperature_gate_on = false;
  } else if (inputs.function == Function::HEATING) {
    desired = result.thermal_mode == ThermalMode::HEATING;
    result.status = desired ? Status::HEATING_ACTIVE : Status::NO_HEATING;
  } else if (inputs.function == Function::COOLING) {
    desired = result.thermal_mode == ThermalMode::COOLING;
    result.status = desired ? Status::COOLING_ACTIVE : Status::NO_COOLING;
  } else if (inputs.function == Function::HEATING_OR_COOLING) {
    desired = result.thermal_mode != ThermalMode::NONE;
    result.status = result.thermal_mode == ThermalMode::HEATING
                        ? Status::HEATING_ACTIVE
                        : (result.thermal_mode == ThermalMode::COOLING ? Status::COOLING_ACTIVE : Status::NO_THERMAL);
  }

  if (!result.external_control) {
    if (!inputs.temperature_gate_enabled || !desired) {
      state.temperature_gate_on = false;
    } else if (isnan(inputs.supply_c)) {
      state.temperature_gate_on = false;
      result.status = Status::SUPPLY_UNAVAILABLE;
    } else {
      const float hysteresis_c = isnan(inputs.hysteresis_c) ? 2.0f : inputs.hysteresis_c;
      if (result.thermal_mode == ThermalMode::HEATING) {
        const float start_c = isnan(inputs.heating_start_c) ? 30.0f : inputs.heating_start_c;
        if (inputs.supply_c >= start_c)
          state.temperature_gate_on = true;
        else if (inputs.supply_c <= start_c - hysteresis_c)
          state.temperature_gate_on = false;
        if (!state.temperature_gate_on) result.status = Status::WAITING_WARM;
      } else {
        const float start_c = isnan(inputs.cooling_start_c) ? 18.0f : inputs.cooling_start_c;
        if (inputs.supply_c <= start_c)
          state.temperature_gate_on = true;
        else if (inputs.supply_c >= start_c + hysteresis_c)
          state.temperature_gate_on = false;
        if (!state.temperature_gate_on) result.status = Status::WAITING_COLD;
      }
    }
    desired = inputs.temperature_gate_enabled && desired ? state.temperature_gate_on : desired;
    result.changed = desired != state.relay_on;
    state.relay_on = desired;
  }
  result.relay_on = state.relay_on;
  return result;
}

inline bool apply_external_command(State& state, Function function, bool relay_on) {
  if (function != Function::EXTERNAL) return false;
  state.relay_on = relay_on;
  return true;
}

}  // namespace oq_aux_relay
