#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace oq_supervisory_power {

struct HpMeasurement {
  bool online = false;
  bool voltage_valid = false;
  bool current_valid = false;
  uint32_t voltage_updated_ms = 0;
  uint32_t current_updated_ms = 0;
};

struct Input {
  uint32_t now_ms = 0;
  HpMeasurement hp1;
  HpMeasurement hp2;
  bool total_power_valid = false;
  float total_power_w = NAN;
};

struct Config {
  bool duo = false;
  uint32_t tick_s = 0;
  uint32_t peak_trip_s = 0;
  uint32_t soft_trip_s = 0;
  uint32_t recover_s = 0;
  uint32_t measurement_stale_ms = 0;
  int fallback_cap_f = 0;
  int max_cap_f = 0;
  float soft_limit_w = NAN;
  float peak_limit_w = NAN;
  float recover_limit_w = NAN;
};

struct State {
  int cap_f = 0;
  uint32_t over_soft_s = 0;
  uint32_t over_peak_s = 0;
  uint32_t under_ok_s = 0;
};

struct Output {
  State state;
  bool measurement_valid = false;
};

struct Thresholds {
  float soft_w;
  float peak_w;
  float recover_w;
};

inline float standard_current_a(bool duo, bool generation_v2, float v1_a, float v2_a) {
  return duo && generation_v2 ? v2_a : v1_a;
}

inline float maximum_current_a(bool duo, bool generation_v2, float v1_a, float v2_a) {
  return standard_current_a(duo, generation_v2, v1_a, v2_a);
}
// Absolute technische bovengrens: Duo V2 mag tot de bevestigde V2-max-grens,
// Duo V1/V1.5 tot de V2-standaard (zelfde buitenunit-/regelplatform als Duo
// V2). Single en Duo met onbekende generatie blijven conservatief op de
// V1-standaard staan, zodat geen hogere waarde wordt vrijgegeven zonder
// betrouwbare detectie.
inline float absolute_maximum_current_a(bool duo, bool generation_known, bool generation_v2, float v1_a, float v2_a,
                                        float v2_max_a) {
  if (!duo) {
    return v1_a;
  }
  if (!generation_known) {
    return v1_a;
  }
  return generation_v2 ? v2_max_a : v2_a;
}

inline float effective_current_a(float configured_a, float minimum_a, float maximum_a) {
  return std::isnan(configured_a) ? maximum_a : std::min(maximum_a, std::max(minimum_a, configured_a));
}

inline Thresholds thresholds(float current_a, float mains_voltage_v) {
  return {
      current_a * mains_voltage_v * (3400.0f / (16.0f * 230.0f)),
      current_a * mains_voltage_v * (3650.0f / (16.0f * 230.0f)),
      current_a * mains_voltage_v * (3300.0f / (16.0f * 230.0f)),
  };
}

inline int fallback_cap(bool duo, float current_a, float v1_a, int configured_cap_f, int max_cap_f) {
  const float scale =
      std::isfinite(current_a) && std::isfinite(v1_a) && v1_a > 0.0f ? std::min(current_a, v1_a) / v1_a : 0.0f;
  const float topology_cap = duo ? static_cast<float>(configured_cap_f) : configured_cap_f / 2.0f;
  return std::clamp(static_cast<int>(std::floor(topology_cap * scale)), 0, std::max(0, max_cap_f));
}

inline uint32_t seconds_to_ms(uint32_t seconds) {
  constexpr uint32_t max_seconds = std::numeric_limits<uint32_t>::max() / 1000UL;
  return std::min(seconds, max_seconds) * 1000UL;
}

inline bool fresh(uint32_t now_ms, uint32_t updated_ms, uint32_t stale_ms) {
  return updated_ms != 0 && static_cast<uint32_t>(now_ms - updated_ms) <= stale_ms;
}

inline bool valid(const HpMeasurement& hp, uint32_t now_ms, uint32_t stale_ms) {
  return hp.online && hp.voltage_valid && hp.current_valid && fresh(now_ms, hp.voltage_updated_ms, stale_ms) &&
         fresh(now_ms, hp.current_updated_ms, stale_ms);
}

inline uint32_t saturated_add(uint32_t value, uint32_t increment) {
  const uint32_t maximum = std::numeric_limits<uint32_t>::max();
  return increment > maximum - value ? maximum : value + increment;
}

inline Output step(const Input& input, const Config& config, State state) {
  const bool limits_valid = std::isfinite(config.soft_limit_w) && std::isfinite(config.peak_limit_w) &&
                            std::isfinite(config.recover_limit_w) && config.recover_limit_w >= 0.0f &&
                            config.recover_limit_w <= config.soft_limit_w && config.soft_limit_w <= config.peak_limit_w;
  const bool measurement_valid = limits_valid && valid(input.hp1, input.now_ms, config.measurement_stale_ms) &&
                                 (!config.duo || valid(input.hp2, input.now_ms, config.measurement_stale_ms)) &&
                                 input.total_power_valid && std::isfinite(input.total_power_w);
  if (!measurement_valid)
    return {{std::clamp(config.fallback_cap_f, 0, std::max(0, config.max_cap_f)), 0, 0, 0}, false};

  state.over_peak_s = input.total_power_w > config.peak_limit_w ? saturated_add(state.over_peak_s, config.tick_s) : 0;
  state.over_soft_s = input.total_power_w > config.soft_limit_w ? saturated_add(state.over_soft_s, config.tick_s) : 0;
  state.under_ok_s = input.total_power_w < config.recover_limit_w ? saturated_add(state.under_ok_s, config.tick_s) : 0;
  if (state.over_peak_s >= config.peak_trip_s) {
    state.cap_f -= 2;
    state.over_peak_s = state.over_soft_s = state.under_ok_s = 0;
  } else if (state.over_soft_s >= config.soft_trip_s) {
    --state.cap_f;
    state.over_soft_s = state.under_ok_s = 0;
  }
  if (state.under_ok_s >= config.recover_s) {
    ++state.cap_f;
    state.under_ok_s = 0;
  }
  state.cap_f = std::clamp(state.cap_f, 0, std::max(0, config.max_cap_f));
  return {state, true};
}

}  // namespace oq_supervisory_power
