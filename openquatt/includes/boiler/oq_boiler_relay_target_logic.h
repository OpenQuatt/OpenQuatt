#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_boiler {

// An R1/on-off boiler has no channel to receive a target temperature the way
// OpenTherm does: the relay only carries ON/OFF. This controller realises the
// same intent as OpenTherm does by regulating the binary output around the
// target temperature that the boiler command already carries.
//
// The two deltas are deliberately asymmetric and are policy, not physics:
// a binary burner is allowed to sit further below the requested target before
// it is energised again, and is released as soon as the measured supply has
// recovered to within stop_delta_c of that target. They are isolated here so
// they can be retuned from hardware evidence without touching the heating
// strategy, supervisory or the OpenTherm path.
struct RelayTargetConfig {
  float start_delta_c = 2.0f;
  float stop_delta_c = 0.5f;
};

enum RelayTargetState : uint8_t {
  // Target control does not own this output (OpenTherm selected, or a command
  // source that keeps its own semantics such as CM4 fallback or CM100).
  RELAY_TARGET_NOT_APPLICABLE = 0,
  // The command does not ask for heat, so the relay is released.
  RELAY_TARGET_IDLE = 1,
  // The supply is far enough below target to energise the relay.
  RELAY_TARGET_START = 2,
  // The supply sits inside the dead band while the relay is on, so the
  // previous output state is retained instead of toggling around one value.
  RELAY_TARGET_HOLD = 3,
  // The requested target is already met, so the relay stays released.
  RELAY_TARGET_SATISFIED = 4,
  // Target control applies but cannot be judged; fails safe to released.
  RELAY_TARGET_UNAVAILABLE = 5,
};

struct RelayTargetInput {
  bool applicable = false;
  bool heat_request = false;
  bool output_active = false;
  float supply_c = NAN;
  float target_c = NAN;
  RelayTargetConfig config = {};
};

struct RelayTargetDecision {
  bool applicable = false;
  bool requested_active = false;
  bool satisfied = false;
  uint8_t state = RELAY_TARGET_NOT_APPLICABLE;
};

// Both deltas must be positive and the start threshold must sit strictly below
// the stop threshold, otherwise the band collapses or inverts and the relay
// would toggle around a single temperature. Invalid policy is rejected instead
// of silently corrected so a misconfiguration cannot masquerade as regulation.
inline bool relay_target_config_valid(const RelayTargetConfig& config) {
  if (!isfinite(config.start_delta_c) || !isfinite(config.stop_delta_c)) return false;
  return config.stop_delta_c > 0.0f && config.start_delta_c > config.stop_delta_c;
}

inline RelayTargetDecision evaluate_relay_target(const RelayTargetInput& input) {
  RelayTargetDecision decision;
  decision.applicable = input.applicable;
  decision.requested_active = input.output_active;
  if (!input.applicable) return decision;

  if (!input.heat_request) {
    decision.requested_active = false;
    decision.state = RELAY_TARGET_IDLE;
    return decision;
  }

  // Never invent a temperature decision. An unusable measurement or an
  // unusable policy leaves the binary output in its safe state and is
  // reported separately from a satisfied target.
  if (!relay_target_config_valid(input.config) || !isfinite(input.supply_c) || !isfinite(input.target_c) ||
      input.target_c <= 0.0f) {
    decision.requested_active = false;
    decision.state = RELAY_TARGET_UNAVAILABLE;
    return decision;
  }

  const float start_threshold_c = input.target_c - input.config.start_delta_c;
  const float stop_threshold_c = input.target_c - input.config.stop_delta_c;
  if (decision.requested_active) {
    if (input.supply_c >= stop_threshold_c) {
      decision.requested_active = false;
      decision.satisfied = true;
      decision.state = RELAY_TARGET_SATISFIED;
    } else {
      decision.state = RELAY_TARGET_HOLD;
    }
    return decision;
  }

  if (input.supply_c <= start_threshold_c) {
    decision.requested_active = true;
    decision.state = RELAY_TARGET_START;
    return decision;
  }
  decision.satisfied = true;
  decision.state = RELAY_TARGET_SATISFIED;
  return decision;
}

inline const char* relay_target_state_text(uint8_t state) {
  switch (state) {
    case RELAY_TARGET_IDLE:
      return "idle: no boiler heat request";
    case RELAY_TARGET_START:
      return "starting: supply below target";
    case RELAY_TARGET_HOLD:
      return "holding: supply inside target band";
    case RELAY_TARGET_SATISFIED:
      return "satisfied: requested target met";
    case RELAY_TARGET_UNAVAILABLE:
      return "unavailable: target or supply invalid";
    default:
      return "not applicable";
  }
}

}  // namespace oq_boiler
