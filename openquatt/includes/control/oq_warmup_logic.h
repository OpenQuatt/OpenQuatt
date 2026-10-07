#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace oq_warmup {

enum class Status : uint8_t {
  // Stable diagnostic codes used by the web status API.
  IDLE = 0,
  DISABLED = 1,
  WARMING = 2,
  COMFORT_REACHED = 3,
  SETPOINT_LOWERED = 4,
  TIME_LIMIT = 5,
  INPUT_UNAVAILABLE = 6,
  SOURCE_CHANGED = 7,
  MODE_CHANGED = 8,
  SETTINGS_CHANGED = 9,
};

inline constexpr float MAX_OFFSET_C = 0.5f;
inline constexpr uint32_t MAX_DURATION_MS = 8UL * 3600000UL;

struct Settings {
  float trigger_c = 1.5f;
  float step_c = 0.1f;
  uint32_t step_ms = 45UL * 60000UL;
};

struct Input {
  uint32_t now_ms = 0;
  bool enabled = false;
  bool automatic_heating = false;
  bool fresh = false;
  uint32_t room_source = 0;
  uint32_t setpoint_source = 0;
  float room_c = NAN;
  float requested_c = NAN;
  float comfort_below_c = NAN;
};

struct State {
  bool initialized = false;
  bool active = false;
  uint32_t room_source = 0;
  uint32_t setpoint_source = 0;
  float last_requested_c = NAN;
  float target_c = NAN;
  float offset_c = 0.0f;
  uint32_t started_ms = 0;
  uint32_t step_started_ms = 0;
  Status status = Status::IDLE;
};

static_assert(sizeof(State) <= 48U, "Warmup session must remain small and allocation-free");
static_assert(sizeof(float) == 4U, "Persisted warmup layout requires 32-bit floats");

inline bool valid_settings(const Settings& s) {
  return std::isfinite(s.trigger_c) && s.trigger_c >= 0.5f && s.trigger_c <= 5.0f && std::isfinite(s.step_c) &&
         s.step_c >= 0.1f && s.step_c <= 0.5f && s.step_ms >= 5UL * 60000UL && s.step_ms <= 120UL * 60000UL;
}

inline uint32_t duration_ms(float value, float scale, float low, float high) {
  return std::isfinite(value) && value >= low && value <= high ? static_cast<uint32_t>(value * scale) : 0;
}

// Persisted layout: enabled, trigger, step, step minutes, maximum offset,
// maximum hours. Invalid storage resets the entire block with permission off.
inline void normalize_stored_settings(float (&saved)[6]) {
  const Settings settings{saved[1], saved[2], duration_ms(saved[3], 60000.0f, 5.0f, 120.0f)};
  const bool legacy_limits_valid = std::isfinite(saved[4]) && saved[4] >= 0.1f && saved[4] <= 2.0f &&
                                   duration_ms(saved[5], 3600000.0f, 1.0f, 24.0f) != 0;
  if ((saved[0] == 0.0f || saved[0] == 1.0f) && valid_settings(settings) && legacy_limits_valid) {
    // Preserve valid settings from the draft layout, but fix v1 safety limits.
    saved[4] = MAX_OFFSET_C;
    saved[5] = 8.0f;
    return;
  }
  const float defaults[6] = {0.0f, 1.5f, 0.1f, 45.0f, 0.5f, 8.0f};
  for (int i = 0; i < 6; ++i) saved[i] = defaults[i];
}

inline State cancel(Status status) {
  State state;
  state.status = status;
  return state;
}

// All timing uses unsigned elapsed durations (including a millis() rollover).
// A cancelled session never establishes a baseline from unavailable input.
inline State evaluate(const Input& in, const Settings& settings, State state) {
  if (!in.enabled) return cancel(Status::DISABLED);
  if (!in.automatic_heating) return cancel(Status::MODE_CHANGED);
  if (!in.fresh || in.room_source == 0 || in.setpoint_source == 0 || !std::isfinite(in.room_c) ||
      !std::isfinite(in.requested_c) || in.requested_c <= 0.0f || !std::isfinite(in.comfort_below_c) ||
      in.comfort_below_c < 0.0f || !valid_settings(settings))
    return cancel(Status::INPUT_UNAVAILABLE);

  const bool source_changed =
      state.initialized && (state.room_source != in.room_source || state.setpoint_source != in.setpoint_source);
  if (!state.initialized || source_changed) {
    const Status status = source_changed ? Status::SOURCE_CHANGED : state.status;
    state = cancel(status);
    state.initialized = true;
    state.room_source = in.room_source;
    state.setpoint_source = in.setpoint_source;
    state.last_requested_c = in.requested_c;
    return state;
  }

  const float change_c = in.requested_c - state.last_requested_c;
  state.last_requested_c = in.requested_c;
  const bool comfortable = in.room_c >= in.requested_c - in.comfort_below_c;
  if (state.active && change_c < -0.0001f) {
    state.active = false;
    state.status = Status::SETPOINT_LOWERED;
  } else if (state.active && comfortable) {
    state.active = false;
    state.status = Status::COMFORT_REACHED;
  } else if (state.active && static_cast<uint32_t>(in.now_ms - state.started_ms) >= MAX_DURATION_MS) {
    state.active = false;
    state.status = Status::TIME_LIMIT;
  } else if (!state.active && change_c > settings.trigger_c + 0.0001f && !comfortable) {
    state.active = true;
    state.status = Status::WARMING;
    state.offset_c = settings.step_c;
    state.started_ms = in.now_ms;
    state.step_started_ms = in.now_ms;
    state.target_c = std::min(in.requested_c, in.room_c + state.offset_c);
  } else if (state.active) {
    if (in.room_c >= state.target_c) {
      state.target_c = std::min(in.requested_c, in.room_c + state.offset_c);
      state.step_started_ms = in.now_ms;
    } else if (static_cast<uint32_t>(in.now_ms - state.step_started_ms) >= settings.step_ms) {
      state.offset_c = std::min(MAX_OFFSET_C, state.offset_c + settings.step_c);
      // Cooling during a step must not move the fixed target backwards.
      state.target_c = std::min(in.requested_c, std::max(state.target_c, in.room_c + state.offset_c));
      state.step_started_ms = in.now_ms;
    }
  }
  if (!state.active) {
    state.offset_c = 0.0f;
    state.target_c = NAN;
  }
  return state;
}

inline float effective_target(const State& state, float requested_c) {
  return state.active ? std::min(state.target_c, requested_c) : requested_c;
}

}  // namespace oq_warmup
