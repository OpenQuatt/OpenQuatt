#pragma once

#include "oq_thermal_limits_logic.h"

namespace oq_thermal_limits_runtime {

class Runtime {
 public:
  void tick() {
    oq_thermal_limits::update(state_, {(uint32_t)millis(), id(water_supply_temp_selected).state,
                                       id(max_water_temp_limit_c).state, id(oq_control_mode_code)});
    id(oq_water_temp_limit_factor) = state_.limit_factor;
    id(oq_water_temp_boiler_inhibit_active) = state_.boiler_inhibit;
    id(oq_water_temp_trip_active) = state_.trip;
    id(oq_water_temp_hard_trip_active) = state_.hard_trip;
  }

 private:
  oq_thermal_limits::State state_{};
};

inline Runtime& runtime() {
  static Runtime value;
  return value;
}

}  // namespace oq_thermal_limits_runtime
