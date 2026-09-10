#pragma once

#include <algorithm>
#include <cstdint>

namespace oq_cooling_start_block {

// Effective cooling start-block reasons. These strings are published to the
// diagnostic text sensor and mapped to Dutch copy in the web UI. Keep them
// stable: they are part of the debug-recording contract.
inline const char* kReady = "Ready";
inline const char* kCoolingMinOff = "Cooling minimum off-time";
inline const char* kCoolingConfirm = "Waiting for confirmed cooling stop";
inline const char* kCompressorRestart = "Compressor restart protection";
inline const char* kStartupInhibit = "Startup inhibit after reboot";
inline const char* kStartLimit = "Compressor start limit (6/hour)";
inline const char* kOtherBlocked = "Compressor start blocked";

struct Inputs {
  // Cooling demand context. When false, no start block is reported (Ready),
  // so heating stops or idle periods never show a cooling wait.
  bool cooling_demand_active = false;
  // True when at least one compressor is already running for cooling.
  // Running always wins over waiting: the plant is in real cooling operation.
  bool any_hp_running = false;
  // Shared cooling restart state (minimum-off-time mode). Blocks both HPs.
  uint32_t cooling_remaining_ms = 0;
  bool cooling_confirmation_pending = false;
  // Per-HP general restart protection from the incident manager, including
  // post-reboot startup inhibit. Remaining is 0 when that HP is free.
  uint32_t hp1_rest_remaining_ms = 0;
  uint32_t hp2_rest_remaining_ms = 0;
  bool hp1_startup_inhibited = false;
  bool hp2_startup_inhibited = false;
  // Per-HP compressor start quota (6 starts/hour). Remaining is 0 when free.
  uint32_t hp1_start_limit_remaining_ms = 0;
  uint32_t hp2_start_limit_remaining_ms = 0;
  // True when dispatch/demand wants cooling but no HP can serve for reasons
  // outside the timed guards above (candidate unavailable, frequency cap,
  // incident block, ...). No countdown is invented for this case.
  bool dispatch_blocked_other = false;
  bool duo = false;
};

struct Result {
  const char* reason = kReady;
  uint32_t remaining_ms = 0;
  bool blocked = false;
  // True when the remaining time is known and applies to this block.
  bool has_countdown = false;
};

inline bool is_time_bound_reason(const char* reason) {
  return reason == kCoolingMinOff || reason == kCompressorRestart || reason == kStartupInhibit || reason == kStartLimit;
}

inline uint32_t max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }

inline Result resolve(const Inputs& in) {
  Result out;
  if (!in.cooling_demand_active || in.any_hp_running) return out;

  // Shared cooling restart guard blocks both HPs first, matching the actuator
  // contract order (cooling rest before per-HP rest before start quota).
  if (in.cooling_remaining_ms > 0) {
    out.reason = kCoolingMinOff;
    out.remaining_ms = in.cooling_remaining_ms;
    out.blocked = true;
    out.has_countdown = true;
    return out;
  }
  if (in.cooling_confirmation_pending) {
    out.reason = kCoolingConfirm;
    out.blocked = true;
    return out;
  }

  const uint32_t hp1_rest = in.hp1_rest_remaining_ms;
  const uint32_t hp2_rest = in.duo ? in.hp2_rest_remaining_ms : 0U;
  const uint32_t rest_remaining_ms = max_u32(hp1_rest, hp2_rest);
  if (rest_remaining_ms > 0) {
    const bool startup = in.hp1_startup_inhibited || (in.duo && in.hp2_startup_inhibited);
    out.reason = startup ? kStartupInhibit : kCompressorRestart;
    out.remaining_ms = rest_remaining_ms;
    out.blocked = true;
    out.has_countdown = true;
    return out;
  }

  const uint32_t hp1_limit = in.hp1_start_limit_remaining_ms;
  const uint32_t hp2_limit = in.duo ? in.hp2_start_limit_remaining_ms : 0U;
  const uint32_t limit_remaining_ms = max_u32(hp1_limit, hp2_limit);
  if (limit_remaining_ms > 0) {
    out.reason = kStartLimit;
    out.remaining_ms = limit_remaining_ms;
    out.blocked = true;
    out.has_countdown = true;
    return out;
  }

  if (in.dispatch_blocked_other) {
    out.reason = kOtherBlocked;
    out.blocked = true;
    return out;
  }

  return out;
}

inline uint32_t remaining_seconds_ceil(uint32_t remaining_ms) { return (remaining_ms + 999UL) / 1000UL; }

}  // namespace oq_cooling_start_block
