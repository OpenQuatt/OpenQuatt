#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_boiler_defrost_assist {

// Optional, opt-in auxiliary heat while an outdoor unit defrosts.
//
// A defrosting outdoor unit takes heat out of the water instead of adding it,
// so the supply temperature drops for the duration of the cycle. With this
// policy enabled the supervisory may hand the boiler a bounded task for that
// cycle only: hold the supply at the temperature the system had just before
// the defrost began. It never asks for more than that reference, so the boiler
// only compensates the dip and does not take over normal heating.
//
// This is supervisory policy. The boiler chain itself still has no defrost
// observation: it only sees CM3 ownership and a target temperature, exactly as
// it does for a cold water start.
struct Config {
  // Hard bound on one assist session. A defrost that is still reported after
  // this time (for example a stale flag) releases the boiler.
  uint32_t maximum_ms = 900000UL;
};

struct State {
  bool active = false;
  // One session per defrost report: once a session has ended for any reason,
  // no new one starts until the report has cleared. A stuck report can
  // therefore never add up to more than one bounded session.
  bool session_closed = false;
  uint32_t started_ms = 0;
  // Last supply temperature seen while heating without a defrost. Frozen
  // during a defrost so the dip itself cannot lower the reference.
  float reference_supply_c = NAN;
  // Reference latched at the start of the active session.
  float target_c = NAN;
};

struct Input {
  uint32_t now_ms = 0;
  // User opt-in combined with the general boiler assist permission.
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

inline Output update(const Input& input, State state, const Config& config = {}) {
  Output output;
  const bool was_active = state.active;

  // Only a supply measured during heat-pump heating, without a defrost, is a
  // valid reference. It is dropped whenever heating or the measurement is
  // lost, also while a defrost report is still set, so an old value can never
  // become a boiler target.
  const bool permitted = input.enabled && input.heating_owned && input.supply_reference_valid;
  if (!input.heating_owned || !input.supply_reference_valid) {
    state.reference_supply_c = NAN;
  } else if (!input.defrost_active && isfinite(input.supply_c)) {
    state.reference_supply_c = input.supply_c;
  }

  if (!input.defrost_active) {
    state.active = false;
    state.session_closed = false;
  } else if (state.active) {
    if (!permitted) {
      state.active = false;
      state.session_closed = true;
    } else if (static_cast<uint32_t>(input.now_ms - state.started_ms) >= config.maximum_ms) {
      state.active = false;
      state.session_closed = true;
      output.expired_now = true;
    }
  } else if (permitted && !state.session_closed && reference_usable(state.reference_supply_c)) {
    state.active = true;
    state.started_ms = input.now_ms;
    state.target_c = state.reference_supply_c;
    output.started = true;
  }

  if (!state.active) state.target_c = NAN;
  output.released = was_active && !state.active;
  output.requested = state.active;
  output.target_c = state.target_c;
  output.state = state;
  return output;
}

}  // namespace oq_boiler_defrost_assist
