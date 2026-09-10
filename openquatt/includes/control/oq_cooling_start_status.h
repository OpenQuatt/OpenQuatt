#pragma once

#include <stdint.h>

namespace oq_cooling_start_status {

// Reason codes for the effective cooling start block (issue #642). The
// dispatch and the actuator write these when they actually refuse a wanted
// compressor start; the web UI only maps them to a label plus countdown.
enum Reason : uint8_t {
  NONE = 0,
  COOLING_MIN_OFF = 1,
  COOLING_CONFIRM = 2,
  HP_RESTART = 3,
  STARTUP_INHIBIT = 4,
  START_LIMIT = 5,
  OTHER = 6,
};

inline const char* reason_name(uint8_t reason) {
  switch (reason) {
    case COOLING_MIN_OFF:
      return "Cooling minimum off-time";
    case COOLING_CONFIRM:
      return "Waiting for confirmed cooling stop";
    case HP_RESTART:
      return "Compressor restart protection";
    case STARTUP_INHIBIT:
      return "Startup inhibit after reboot";
    case START_LIMIT:
      return "Compressor start limit (6/hour)";
    case OTHER:
      return "Compressor start blocked";
    case NONE:
    default:
      return "Ready";
  }
}

inline bool reason_has_countdown(uint8_t reason) {
  return reason == COOLING_MIN_OFF || reason == HP_RESTART || reason == STARTUP_INHIBIT || reason == START_LIMIT;
}

inline uint16_t ceil_seconds(uint32_t remaining_ms) {
  const uint32_t seconds = (remaining_ms + 999UL) / 1000UL;
  return seconds > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(seconds);
}

struct Status {
  uint8_t reason = NONE;
  uint16_t remaining_s = 0;
};

// Maps an actuator refuse to a status from plain booleans, so this stays
// testable without ESPHome dependencies. The caller passes its real
// preflight outcome: exactly one flag is true when a start is refused.
inline Status map_actuator_refuse(bool cooling_rest, bool hp_rest, bool start_limit, uint16_t aux_s,
                                  bool startup_inhibited) {
  if (cooling_rest) {
    return aux_s > 0 ? Status{COOLING_MIN_OFF, aux_s} : Status{COOLING_CONFIRM, 0};
  }
  if (hp_rest) {
    return startup_inhibited ? Status{STARTUP_INHIBIT, aux_s} : Status{HP_RESTART, aux_s};
  }
  if (start_limit) {
    return Status{START_LIMIT, aux_s};
  }
  return Status{OTHER, 0};
}

// Merges the dispatch slot with the actuator slot. Both writers refresh their
// slot every tick, so this is order-independent: the actuator is the final
// gate and wins whenever it refuses; otherwise the dispatch verdict stands.
inline Status merge(uint8_t dispatch_reason, uint16_t dispatch_remaining_s, uint8_t actuator_reason,
                    uint16_t actuator_remaining_s) {
  if (actuator_reason != NONE) {
    return Status{actuator_reason, actuator_remaining_s};
  }
  return Status{dispatch_reason, dispatch_remaining_s};
}

}  // namespace oq_cooling_start_status
