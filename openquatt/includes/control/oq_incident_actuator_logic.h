#pragma once

#include <stdint.h>

namespace oq_incident_actuator {

enum class Action : uint8_t {
  FOLLOW_REQUEST = 0,
  BLOCK_NEW_START = 1,
  FORCE_STOP = 2,
};

struct Inputs {
  int requested_level = 0;
  int previous_applied_level = 0;
  bool available_for_start = false;
  bool must_stop = false;
};

struct Decision {
  Action action = Action::FOLLOW_REQUEST;
  int guarded_level = 0;
  bool bypass_runtime_and_defrost_holds = false;
};

inline Decision decide(const Inputs& inputs) {
  if (inputs.must_stop) {
    return Decision{Action::FORCE_STOP, 0, true};
  }
  if (inputs.requested_level > 0 && inputs.previous_applied_level <= 0 && !inputs.available_for_start) {
    return Decision{Action::BLOCK_NEW_START, 0, false};
  }
  return Decision{Action::FOLLOW_REQUEST, inputs.requested_level, false};
}

inline bool safe_stop_write_retry_due(bool stop_confirmation_pending, uint32_t now_ms, uint32_t last_write_ms,
                                      uint32_t retry_interval_ms) {
  if (!stop_confirmation_pending) return false;
  if (last_write_ms == 0U) return true;
  return static_cast<uint32_t>(now_ms - last_write_ms) >= retry_interval_ms;
}

inline bool requires_stop_notification(bool initial_stop_registration_required, bool must_stop, bool stop_confirmed,
                                       bool stop_confirmation_pending) {
  return initial_stop_registration_required || stop_confirmation_pending || (must_stop && !stop_confirmed);
}

// Establish the incident manager's fresh-observation baseline before the
// first active mode/level command. A denied start produces only a safe write.
template <typename StartGate, typename ActiveWrite, typename SafeWrite>
inline bool apply_start_gate_before_active_write(bool start_required, StartGate start_gate, ActiveWrite active_write,
                                                 SafeWrite safe_write) {
  if (start_required && !start_gate()) {
    safe_write();
    return false;
  }
  active_write();
  return true;
}

// Register a stop before Standby/level-0 so only observations newer than the
// command can confirm it.
template <typename StopNotify, typename SafeWrite>
inline void apply_stop_notification_before_safe_write(bool stop_notification_required, StopNotify stop_notify,
                                                      SafeWrite safe_write) {
  if (stop_notification_required) stop_notify();
  safe_write();
}

}  // namespace oq_incident_actuator
