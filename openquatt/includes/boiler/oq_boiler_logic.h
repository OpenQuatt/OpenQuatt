#pragma once

#include <math.h>
#include <stdint.h>

#include "oq_boiler_relay_target_logic.h"

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
  BLOCK_BOILER_TOO_HOT_FOR_START = 21,
  BLOCK_BOILER_TEMPERATURE_UNAVAILABLE = 22,
  BLOCK_OPENTHERM_NOT_VERIFIED = 23,
  BLOCK_OPENTHERM_LINK_LOST = 24,
  // The command still asks for heat, but the R1 relay is not energised
  // because the requested target temperature is already met. This is a
  // normal outcome of target control, not a safety failure or a fault.
  BLOCK_TARGET_SATISFIED = 25,
  // The command still asks for heat and the R1 relay is not energised
  // because the supply is inside the target band but not yet at the stop
  // threshold. The relay is correctly held off, but the requested target is
  // not reached, so it must not be reported as satisfied. Like
  // BLOCK_TARGET_SATISFIED this is a normal control stop, not a fault.
  BLOCK_TARGET_HOLD_OFF = 26,
};

enum BoilerStartThermalState : uint8_t {
  BOILER_START_THERMAL_IDLE = 0,
  BOILER_START_THERMAL_NOT_APPLICABLE = 1,
  BOILER_START_THERMAL_SAFE = 2,
  BOILER_START_THERMAL_HOT = 3,
  BOILER_START_THERMAL_UNKNOWN = 4,
};

struct BoilerStartThermalDecision {
  uint8_t state = BOILER_START_THERMAL_IDLE;
  float safe_ceiling_c = NAN;
};

inline BoilerStartThermalDecision evaluate_boiler_start_thermal_state(
    bool opentherm_selected, bool boiler_temperature_fresh, float boiler_temperature_c, float system_supply_c,
    float requested_target_c, float maximum_water_temperature_c, float operating_margin_c = 2.0f) {
  if (!opentherm_selected) {
    return BoilerStartThermalDecision{BOILER_START_THERMAL_NOT_APPLICABLE, NAN};
  }
  if (!boiler_temperature_fresh || !isfinite(boiler_temperature_c) || !isfinite(system_supply_c) ||
      !isfinite(requested_target_c) || !isfinite(maximum_water_temperature_c) || !isfinite(operating_margin_c) ||
      maximum_water_temperature_c <= 0.0f || operating_margin_c < 0.0f) {
    return BoilerStartThermalDecision{BOILER_START_THERMAL_UNKNOWN, NAN};
  }

  const float operating_reference_c = fmaxf(system_supply_c, requested_target_c);
  const float safe_ceiling_c = fminf(maximum_water_temperature_c, operating_reference_c + operating_margin_c);
  return BoilerStartThermalDecision{
      boiler_temperature_c > safe_ceiling_c ? BOILER_START_THERMAL_HOT : BOILER_START_THERMAL_SAFE,
      safe_ceiling_c,
  };
}

inline const char* boiler_start_thermal_state_text(uint8_t state) {
  switch (state) {
    case BOILER_START_THERMAL_NOT_APPLICABLE:
      return "not applicable (R1)";
    case BOILER_START_THERMAL_SAFE:
      return "safe";
    case BOILER_START_THERMAL_HOT:
      return "blocked: boiler too hot";
    case BOILER_START_THERMAL_UNKNOWN:
      return "unknown: boiler temperature unavailable or stale";
    default:
      return "idle";
  }
}

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
  uint8_t boiler_start_thermal_state;
  bool target_required;
  bool target_valid;
  // Pre-computed R1 target-control request. The relay has no target channel, so
  // the requested target temperature is realised by regulating the binary
  // output around it. A default-constructed value is "not applicable", which
  // leaves every other output path and caller untouched.
  RelayTargetDecision relay_target;
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

// Which command sources hand the requested target temperature to the R1 relay
// for local regulation. OpenTherm receives the target over the bus instead, and
// CM4 fault fallback and CM100 commissioning deliberately keep their existing
// on/off semantics: they are not normal auxiliary heat and must not change
// meaning as a side effect of target control.
inline bool relay_target_control_applies(uint8_t source, bool opentherm_selected) {
  if (opentherm_selected) return false;
  return source == COMMAND_SOURCE_POWER_HOUSE || source == COMMAND_SOURCE_HEATING_CURVE ||
         source == COMMAND_SOURCE_COLD_START;
}

inline uint8_t refine_opentherm_transport_block_reason(uint8_t block_reason, bool opentherm_selected,
                                                       bool ever_verified, bool currently_verified) {
  if (!opentherm_selected || block_reason != BLOCK_TRANSPORT_UNAVAILABLE) return block_reason;
  if (!ever_verified) return BLOCK_OPENTHERM_NOT_VERIFIED;
  return currently_verified ? BLOCK_TRANSPORT_UNAVAILABLE : BLOCK_OPENTHERM_LINK_LOST;
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
  } else if (command.heat_request && !input.output_active &&
             input.boiler_start_thermal_state == BOILER_START_THERMAL_HOT) {
    decision.force_off = true;
    decision.block_reason = BLOCK_BOILER_TOO_HOT_FOR_START;
  } else if (command.heat_request && !input.output_active && command.source == COMMAND_SOURCE_COMMISSIONING &&
             input.boiler_start_thermal_state == BOILER_START_THERMAL_UNKNOWN) {
    // A service test may only energize an OpenTherm boiler after proving the
    // chosen operating point is thermally suitable. Normal CM3/CM4 operation
    // retains the existing supply-temperature fail-safe when optional ID25 is
    // unsupported or temporarily unavailable.
    decision.force_off = true;
    decision.block_reason = BLOCK_BOILER_TEMPERATURE_UNAVAILABLE;
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
  } else if (command.heat_request && (input.target_required || input.relay_target.applicable) && !input.target_valid) {
    // Both transports need a usable target for a heat request: OpenTherm sends
    // it as TSet, and R1 has to regulate the binary output around it. R1
    // therefore reuses this same validation instead of a second bound, and
    // both withdraw heat immediately.
    decision.force_off = true;
    decision.block_reason = BLOCK_TARGET_INVALID;
  } else if (!command.heat_request) {
    decision.block_reason =
        command.source == COMMAND_SOURCE_COMMISSIONING ? BLOCK_COMMISSIONING_WAITING : BLOCK_NO_HEAT_REQUEST;
  } else {
    decision.desired_active = true;
  }

  // The relay has no target channel, so the requested target is realised by
  // regulating the binary output around it. target_required is set when
  // OpenTherm is selected, and that transport realises the target over the bus
  // instead, so it must be immune here even if a decision is offered.
  const bool relay_target_withholds_output = decision.desired_active && !input.target_required &&
                                             input.relay_target.applicable && !input.relay_target.requested_active;
  const bool relay_target_satisfied = relay_target_withholds_output && input.relay_target.satisfied;
  const bool relay_target_holding_off =
      relay_target_withholds_output && input.relay_target.state == RELAY_TARGET_HOLD_OFF;
  // Both states above withheld the relay on a target control that did judge the
  // request, so neither of them is a blockade.
  const bool relay_target_normal_stop = relay_target_satisfied || relay_target_holding_off;
  // Everything else that withholds the output is a target control that cannot
  // judge the request at all, because the measurement or the policy is
  // unusable. That fails safe to released without consulting anti-cycling: an
  // unjudgeable command must not keep an energised burner, exactly as an
  // invalid TSet forces CH off immediately on OpenTherm.
  const bool relay_target_failsafe = relay_target_withholds_output && !relay_target_normal_stop;

  if (decision.force_off) {
    decision.output_active = false;
  } else if (relay_target_failsafe) {
    decision.output_active = false;
    decision.block_reason = BLOCK_TARGET_INVALID;
  } else if (decision.desired_active && !relay_target_withholds_output) {
    if (!input.output_active && minimum_time_active(input.now_ms, input.output_last_change_ms, input.min_off_ms)) {
      decision.output_active = false;
      decision.block_reason = BLOCK_MIN_OFF_TIME;
    } else {
      decision.output_active = true;
      decision.block_reason = BLOCK_NONE;
    }
  } else if (input.output_active && minimum_time_active(input.now_ms, input.output_last_change_ms, input.min_on_ms)) {
    // Releasing the relay follows the normal anti-cycling rule, whether the
    // heat request ended or a judged target says the relay is not needed. A
    // safety trip, a loss of ownership, or an unavailable target control above
    // withdraws heat immediately instead.
    decision.output_active = true;
    decision.block_reason = BLOCK_MIN_ON_TIME;
  } else if (relay_target_withholds_output) {
    decision.output_active = false;
    decision.block_reason = relay_target_satisfied ? BLOCK_TARGET_SATISFIED : BLOCK_TARGET_HOLD_OFF;
  }

  // A satisfied target, and a relay that is held off inside the band, both keep
  // the boiler demand owned by the active control mode. They must stay
  // distinguishable from a blocked or faulted boiler, so they are not reported
  // as blocked even though the physical output is off. An unavailable target
  // control is a real blockade and keeps the flag.
  decision.blocked = decision.demand_present && !decision.output_active && !relay_target_normal_stop;
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
    case BLOCK_TARGET_SATISFIED:
      return "requested boiler target temperature satisfied";
    case BLOCK_TARGET_HOLD_OFF:
      return "boiler target control holding off inside target band";
    case BLOCK_TRANSPORT_SETTLING:
      return "boiler transport change settling";
    case BLOCK_AWAITING_FRESH_COMMAND:
      return "awaiting fresh boiler command";
    case BLOCK_CONNECTION_MISMATCH:
      return "OpenTherm boiler detected while R1 is selected";
    case BLOCK_OPENTHERM_NOT_VERIFIED:
      return "OpenTherm connection not verified";
    case BLOCK_OPENTHERM_LINK_LOST:
      return "OpenTherm link lost after verified connection";
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
    case BLOCK_BOILER_TOO_HOT_FOR_START:
      return "boiler temperature too high for safe start";
    case BLOCK_BOILER_TEMPERATURE_UNAVAILABLE:
      return "boiler temperature unavailable for safe commissioning start";
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
