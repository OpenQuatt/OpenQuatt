#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_boiler_defrost_assist {

// Optional, opt-in auxiliary heat while an outdoor unit defrosts.
//
// A defrosting outdoor unit takes heat out of the water instead of adding it,
// so the supply temperature drops for the duration of the cycle. With this
// policy enabled the supervisory may hand the boiler a bounded task for that
// cycle only: hold the supply around the temperature the system had just
// before the defrost began. The target sits a small margin above that
// reference so a binary boiler does not release the moment the dip is closed,
// and the dispatcher keeps it below the water-limit soft band. The boiler
// therefore compensates the dip and does not take over normal heating.
//
// This is supervisory policy. The boiler chain itself still has no defrost
// observation: it only sees CM3 ownership and a target temperature, exactly as
// it does for a cold water start.
//
// The session bypasses the normal CM2/CM3 promotion and demotion timers, so it
// carries its own anti-cycling: a report must persist before a session starts,
// a started session runs a minimum time and ends only after the report has been
// clear for a release time, and a new session needs the report to have been
// clear for a rearm time after the previous session ended. A flickering report
// can therefore not cycle the control mode or the boiler faster than once per
// minimum run (or release time, if longer) plus rearm and confirm time. In the
// firmware the reference settle time after boiler heat stretches the gap
// further. The release time also carries a session across a short gap between
// two outdoor units that defrost one after the other.
//
// A target alone does not make a heat source fire: every boiler decides for
// itself below which water temperature it starts, and that offset is not known
// from outside. Where the transport reports whether the source fires
// (OpenTherm), the session therefore confirms the flame. It starts at the low
// target and raises it in steps for as long as the source does not fire, never
// above the ceiling the dispatcher applies. Without that feedback (R1) the
// target stays at the reference plus margin.
struct Config {
  // Hard bound on one assist session. A defrost that is still reported after
  // this time (for example a stale flag) releases the boiler.
  uint32_t maximum_ms = 900000UL;
  // Margin of the boiler target above the pre-defrost supply reference. Policy,
  // not physics: with the R1 target band it keeps the supply at or above the
  // reference for the whole cycle instead of cycling around it.
  float target_margin_c = 3.0f;
  // The report must persist this long before a session starts.
  uint32_t confirm_ms = 10000UL;
  // A started session is held this long even if the report clears earlier.
  // Losing permission still releases at once.
  uint32_t minimum_ms = 60000UL;
  // After a session, the report must have been clear this long before a new
  // session may start.
  uint32_t rearm_ms = 120000UL;
  // A session ends only once the report has been clear this long. The heat
  // pump still delivers cold water in the first minutes after a defrost.
  uint32_t release_ms = 120000UL;
  // After the boiler last heated, the supply must settle this long before it
  // can be a reference again.
  uint32_t reference_settle_ms = 300000UL;
  // Time an applied command gets before the first raise. A gas boiler needs
  // this to purge and ignite.
  uint32_t ignition_wait_ms = 45000UL;
  // One raise of the target while the source does not fire.
  float raise_step_c = 2.0f;
  // Time between two raises while searching.
  uint32_t raise_interval_ms = 30000UL;
  // After a confirmed flame, the flame must be gone this long before the one
  // extra raise.
  uint32_t flame_loss_confirm_ms = 20000UL;
  // Bound on the whole raise above the base target. The search cannot tell
  // a target that is too low from a source that is not ready (anti-cycle
  // time, lockout). Without this bound such a source would pull the target
  // up to the ceiling and then heat to it.
  float max_raise_c = 8.0f;
};

struct State {
  bool active = false;
  // One session per defrost report: once a session has ended for any reason,
  // no new one starts until the report has been clear for the rearm time. A
  // stuck or flickering report can therefore never add up to more than one
  // bounded session per rearm period.
  bool session_closed = false;
  uint32_t started_ms = 0;
  bool report_timing = false;
  uint32_t report_since_ms = 0;
  bool clear_timing = false;
  uint32_t clear_since_ms = 0;
  // The boiler heated within the settle time, so the supply may still hold
  // boiler heat.
  bool boiler_heat_recent = false;
  uint32_t boiler_heat_last_ms = 0;
  // Last supply temperature seen while heating without a defrost. Frozen
  // during a defrost so the dip itself cannot lower the reference.
  float reference_supply_c = NAN;
  // Reference plus margin, latched at the start of the active session.
  float target_c = NAN;
  // Raise above that base target, for this session only.
  float raise_c = 0.0f;
  bool flame_confirmed = false;
  // After a confirmed flame the target is raised once more at most, so the
  // source's own anti-cycle time cannot drive the target to the ceiling.
  bool extra_step_used = false;
  bool wait_timing = false;
  uint32_t wait_since_ms = 0;
  bool ceiling_reported = false;
};

struct Input {
  uint32_t now_ms = 0;
  // User opt-in combined with the presence of the auxiliary heat source.
  bool enabled = false;
  // Heat-pump heating owns the session: heat request in CM2 or CM3.
  bool heating_owned = false;
  // Any outdoor unit reports a defrost.
  bool defrost_active = false;
  // The supply value is a real system supply measurement downstream of the
  // boiler. A fallback to a heat-pump outlet collapses during a defrost and
  // cannot see boiler heat, so it can neither be a reference nor regulate one.
  bool supply_reference_valid = false;
  float supply_c = NAN;
  // The boiler may be heating: a control mode that owns it (normal assist,
  // cold start, this assist, fallback) or an active boiler output.
  bool boiler_heat_active = false;
  // The transport reports whether the source fires, and that report is fresh.
  bool feedback_available = false;
  // The boiler chain has actually switched heating on for this session. While
  // it has not (start guard, minimum off-time, inhibit), a missing flame says
  // nothing about the target.
  bool command_applied = false;
  // The source fires. A flame for domestic hot water does not count.
  bool firing = false;
  bool dhw_active = false;
  float boiler_water_c = NAN;
  // Highest target the dispatcher lets through.
  float target_ceiling_c = NAN;
};

struct Output {
  State state;
  bool requested = false;
  float target_c = NAN;
  bool started = false;
  bool released = false;
  bool expired_now = false;
  bool raised = false;
  bool flame_confirmed_now = false;
  // The ceiling was reached and the source still does not fire. Reported once.
  bool ceiling_reached_now = false;
};

inline bool reference_usable(float reference_c) { return isfinite(reference_c) && reference_c > 0.0f; }

// An unusable margin never raises the target above the reference.
inline float usable_margin(float margin_c) { return isfinite(margin_c) && margin_c > 0.0f ? margin_c : 0.0f; }

inline bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t duration_ms) {
  return static_cast<uint32_t>(now_ms - since_ms) >= duration_ms;
}

// An unusable step never raises the target.
inline float usable_step(float step_c) { return isfinite(step_c) && step_c > 0.0f ? step_c : 0.0f; }

// Base target plus raise, kept at or below a known ceiling.
inline float effective_target(const State& state, float ceiling_c) {
  const float target_c = state.target_c + state.raise_c;
  return isfinite(ceiling_c) && target_c > ceiling_c ? ceiling_c : target_c;
}

// Raises the session target while the source demonstrably does not fire.
inline void confirm_flame(const Input& input, State& state, Output& output, const Config& config) {
  const float step_c = usable_step(config.raise_step_c);
  // Only a flame that answers this session's own applied command confirms
  // it. A flame that another command still holds (normal assist before a
  // hand-over) says nothing about this target.
  if (input.feedback_available && input.command_applied && input.firing && !input.dhw_active) {
    if (!state.flame_confirmed) {
      state.flame_confirmed = true;
      output.flame_confirmed_now = true;
    }
    state.wait_timing = false;
    return;
  }
  // Only an applied command that the source ignores is a reason to raise.
  // Without feedback, during hot water, or without a ceiling to stay under,
  // the target reached so far simply stands.
  if (!input.feedback_available || !input.command_applied || input.dhw_active || !isfinite(input.target_ceiling_c) ||
      step_c <= 0.0f) {
    state.wait_timing = false;
    return;
  }
  // The raise ends at the ceiling or at its own bound, whichever is lower.
  const float bound_c = state.target_c + usable_step(config.max_raise_c);
  const float limit_c = bound_c < input.target_ceiling_c ? bound_c : input.target_ceiling_c;
  const bool at_ceiling = state.target_c + state.raise_c >= limit_c;
  const auto raise = [&]() {
    const float room_c = limit_c - state.target_c - state.raise_c;
    state.raise_c += step_c < room_c ? step_c : room_c;
    output.raised = true;
  };
  if (!state.flame_confirmed) {
    if (!state.wait_timing) {
      state.wait_timing = true;
      state.wait_since_ms = input.now_ms;
    }
    const uint32_t wait_ms = state.raise_c > 0.0f ? config.raise_interval_ms : config.ignition_wait_ms;
    if (!elapsed(input.now_ms, state.wait_since_ms, wait_ms)) return;
    state.wait_since_ms = input.now_ms;
    if (!at_ceiling) {
      raise();
    } else if (!state.ceiling_reported) {
      state.ceiling_reported = true;
      output.ceiling_reached_now = true;
    }
    return;
  }
  // The source fired earlier in this session and stopped. A source that
  // reached its target is satisfied; one that sits below it gets one more
  // step. An unknown temperature is never a reason to raise.
  const float observed_c = isfinite(input.boiler_water_c) ? input.boiler_water_c : input.supply_c;
  const bool below_target = isfinite(observed_c) && observed_c < effective_target(state, input.target_ceiling_c);
  if (state.extra_step_used || !below_target) {
    state.wait_timing = false;
    return;
  }
  if (!state.wait_timing) {
    state.wait_timing = true;
    state.wait_since_ms = input.now_ms;
  }
  if (!elapsed(input.now_ms, state.wait_since_ms, config.flame_loss_confirm_ms)) return;
  state.extra_step_used = true;
  state.wait_timing = false;
  if (!at_ceiling) raise();
}

inline Output update(const Input& input, State state, const Config& config = {}) {
  Output output;
  const bool was_active = state.active;
  const bool permitted = input.enabled && input.heating_owned && input.supply_reference_valid;

  // Time the report and its absence, so a single sample decides nothing.
  if (input.defrost_active) {
    if (!state.report_timing) {
      state.report_timing = true;
      state.report_since_ms = input.now_ms;
    }
    state.clear_timing = false;
  } else {
    state.report_timing = false;
    if (!state.clear_timing) {
      state.clear_timing = true;
      state.clear_since_ms = input.now_ms;
    }
  }

  if (state.active) {
    if (!permitted) {
      state.active = false;
    } else if (elapsed(input.now_ms, state.started_ms, config.maximum_ms)) {
      state.active = false;
      output.expired_now = true;
    } else if (state.clear_timing && elapsed(input.now_ms, state.clear_since_ms, config.release_ms) &&
               elapsed(input.now_ms, state.started_ms, config.minimum_ms)) {
      state.active = false;
    }
    if (!state.active) {
      state.session_closed = true;
      // The rearm time counts from the end of the session, not from an
      // earlier moment at which the report happened to clear.
      state.clear_since_ms = input.now_ms;
    }
  } else {
    if (state.session_closed && state.clear_timing && elapsed(input.now_ms, state.clear_since_ms, config.rearm_ms)) {
      state.session_closed = false;
    }
    if (input.defrost_active && permitted && !state.session_closed && reference_usable(state.reference_supply_c) &&
        elapsed(input.now_ms, state.report_since_ms, config.confirm_ms)) {
      state.active = true;
      state.started_ms = input.now_ms;
      state.target_c = state.reference_supply_c + usable_margin(config.target_margin_c);
      output.started = true;
    }
  }

  // Only a supply measured during heat-pump heating, without a defrost and
  // without boiler heat still in the water, is a valid reference. It is
  // dropped whenever heating or the measurement is lost, also while a defrost
  // report is still set, so an old value can never become a boiler target.
  // It is also dropped while the boiler heats and for the settle time after,
  // so neither normal assist nor an earlier session lifts the next target.
  // Holding it until the rearm time has passed keeps the boiler heat of one
  // session out of the reference for the next.
  if (input.boiler_heat_active) {
    state.boiler_heat_recent = true;
    state.boiler_heat_last_ms = input.now_ms;
  } else if (state.boiler_heat_recent && elapsed(input.now_ms, state.boiler_heat_last_ms, config.reference_settle_ms)) {
    state.boiler_heat_recent = false;
  }
  if (!input.heating_owned || !input.supply_reference_valid || state.boiler_heat_recent) {
    state.reference_supply_c = NAN;
  } else if (!input.defrost_active && !state.active && !state.session_closed && isfinite(input.supply_c)) {
    state.reference_supply_c = input.supply_c;
  }

  if (state.active) {
    confirm_flame(input, state, output, config);
  } else {
    state.target_c = NAN;
    state.raise_c = 0.0f;
    state.flame_confirmed = false;
    state.extra_step_used = false;
    state.wait_timing = false;
    state.ceiling_reported = false;
  }
  output.released = was_active && !state.active;
  output.requested = state.active;
  output.target_c = state.active ? effective_target(state, input.target_ceiling_c) : NAN;
  output.state = state;
  return output;
}

}  // namespace oq_boiler_defrost_assist
