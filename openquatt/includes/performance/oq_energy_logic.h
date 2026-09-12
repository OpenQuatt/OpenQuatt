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

  const float power_w = 5.93f + 1.02579f * in.voltage_v * in.current_a + (in.fan_speed * -0.0133119f) +
                        (in.pump_relay_running ? in.pump_power_w : 0.0f) + (in.bottom_plate_heater ? 140.0f : 0.0f) +
                        (in.crankcase_heater ? 33.62f : 0.0f);
  if (!isfinite(power_w)) return {};
  return {nonnegative(power_w), HpInputPowerStatus::V2_ESTIMATED};
}

inline float hp_heating_power(float mode, float inlet_c, float outlet_c, float flow_lph, float cp_j_per_kgk) {
  if (mode != 2.0f || isnan(inlet_c) || isnan(outlet_c) || isnan(flow_lph)) return 0.0f;
  return (flow_lph / 3600.0f) * cp_j_per_kgk * (outlet_c - inlet_c);
}

inline float hp_cooling_power(float mode, float inlet_c, float outlet_c, float flow_lph, float cp_j_per_kgk) {
  if (mode != 1.0f || isnan(inlet_c) || isnan(outlet_c) || isnan(flow_lph)) return 0.0f;
  return nonnegative((flow_lph / 3600.0f) * cp_j_per_kgk * (inlet_c - outlet_c));
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

inline float heating_input_power(bool cooling_session, float hp1_mode, float hp1_power, float hp2_mode = NAN,
                                 float hp2_power = NAN) {
  if (cooling_session) return 0.0f;
  return (hp1_mode == 2.0f ? value_or_zero(hp1_power) : 0.0f) + (hp2_mode == 2.0f ? value_or_zero(hp2_power) : 0.0f);
}

inline float heating_input_power_required(bool cooling_session, float hp1_mode, float hp1_power,
                                          bool second_required = false, float hp2_mode = NAN, float hp2_power = NAN) {
  if (cooling_session) return 0.0f;
  const float hp1_heating_power = hp1_mode == 2.0f ? hp1_power : 0.0f;
  const float hp2_heating_power = hp2_mode == 2.0f ? hp2_power : 0.0f;
  return nonnegative_sum_required(hp1_heating_power, hp2_heating_power, second_required);
}

inline float cooling_input_power(bool cooling_session, float hp1_power, float hp2_power = NAN) {
  return cooling_session ? sum_or_zero(hp1_power, hp2_power) : 0.0f;
}

inline float ratio_or_nan(float output, float input, float minimum_input) {
  if (isnan(output) || isnan(input) || input < minimum_input) return NAN;
  return output / input;
}

inline float instant_ratio_or_nan(float output, float input, float minimum_abs_input) {
  if (isnan(output) || isnan(input) || fabsf(input) < minimum_abs_input) return NAN;
  return output / input;
}

}  // namespace oq_energy
