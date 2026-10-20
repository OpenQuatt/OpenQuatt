#pragma once

#include <math.h>
#include <stdint.h>

#include "../odu/oq_odu_generation.h"

namespace oq_energy {

struct HpElectricalInputs {
  float voltage_v;
  float current_a;
  float fan_speed;
  bool pump_relay_known;
  bool pump_relay_running;
  float pump_power_w;
  bool bottom_plate_heater;
  bool crankcase_heater;
};

struct HpElectricalFreshness {
  bool online;
  uint32_t now_ms;
  uint32_t stale_ms;
  uint32_t voltage_updated_ms;
  uint32_t current_updated_ms;
  uint32_t fan_updated_ms;
  uint32_t status_updated_ms;
  uint32_t pump_updated_ms;
};

enum class HpInputPowerStatus : uint8_t {
  LEGACY_ESTIMATED = 0,
  V2_ESTIMATED = 1,
  INVALID_OR_STALE = 2,
  UNKNOWN_VARIANT = 3,
};

inline const char* hp_input_power_status_name(HpInputPowerStatus status) {
  switch (status) {
    case HpInputPowerStatus::LEGACY_ESTIMATED:
      return "legacy_estimated";
    case HpInputPowerStatus::V2_ESTIMATED:
      return "v2_estimated";
    case HpInputPowerStatus::INVALID_OR_STALE:
      return "invalid_or_stale";
    case HpInputPowerStatus::UNKNOWN_VARIANT:
      return "unknown_variant";
    default:
      return "invalid_status";
  }
}

struct HpInputPowerEstimate {
  float watts{NAN};
  HpInputPowerStatus status{HpInputPowerStatus::INVALID_OR_STALE};

  bool available() const {
    return (status == HpInputPowerStatus::LEGACY_ESTIMATED || status == HpInputPowerStatus::V2_ESTIMATED) &&
           isfinite(watts);
  }
};

inline float value_or_zero(float value) { return isnan(value) ? 0.0f : value; }

inline float nonnegative(float value) { return value < 0.0f ? 0.0f : value; }

inline float hp_input_power(const HpElectricalInputs& in) {
  const float power_w = 5.150232354845286f +
                        (value_or_zero(in.voltage_v) * value_or_zero(in.current_a) * 1.1240096401010435f) +
                        (value_or_zero(in.fan_speed) * -0.04858859969715763f) +
                        (in.pump_relay_known && in.pump_relay_running ? value_or_zero(in.pump_power_w) : 0.0f) +
                        (in.bottom_plate_heater ? 150.06430841218332f : 0.0f) + (in.crankcase_heater ? 40.0f : 0.0f);
  return nonnegative(power_w);
}

inline bool measurement_fresh(uint32_t now_ms, uint32_t updated_ms, uint32_t stale_ms) {
  return updated_ms != 0U && static_cast<uint32_t>(now_ms - updated_ms) <= stale_ms;
}

inline bool v2_input_telemetry_fresh(const HpElectricalFreshness& freshness, bool pump_running) {
  return freshness.online && measurement_fresh(freshness.now_ms, freshness.voltage_updated_ms, freshness.stale_ms) &&
         measurement_fresh(freshness.now_ms, freshness.current_updated_ms, freshness.stale_ms) &&
         measurement_fresh(freshness.now_ms, freshness.fan_updated_ms, freshness.stale_ms) &&
         measurement_fresh(freshness.now_ms, freshness.status_updated_ms, freshness.stale_ms) &&
         (!pump_running || measurement_fresh(freshness.now_ms, freshness.pump_updated_ms, freshness.stale_ms));
}

inline HpInputPowerEstimate hp_input_power_for_variant(oq_odu::Variant variant, const HpElectricalInputs& in,
                                                       bool v2_telemetry_fresh) {
  if (variant == oq_odu::Variant::V1 || variant == oq_odu::Variant::V1_5) {
    return {hp_input_power(in), HpInputPowerStatus::LEGACY_ESTIMATED};
  }
  if (variant != oq_odu::Variant::V2_OLD_MODEL && variant != oq_odu::Variant::V2_NEW_MODEL) {
    return {NAN, HpInputPowerStatus::UNKNOWN_VARIANT};
  }
  if (!v2_telemetry_fresh || !in.pump_relay_known || !isfinite(in.voltage_v) || in.voltage_v <= 0.0f ||
      !isfinite(in.current_a) || in.current_a < 0.0f || !isfinite(in.fan_speed) || in.fan_speed < 0.0f ||
      (in.pump_relay_running && (!isfinite(in.pump_power_w) || in.pump_power_w < 0.0f))) {
    return {};
  }

  // V2 switches the 140 W bottom plate and 140 W drain pan heaters together via R2108 bit 2.
  const float power_w = 5.93f + 1.02579f * in.voltage_v * in.current_a + (in.fan_speed * -0.0133119f) +
                        (in.pump_relay_running ? in.pump_power_w : 0.0f) + (in.bottom_plate_heater ? 280.0f : 0.0f) +
                        (in.crankcase_heater ? 33.62f : 0.0f);
  if (!isfinite(power_w)) return {};
  return {nonnegative(power_w), HpInputPowerStatus::V2_ESTIMATED};
}

// Signed thermal transfer to water. Callers decide whether operating mode and
// measurement provenance make this value suitable for their purpose.
inline float hydronic_heat_power(float inlet_c, float outlet_c, float flow_lph, float cp_j_per_kgk) {
  if (isnan(inlet_c) || isnan(outlet_c) || isnan(flow_lph) || isnan(cp_j_per_kgk)) return NAN;
  return (flow_lph / 3600.0f) * cp_j_per_kgk * (outlet_c - inlet_c);
}

inline float hp_heating_power(float mode, float inlet_c, float outlet_c, float flow_lph, float cp_j_per_kgk) {
  if (mode != 2.0f || isnan(inlet_c) || isnan(outlet_c) || isnan(flow_lph)) return 0.0f;
  return hydronic_heat_power(inlet_c, outlet_c, flow_lph, cp_j_per_kgk);
}

inline float hp_cooling_power(float mode, float inlet_c, float outlet_c, float flow_lph, float cp_j_per_kgk) {
  if (mode != 1.0f || isnan(inlet_c) || isnan(outlet_c) || isnan(flow_lph)) return 0.0f;
  return nonnegative(-hydronic_heat_power(inlet_c, outlet_c, flow_lph, cp_j_per_kgk));
}

inline float sum_or_zero(float first, float second = NAN) { return value_or_zero(first) + value_or_zero(second); }

inline float nonnegative_sum(float first, float second = NAN) { return nonnegative(sum_or_zero(first, second)); }

inline float nonnegative_sum_required(float first, float second = NAN, bool second_required = false) {
  if (!isfinite(first) || (second_required && !isfinite(second))) return NAN;
  return nonnegative(first + (second_required ? second : 0.0f));
}

inline float sum_available(float first, float second = NAN) {
  return isnan(first) && isnan(second) ? NAN : sum_or_zero(first, second);
}

inline float attributed_input_power(float total, bool cooling_purpose, bool for_cooling) {
  if (cooling_purpose != for_cooling) return 0.0f;
  return isfinite(total) && total >= 0.0f ? total : NAN;
}

// Only a new thermal purpose changes the owner; postflow, standby and idle
// service retain it. An unresolved request must not invent a new owner.
inline bool cooling_energy_purpose(bool previous, int desired_mode, int next_mode, bool heating_request,
                                   bool cooling_request, int manual_hp1_mode = 0, int manual_hp2_mode = 0) {
  if (desired_mode == 100) {
    const bool heating = manual_hp1_mode == 2 || manual_hp2_mode == 2;
    const bool cooling = manual_hp1_mode == 1 || manual_hp2_mode == 1;
    return heating != cooling ? cooling : previous;
  }
  if (desired_mode == 5) return true;
  if (desired_mode == 2 || desired_mode == 3 || desired_mode == 4 || desired_mode == 98) return false;
  if (desired_mode == 1) {
    if (next_mode == 5) return true;
    if (next_mode == 2 || next_mode == 3 || next_mode == 4 || next_mode == 98) return false;
    if (next_mode != 0) return previous;
    if (heating_request != cooling_request) return cooling_request;
  }
  return previous;
}

struct RestoredElectricalEnergy {
  float heating{NAN};
  float cooling{NAN};
  float total{NAN};
};

// Legacy total checkpoints contain pump/standby energy absent from the old
// heating counter. Preserve every restored counter without decreasing one.
inline RestoredElectricalEnergy reconcile_electrical_energy(float heating, float cooling, float checkpoint,
                                                            bool cooling_purpose = false) {
  if (!isfinite(heating) || heating < 0.0f || !isfinite(cooling) || cooling < 0.0f) return {};
  float total = heating + cooling;
  if (!isfinite(total)) return {};
  if (isfinite(checkpoint) && checkpoint > total) {
    if (cooling_purpose)
      cooling = checkpoint - heating;
    else
      heating = checkpoint - cooling;
    total = heating + cooling;
  }
  return {heating, cooling, total};
}

struct DailyElectricalPublication {
  uint32_t date_key{0};
  float heating{NAN};
  float cooling{NAN};
  float total{NAN};
  bool waiting_for_reset{false};
  bool heating_reset{false};
  bool cooling_reset{false};
};

// Observe source callbacks synchronously: a reset to zero can be followed by a
// power sample before the deferred total publication runs.
inline void observe_daily_electrical_source(DailyElectricalPublication& previous, uint32_t date_key, bool for_cooling,
                                            float value) {
  if (date_key != 0 && date_key != previous.date_key) {
    previous.waiting_for_reset = previous.date_key != 0;
    previous.date_key = date_key;
    previous.total = NAN;
    previous.heating_reset = previous.heating == 0.0f;
    previous.cooling_reset = previous.cooling == 0.0f;
  }
  if (previous.waiting_for_reset && isfinite(value)) {
    if (for_cooling)
      previous.cooling_reset = previous.cooling_reset || value == 0.0f || value < previous.cooling;
    else
      previous.heating_reset = previous.heating_reset || value == 0.0f || value < previous.heating;
  }
}

// Daily source timers can reset on different loop iterations. Wait until each
// nonzero source has reset; never publish the intermediate leftover as a new day.
inline bool daily_electrical_total_ready(DailyElectricalPublication& previous, uint32_t date_key, float heating,
                                         float cooling) {
  observe_daily_electrical_source(previous, date_key, false, heating);
  observe_daily_electrical_source(previous, date_key, true, cooling);
  const float total = reconcile_electrical_energy(heating, cooling, NAN).total;
  if (!isfinite(total)) return true;  // Publish unavailable without changing the recovery baseline.
  if (previous.waiting_for_reset) {
    if (!previous.heating_reset || !previous.cooling_reset) return false;
    previous.waiting_for_reset = false;
  }
  // A zero-valued source may briefly accrue energy before its own reset timer.
  // Withhold that later lower sample rather than creating another HA reset.
  if (isfinite(previous.total) && total < previous.total) return false;
  previous.heating = heating;
  previous.cooling = cooling;
  previous.total = total;
  return true;
}

inline float ratio_or_nan(float output, float input, float minimum_input) {
  if (isnan(output) || isnan(input) || input < minimum_input) return NAN;
  return output / input;
}

struct HeatingCopState {
  float working_mode{NAN};
  float compressor_hz{NAN};
  float defrost{NAN};
  bool fresh{false};
};

inline bool heating_cop_state_valid(const HeatingCopState& state) {
  return state.fresh &&
         (state.working_mode == 0.0f || state.working_mode == 1.0f || state.working_mode == 2.0f ||
          state.working_mode == 4.0f) &&
         isfinite(state.compressor_hz) && state.compressor_hz >= 0.0f && state.compressor_hz <= 120.0f &&
         (state.defrost == 0.0f || state.defrost == 1.0f);
}

inline float heating_cop_or_nan(float output, float input, float minimum_input, const HeatingCopState& first,
                                const HeatingCopState& second = {}, bool second_required = false) {
  if (!heating_cop_state_valid(first) || (second_required && !heating_cop_state_valid(second))) return NAN;
  const bool first_heating = first.working_mode == 2.0f;
  const bool second_heating = second_required && second.working_mode == 2.0f;
  if (first.working_mode == 4.0f || first.defrost != 0.0f ||
      (second_required && (second.working_mode == 4.0f || second.defrost != 0.0f)))
    return NAN;
  // Heating mode can precede compressor start. Positive heat alone may also be
  // residual heat or a sensor offset while only the circulation pump is running.
  if (!(first_heating && first.compressor_hz > 0.0f) && !(second_heating && second.compressor_hz > 0.0f)) return NAN;
  if (!isfinite(output) || output <= 0.0f || !isfinite(input) || input <= 0.0f || input < minimum_input) return NAN;
  return output / input;
}

inline float instant_ratio_or_nan(float output, float input, float minimum_abs_input) {
  if (isnan(output) || isnan(input) || fabsf(input) < minimum_abs_input) return NAN;
  return output / input;
}

}  // namespace oq_energy
