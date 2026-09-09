#pragma once

#include <cmath>
#include <cstdint>

namespace oq_heat_intent {

enum Reason : uint8_t {
  NONE = 0,
  ROOM_DEMAND = 1,
  SETPOINT_RAISE = 2,
};

struct State {
  bool initialized = false;
  uint8_t setpoint_source = 0;
  float last_setpoint_c = NAN;
  bool setpoint_raise_active = false;
  uint32_t room_confirm_since_ms = 0;
};

struct Input {
  uint32_t now_ms = 0;
  bool strategy_active = false;
  bool heating_enable_valid = false;
  bool heating_enabled = false;
  bool room_fresh = false;
  bool setpoint_fresh = false;
  bool compressor_active = false;
  uint8_t setpoint_source = 0;
  float room_c = NAN;
  float setpoint_c = NAN;
  float room_resume_delta_c = 0.0f;
  float setpoint_raise_delta_c = 0.20f;
  uint32_t room_confirm_ms = 0;
};

struct Decision {
  State next;
  bool active = false;
  bool setpoint_raise_edge = false;
  bool setpoint_raise_cancelled = false;
  bool room_condition = false;
  Reason reason = NONE;
};

inline uint32_t timestamp_ms(uint32_t now_ms) { return now_ms == 0 ? UINT32_MAX : now_ms; }

inline Decision evaluate(const Input& input, State state) {
  Decision out;
  const bool valid = input.strategy_active && input.heating_enable_valid && input.heating_enabled && input.room_fresh &&
                     input.setpoint_fresh && input.setpoint_source != 0 && std::isfinite(input.room_c) &&
                     std::isfinite(input.setpoint_c) && std::isfinite(input.room_resume_delta_c) &&
                     input.room_resume_delta_c >= 0.0f && std::isfinite(input.setpoint_raise_delta_c) &&
                     input.setpoint_raise_delta_c > 0.0f;
  if (!valid) {
    out.setpoint_raise_cancelled = state.setpoint_raise_active;
    return out;
  }

  if (!state.initialized || state.setpoint_source != input.setpoint_source || !std::isfinite(state.last_setpoint_c)) {
    state = {true, input.setpoint_source, input.setpoint_c, false, 0};
  } else {
    const float change_c = input.setpoint_c - state.last_setpoint_c;
    if (change_c < -0.01f) {
      out.setpoint_raise_cancelled = state.setpoint_raise_active;
      state.setpoint_raise_active = false;
    } else if (!input.compressor_active && change_c + 0.0001f >= input.setpoint_raise_delta_c &&
               input.setpoint_c > input.room_c) {
      state.setpoint_raise_active = true;
      out.setpoint_raise_edge = true;
    }
    state.last_setpoint_c = input.setpoint_c;
  }

  if (input.compressor_active || input.setpoint_c <= input.room_c) state.setpoint_raise_active = false;

  out.room_condition = input.room_c <= input.setpoint_c - input.room_resume_delta_c;
  if (out.room_condition) {
    if (input.room_confirm_ms == 0) {
      state.room_confirm_since_ms = 0;
    } else if (state.room_confirm_since_ms == 0) {
      state.room_confirm_since_ms = timestamp_ms(input.now_ms);
    }
  } else {
    state.room_confirm_since_ms = 0;
  }

  const bool room_confirmed =
      out.room_condition &&
      (input.room_confirm_ms == 0 ||
       (state.room_confirm_since_ms != 0 &&
        static_cast<uint32_t>(input.now_ms - state.room_confirm_since_ms) >= input.room_confirm_ms));
  out.active = state.setpoint_raise_active || room_confirmed;
  out.reason = state.setpoint_raise_active ? SETPOINT_RAISE : (room_confirmed ? ROOM_DEMAND : NONE);
  out.next = state;
  return out;
}

inline const char* reason_name(Reason reason) {
  if (reason == SETPOINT_RAISE) return "setpoint_raise";
  if (reason == ROOM_DEMAND) return "room_demand";
  return "none";
}

}  // namespace oq_heat_intent
