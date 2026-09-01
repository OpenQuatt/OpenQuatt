#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace oq_supervisory_safety {

struct Input {
  uint32_t now_ms = 0;
  bool thermal_request = false;
  bool flow_has_state = false;
  float flow_lph = NAN;
  bool outside_temperature_has_state = false;
  float outside_temperature_c = NAN;
};

struct Config {
  float minimum_flow_lph = NAN;
  uint32_t low_flow_fault_ms = 0;
  uint32_t flow_recover_ms = 0;
  float frost_on_c = NAN;
  float frost_off_c = NAN;
  uint32_t frost_nan_grace_ms = 0;
};

struct State {
  bool initialized = false;
  uint32_t boot_ms = 0;
  bool low_flow_timing = false;
  uint32_t low_flow_since_ms = 0;
  bool flow_recovery_timing = false;
  uint32_t flow_recovery_since_ms = 0;
  bool low_flow_fault_active = false;
  bool frost_initialized = false;
  bool frost_active = false;
};

struct Output {
  State state;
  bool flow_valid = false;
  bool flow_low = true;
  bool flow_ok = false;
  bool minimum_flow_ok = false;
  bool low_flow_fault_started = false;
  bool low_flow_fault_cleared = false;
  bool frost_active = false;
  bool frost_nan_grace_active = false;
};

inline uint32_t seconds_to_ms(uint32_t seconds) {
  constexpr uint32_t maximum_seconds = std::numeric_limits<uint32_t>::max() / 1000UL;
  return (seconds > maximum_seconds ? maximum_seconds : seconds) * 1000UL;
}

inline bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t duration_ms) {
  return static_cast<uint32_t>(now_ms - since_ms) >= duration_ms;
}

inline bool force_standby(bool low_flow_fault_active, bool defrost_active, int selected_level, int previous_level) {
  if (!low_flow_fault_active) return false;
  return !defrost_active || (selected_level <= 0 && previous_level <= 0);
}

inline Output step(const Input& input, const Config& config, State state) {
  if (!state.initialized) {
    state.initialized = true;
    state.boot_ms = input.now_ms;
  }

  const bool previous_fault = state.low_flow_fault_active;
  const bool minimum_flow_valid = std::isfinite(config.minimum_flow_lph) && config.minimum_flow_lph >= 0.0f;
  const bool flow_valid = input.flow_has_state && std::isfinite(input.flow_lph);
  const bool flow_ok = minimum_flow_valid && flow_valid && input.flow_lph >= config.minimum_flow_lph;
  const bool flow_low = !flow_ok;

  if (!input.thermal_request) {
    state.low_flow_timing = false;
    state.flow_recovery_timing = false;
    state.low_flow_fault_active = false;
  } else if (flow_low) {
    if (!state.low_flow_timing) {
      state.low_flow_timing = true;
      state.low_flow_since_ms = input.now_ms;
    }
    state.flow_recovery_timing = false;
    if (elapsed(input.now_ms, state.low_flow_since_ms, config.low_flow_fault_ms)) {
      state.low_flow_fault_active = true;
    }
  } else {
    state.low_flow_timing = false;
    if (state.low_flow_fault_active) {
      if (!state.flow_recovery_timing) {
        state.flow_recovery_timing = true;
        state.flow_recovery_since_ms = input.now_ms;
      }
      if (elapsed(input.now_ms, state.flow_recovery_since_ms, config.flow_recover_ms)) {
        state.low_flow_fault_active = false;
        state.flow_recovery_timing = false;
      }
    } else {
      state.flow_recovery_timing = false;
    }
  }

  const bool frost_nan_grace_active =
      config.frost_nan_grace_ms > 0 && !elapsed(input.now_ms, state.boot_ms, config.frost_nan_grace_ms);
  const bool frost_thresholds_valid =
      std::isfinite(config.frost_on_c) && std::isfinite(config.frost_off_c) && config.frost_on_c <= config.frost_off_c;
  const bool outside_temperature_valid =
      frost_thresholds_valid && input.outside_temperature_has_state && std::isfinite(input.outside_temperature_c);
  if (input.thermal_request) {
    state.frost_active = false;
    state.frost_initialized = true;
  } else if (!outside_temperature_valid) {
    state.frost_active = !frost_nan_grace_active;
    if (!frost_nan_grace_active) state.frost_initialized = true;
  } else if (!state.frost_initialized) {
    // Reconstruct the non-persistent hysteresis conservatively after boot.
    state.frost_active = input.outside_temperature_c < config.frost_off_c;
    state.frost_initialized = true;
  } else if (state.frost_active) {
    state.frost_active = input.outside_temperature_c < config.frost_off_c;
  } else {
    state.frost_active = input.outside_temperature_c < config.frost_on_c;
  }

  return {state,
          flow_valid,
          flow_low,
          flow_ok,
          input.thermal_request ? (!state.low_flow_fault_active && flow_ok) : true,
          !previous_fault && state.low_flow_fault_active,
          previous_fault && !state.low_flow_fault_active,
          state.frost_active,
          frost_nan_grace_active};
}

}  // namespace oq_supervisory_safety
