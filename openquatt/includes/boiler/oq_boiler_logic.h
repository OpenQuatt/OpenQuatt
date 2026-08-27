#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_boiler {

enum CommandSource : uint8_t {
  COMMAND_SOURCE_NONE = 0,
  COMMAND_SOURCE_POWER_HOUSE = 1,
  COMMAND_SOURCE_CM3 = COMMAND_SOURCE_POWER_HOUSE,
  COMMAND_SOURCE_COMMISSIONING = 2,
  COMMAND_SOURCE_HEATING_CURVE = 3,
  COMMAND_SOURCE_FALLBACK = 4,
  COMMAND_SOURCE_COLD_START = 5,
};

enum BlockReason : uint8_t {
  BLOCK_NONE = 0,
  BLOCK_ASSIST_DISABLED = 1,
  BLOCK_COMMAND_INVALID = 2,
  BLOCK_COMMAND_STALE = 3,
  BLOCK_SUPPLY_UNAVAILABLE = 4,
  BLOCK_WATER_TEMP_INHIBIT = 5,
  BLOCK_WATER_TEMP_HARD_TRIP = 6,
  BLOCK_COMMISSIONING_WAITING = 7,
  BLOCK_NO_HEAT_REQUEST = 8,
  BLOCK_MIN_ON_TIME = 9,
  BLOCK_MIN_OFF_TIME = 10,
  BLOCK_TRANSPORT_UNAVAILABLE = 11,
  BLOCK_TARGET_INVALID = 12,
  BLOCK_TRANSPORT_SETTLING = 13,
  BLOCK_AWAITING_FRESH_COMMAND = 14,
  BLOCK_CONNECTION_MISMATCH = 15,
  BLOCK_FALLBACK_DISABLED = 16,
  BLOCK_FLOW_UNAVAILABLE = 17,
  BLOCK_FLOW_INSUFFICIENT = 18,
  BLOCK_HP_STOP_UNCONFIRMED = 19,
  BLOCK_SOURCE_NOT_CONNECTED = 20,
};

struct BoilerCommand {
  bool valid;
  bool demand_present;
  bool heat_request;
  float requested_power_w;
  float target_temperature_c;
  uint8_t source;
  uint32_t updated_at_ms;
};

struct ControllerInput {
  bool source_present;
  bool assist_enabled;
  bool fallback_enabled;
  bool supply_temperature_valid;
  bool flow_valid;
  bool flow_sufficient;
  bool fallback_outputs_safe;
  bool boiler_inhibit_active;
  bool hard_trip_active;
  bool connection_mismatch;
  bool transport_available;
  bool transport_settled;
  bool command_rearmed;
  bool target_required;
  bool target_valid;
  bool output_active;
  uint32_t now_ms;
  uint32_t command_max_age_ms;
  uint32_t output_last_change_ms;
  uint32_t min_on_ms;
  uint32_t min_off_ms;
};

struct PowerTarget {
  bool valid;
  float requested_power_w;
  float target_temperature_c;
};

struct AssistSignal {
  bool need_on;
  bool okay_off;
};

inline AssistSignal power_house_assist(float deficit_w, float on_threshold_w, float off_threshold_w) {
  return AssistSignal{
      !isnan(deficit_w) && deficit_w >= on_threshold_w,
      isnan(deficit_w) || deficit_w <= off_threshold_w,
  };
}

inline AssistSignal heating_curve_assist(bool heat_request, bool hp_saturated, float target_temperature_c,
                                         float supply_temperature_c, float on_delta_c, float off_delta_c) {
  const bool temperatures_valid = !isnan(target_temperature_c) && !isnan(supply_temperature_c);
  const float target_error_c = temperatures_valid ? target_temperature_c - supply_temperature_c : NAN;
  return AssistSignal{
      heat_request && hp_saturated && temperatures_valid && target_error_c >= on_delta_c,
      !heat_request || !hp_saturated || !temperatures_valid || target_error_c <= off_delta_c,
  };
}

inline bool cm3_should_hold(bool minimum_run_elapsed, bool okay_off, bool demote_confirmation_elapsed) {
  return !minimum_run_elapsed || !okay_off || !demote_confirmation_elapsed;
}

inline PowerTarget target_from_power(float requested_power_w, float rated_power_w, float inlet_temperature_c,
                                     float flow_lph, float cp_j_per_kgk, float maximum_temperature_c) {
  PowerTarget target{false, 0.0f, NAN};
  if (isnan(requested_power_w) || isnan(rated_power_w) || isnan(inlet_temperature_c) || isnan(flow_lph) ||
      isnan(cp_j_per_kgk) || isnan(maximum_temperature_c) || requested_power_w <= 0.0f || rated_power_w <= 0.0f ||
      flow_lph <= 0.0f || cp_j_per_kgk <= 0.0f || inlet_temperature_c >= maximum_temperature_c) {
    return target;
  }

  const float thermal_conductance_w_per_k = (flow_lph / 3600.0f) * cp_j_per_kgk;
  const float maximum_hydraulic_power_w = thermal_conductance_w_per_k * (maximum_temperature_c - inlet_temperature_c);
  float usable_power_w = fminf(requested_power_w, rated_power_w);
  usable_power_w = fminf(usable_power_w, maximum_hydraulic_power_w);
  if (usable_power_w <= 0.0f) return target;

  target.valid = true;
  target.requested_power_w = usable_power_w;
  target.target_temperature_c = inlet_temperature_c + usable_power_w / thermal_conductance_w_per_k;
  return target;
}

struct ControllerDecision {
  bool demand_present;
  bool desired_active;
  bool output_active;
  bool force_off;
  bool blocked;
  uint8_t block_reason;
};

inline BoilerCommand make_legacy_command(int control_mode_code, bool commissioning_active,
                                         bool commissioning_boiler_task, bool commissioning_boiler_request,
                                         uint32_t now_ms) {
  const bool in_cm3 = control_mode_code == 3;
  const bool commissioning_task_active = control_mode_code == 100 && commissioning_active && commissioning_boiler_task;
  const bool commissioning_heat_request = commissioning_task_active && commissioning_boiler_request;

  BoilerCommand command{};
  command.valid = true;
  command.demand_present = in_cm3 || commissioning_task_active;
  command.heat_request = in_cm3 || commissioning_heat_request;
  command.requested_power_w = NAN;
  command.target_temperature_c = NAN;
  command.source = commissioning_task_active ? COMMAND_SOURCE_COMMISSIONING
                   : in_cm3                  ? COMMAND_SOURCE_CM3
                                             : COMMAND_SOURCE_NONE;
  command.updated_at_ms = now_ms;
  return command;
}

inline bool command_is_fresh(const BoilerCommand& command, uint32_t now_ms, uint32_t max_age_ms) {
  if (!command.valid || command.updated_at_ms == 0) return false;
  if (max_age_ms == 0) return true;
  return (uint32_t)(now_ms - command.updated_at_ms) <= max_age_ms;
}

inline bool strategy_output_is_current(bool output_valid, uint8_t output_source, uint8_t active_source,
                                       uint32_t updated_at_ms) {
  return output_valid && output_source == active_source && updated_at_ms != 0;
}

inline bool timestamp_is_strictly_newer(uint32_t candidate_ms, uint32_t reference_ms) {
  if (candidate_ms == 0 || candidate_ms == reference_ms) return false;
  return static_cast<int32_t>(candidate_ms - reference_ms) > 0;
}

inline bool command_satisfies_rearm(bool rearm_required, const BoilerCommand& command, uint32_t reference_ms) {
  return !rearm_required || (command.valid && timestamp_is_strictly_newer(command.updated_at_ms, reference_ms));
}

inline bool settle_period_elapsed(bool settle_required, uint32_t now_ms, uint32_t started_ms, uint32_t settle_ms) {
  return !settle_required || settle_ms == 0 || (uint32_t)(now_ms - started_ms) >= settle_ms;
}

inline bool connection_guard_active(bool startup_probe_active, bool connection_mismatch) {
  return startup_probe_active || connection_mismatch;
}

inline bool transport_available_for_selection(bool runtime_available, bool opentherm_selected, bool opentherm_supported,
                                              bool opentherm_link_available, bool startup_probe_active,
                                              bool connection_mismatch) {
  if (!runtime_available) return false;
  if (opentherm_selected) {
    return opentherm_supported && opentherm_link_available;
  }
  return !connection_guard_active(startup_probe_active, connection_mismatch);
}

inline bool relay_must_be_off(bool opentherm_selected, bool startup_probe_active, bool connection_mismatch) {
  return opentherm_selected || connection_guard_active(startup_probe_active, connection_mismatch);
}

inline bool minimum_time_active(uint32_t now_ms, uint32_t last_change_ms, uint32_t minimum_time_ms) {
  if (minimum_time_ms == 0 || last_change_ms == 0) return false;
  return (uint32_t)(now_ms - last_change_ms) < minimum_time_ms;
}

inline float effective_output_target(bool output_active, bool target_required, bool desired_active,
                                     bool command_target_valid, float command_target_c, float previous_target_c) {
  if (!output_active || !target_required) return NAN;
  if (desired_active && command_target_valid) return command_target_c;
  return previous_target_c;
}

inline ControllerDecision evaluate(const BoilerCommand& command, const ControllerInput& input) {
  ControllerDecision decision{};
  decision.demand_present = command.demand_present;
  decision.desired_active = false;
  decision.output_active = false;
  decision.force_off = false;
  decision.blocked = false;
  decision.block_reason = BLOCK_NONE;

  if (input.hard_trip_active) {
    decision.force_off = true;
    decision.block_reason = BLOCK_WATER_TEMP_HARD_TRIP;
  } else if (input.boiler_inhibit_active) {
    decision.force_off = true;
    decision.block_reason = BLOCK_WATER_TEMP_INHIBIT;
  } else if (!input.source_present) {
    decision.force_off = true;
    decision.block_reason = BLOCK_SOURCE_NOT_CONNECTED;
  } else if (command.source == COMMAND_SOURCE_FALLBACK && !input.fallback_enabled) {
    decision.force_off = true;
    decision.block_reason = BLOCK_FALLBACK_DISABLED;
  } else if ((command.source == COMMAND_SOURCE_POWER_HOUSE || command.source == COMMAND_SOURCE_HEATING_CURVE ||
              command.source == COMMAND_SOURCE_COLD_START) &&
             !input.assist_enabled) {
    decision.force_off = true;
    decision.block_reason = BLOCK_ASSIST_DISABLED;
  } else if (!command.valid) {
    decision.force_off = true;
    decision.block_reason = BLOCK_COMMAND_INVALID;
  } else if (!command_is_fresh(command, input.now_ms, input.command_max_age_ms)) {
    decision.force_off = true;
    decision.block_reason = BLOCK_COMMAND_STALE;
  } else if (!input.supply_temperature_valid) {
    decision.force_off = true;
    decision.block_reason = BLOCK_SUPPLY_UNAVAILABLE;
  } else if (!input.flow_valid) {
    decision.force_off = true;
    decision.block_reason = BLOCK_FLOW_UNAVAILABLE;
  } else if (!input.flow_sufficient) {
    decision.force_off = true;
    decision.block_reason = BLOCK_FLOW_INSUFFICIENT;
  } else if (command.source == COMMAND_SOURCE_FALLBACK && !input.fallback_outputs_safe) {
    decision.force_off = true;
    decision.block_reason = BLOCK_HP_STOP_UNCONFIRMED;
  } else if (!command.demand_present) {
    // Losing the owning control context (for example CM3 -> CM5) is not a
    // normal anti-cycling stop. Withdraw heat immediately, even inside the
    // configured minimum on-time.
    decision.force_off = true;
    decision.block_reason = BLOCK_NO_HEAT_REQUEST;
  } else if (!input.transport_settled) {
    decision.force_off = true;
    decision.block_reason = BLOCK_TRANSPORT_SETTLING;
  } else if (input.connection_mismatch) {
    decision.force_off = true;
    decision.block_reason = BLOCK_CONNECTION_MISMATCH;
  } else if (!input.transport_available) {
    decision.force_off = true;
    decision.block_reason = BLOCK_TRANSPORT_UNAVAILABLE;
  } else if (!input.command_rearmed) {
    decision.force_off = true;
    decision.block_reason = BLOCK_AWAITING_FRESH_COMMAND;
  } else if (command.heat_request && input.target_required && !input.target_valid) {
    decision.force_off = true;
    decision.block_reason = BLOCK_TARGET_INVALID;
  } else if (!command.heat_request) {
    decision.block_reason =
        command.source == COMMAND_SOURCE_COMMISSIONING ? BLOCK_COMMISSIONING_WAITING : BLOCK_NO_HEAT_REQUEST;
  } else {
    decision.desired_active = true;
  }

  if (decision.force_off) {
    decision.output_active = false;
  } else if (decision.desired_active) {
    if (!input.output_active && minimum_time_active(input.now_ms, input.output_last_change_ms, input.min_off_ms)) {
      decision.output_active = false;
      decision.block_reason = BLOCK_MIN_OFF_TIME;
    } else {
      decision.output_active = true;
      decision.block_reason = BLOCK_NONE;
    }
  } else if (input.output_active && minimum_time_active(input.now_ms, input.output_last_change_ms, input.min_on_ms)) {
    decision.output_active = true;
    decision.block_reason = BLOCK_MIN_ON_TIME;
  }

  decision.blocked = decision.demand_present && !decision.output_active;
  return decision;
}

inline const char* block_reason_text(uint8_t reason) {
  switch (reason) {
    case BLOCK_ASSIST_DISABLED:
      return "boiler/CV assist disabled";
    case BLOCK_COMMAND_INVALID:
      return "boiler command invalid";
    case BLOCK_COMMAND_STALE:
      return "boiler command stale";
    case BLOCK_SUPPLY_UNAVAILABLE:
      return "water supply temperature unavailable";
    case BLOCK_WATER_TEMP_INHIBIT:
      return "water temperature boiler inhibit active";
    case BLOCK_WATER_TEMP_HARD_TRIP:
      return "water temperature hard trip active";
    case BLOCK_COMMISSIONING_WAITING:
      return "CM100 boiler commissioning waiting for flow settle";
    case BLOCK_NO_HEAT_REQUEST:
      return "no boiler heat request";
    case BLOCK_MIN_ON_TIME:
      return "boiler minimum on-time active";
    case BLOCK_MIN_OFF_TIME:
      return "boiler minimum off-time active";
    case BLOCK_TRANSPORT_UNAVAILABLE:
      return "selected boiler transport unavailable";
    case BLOCK_TARGET_INVALID:
      return "boiler target temperature invalid";
    case BLOCK_TRANSPORT_SETTLING:
      return "boiler transport change settling";
    case BLOCK_AWAITING_FRESH_COMMAND:
      return "awaiting fresh boiler command";
    case BLOCK_CONNECTION_MISMATCH:
      return "OpenTherm boiler detected while R1 is selected";
    case BLOCK_FALLBACK_DISABLED:
      return "boiler fallback disabled";
    case BLOCK_FLOW_UNAVAILABLE:
      return "flow unavailable";
    case BLOCK_FLOW_INSUFFICIENT:
      return "flow too low";
    case BLOCK_HP_STOP_UNCONFIRMED:
      return "heat-pump stop is not confirmed";
    case BLOCK_SOURCE_NOT_CONNECTED:
      return "auxiliary heat source not connected";
    default:
      return "";
  }
}

inline const char* commissioning_start_failure_reason(uint8_t block_reason, bool opentherm_selected,
                                                      bool output_requested, bool opentherm_link_available) {
  const char* controller_reason = block_reason_text(block_reason);
  if (controller_reason[0] != '\0') return controller_reason;
  if (opentherm_selected && !opentherm_link_available) return "OpenTherm link unavailable";
  if (!output_requested) return "boiler request not applied";
  if (opentherm_selected) return "OpenTherm CH active not confirmed";
  return "boiler active state not confirmed";
}

}  // namespace oq_boiler
