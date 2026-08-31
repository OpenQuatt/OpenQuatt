#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace oq_supervisory_state {

inline uint32_t seconds_to_ms(uint32_t seconds) {
  constexpr uint32_t maximum_seconds = std::numeric_limits<uint32_t>::max() / 1000UL;
  return std::min(seconds, maximum_seconds) * 1000UL;
}

inline bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t duration_ms) {
  return static_cast<uint32_t>(now_ms - since_ms) >= duration_ms;
}

inline bool window_active(uint32_t now_ms, uint32_t until_ms) {
  return until_ms != 0 && static_cast<uint32_t>(until_ms - now_ms) < 0x80000000UL;
}

inline float finite_clamp(float value, float fallback, float minimum, float maximum) {
  return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
}

struct LowLoadState {
  bool heat_latched = false;
  float cached_minimum_power_w = NAN;
  uint32_t cached_minimum_power_updated_ms = 0;
  uint32_t reentry_block_until_ms = 0;
};

struct LowLoadInput {
  uint32_t now_ms = 0;
  bool active = false;
  bool raw_heating_request = false;
  bool openquatt_enabled = false;
  float requested_power_w = NAN;
  float live_minimum_power_w = NAN;
  float off_factor = NAN;
  float on_factor = NAN;
  float minimum_hysteresis_w = NAN;
  float fallback_off_w = NAN;
  float fallback_on_w = NAN;
  uint32_t cache_max_ms = 0;
};

struct LowLoadOutput {
  LowLoadState state;
  bool heating_request = false;
  bool reentry_block_active = false;
  float effective_minimum_power_w = NAN;
  float off_threshold_w = NAN;
  float on_threshold_w = NAN;
  int source_code = 0;
};

inline LowLoadOutput update_low_load(const LowLoadInput& input, LowLoadState state) {
  if (!input.active) {
    state.heat_latched = input.raw_heating_request;
    state.reentry_block_until_ms = 0;
    return {state, input.raw_heating_request && input.openquatt_enabled, false, NAN, NAN, NAN, 0};
  }

  const float off_factor = finite_clamp(input.off_factor > 0.0f ? input.off_factor : NAN, 0.75f, 0.60f, 0.90f);
  const float on_factor = finite_clamp(input.on_factor > 0.0f ? input.on_factor : NAN, 1.00f, 0.85f, 1.10f);
  const float minimum_hysteresis_w =
      finite_clamp(input.minimum_hysteresis_w > 0.0f ? input.minimum_hysteresis_w : NAN, 200.0f, 100.0f, 400.0f);
  float off_w = std::isfinite(input.fallback_off_w) && input.fallback_off_w >= 0.0f ? input.fallback_off_w : 900.0f;
  float on_w = std::isfinite(input.fallback_on_w) && input.fallback_on_w >= 0.0f ? input.fallback_on_w : 1300.0f;
  float effective_minimum_power_w = NAN;
  int source_code = 3;

  if (std::isfinite(input.live_minimum_power_w) && input.live_minimum_power_w > 0.0f) {
    effective_minimum_power_w = input.live_minimum_power_w;
    state.cached_minimum_power_w = input.live_minimum_power_w;
    state.cached_minimum_power_updated_ms = input.now_ms;
    source_code = 1;
  } else if (std::isfinite(state.cached_minimum_power_w) && state.cached_minimum_power_w > 0.0f &&
             state.cached_minimum_power_updated_ms != 0 &&
             static_cast<uint32_t>(input.now_ms - state.cached_minimum_power_updated_ms) <= input.cache_max_ms) {
    effective_minimum_power_w = state.cached_minimum_power_w;
    source_code = 2;
  }

  if (std::isfinite(effective_minimum_power_w)) {
    off_w = std::clamp(off_factor * effective_minimum_power_w, 500.0f, 1600.0f);
    on_w = std::clamp(on_factor * effective_minimum_power_w, 600.0f, 2200.0f);
  }
  if (on_w < off_w + minimum_hysteresis_w) on_w = off_w + minimum_hysteresis_w;
  if (on_w > 2200.0f) on_w = 2200.0f;
  if (off_w > on_w - minimum_hysteresis_w) off_w = std::max(500.0f, on_w - minimum_hysteresis_w);

  if (std::isfinite(input.requested_power_w)) {
    if (!state.heat_latched && input.requested_power_w >= on_w) state.heat_latched = true;
    if (state.heat_latched && input.requested_power_w <= off_w) state.heat_latched = false;
  } else {
    state.heat_latched = input.raw_heating_request;
  }

  bool reentry_block_active = window_active(input.now_ms, state.reentry_block_until_ms);
  if (reentry_block_active && std::isfinite(input.requested_power_w) && input.requested_power_w >= on_w) {
    state.reentry_block_until_ms = 0;
    reentry_block_active = false;
  }
  const bool heating_request = input.raw_heating_request && state.heat_latched && input.openquatt_enabled;
  return {state, heating_request, reentry_block_active, effective_minimum_power_w, off_w, on_w, source_code};
}

struct ConfirmationState {
  bool timing = false;
  uint32_t since_ms = 0;
};

struct ConfirmationOutput {
  ConfirmationState state;
  bool confirmed = false;
};

inline ConfirmationOutput confirm_request(uint32_t now_ms, bool request, bool confirmation_scope,
                                          uint32_t confirmation_ms, ConfirmationState state) {
  if (!request || !confirmation_scope || confirmation_ms == 0) {
    state.timing = false;
    return {state, request};
  }
  if (!state.timing) {
    state.timing = true;
    state.since_ms = now_ms;
  }
  return {state, elapsed(now_ms, state.since_ms, confirmation_ms)};
}

enum class IdleExitReason : uint8_t {
  NOT_IN_CM2,
  NO_HEAT_REQUEST,
  CURVE_MODE,
  LEVELS_ON,
  UNITS_NOT_IDLE,
  STARTUP_GRACE,
  HIGH_LOAD,
  TIMING,
  TRIP,
};

struct IdleExitState {
  bool timing = false;
  uint32_t since_ms = 0;
};

struct IdleExitInput {
  uint32_t now_ms = 0;
  uint32_t timeout_ms = 0;
  bool in_cm2 = false;
  bool heating_request = false;
  bool curve_mode = false;
  bool both_levels_off = false;
  bool both_units_idle = false;
  bool startup_grace_active = false;
  bool high_load_block = false;
};

struct IdleExitOutput {
  IdleExitState state;
  bool trip = false;
  IdleExitReason reason = IdleExitReason::NOT_IN_CM2;
};

inline IdleExitOutput update_idle_exit(const IdleExitInput& input, IdleExitState state) {
  IdleExitReason reason = IdleExitReason::NOT_IN_CM2;
  if (input.in_cm2) {
    if (!input.heating_request)
      reason = IdleExitReason::NO_HEAT_REQUEST;
    else if (input.curve_mode)
      reason = IdleExitReason::CURVE_MODE;
    else if (!input.both_levels_off)
      reason = IdleExitReason::LEVELS_ON;
    else if (!input.both_units_idle)
      reason = IdleExitReason::UNITS_NOT_IDLE;
    else if (input.startup_grace_active)
      reason = IdleExitReason::STARTUP_GRACE;
    else if (input.high_load_block)
      reason = IdleExitReason::HIGH_LOAD;
    else
      reason = IdleExitReason::TIMING;
  }
  const bool eligible = reason == IdleExitReason::TIMING;
  if (!eligible) {
    state.timing = false;
    return {state, false, reason};
  }
  if (!state.timing) {
    state.timing = true;
    state.since_ms = input.now_ms;
  }
  if (elapsed(input.now_ms, state.since_ms, input.timeout_ms)) return {state, true, IdleExitReason::TRIP};
  return {state, false, reason};
}

inline const char* idle_exit_reason_name(IdleExitReason reason) {
  switch (reason) {
    case IdleExitReason::NO_HEAT_REQUEST:
      return "no_heat_req";
    case IdleExitReason::CURVE_MODE:
      return "blocked_curve_mode";
    case IdleExitReason::LEVELS_ON:
      return "levels_on";
    case IdleExitReason::UNITS_NOT_IDLE:
      return "units_not_idle";
    case IdleExitReason::STARTUP_GRACE:
      return "blocked_startup_grace";
    case IdleExitReason::HIGH_LOAD:
      return "blocked_high_load";
    case IdleExitReason::TIMING:
      return "timing";
    case IdleExitReason::TRIP:
      return "trip";
    default:
      return "not_in_cm2";
  }
}

struct OverrideState {
  int last_mode = 0;
  bool timing = false;
  uint32_t since_ms = 0;
};

struct OverrideOutput {
  OverrideState state;
  int effective_mode = 0;
  bool expired = false;
};

inline OverrideOutput update_override(uint32_t now_ms, int selected_mode, uint32_t maximum_ms, OverrideState state) {
  const int mode = selected_mode >= 0 && selected_mode <= 3 ? selected_mode : 0;
  if (mode != state.last_mode) {
    state.last_mode = mode;
    state.timing = mode != 0;
    state.since_ms = now_ms;
  }
  if (mode != 0 && state.timing && elapsed(now_ms, state.since_ms, maximum_ms)) {
    return {{0, false, 0}, 0, true};
  }
  if (mode == 0) state.timing = false;
  return {state, mode, false};
}

enum class SilentOverride : uint8_t { SCHEDULE, ON, OFF };

struct SilentWindowOutput {
  bool active = false;
  const char* status = "time_invalid";
};

inline SilentWindowOutput silent_window(bool time_valid, int current_minute, int start_minute, int end_minute,
                                        SilentOverride override_mode) {
  SilentWindowOutput output;
  if (time_valid) {
    const int current = std::clamp(current_minute, 0, 1439);
    const int start = std::clamp(start_minute, 0, 1439);
    const int end = std::clamp(end_minute, 0, 1439);
    if (start == end) {
      output.status = "window_disabled";
    } else {
      output.active = start < end ? current >= start && current < end : current >= start || current < end;
      output.status = output.active ? "in_window" : "out_of_window";
    }
  }
  if (override_mode == SilentOverride::ON) {
    output.active = true;
    output.status = "forced_on";
  } else if (override_mode == SilentOverride::OFF) {
    output.active = false;
    output.status = "forced_off";
  }
  return output;
}

struct StickyPumpState {
  bool cm0_timing = false;
  uint32_t cm0_since_ms = 0;
  uint32_t active_until_ms = 0;
};

struct StickyPumpOutput {
  StickyPumpState state;
  bool active = false;
  bool started = false;
};

inline StickyPumpOutput update_sticky_pump(uint32_t now_ms, bool in_cm0, uint32_t wait_ms, uint32_t run_ms,
                                           StickyPumpState state) {
  if (!in_cm0) return {{}, false, false};
  if (!state.cm0_timing) {
    state.cm0_timing = true;
    state.cm0_since_ms = now_ms;
  }
  bool active = window_active(now_ms, state.active_until_ms);
  if (!active && state.active_until_ms != 0) {
    state.active_until_ms = 0;
    state.cm0_since_ms = now_ms;
  }
  bool started = false;
  if (!active && elapsed(now_ms, state.cm0_since_ms, wait_ms)) {
    state.active_until_ms = now_ms + run_ms;
    active = true;
    started = true;
  }
  return {state, active, started};
}

}  // namespace oq_supervisory_state
