#pragma once

#include "oq_cic_register_logic.h"

#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
namespace oq_cic {

inline uint16_t read(uint16_t value) {
  id(cic_compatibility_last_request_ms) = millis();
  return gated(id(cic_compatibility_enable).state, value);
}

inline uint16_t constant(uint16_t value) {
  id(cic_compatibility_last_request_ms) = millis();
  return value;
}

inline uint16_t read_hp1_status_flags() {
  return read(status_flags(id(hp1_fan_low_speed_mode).state, id(hp1_bottom_plate_heater).state,
                           id(hp1_crankcase_heater).state, id(hp1_fan_defrost_mode).state,
                           id(hp1_fan_high_speed_mode).state, id(hp1_4_way_valve).state, id(hp1_pump_relay).state));
}

#if OQ_TOPOLOGY_DUO
inline uint16_t read_hp2_status_flags() {
  return read(status_flags(id(hp2_fan_low_speed_mode).state, id(hp2_bottom_plate_heater).state,
                           id(hp2_crankcase_heater).state, id(hp2_fan_defrost_mode).state,
                           id(hp2_fan_high_speed_mode).state, id(hp2_4_way_valve).state, id(hp2_pump_relay).state));
}
#endif

}  // namespace oq_cic
#endif
