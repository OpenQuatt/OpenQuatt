#pragma once

#include <cmath>
#include <cstdint>

#include "oq_supervisory_power_limiter_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_supervisory_power_runtime {

#if OQ_TOPOLOGY_DUO
#define OQ_POWER_SECONDARY_ID(suffix) id(hp2_##suffix)
#else
#define OQ_POWER_SECONDARY_ID(suffix) id(hp1_##suffix)
#endif

struct TickConfig {
  uint32_t now_ms;
  uint32_t tick_s;
  float v1_current_a;
  float v2_current_a;
  float minimum_current_a;
  float mains_voltage_v;
  uint32_t peak_trip_s;
  uint32_t soft_trip_s;
  uint32_t recover_s;
  uint32_t measurement_stale_s;
  int fallback_cap_f;
  int max_cap_f;
};

inline bool generation_v2() {
#if OQ_TOPOLOGY_DUO
  return id(hp_generation).has_state() && id(hp_generation).current_option() == "V2";
#else
  return false;
#endif
}

inline float maximum_current_a(float v1_a, float v2_a) {
  return oq_supervisory_power::maximum_current_a(OQ_TOPOLOGY_DUO, generation_v2(), v1_a, v2_a);
}

inline float configured_current_a(float minimum_a, float v1_a, float v2_a) {
  return oq_supervisory_power::effective_current_a(id(oq_electrical_current_limit_configured_a), minimum_a,
                                                   maximum_current_a(v1_a, v2_a));
}

class Runtime {
 public:
  void tick(const TickConfig& tick) {
    const float current_a = configured_current_a(tick.minimum_current_a, tick.v1_current_a, tick.v2_current_a);
    const auto limits = oq_supervisory_power::thresholds(current_a, tick.mains_voltage_v);
    const oq_supervisory_power::Config config{
        OQ_TOPOLOGY_DUO,
        tick.tick_s,
        tick.peak_trip_s,
        tick.soft_trip_s,
        tick.recover_s,
        oq_supervisory_power::seconds_to_ms(tick.measurement_stale_s),
        oq_supervisory_power::fallback_cap(OQ_TOPOLOGY_DUO, current_a, tick.v1_current_a, tick.fallback_cap_f,
                                           tick.max_cap_f),
        tick.max_cap_f,
        limits.soft_w,
        limits.peak_w,
        limits.recover_w,
    };
    const oq_supervisory_power::Input input{
        tick.now_ms,
        this->capture_(id(hp1_is_online), id(hp1_voltage).has_state(), id(hp1_voltage).state,
                       id(hp1_current).has_state(), id(hp1_current).state, id(hp1_voltage_last_update_ms),
                       id(hp1_current_last_update_ms)),
        this->capture_(OQ_POWER_SECONDARY_ID(is_online), OQ_POWER_SECONDARY_ID(voltage).has_state(),
                       OQ_POWER_SECONDARY_ID(voltage).state, OQ_POWER_SECONDARY_ID(current).has_state(),
                       OQ_POWER_SECONDARY_ID(current).state, OQ_POWER_SECONDARY_ID(voltage_last_update_ms),
                       OQ_POWER_SECONDARY_ID(current_last_update_ms)),
        id(oq_total_power_input).has_state(),
        id(oq_total_power_input).state,
    };
    if (!this->initialized_) {
      this->state_.cap_f = id(oq_power_cap_f);
      this->initialized_ = true;
    }
    const auto output = oq_supervisory_power::step(input, config, this->state_);
    this->state_ = output.state;
    id(oq_power_cap_f) = output.state.cap_f;
    id(oq_power_limit_soft_w) = limits.soft_w;
    id(oq_power_limit_peak_w) = limits.peak_w;
    id(oq_power_limit_recover_w) = limits.recover_w;
  }

 private:
  static oq_supervisory_power::HpMeasurement capture_(bool online, bool voltage_has_state, float voltage,
                                                      bool current_has_state, float current,
                                                      uint32_t voltage_updated_ms, uint32_t current_updated_ms) {
    return {online, voltage_has_state && std::isfinite(voltage), current_has_state && std::isfinite(current),
            voltage_updated_ms, current_updated_ms};
  }

  bool initialized_ = false;
  oq_supervisory_power::State state_;
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

#undef OQ_POWER_SECONDARY_ID
}  // namespace oq_supervisory_power_runtime
#endif
