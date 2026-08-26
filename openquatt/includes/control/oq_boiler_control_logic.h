#pragma once

#include <stdint.h>

#include "../boiler/oq_boiler_logic.h"
#include "oq_boiler_output_logic.h"

namespace oq_boiler {

inline BoilerRole boiler_role_for_source(uint8_t source) {
  switch (source) {
    case COMMAND_SOURCE_POWER_HOUSE:
    case COMMAND_SOURCE_HEATING_CURVE:
    case COMMAND_SOURCE_COLD_START:
      return BoilerRole::ASSIST_CM3;
    case COMMAND_SOURCE_FALLBACK:
      return BoilerRole::FALLBACK_CM4;
    case COMMAND_SOURCE_COMMISSIONING:
      return BoilerRole::COMMISSIONING_CM100;
    default:
      return BoilerRole::OFF;
  }
}

struct BoilerControllerLogInputs {
  BoilerRole role = BoilerRole::OFF;
  uint8_t controller_block_reason = BLOCK_NONE;
};

enum class BoilerLogReason : uint8_t {
  LESS_POWER = 0,
  FLOW_TOO_LOW = 1,
  SOFT_GUARD = 2,
  SENSOR_FALLBACK = 3,
  NO_CANDIDATE = 4,
  FALLBACK_BLOCKED = 5,
  HP_STOP_UNCONFIRMED = 6,
  FLOW_PREFLOW = 7,
  BOILER_FALLBACK = 8,
  HP_RECOVERED = 9,
  COMMISSIONING = 10,
  COOLING_REQUEST = 11,
  FROST_PROTECTION = 12,
  HEATING_REQUEST_CLEARED = 13,
  MIN_REST_ACTIVE = 14,
};

struct BoilerLogReasonCodes {
  uint8_t less_power = 0;
  uint8_t flow_too_low = 0;
  uint8_t soft_guard = 0;
  uint8_t sensor_fallback = 0;
  uint8_t no_candidate = 0;
  uint8_t fallback_blocked = 0;
  uint8_t hp_stop_unconfirmed = 0;
  uint8_t flow_preflow = 0;
  uint8_t boiler_fallback = 0;
  uint8_t hp_recovered = 0;
  uint8_t commissioning = 0;
  uint8_t cooling_request = 0;
  uint8_t frost_protection = 0;
  uint8_t heating_request_cleared = 0;
  uint8_t min_rest_active = 0;
};

struct BoilerLogSeverityCodes {
  uint8_t normal = 0;
  uint8_t limited = 0;
};

struct BoilerLogCodes {
  BoilerLogReasonCodes reason;
  BoilerLogSeverityCodes severity;
};

struct BoilerLogDecision {
  BoilerLogReason reason = BoilerLogReason::LESS_POWER;
  uint8_t reason_code = 0;
  uint8_t severity = 0;
};

inline uint8_t boiler_log_reason_code(BoilerLogReason reason, const BoilerLogReasonCodes& codes) {
  switch (reason) {
    case BoilerLogReason::LESS_POWER:
      return codes.less_power;
    case BoilerLogReason::FLOW_TOO_LOW:
      return codes.flow_too_low;
    case BoilerLogReason::SOFT_GUARD:
      return codes.soft_guard;
    case BoilerLogReason::SENSOR_FALLBACK:
      return codes.sensor_fallback;
    case BoilerLogReason::NO_CANDIDATE:
      return codes.no_candidate;
    case BoilerLogReason::FALLBACK_BLOCKED:
      return codes.fallback_blocked;
    case BoilerLogReason::HP_STOP_UNCONFIRMED:
      return codes.hp_stop_unconfirmed;
    case BoilerLogReason::FLOW_PREFLOW:
      return codes.flow_preflow;
    case BoilerLogReason::BOILER_FALLBACK:
      return codes.boiler_fallback;
    case BoilerLogReason::HP_RECOVERED:
      return codes.hp_recovered;
    case BoilerLogReason::COMMISSIONING:
      return codes.commissioning;
    case BoilerLogReason::COOLING_REQUEST:
      return codes.cooling_request;
    case BoilerLogReason::FROST_PROTECTION:
      return codes.frost_protection;
    case BoilerLogReason::HEATING_REQUEST_CLEARED:
      return codes.heating_request_cleared;
    case BoilerLogReason::MIN_REST_ACTIVE:
      return codes.min_rest_active;
  }
  return codes.less_power;
}

inline bool boiler_log_reason_is_normal(BoilerLogReason reason) {
  return reason == BoilerLogReason::BOILER_FALLBACK || reason == BoilerLogReason::LESS_POWER ||
         reason == BoilerLogReason::HP_RECOVERED || reason == BoilerLogReason::HEATING_REQUEST_CLEARED;
}

inline BoilerLogDecision make_boiler_log_decision(BoilerLogReason reason, const BoilerLogCodes& codes) {
  return BoilerLogDecision{
      reason,
      boiler_log_reason_code(reason, codes.reason),
      boiler_log_reason_is_normal(reason) ? codes.severity.normal : codes.severity.limited,
  };
}

inline BoilerLogDecision classify_boiler_controller_log(const BoilerControllerLogInputs& inputs,
                                                        const BoilerLogCodes& codes) {
  BoilerLogReason reason = BoilerLogReason::LESS_POWER;
  switch (inputs.controller_block_reason) {
    case BLOCK_WATER_TEMP_INHIBIT:
    case BLOCK_WATER_TEMP_HARD_TRIP:
    case BLOCK_BOILER_TOO_HOT_FOR_START:
      reason = BoilerLogReason::SOFT_GUARD;
      break;
    case BLOCK_FLOW_UNAVAILABLE:
    case BLOCK_FLOW_INSUFFICIENT:
      reason = BoilerLogReason::FLOW_TOO_LOW;
      break;
    case BLOCK_COMMAND_INVALID:
    case BLOCK_COMMAND_STALE:
    case BLOCK_SUPPLY_UNAVAILABLE:
    case BLOCK_TRANSPORT_UNAVAILABLE:
    case BLOCK_TARGET_INVALID:
    case BLOCK_TRANSPORT_SETTLING:
    case BLOCK_AWAITING_FRESH_COMMAND:
    case BLOCK_CONNECTION_MISMATCH:
    case BLOCK_BOILER_TEMPERATURE_UNAVAILABLE:
      reason = BoilerLogReason::SENSOR_FALLBACK;
      break;
    case BLOCK_ASSIST_DISABLED:
    case BLOCK_SOURCE_NOT_CONNECTED:
      reason = BoilerLogReason::NO_CANDIDATE;
      break;
    case BLOCK_FALLBACK_DISABLED:
      reason = BoilerLogReason::FALLBACK_BLOCKED;
      break;
    case BLOCK_HP_STOP_UNCONFIRMED:
      reason = BoilerLogReason::HP_STOP_UNCONFIRMED;
      break;
    case BLOCK_COMMISSIONING_WAITING:
      reason = BoilerLogReason::FLOW_PREFLOW;
      break;
    case BLOCK_MIN_OFF_TIME:
      reason = BoilerLogReason::MIN_REST_ACTIVE;
      break;
    default:
      if (inputs.role == BoilerRole::FALLBACK_CM4) {
        reason = inputs.controller_block_reason == BLOCK_NONE ? BoilerLogReason::BOILER_FALLBACK
                                                              : BoilerLogReason::FALLBACK_BLOCKED;
      }
      break;
  }
  return make_boiler_log_decision(reason, codes);
}

struct BoilerStopLogInputs {
  BoilerRole stopped_role = BoilerRole::OFF;
  bool continuous_handover = false;
  int current_mode = 0;
  bool heating_demand = false;
  bool fallback_enabled = false;
  bool fallback_outputs_safe = false;
  BoilerLogReason guard_reason = BoilerLogReason::LESS_POWER;
};

inline BoilerLogDecision classify_boiler_stop_log(const BoilerStopLogInputs& inputs, const BoilerLogCodes& codes) {
  BoilerLogReason reason = inputs.guard_reason;
  if (inputs.continuous_handover && inputs.stopped_role == BoilerRole::ASSIST_CM3 && inputs.current_mode == 4) {
    reason = BoilerLogReason::BOILER_FALLBACK;
  } else if (inputs.stopped_role == BoilerRole::FALLBACK_CM4) {
    if (inputs.continuous_handover || inputs.current_mode == 2 || inputs.current_mode == 3) {
      reason = BoilerLogReason::HP_RECOVERED;
    } else if (inputs.current_mode == 100) {
      reason = BoilerLogReason::COMMISSIONING;
    } else if (inputs.current_mode == 5) {
      reason = BoilerLogReason::COOLING_REQUEST;
    } else if (inputs.current_mode == 98) {
      reason = BoilerLogReason::FROST_PROTECTION;
    } else if (!inputs.heating_demand) {
      reason = BoilerLogReason::HEATING_REQUEST_CLEARED;
    } else if (!inputs.fallback_enabled) {
      reason = BoilerLogReason::FALLBACK_BLOCKED;
    } else if (!inputs.fallback_outputs_safe) {
      reason = BoilerLogReason::HP_STOP_UNCONFIRMED;
    } else if (reason == BoilerLogReason::LESS_POWER) {
      reason = BoilerLogReason::FALLBACK_BLOCKED;
    }
  }
  return make_boiler_log_decision(reason, codes);
}

}  // namespace oq_boiler
