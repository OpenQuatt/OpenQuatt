#pragma once

#include <cmath>
#include <cstdint>

#include "oq_heat_intent_logic.h"

namespace oq_heat_intent_runtime {

inline bool room_temperature_fresh(bool ot_fresh) {
  if (!id(room_temp_source).has_state()) return false;
  const auto source = id(room_temp_source).current_option();
  return (source == "HA input" && id(room_temp_valid_ha).state && id(thermostat_room_temp_ha).has_state() &&
          std::isfinite(id(thermostat_room_temp_ha).state) && !id(oq_room_temp_selected_hold_active)) ||
         (source == "OT thermostat" && ot_fresh && id(ot_thermostat_room_temp).has_state() &&
          std::isfinite(id(ot_thermostat_room_temp).state)) ||
         (source == "CIC" && id(feed_ok).has_state() && id(feed_ok).state && id(cic_data_stale).has_state() &&
          !id(cic_data_stale).state && id(cic_room_temp).has_state() && std::isfinite(id(cic_room_temp).state)) ||
         (source == "API input" && id(api_input_room_temperature_valid).has_state() &&
          id(api_input_room_temperature_valid).state && id(api_input_room_temperature).has_state() &&
          std::isfinite(id(api_input_room_temperature).state)) ||
         (source == "MQTT" && id(mqtt_room_temperature_valid).has_state() && id(mqtt_room_temperature_valid).state &&
          id(mqtt_room_temperature).has_state() && std::isfinite(id(mqtt_room_temperature).state));
}

inline bool room_setpoint_fresh(bool ot_fresh) {
  if (!id(room_setpoint_source).has_state()) return false;
  const auto source = id(room_setpoint_source).current_option();
  return (source == "HA input" && id(room_setpoint_valid_ha).state && id(thermostat_setpoint_ha).has_state() &&
          std::isfinite(id(thermostat_setpoint_ha).state) && !id(oq_room_setpoint_selected_hold_active)) ||
         (source == "OT thermostat" && ot_fresh && id(ot_thermostat_room_setpoint).has_state() &&
          std::isfinite(id(ot_thermostat_room_setpoint).state)) ||
         (source == "CIC" && id(feed_ok).has_state() && id(feed_ok).state && id(cic_data_stale).has_state() &&
          !id(cic_data_stale).state && id(cic_room_setpoint).has_state() &&
          std::isfinite(id(cic_room_setpoint).state)) ||
         (source == "API input" && id(api_input_room_setpoint_valid).has_state() &&
          id(api_input_room_setpoint_valid).state && id(api_input_room_setpoint).has_state() &&
          std::isfinite(id(api_input_room_setpoint).state)) ||
         (source == "MQTT" && id(mqtt_room_setpoint_valid).has_state() && id(mqtt_room_setpoint_valid).state &&
          id(mqtt_room_setpoint).has_state() && std::isfinite(id(mqtt_room_setpoint).state));
}

inline uint8_t setpoint_source_code() {
  if (!id(room_setpoint_source).has_state()) return 0;
  const auto source = id(room_setpoint_source).current_option();
  if (source == "HA input") return 1;
  if (source == "OT thermostat") return 2;
  if (source == "CIC") return 3;
  if (source == "API input") return 4;
  if (source == "MQTT") return 5;
  return 0;
}

inline oq_heat_intent::Decision evaluate(uint32_t now_ms, bool compressor_active, float room_resume_delta_c,
                                         uint32_t room_confirm_ms, bool ot_room_temperature_fresh,
                                         bool ot_room_setpoint_fresh, const oq_heat_intent::State& state) {
  const float room_c = id(room_temp_selected).state;
  const float setpoint_c = id(room_setpoint_selected).state;
  const bool room_fresh = std::isfinite(room_c) && std::isfinite(setpoint_c) &&
                          room_temperature_fresh(ot_room_temperature_fresh) &&
                          room_setpoint_fresh(ot_room_setpoint_fresh);
  return oq_heat_intent::evaluate(
      {now_ms, true, id(heating_enable_valid).has_state() && id(heating_enable_valid).state,
       id(heating_enable_selected).has_state() && id(heating_enable_selected).state, room_fresh,
       room_setpoint_fresh(ot_room_setpoint_fresh), compressor_active, setpoint_source_code(), room_c, setpoint_c,
       room_resume_delta_c, 0.20f, room_confirm_ms},
      state);
}

}  // namespace oq_heat_intent_runtime
