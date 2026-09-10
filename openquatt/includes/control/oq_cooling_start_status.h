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

inline uint16_t ceil_seconds(uint32_t remaining_ms) {
  const uint32_t seconds = (remaining_ms + 999UL) / 1000UL;
  return seconds > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(seconds);
}

struct Status {
  // Contract, enforced by every writer and covered by host tests: timed
  // reasons always carry remaining_s > 0, the others always 0. The UI keys
  // its countdown off remaining_s and needs no reason table.
  uint8_t reason = NONE;
  uint16_t remaining_s = 0;
};

// Maps an actuator refuse to a status from plain booleans, so this stays
// testable without ESPHome dependencies. The caller passes its real
// preflight outcome: exactly one flag is true when a start is refused.
inline Status map_actuator_refuse(bool cooling_rest, bool hp_rest, bool start_limit, uint16_t aux_s) {
  if (cooling_rest) {
    return aux_s > 0 ? Status{COOLING_MIN_OFF, aux_s} : Status{COOLING_CONFIRM, 0};
  }
  if (hp_rest) {
    return Status{HP_RESTART, aux_s};
  }
  if (start_limit) {
    return Status{START_LIMIT, aux_s};
  }
  return Status{OTHER, 0};
}

// Aggregates the per-HP actuator verdicts into the single shared slot. Called
// once per tick after every HP ran, so the diagnosis never depends on HP
// processing order: the first refusing HP wins, deterministically.
inline Status aggregate_actuator_slot(Status hp1, Status hp2 = Status{}) { return hp1.reason != NONE ? hp1 : hp2; }

// Names a startup inhibit that the dispatch can only see as an unavailable
// candidate: inhibited HPs get their requests zeroed upstream and never reach
// the actuator preflight, so the incident manager's own flags and remaining
// time resolve the underlying cause here. Call only for an OTHER verdict;
// timed verdicts already name a genuine block.
inline Status resolve_startup_override(bool hp1_inhibited, bool hp2_inhibited, uint32_t hp1_remaining_ms,
                                       uint32_t hp2_remaining_ms) {
  uint32_t remaining_ms = hp1_inhibited ? hp1_remaining_ms : 0;
  if (hp2_inhibited && hp2_remaining_ms > remaining_ms) remaining_ms = hp2_remaining_ms;
  if (!hp1_inhibited && !hp2_inhibited) {
    return Status{OTHER, 0};
  }
  return Status{STARTUP_INHIBIT, ceil_seconds(remaining_ms)};
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
