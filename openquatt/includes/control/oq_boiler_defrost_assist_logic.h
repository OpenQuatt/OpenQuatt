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
  // this time (for example a stale flag) releases the boiler and stays released
  // until the defrost report has cleared again.
  uint32_t maximum_ms = 900000UL;
};

struct State {
  bool active = false;
  bool expired = false;
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

  if (!input.defrost_active) {
    state.active = false;
    state.expired = false;
    // Only a supply measured during heat-pump heating is a valid reference for
    // the next defrost. Outside heating the reference is dropped so an old
    // value can never become a boiler target.
    if (!input.heating_owned) {
      state.reference_supply_c = NAN;
    } else if (isfinite(input.supply_c)) {
      state.reference_supply_c = input.supply_c;
    }
  } else if (!input.enabled || !input.heating_owned) {
    state.active = false;
  } else if (state.active) {
    if (static_cast<uint32_t>(input.now_ms - state.started_ms) >= config.maximum_ms) {
      state.active = false;
      state.expired = true;
      output.expired_now = true;
    }
  } else if (!state.expired && reference_usable(state.reference_supply_c)) {
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
