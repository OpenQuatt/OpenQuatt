#pragma once

#include <stdint.h>

namespace oq_ot_slave {

// Room values are expected cyclically, but allow slow masters the same ten-minute
// freshness window used by the external room-temperature ingress contracts.
constexpr uint32_t ROOM_SIGNAL_TIMEOUT_MS = 600000UL;

inline bool room_signal_fresh(bool enabled, bool runtime_started, bool runtime_paused, uint32_t last_update_ms,
                              uint32_t now_ms, uint32_t timeout_ms = ROOM_SIGNAL_TIMEOUT_MS) {
  if (!enabled || !runtime_started || runtime_paused || last_update_ms == 0 || timeout_ms == 0) return false;
  return static_cast<uint32_t>(now_ms - last_update_ms) <= timeout_ms;
}

}  // namespace oq_ot_slave
