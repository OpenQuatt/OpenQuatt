#pragma once

#include <cmath>
#include <cstdint>

namespace oq_heat_intent {

enum Reason : uint8_t {
  NONE = 0,
  ROOM_DEMAND = 1,
  SETPOINT_RAISE = 2,
  ROOM_RECOVERY = 3,
};

struct State {
  bool initialized = false;
  uint8_t setpoint_source = 0;
  float last_setpoint_c = NAN;
  bool setpoint_raise_active = false;
  uint32_t room_confirm_since_ms = 0;
  bool room_start_armed = false;
  bool room_recovery_active = false;
  bool controlled_warmup = false;
};

static_assert(sizeof(State) <= 20U, "Warmup recovery must fit the existing intent state budget");

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
  float requested_setpoint_c = NAN;
  bool allow_setpoint_raise = true;
  bool controlled_warmup = false;
};

struct Decision {
  State next;
  bool active = false;
  bool setpoint_raise_edge = false;
  bool setpoint_raise_cancelled = false;
  bool room_condition = false;
  bool fast_start = false;
  bool room_recovery_active = false;
  Reason reason = NONE;
};

inline uint32_t timestamp_ms(uint32_t now_ms) { return now_ms == 0 ? UINT32_MAX : now_ms; }

inline Decision evaluate(const Input& input, State state) {
  Decision out;
  const bool valid =
      input.strategy_active && input.heating_enable_valid && input.heating_enabled && input.room_fresh &&
      input.setpoint_fresh && input.setpoint_source != 0 && std::isfinite(input.room_c) &&
      std::isfinite(input.setpoint_c) && std::isfinite(input.room_resume_delta_c) &&
      input.room_resume_delta_c >= 0.0f && std::isfinite(input.setpoint_raise_delta_c) &&
      input.setpoint_raise_delta_c > 0.0f &&
      (!input.controlled_warmup || (std::isfinite(input.requested_setpoint_c) && input.requested_setpoint_c > 0.0f &&
                                    input.requested_setpoint_c >= input.setpoint_c));
  if (!valid) {
    out.setpoint_raise_cancelled = state.setpoint_raise_active;
    return out;
  }

  const float observed_setpoint_c =
      std::isfinite(input.requested_setpoint_c) ? input.requested_setpoint_c : input.setpoint_c;
  const bool allow_setpoint_raise = input.allow_setpoint_raise && !input.controlled_warmup;
  // The warmup floor belongs to this session only. Do not carry an armed start
  // or recovery across its handoff to/from ordinary thermostat regulation.
  if (state.controlled_warmup != input.controlled_warmup) {
    state.setpoint_raise_active = false;
    state.room_confirm_since_ms = 0;
    state.room_start_armed = false;
    state.room_recovery_active = false;
  }
  if (!state.initialized || state.setpoint_source != input.setpoint_source || !std::isfinite(state.last_setpoint_c)) {
    state = {true, input.setpoint_source, observed_setpoint_c, false, 0, false, false};
  } else {
    const float change_c = observed_setpoint_c - state.last_setpoint_c;
    if (change_c < -0.01f) {
      out.setpoint_raise_cancelled = state.setpoint_raise_active;
      state.setpoint_raise_active = false;
      state.room_start_armed = false;
      state.room_recovery_active = false;
    } else if (allow_setpoint_raise && !input.compressor_active && change_c + 0.0001f >= input.setpoint_raise_delta_c &&
               input.setpoint_c > input.room_c) {
      state.setpoint_raise_active = true;
      out.setpoint_raise_edge = true;
    }
    state.last_setpoint_c = observed_setpoint_c;
  }
  state.controlled_warmup = input.controlled_warmup;

  if (!allow_setpoint_raise || input.compressor_active || input.setpoint_c <= input.room_c)
    state.setpoint_raise_active = false;

  // A water/dispatch/protection interruption ends the retained run. A later
  // restart must confirm room demand again, not inherit an old warmup floor.
  if (input.controlled_warmup && state.room_recovery_active && !input.compressor_active) {
    state.room_recovery_active = false;
    state.room_start_armed = false;
    state.room_confirm_since_ms = 0;
  }

  // An intermediate target is a step-completion point, not a new comfort band.
  // In particular a 0.1 C step must still request heat with a 0.2 C comfort band.
  out.room_condition = input.controlled_warmup ? input.room_c < input.setpoint_c &&
                                                     input.room_c < observed_setpoint_c - input.room_resume_delta_c
                                               : input.room_c <= input.setpoint_c - input.room_resume_delta_c;
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

  // Ordinary recovery belongs to a confirmed room-demand start. Warmup may
  // also retain an already running HP: it needs no new compressor start and
  // must survive the pending-raise/step handoff before the 1s owner tick.
  const bool warmup_recovery =
      input.controlled_warmup && input.room_c < observed_setpoint_c - input.room_resume_delta_c;
  if (!state.room_recovery_active) {
    if (input.compressor_active && (state.room_start_armed || warmup_recovery)) {
      state.room_start_armed = false;
      state.room_recovery_active = true;
    } else if (room_confirmed && !input.compressor_active) {
      state.room_start_armed = true;
    } else if (!room_confirmed) {
      state.room_start_armed = false;
    }
  }
  // Keep minimum viable heating through intermediate steps, also when the
  // consumer runs before the 1s warmup owner has advanced the next target.
  // Release at real comfort; ordinary recovery keeps its existing hysteresis.
  const float recovery_release_c = input.controlled_warmup ? observed_setpoint_c - input.room_resume_delta_c
                                                           : input.setpoint_c - 0.5f * input.room_resume_delta_c;
  if (state.room_recovery_active && input.room_c >= recovery_release_c) state.room_recovery_active = false;

  out.fast_start =
      !input.compressor_active && !state.room_recovery_active && (state.setpoint_raise_active || room_confirmed);
  out.room_recovery_active = state.room_recovery_active;
  out.active = state.setpoint_raise_active || room_confirmed || state.room_recovery_active;
  out.reason = state.setpoint_raise_active  ? SETPOINT_RAISE
               : state.room_recovery_active ? ROOM_RECOVERY
               : room_confirmed             ? ROOM_DEMAND
                                            : NONE;
  out.next = state;
  return out;
}

inline const char* reason_name(Reason reason) {
  if (reason == SETPOINT_RAISE) return "setpoint_raise";
  if (reason == ROOM_RECOVERY) return "room_recovery";
  if (reason == ROOM_DEMAND) return "room_demand";
  return "none";
}

}  // namespace oq_heat_intent
