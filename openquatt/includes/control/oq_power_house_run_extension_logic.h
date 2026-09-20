#pragma once

#include <cmath>
#include <cstdint>

namespace oq_power_house_run_extension {

enum class Phase : uint8_t {
  INACTIVE = 0,
  RUNNING = 1,
  EXTENDING = 2,
  COMFORT_STOP = 3,
  WAIT_WARM_RESTART = 4,
  WARM_RESTART = 5,
};

constexpr float kRestartHysteresisC = 0.2f;
constexpr float kWaterLimitFloorGate = 0.999f;

struct Input {
  bool enabled = false;
  bool cycle_active = false;
  bool actual_heating_active = false;
  bool inputs_valid = false;
  bool heating_allowed = false;
  float room_c = NAN;
  float setpoint_c = NAN;
  float base_requested_w = 0.0f;
  float minimum_viable_w = NAN;
  float water_limit_factor = NAN;
};

struct Tuning {
  float stop_margin_c = 0.5f;
  float restart_hysteresis_c = kRestartHysteresisC;
};

struct State {
  Phase phase = Phase::INACTIVE;
  float last_setpoint_c = NAN;
  bool cycle_armed = false;
};

struct Decision {
  State next;
  bool force_comfort_stop = false;
  bool warm_restart_intent = false;
  bool floor_active = false;
  float floor_w = 0.0f;
  float comfort_stop_c = NAN;
  float warm_restart_c = NAN;
};

inline const char* phase_name(Phase phase) {
  switch (phase) {
    case Phase::INACTIVE:
      return "inactive";
    case Phase::RUNNING:
      return "normal";
    case Phase::EXTENDING:
      return "extending";
    case Phase::COMFORT_STOP:
      return "comfort_stop";
    case Phase::WAIT_WARM_RESTART:
      return "wait_warm_restart";
    case Phase::WARM_RESTART:
      return "warm_restart";
  }
  return "inactive";
}

inline bool pmin_valid(float minimum_viable_w) { return std::isfinite(minimum_viable_w) && minimum_viable_w > 0.0f; }

inline bool water_floor_allowed(float water_limit_factor) {
  return std::isfinite(water_limit_factor) && water_limit_factor >= kWaterLimitFloorGate;
}

inline State reset_state() { return {}; }

inline Decision evaluate(const Input& in, const Tuning& tuning, State state) {
  Decision out;
  const float stop_margin_c = std::isfinite(tuning.stop_margin_c) ? tuning.stop_margin_c : 0.5f;
  const float hysteresis_c =
      std::isfinite(tuning.restart_hysteresis_c) ? tuning.restart_hysteresis_c : kRestartHysteresisC;
  const float comfort_stop_c = std::isfinite(in.setpoint_c) ? in.setpoint_c + stop_margin_c : NAN;
  const float warm_restart_c = std::isfinite(comfort_stop_c) ? comfort_stop_c - hysteresis_c : NAN;
  out.comfort_stop_c = comfort_stop_c;
  out.warm_restart_c = warm_restart_c;

  const bool room_finite = std::isfinite(in.room_c) && std::isfinite(in.setpoint_c);
  const bool thresholds_finite = std::isfinite(comfort_stop_c) && std::isfinite(warm_restart_c);
  const bool usable = in.enabled && in.inputs_valid && in.heating_allowed && room_finite && thresholds_finite;

  // Setpoint tracking: a relevant drop cancels a pending warm restart intent.
  const bool setpoint_drop = std::isfinite(in.setpoint_c) && std::isfinite(state.last_setpoint_c) &&
                             (in.setpoint_c < state.last_setpoint_c - 0.01f);
  State next = state;
  if (std::isfinite(in.setpoint_c)) next.last_setpoint_c = in.setpoint_c;

  if (!in.enabled) {
    next.phase = Phase::INACTIVE;
    next.cycle_armed = false;
    out.next = next;
    return out;
  }

  // Arming: only a confirmed normal heating run arms a #608 cycle.
  // Enabling while idle must not start anything.
  if (in.cycle_active && in.actual_heating_active && in.inputs_valid && in.heating_allowed) {
    next.cycle_armed = true;
  }

  // Fail closed on stale/invalid inputs or when heating is no longer permitted.
  // This also clears a stale warm-restart intent.
  if (!in.inputs_valid || !in.heating_allowed) {
    next.phase = Phase::INACTIVE;
    next.cycle_armed = false;
    out.next = next;
    return out;
  }

  const bool floor_candidate =
      usable && next.cycle_armed && pmin_valid(in.minimum_viable_w) && water_floor_allowed(in.water_limit_factor);
  const bool room_below_stop = room_finite && thresholds_finite && (in.room_c < comfort_stop_c);
  const bool room_at_stop = room_finite && thresholds_finite && (in.room_c >= comfort_stop_c);
  const bool room_ready_restart = room_finite && thresholds_finite && (in.room_c <= warm_restart_c);

  switch (state.phase) {
    case Phase::INACTIVE: {
      if (!next.cycle_armed || !in.cycle_active) {
        next.phase = Phase::INACTIVE;
        if (!in.cycle_active) next.cycle_armed = false;
        break;
      }
      if (room_at_stop) {
        next.phase = Phase::COMFORT_STOP;
        out.force_comfort_stop = true;
      } else if (floor_candidate && in.base_requested_w < in.minimum_viable_w && room_below_stop) {
        next.phase = Phase::EXTENDING;
        out.floor_active = true;
        out.floor_w = in.minimum_viable_w;
      } else {
        next.phase = Phase::RUNNING;
      }
      break;
    }
    case Phase::RUNNING: {
      if (!in.cycle_active) {
        next.phase = Phase::INACTIVE;
        next.cycle_armed = false;
        break;
      }
      if (room_at_stop) {
        next.phase = Phase::COMFORT_STOP;
        out.force_comfort_stop = true;
        break;
      }
      if (floor_candidate && in.base_requested_w < in.minimum_viable_w && room_below_stop) {
        next.phase = Phase::EXTENDING;
        out.floor_active = true;
        out.floor_w = in.minimum_viable_w;
      } else {
        next.phase = Phase::RUNNING;
      }
      break;
    }
    case Phase::EXTENDING: {
      if (!in.cycle_active) {
        next.phase = Phase::INACTIVE;
        next.cycle_armed = false;
        break;
      }
      if (room_at_stop) {
        next.phase = Phase::COMFORT_STOP;
        out.force_comfort_stop = true;
        break;
      }
      // Water limiter or missing Pmin must never be overruled: fall back to normal.
      if (!floor_candidate) {
        next.phase = Phase::RUNNING;
        break;
      }
      if (in.base_requested_w >= in.minimum_viable_w) {
        next.phase = Phase::RUNNING;
        break;
      }
      if (!room_below_stop) {
        next.phase = Phase::COMFORT_STOP;
        out.force_comfort_stop = true;
        break;
      }
      next.phase = Phase::EXTENDING;
      out.floor_active = true;
      out.floor_w = in.minimum_viable_w;
      break;
    }
    case Phase::COMFORT_STOP: {
      // Transient: the runtime zeroes the normal space-heating request and the
      // central stop/postflow logic handles the compressor stop.
      out.force_comfort_stop = true;
      next.phase = Phase::WAIT_WARM_RESTART;
      break;
    }
    case Phase::WAIT_WARM_RESTART: {
      if (setpoint_drop) {
        next.phase = Phase::INACTIVE;
        next.cycle_armed = false;
        break;
      }
      // A normal Power House start (e.g. after a setpoint raise) may take over.
      if (in.cycle_active && in.actual_heating_active) {
        if (room_at_stop) {
          next.phase = Phase::COMFORT_STOP;
          out.force_comfort_stop = true;
        } else {
          next.phase = Phase::RUNNING;
        }
        break;
      }
      if (room_ready_restart && in.base_requested_w > 0.0f) {
        next.phase = Phase::WARM_RESTART;
        out.warm_restart_intent = true;
        // Temporarily request at least Pmin so the existing low-load logic can
        // actually start the compressor when base demand is below Pmin.
        if (floor_candidate && in.base_requested_w < in.minimum_viable_w) {
          out.floor_active = true;
          out.floor_w = in.minimum_viable_w;
        }
      } else {
        next.phase = Phase::WAIT_WARM_RESTART;
      }
      break;
    }
    case Phase::WARM_RESTART: {
      if (setpoint_drop) {
        next.phase = Phase::INACTIVE;
        next.cycle_armed = false;
        break;
      }
      out.warm_restart_intent = true;
      // Run confirmed: hand back to normal demand plus optional floor.
      if (in.cycle_active && in.actual_heating_active) {
        if (room_at_stop) {
          next.phase = Phase::COMFORT_STOP;
          out.force_comfort_stop = true;
          out.floor_active = false;
          out.floor_w = 0.0f;
        } else if (floor_candidate && in.base_requested_w < in.minimum_viable_w && room_below_stop) {
          next.phase = Phase::EXTENDING;
          out.floor_active = true;
          out.floor_w = in.minimum_viable_w;
        } else {
          next.phase = Phase::RUNNING;
        }
        break;
      }
      // Keep intent across sensor jitter: do not flap on room noise once warm
      // restart is active. Only a comfort stop, setpoint drop or reset clears it.
      if (room_at_stop) {
        next.phase = Phase::COMFORT_STOP;
        out.force_comfort_stop = true;
        out.floor_active = false;
        out.floor_w = 0.0f;
        break;
      }
      next.phase = Phase::WARM_RESTART;
      if (floor_candidate && in.base_requested_w < in.minimum_viable_w) {
        out.floor_active = true;
        out.floor_w = in.minimum_viable_w;
      }
      break;
    }
  }

  out.next = next;
  return out;
}

inline const char* status_value(const Decision& decision, bool enabled, bool blocked) {
  if (!enabled) return "inactive";
  if (blocked) return "blocked";
  return phase_name(decision.next.phase);
}

}  // namespace oq_power_house_run_extension
