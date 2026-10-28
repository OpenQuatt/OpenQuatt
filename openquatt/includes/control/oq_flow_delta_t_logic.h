#pragma once

// Delta T flow control.
//
// Optional third flow mode next to "Flow Setpoint" and "Manual PWM". Instead of
// a fixed heating flow, the setpoint is moved slowly so the water temperature
// rise across each running heat pump approaches a target. A fixed flow is only
// right at one load: at low load the pump moves more water than the heat needs,
// and at high load the large temperature rise forces a hotter supply, which
// costs compressor efficiency.
//
// Heating only. In cooling the strategy pins the supply near the dew-point
// floor, so a lower flow mainly reduces the delivered cooling instead of saving
// energy; cooling keeps its fixed setpoint.
//
// The flow PI stays the owner of the pump. This only supplies the setpoint, and
// the PI's own setpoint ramp smooths every step.
//
// Pure logic, no id() and no ESPHome dependencies, so it can be exercised on the
// host. The runtime glue lives in oq_flow_delta_t_runtime.h.

#include <math.h>
#include <stdint.h>

namespace oq_flow_delta_t {

// Pacing is deliberately slow. The gain is in steady operation and heat demand
// changes over hours, so a slow loop loses almost nothing while staying well
// clear of the 10 s flow PI, the compressor and slow floor heating. The step
// interval, the step size and the dead band are configurable; these are the
// defaults, and pacing() keeps any configured value inside sane bounds.
constexpr float kDefaultStepIntervalMin = 5.0f;  // at most one step per 5 minutes
constexpr float kDefaultMaxStepLph = 50.0f;      // never more than this per step
constexpr float kDefaultDeadbandK = 0.3f;        // ignore differences at sensor-error level
constexpr uint32_t kSettleMs = 300000u;          // after a start, or a unit joining or leaving
constexpr uint32_t kDefrostHoldMs = 600000u;     // after any defrost
constexpr int kMinSamples = 6;                   // at least a minute of 10 s samples per step
constexpr uint32_t kStepBlankMs = 60000u;        // samples this soon after a step still show the transition
constexpr float kGain = 0.5f;                    // move half of the gap per step
constexpr float kLowFlowMarginLph = 50.0f;       // floor kept above the low-flow fault threshold
constexpr float kShortfallLph = 75.0f;           // flow this far below the setpoint: pump behind
constexpr float kCeilingMarginLph = 25.0f;
constexpr float kMinUsableRiseK = 0.2f;         // below this a sample says nothing about the flow
constexpr uint32_t kWarnAfterMs = 300000u;      // water limit still cutting power after this at maximum flow
constexpr uint32_t kOilReturnHoldMs = 120000u;  // after oil return; the same hold Power House uses

enum class Reason : uint8_t {
  OFF = 0,      // mode not selected
  NOT_HEATING,  // no heat pump heating in CM2
  SETTLING,     // waiting for stable temperatures
  DEFROST,      // defrost, or the hold after it
  NO_DATA,      // missing or stale temperatures or flow
  FLOW_FAULT,   // low-flow fault active
  WATER_LIMIT,  // water temperature protection is cutting power: maximum flow
  ON_TARGET,
  ADJUSTING,
  AT_MINIMUM,
  AT_MAXIMUM,
  PUMP_LIMIT,    // the pump cannot deliver more flow in this installation
  LIMIT_AT_MAX,  // water limit still cutting power at maximum flow: a structural cause
  OIL_RETURN,    // oil return, or the hold after it: flow held, samples discarded
};

inline const char* reason_text(Reason reason) {
  switch (reason) {
    case Reason::OFF:
      return "Off";
    case Reason::NOT_HEATING:
      return "Waiting for heat pump heating";
    case Reason::SETTLING:
      return "Settling";
    case Reason::DEFROST:
      return "Defrost - holding flow";
    case Reason::NO_DATA:
      return "No reliable temperatures - fixed flow";
    case Reason::FLOW_FAULT:
      return "Flow fault - fixed flow";
    case Reason::WATER_LIMIT:
      return "Water temperature limit - maximum flow";
    case Reason::ON_TARGET:
      return "On target";
    case Reason::ADJUSTING:
      return "Adjusting";
    case Reason::AT_MINIMUM:
      return "At minimum flow";
    case Reason::AT_MAXIMUM:
      return "At maximum flow";
    case Reason::PUMP_LIMIT:
      return "Pump at its limit";
    case Reason::LIMIT_AT_MAX:
      return "Water limit despite maximum flow - check maximum flow or hydraulics";
    case Reason::OIL_RETURN:
      return "Oil return - holding flow";
  }
  return "Off";
}

struct Unit {
  bool heating = false;      // compressor running in heating mode
  bool defrost = false;      // defrost flag set or 4-way valve reversed
  bool oil_return = false;   // the outdoor unit runs its compressor oil return cycle
  bool temps_fresh = false;  // inlet and outlet recently received and valid
  float in_c = NAN;
  float out_c = NAN;
  int level = 0;
};

struct Input {
  uint32_t now_ms = 0;
  bool enabled = false;       // Flow Control Mode is Delta T
  bool heating_mode = false;  // CM2, heat pump heating, outside service tasks
  Unit units[2]{};
  int unit_count = 1;             // 1 for a single, 2 for a Duo
  float flow_lph = NAN;           // measured flow, the PI process value
  float base_setpoint_lph = NAN;  // normal Flow Setpoint: start and fallback flow
  float target_k = 3.0f;          // temperature rise per running heat pump
  float min_lph = 500.0f;
  float max_lph = 1200.0f;
  float lowflow_threshold_lph = 400.0f;
  float standard_flow_lph = 800.0f;  // Quatt's own fixed flow for the configured version
  float step_interval_min = kDefaultStepIntervalMin;
  float max_step_lph = kDefaultMaxStepLph;
  float deadband_k = kDefaultDeadbandK;
  bool water_limit_active = false;  // water temperature protection is cutting power
  bool flow_fault = false;          // low-flow fault active
  bool pump_at_full_drive = false;  // the flow PI drives the pump as hard as it can
};

struct State {
  bool active = false;
  float setpoint_lph = NAN;
  bool settling = false;
  uint32_t settle_until_ms = 0;
  bool defrost_hold = false;
  uint32_t defrost_until_ms = 0;
  float defrost_flow_lph = NAN;  // held through a defrost; the next cycle starts here
  uint32_t last_step_ms = 0;
  uint8_t unit_mask = 0;
  int levels[2] = {0, 0};
  float rise_sum_k = 0.0f;
  int rise_count = 0;
  float ceiling_lph = NAN;
  Reason reason = Reason::OFF;
  float last_rise_k = NAN;
  float unit_rise_sum_k[2] = {0.0f, 0.0f};
  float last_unit_rise_k[2] = {NAN, NAN};
  bool limit_timing = false;
  bool oil_hold = false;
  uint32_t oil_until_ms = 0;
  uint32_t limit_since_ms = 0;
  bool warning = false;
};

struct Output {
  State next{};
  float setpoint_lph = NAN;  // what the PI should track
  bool controlling = false;  // false: the base setpoint is passed through untouched
  int counted_units = 0;
  float min_lph = NAN;
  float max_lph = NAN;
  bool warning = false;  // water limit still cutting power at maximum flow
};

// True while now is before until. Only meaningful for a deadline that was
// actually set; callers keep an explicit flag for that, because a zero
// deadline compared this way reads as "in the future" after 24.8 days uptime.
inline bool before(uint32_t now_ms, uint32_t until_ms) { return static_cast<int32_t>(now_ms - until_ms) < 0; }

inline float clampf(float value, float low, float high) { return value < low ? low : (value > high ? high : value); }

struct Pacing {
  uint32_t step_interval_ms = 0;
  float max_step_lph = 0.0f;
  float deadband_k = 0.0f;
};

// The configured pacing, held to bounds that keep the loop slow and stable: a
// step interval of zero would hunt, a step of zero would freeze, and a dead
// band under sensor resolution would chase noise. The shortest interval is the
// blanking after a step plus a minute of samples. A missing value falls back to
// its default.
inline Pacing pacing(const Input& in) {
  Pacing out;
  const float minutes =
      isfinite(in.step_interval_min) ? clampf(in.step_interval_min, 2.0f, 30.0f) : kDefaultStepIntervalMin;
  out.step_interval_ms = static_cast<uint32_t>(minutes * 60000.0f);
  out.max_step_lph = isfinite(in.max_step_lph) ? clampf(in.max_step_lph, 10.0f, 200.0f) : kDefaultMaxStepLph;
  out.deadband_k = isfinite(in.deadband_k) ? clampf(in.deadband_k, 0.1f, 1.0f) : kDefaultDeadbandK;
  return out;
}

struct Band {
  float min_lph = NAN;
  float max_lph = NAN;
  bool valid = false;
};

// The usable flow band. The maximum never sits below the normal Flow Setpoint,
// nor below Quatt's own fixed flow for the configured version, so at full power
// this mode never moves less water than either would. A lower maximum would
// push the supply up until the water temperature protection cut power,
// silently and exactly when it is coldest. The minimum stays clear of the
// low-flow fault and below the maximum. Settings that contradict each other are
// corrected into this band rather than refused.
inline Band band(const Input& in) {
  Band out;
  if (!isfinite(in.base_setpoint_lph) || in.base_setpoint_lph <= 0.0f || !isfinite(in.min_lph) ||
      !isfinite(in.max_lph) || !isfinite(in.lowflow_threshold_lph))
    return out;
  const float standard_lph = isfinite(in.standard_flow_lph) ? in.standard_flow_lph : 0.0f;
  out.max_lph = fmaxf(fmaxf(in.max_lph, in.base_setpoint_lph), standard_lph);
  out.min_lph = fminf(fmaxf(in.min_lph, in.lowflow_threshold_lph + kLowFlowMarginLph), out.max_lph);
  out.valid = true;
  return out;
}

// Start a fresh averaging window; per-unit sums always move with the average.
inline void clear_window(State& state) {
  state.rise_sum_k = 0.0f;
  state.rise_count = 0;
  state.unit_rise_sum_k[0] = 0.0f;
  state.unit_rise_sum_k[1] = 0.0f;
}

inline void reset_cycle(State& state) {
  state.active = false;
  state.setpoint_lph = NAN;
  state.settling = false;
  clear_window(state);
  state.last_rise_k = NAN;
  state.last_unit_rise_k[0] = NAN;
  state.last_unit_rise_k[1] = NAN;
  state.limit_timing = false;
  state.warning = false;
  state.oil_hold = false;
  state.ceiling_lph = NAN;
  state.defrost_flow_lph = NAN;
  state.unit_mask = 0;
  state.levels[0] = 0;
  state.levels[1] = 0;
}

inline Output pass_through(Output out, const Input& in, Reason reason) {
  reset_cycle(out.next);
  out.next.reason = reason;
  out.setpoint_lph = in.base_setpoint_lph;
  out.controlling = false;
  return out;
}

// Defrost and the hold after it. The cycle restarts once the hold ends, from
// the flow held here.
inline Output hold_for_defrost(Output out, const Input& in) {
  const float held_lph = isfinite(out.next.defrost_flow_lph) ? out.next.defrost_flow_lph : in.base_setpoint_lph;
  reset_cycle(out.next);
  out.next.defrost_flow_lph = held_lph;
  out.next.reason = Reason::DEFROST;
  out.setpoint_lph = held_lph;
  out.controlling = isfinite(held_lph);
  return out;
}

// The flow loop took a path that does not use this mode this tick: manual PWM,
// frost protection, idle or a service task. End the cycle, so the next heating
// tick starts afresh with a settle instead of resuming on samples, a setpoint
// and a pump ceiling from before. The pump output stays with that path.
inline Output stand_aside(const State& state, bool enabled) {
  Output out;
  out.next = state;
  out.next.defrost_hold = false;  // a stale deadline must not outlive the cycle
  reset_cycle(out.next);
  out.next.reason = enabled ? Reason::NOT_HEATING : Reason::OFF;
  return out;
}

inline Output decide(const Input& in, const State& state) {
  Output out;
  out.next = state;
  out.setpoint_lph = in.base_setpoint_lph;
  const Band limits = band(in);
  out.min_lph = limits.min_lph;
  out.max_lph = limits.max_lph;

  if (!in.enabled || !in.heating_mode) {
    // Outside heat pump heating a defrost hold has nothing left to protect, and
    // its deadline would go stale over a long pause.
    out.next.defrost_hold = false;
    return pass_through(out, in, in.enabled ? Reason::NOT_HEATING : Reason::OFF);
  }

  const int units = in.unit_count >= 2 ? 2 : 1;
  bool any_defrost = false;
  for (int i = 0; i < units; ++i) any_defrost = any_defrost || in.units[i].defrost;
  if (any_defrost && !out.next.defrost_hold) {
    // Defrost pulls heat back out of the water. Less water would leave the heat
    // exchanger colder and slow the defrost, so it keeps the flow it had, and
    // never less than the fixed Flow Setpoint it would get in fixed mode.
    const bool running = out.next.active && isfinite(out.next.setpoint_lph);
    out.next.defrost_flow_lph = running ? fmaxf(out.next.setpoint_lph, in.base_setpoint_lph) : in.base_setpoint_lph;
  }
  if (any_defrost) {
    out.next.defrost_hold = true;
    out.next.defrost_until_ms = in.now_ms + kDefrostHoldMs;
  }
  if (out.next.defrost_hold) {
    if (any_defrost || before(in.now_ms, out.next.defrost_until_ms)) return hold_for_defrost(out, in);
    out.next.defrost_hold = false;  // never compare against this deadline again
  }

  uint8_t mask = 0;
  int counted = 0;
  for (int i = 0; i < units; ++i) {
    if (!in.units[i].heating) continue;
    mask = static_cast<uint8_t>(mask | (1u << i));
    ++counted;
  }
  out.counted_units = counted;
  if (counted == 0) return pass_through(out, in, Reason::NOT_HEATING);
  if (in.flow_fault) return pass_through(out, in, Reason::FLOW_FAULT);
  if (!limits.valid || !isfinite(in.target_k) || in.target_k <= 0.0f || !isfinite(in.flow_lph))
    return pass_through(out, in, Reason::NO_DATA);
  for (int i = 0; i < units; ++i) {
    if (!in.units[i].heating) continue;
    if (!in.units[i].temps_fresh || !isfinite(in.units[i].in_c) || !isfinite(in.units[i].out_c))
      return pass_through(out, in, Reason::NO_DATA);
  }

  State& next = out.next;
  if (!next.active) {
    // A new cycle starts from the known fixed flow, or from the flow a defrost
    // held, then waits for the water temperatures to mean something before the
    // first step.
    const float start_lph = isfinite(next.defrost_flow_lph) ? next.defrost_flow_lph : in.base_setpoint_lph;
    next.defrost_flow_lph = NAN;
    next.active = true;
    next.setpoint_lph = clampf(start_lph, limits.min_lph, limits.max_lph);
    next.settling = true;
    next.settle_until_ms = in.now_ms + kSettleMs;
    next.last_step_ms = in.now_ms;
    next.unit_mask = mask;
    for (int i = 0; i < 2; ++i) next.levels[i] = in.units[i].level;
    clear_window(next);
    next.ceiling_lph = NAN;
    next.reason = Reason::SETTLING;
  } else if (mask != next.unit_mask) {
    // The second heat pump joining roughly halves the flow needed per kW, so
    // let the temperatures settle before acting on the new situation.
    next.unit_mask = mask;
    next.settling = true;
    next.settle_until_ms = in.now_ms + kSettleMs;
    clear_window(next);
    next.reason = Reason::SETTLING;
  }
  bool level_changed = false;
  for (int i = 0; i < units; ++i) level_changed = level_changed || next.levels[i] != in.units[i].level;
  if (level_changed) {
    // A level change only restarts the averaging window. A full settle on every
    // level step would starve the controller under Power House modulation.
    for (int i = 0; i < 2; ++i) next.levels[i] = in.units[i].level;
    clear_window(next);
  }

  // The live band may have moved under a running cycle. A pump with drive to
  // spare is not at its limit, so the ceiling lifts the moment the PI backs
  // off, for instance because valves opened.
  if (isfinite(next.ceiling_lph) && !in.pump_at_full_drive) next.ceiling_lph = NAN;
  const float upper =
      isfinite(next.ceiling_lph) ? fmaxf(limits.min_lph, fminf(limits.max_lph, next.ceiling_lph)) : limits.max_lph;
  next.setpoint_lph = clampf(next.setpoint_lph, limits.min_lph, limits.max_lph);

  out.controlling = true;
  if (in.water_limit_active) {
    // Flow before power: cooler supply water lets the protection relax so the
    // compressor keeps its power. Hold it there for a settle period afterwards
    // so stepping down again does not re-trigger the protection at once.
    next.setpoint_lph = limits.max_lph;
    next.settling = true;
    next.settle_until_ms = in.now_ms + kSettleMs;
    clear_window(next);
    // Still cutting power once the flow has had time to reach the maximum:
    // more water did not help, so the cause is structural - a maximum set too
    // low, or an installation that cannot take more water.
    if (!next.limit_timing) {
      next.limit_timing = true;
      next.limit_since_ms = in.now_ms;
    }
    next.warning = static_cast<uint32_t>(in.now_ms - next.limit_since_ms) >= kWarnAfterMs;
    next.reason = next.warning ? Reason::LIMIT_AT_MAX : Reason::WATER_LIMIT;
    out.warning = next.warning;
    out.setpoint_lph = next.setpoint_lph;
    return out;
  }
  next.limit_timing = false;
  next.warning = false;

  // Oil return is a protection cycle the outdoor unit owns: it runs the
  // compressor its own way for a few minutes, so the temperature rise says
  // nothing about the flow. Unlike defrost it keeps heating the water, so the
  // flow simply stays where it is; only the samples are discarded, during the
  // cycle and for the same hold Power House uses afterwards. Either unit counts,
  // because on a Duo the water runs through both.
  bool oil_return = false;
  for (int i = 0; i < units; ++i) oil_return = oil_return || in.units[i].oil_return;
  if (oil_return) {
    next.oil_hold = true;
    next.oil_until_ms = in.now_ms + kOilReturnHoldMs;
  }
  if (next.oil_hold) {
    if (oil_return || before(in.now_ms, next.oil_until_ms)) {
      clear_window(next);
      next.reason = Reason::OIL_RETURN;
      out.setpoint_lph = next.setpoint_lph;
      return out;
    }
    next.oil_hold = false;  // never compare against this deadline again
  }
  if (next.settling) {
    if (before(in.now_ms, next.settle_until_ms)) {
      next.reason = Reason::SETTLING;
      out.setpoint_lph = next.setpoint_lph;
      return out;
    }
    next.settling = false;  // never compare against this deadline again
  }
  if (next.reason == Reason::SETTLING || next.reason == Reason::WATER_LIMIT || next.reason == Reason::LIMIT_AT_MAX ||
      next.reason == Reason::OIL_RETURN)
    next.reason = Reason::ADJUSTING;

  // Average temperature rise per running heat pump. A Duo runs in series on one
  // shared flow, so the flow cannot put each unit on its own target; the
  // average is the compromise, and Power House usually runs both alike.
  float rise_k = 0.0f;
  for (int i = 0; i < units; ++i) {
    if (in.units[i].heating) rise_k += in.units[i].out_c - in.units[i].in_c;
  }
  rise_k /= static_cast<float>(counted);
  // The first minute after a step still shows the transition to the new flow.
  // Judging the next step on it would stack steps on a half-settled response
  // and make a fast pacing hunt.
  const bool after_step = static_cast<uint32_t>(in.now_ms - next.last_step_ms) < kStepBlankMs;
  if (rise_k >= kMinUsableRiseK && !after_step) {
    next.rise_sum_k += rise_k;
    next.rise_count++;
    for (int i = 0; i < units; ++i) {
      if (in.units[i].heating) next.unit_rise_sum_k[i] += in.units[i].out_c - in.units[i].in_c;
    }
  }

  const Pacing pace = pacing(in);
  if (static_cast<uint32_t>(in.now_ms - next.last_step_ms) < pace.step_interval_ms || next.rise_count < kMinSamples) {
    out.setpoint_lph = next.setpoint_lph;
    return out;
  }

  const float samples = static_cast<float>(next.rise_count);
  const float average_k = next.rise_sum_k / samples;
  next.last_rise_k = average_k;
  for (int i = 0; i < 2; ++i)
    next.last_unit_rise_k[i] = (i < units && in.units[i].heating) ? next.unit_rise_sum_k[i] / samples : NAN;
  clear_window(next);
  next.last_step_ms = in.now_ms;

  // The flow lags the last setpoint. Only a pump the PI already drives as hard
  // as it can marks the ceiling of this installation; fixed mode could not get
  // more water through either. With drive to spare the PI is still catching
  // up, so the flow is not raised further until it has.
  const bool behind = next.setpoint_lph - in.flow_lph > kShortfallLph;
  if (behind && in.pump_at_full_drive) next.ceiling_lph = fmaxf(limits.min_lph, in.flow_lph + kCeilingMarginLph);
  const bool catching_up = behind && !in.pump_at_full_drive;
  const float step_upper =
      isfinite(next.ceiling_lph) ? fmaxf(limits.min_lph, fminf(limits.max_lph, next.ceiling_lph)) : upper;

  if (fabsf(average_k - in.target_k) <= pace.deadband_k) {
    next.setpoint_lph = clampf(next.setpoint_lph, limits.min_lph, step_upper);
    next.reason = Reason::ON_TARGET;
    out.setpoint_lph = next.setpoint_lph;
    return out;
  }

  // Heat equals flow times temperature rise, so over one step the flow that
  // would give the target follows directly from the measurement.
  const float desired_lph = in.flow_lph * average_k / in.target_k;
  float step = kGain * (desired_lph - next.setpoint_lph);
  step = clampf(step, -pace.max_step_lph, catching_up ? 0.0f : pace.max_step_lph);
  next.setpoint_lph = clampf(next.setpoint_lph + step, limits.min_lph, step_upper);

  if (next.setpoint_lph >= step_upper - 0.5f)
    next.reason =
        (isfinite(next.ceiling_lph) && step_upper < limits.max_lph - 0.5f) ? Reason::PUMP_LIMIT : Reason::AT_MAXIMUM;
  else if (next.setpoint_lph <= limits.min_lph + 0.5f)
    next.reason = Reason::AT_MINIMUM;
  else
    next.reason = Reason::ADJUSTING;
  out.setpoint_lph = next.setpoint_lph;
  return out;
}

}  // namespace oq_flow_delta_t
