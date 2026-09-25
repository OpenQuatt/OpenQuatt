#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "../odu/oq_odu_generation.h"

namespace oq_defrost {

constexpr uint32_t START_TIMEOUT_MS = 210000U;
constexpr uint32_t FRESH_MS = 30000U;
enum class Phase : uint8_t { IDLE, WAITING, ACTIVE, RESYNC };

// Observations and transitions run on the ESPHome main loop, never in HTTP callbacks.
struct Cycle {
  Phase phase{Phase::IDLE};
  const char* result{"IDLE"};
  uint32_t requested_ms{0}, started_ms{0}, ended_ms{0}, duration_s{0};
  uint32_t mode_ms{0}, bit_ms{0}, clear_ms{0};
  int mode{-1};
  bool bit{false}, mode_seen{false}, bit_seen{false}, clearing{false};
  bool manual{false}, history_known{false}, interrupted{false}, continuous{false};
  bool idle_seen{false};

  bool fresh(uint32_t now) const {
    return mode_seen && bit_seen && now - mode_ms <= FRESH_MS && now - bit_ms <= FRESH_MS;
  }
  // Short telemetry gaps preserve ODU ownership; prolonged blindness requires a safety stop.
  bool telemetry_fault(uint32_t now) const { return now - mode_ms > 90000U || now - bit_ms > 90000U; }
  bool observed_active() const { return mode == 4 || bit; }
  bool owns() const { return phase == Phase::WAITING || phase == Phase::ACTIVE; }
  void offline() {
    mode_seen = bit_seen = clearing = false;
    history_known = false;
    continuous = idle_seen = false;
  }
  void observe_mode(float value, uint32_t now) {
    mode_seen = std::isfinite(value);
    mode = mode_seen ? static_cast<int>(std::lround(value)) : -1;
    if (mode_seen) mode_ms = now;
  }
  void observe_bit(bool value, uint32_t now) {
    bit = value;
    bit_seen = true;
    bit_ms = now;
  }
  void request(uint32_t now) {
    phase = Phase::WAITING;
    result = "WAITING";
    manual = true;
    interrupted = false;
    requested_ms = now;
    clearing = false;
  }
  void step(uint32_t now) {
    if (!fresh(now)) {
      clearing = false;
      continuous = false;
      return;
    }
    if (observed_active()) {
      if (phase != Phase::ACTIVE) {
        continuous = idle_seen;
        started_ms = now;
        phase = Phase::ACTIVE;
        result = interrupted ? "SAFETY_STOP" : manual && mode == 4 ? "ACCEPTED" : "ACTIVE";
      }
      if (!interrupted && manual && mode == 4) result = "ACCEPTED";
      clearing = false;
      return;
    }
    idle_seen = true;
    if (phase == Phase::WAITING && now - requested_ms >= START_TIMEOUT_MS) {
      phase = Phase::RESYNC;
      result = "TIMEOUT";
    } else if (phase == Phase::ACTIVE) {
      if (!clearing) {
        clear_ms = now;
        clearing = true;
      }
      // Require both readbacks after clear began; repeated cached samples do not count.
      if (now - clear_ms >= 2000U && mode_ms != clear_ms && bit_ms != clear_ms && mode_ms - clear_ms < FRESH_MS &&
          bit_ms - clear_ms < FRESH_MS) {
        duration_s = (clear_ms - started_ms) / 1000U;
        ended_ms = clear_ms;
        history_known = true;
        phase = Phase::RESYNC;
        result = interrupted ? "SAFETY_STOP" : "RESYNC";
      }
    }
  }
  void resynced() {
    phase = Phase::IDLE;
    manual = false;
    if (std::strcmp(result, "RESYNC") == 0) result = "COMPLETE";
    interrupted = false;
  }
  void safety_stop() {
    if (phase == Phase::WAITING) phase = Phase::RESYNC;
    manual = false;
    interrupted = true;
    result = "SAFETY_STOP";
  }
};

struct Guard {
  bool online{false}, fresh{false}, identity{false}, automatic{false};
  bool incident{true}, service{false}, peer{false}, busy{false};
  int mode{-1};
  float hz{0};
  bool active{false};
};
inline const char* refusal(const Guard& g) {
  if (!g.online || !g.fresh) return "OFFLINE";
  if (!g.identity) return "IDENTITY_REQUIRED";
  if (!g.automatic) return "AUTO_CONTROL_UNAVAILABLE";
  if (g.active) return "ALREADY_ACTIVE";
  if (g.busy || g.service) return "BUSY";
  if (g.incident) return "INCIDENT_BLOCK";
  if (g.peer) return "PEER_DEFROST_ACTIVE";
  if (g.mode != 2 && g.mode != 3) return "NOT_HEATING";
  if (!std::isfinite(g.hz) || g.hz <= 0) return "COMPRESSOR_NOT_RUNNING";
  return "READY";
}
inline bool hp_active(bool heating, bool cooling, bool target_heating, bool target_cooling, int level, bool holding,
                      bool observed) {
  return heating || cooling || target_heating || target_cooling || level > 0 || holding || observed;
}
inline float cm0_pump_target(bool sticky_active, bool hp_active_guard, float sticky_pwm, float stop_pwm) {
  return (sticky_active || hp_active_guard) ? sticky_pwm : stop_pwm;
}
inline bool minimum_flow_ready(float flow_lph, float minimum_lph) {
  return std::isfinite(flow_lph) && std::isfinite(minimum_lph) && flow_lph >= minimum_lph;
}
constexpr uint16_t MODE_REGISTER = 3275U;

// V1 firmware 1.25 has the legacy interval/trend selector but not the V1.5
// mode-4 Ta-Tevap algorithm. Observed V1.5, V2 old-model and V2 new-model
// EEPROM profiles all contain populated mode-4 parameters; both observed V2
// variants also select mode 4 by default. Values differ per variant, so the
// diagnostics must use live parameters instead of a shared hardcoded profile.
inline bool has_mode4_defrost(oq_odu::Variant variant) {
  return variant == oq_odu::Variant::V1_5 || variant == oq_odu::Variant::V2_OLD_MODEL ||
         variant == oq_odu::Variant::V2_NEW_MODEL;
}
inline bool is_supported_defrost_mode(int mode, oq_odu::Variant variant) {
  if (variant == oq_odu::Variant::V1) return mode == 0 || mode == 1 || mode == 3;
  if (has_mode4_defrost(variant)) return mode == 0 || mode == 1 || mode == 3 || mode == 4;
  return false;
}
inline const char* mode_save_guard_refusal(const Guard& g) {
  if (!g.online || !g.fresh) return "OFFLINE";
  if (!g.identity) return "IDENTITY_REQUIRED";
  if (!g.automatic) return "AUTO_CONTROL_UNAVAILABLE";
  if (g.active) return "ALREADY_ACTIVE";
  if (g.busy || g.service) return "BUSY";
  if (g.incident) return "INCIDENT_BLOCK";
  if (g.peer) return "PEER_DEFROST_ACTIVE";
  if (!std::isfinite(g.hz) || g.hz > 0) return "COMPRESSOR_RUNNING";
  return "READY";
}
inline const char* mode_save_error(const Guard& g, int desired, int expected, int current, bool loaded, bool automatic,
                                   oq_odu::Variant variant) {
  if (!loaded) return "LOAD_REQUIRED";
  if (!automatic) return "AUTO_CONTROL_UNAVAILABLE";
  if (!is_supported_defrost_mode(desired, variant)) return "INVALID_MODE";
  if (expected != current) return "STALE";
  if (desired == current) return "NO_CHANGE";
  return mode_save_guard_refusal(g);
}
}  // namespace oq_defrost
