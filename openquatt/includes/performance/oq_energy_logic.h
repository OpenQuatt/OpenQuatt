#pragma once

#include <math.h>

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

inline float sum_available(float first, float second = NAN) {
  return isnan(first) && isnan(second) ? NAN : sum_or_zero(first, second);
}

inline float heating_input_power(bool cooling_session, float hp1_mode, float hp1_power, float hp2_mode = NAN,
                                 float hp2_power = NAN) {
  if (cooling_session) return 0.0f;
  return (hp1_mode == 2.0f ? value_or_zero(hp1_power) : 0.0f) + (hp2_mode == 2.0f ? value_or_zero(hp2_power) : 0.0f);
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
