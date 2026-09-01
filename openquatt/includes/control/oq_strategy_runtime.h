#pragma once

#include <cmath>
#include <cstdint>

#include "oq_heating_curve_runtime.h"
#include "oq_power_house_runtime.h"
#include "oq_strategy_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_strategy_runtime {

class Runtime {
 public:
  void switch_heating_mode(bool heating_curve) {
    id(oq_heat_mode_code) = heating_curve ? 1 : 0;
    this->reset_shared_();
    oq_heating_curve_runtime::runtime().reset_profile();
    oq_power_house_runtime::runtime().reset();
    id(oq_strategy_phase_text).publish_state("idle");
    ESP_LOGD("quatt.strategy", "strategy_switch");
  }

  void tick() {
    id(oq_heat_mode_code) = id(oq_heat_control_mode).active_index().value_or(0) == 1 ? 1 : 0;
    if (id(oq_control_mode_code) == 5)
      id(oq_strategy_active_code) = 1;
    else
      id(oq_strategy_active_code) = id(oq_heat_mode_code) == 1 ? 2 : 3;
    id(oq_strategy_water_limit_factor) = id(oq_water_temp_limit_factor);
    id(oq_strategy_water_trip_active) = id(oq_water_temp_trip_active);
    id(oq_strategy_water_hard_trip_active) = id(oq_water_temp_hard_trip_active);
  }

  float local_outside_temperature(uint32_t stale_s) const {
    return oq_strategy::aggregate_local_outside({
        static_cast<uint32_t>(millis()),
        stale_s * 1000UL,
#if OQ_TOPOLOGY_DUO
        true,
#else
        false,
#endif
        id(hp1_outside_temp).state,
#if OQ_TOPOLOGY_DUO
        id(hp2_outside_temp).state,
#else
        NAN,
#endif
        id(hp1_working_mode).state,
#if OQ_TOPOLOGY_DUO
        id(hp2_working_mode).state,
#else
        NAN,
#endif
        id(hp1_outside_temp_last_change_ms),
#if OQ_TOPOLOGY_DUO
        id(hp2_outside_temp_last_change_ms),
#else
        0,
#endif
        id(hp1_outside_temp_activity_ms),
#if OQ_TOPOLOGY_DUO
        id(hp2_outside_temp_activity_ms),
#else
        0,
#endif
    });
  }

 private:
  void reset_shared_() {
    id(oq_demand_raw) = 0;
    id(oq_strategy_phase_code) = 0;
    id(oq_strategy_requested_power_w) = NAN;
    id(oq_strategy_supply_target_temp) = NAN;
    id(oq_strategy_heat_request_active) = false;
    id(oq_strategy_output_valid) = false;
    id(oq_strategy_output_source_code) = 0;
    id(oq_strategy_output_updated_ms) = 0;
    id(oq_strategy_hp_expected_power_w) = NAN;
    id(oq_strategy_hp_max_power_w) = NAN;
    id(oq_strategy_hp_saturated) = false;
  }
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_strategy_runtime
#endif
