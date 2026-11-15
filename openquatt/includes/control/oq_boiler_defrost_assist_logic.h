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
// a started session runs a minimum time, and a new session needs the report to
// have been clear for a rearm time after the previous session ended. A
// flickering report can therefore not cycle the control mode or the boiler
// faster than once per minimum plus rearm time.
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
  // Last supply temperature seen while heating without a defrost. Frozen
  // during a defrost so the dip itself cannot lower the reference.
  float reference_supply_c = NAN;
  // Reference plus margin, latched at the start of the active session.
  float target_c = NAN;
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
};

struct Output {
  State state;
  bool requested = false;
  float target_c = NAN;
  bool started = false;
  bool released = false;
  bool expired_now = false;
};

inline bool reference_usable(float reference_c) { return isfinite(reference_c) && reference_c > 0.0f; }

// An unusable margin never raises the target above the reference.
inline float usable_margin(float margin_c) { return isfinite(margin_c) && margin_c > 0.0f ? margin_c : 0.0f; }

inline bool elapsed(uint32_t now_ms, uint32_t since_ms, uint32_t duration_ms) {
  return static_cast<uint32_t>(now_ms - since_ms) >= duration_ms;
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
    } else if (!input.defrost_active && elapsed(input.now_ms, state.started_ms, config.minimum_ms)) {
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
  // without assist heat still in the water, is a valid reference. It is
  // dropped whenever heating or the measurement is lost, also while a defrost
  // report is still set, so an old value can never become a boiler target.
  // Holding it until the rearm time has passed keeps the boiler heat of one
  // session out of the reference for the next.
  if (!input.heating_owned || !input.supply_reference_valid) {
    state.reference_supply_c = NAN;
  } else if (!input.defrost_active && !state.active && !state.session_closed && isfinite(input.supply_c)) {
    state.reference_supply_c = input.supply_c;
  }

  if (!state.active) state.target_c = NAN;
  output.released = was_active && !state.active;
  output.requested = state.active;
  output.target_c = state.target_c;
  output.state = state;
  return output;
}

}  // namespace oq_boiler_defrost_assist
