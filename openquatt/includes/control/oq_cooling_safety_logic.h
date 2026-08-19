#pragma once

#include <math.h>
#include <stdint.h>
#include <string>

namespace oq_cooling_safety {

class CoolingSafetyRuntime {
 public:
  bool without_dew_point_enabled() {
    return id(cooling_without_dew_point_mode).has_state() &&
           option_allows_fallback(id(cooling_without_dew_point_mode).current_option());
  }
  bool without_dew_point_user_responsibility_enabled() {
    return id(cooling_without_dew_point_mode).has_state() &&
           id(cooling_without_dew_point_mode).current_option() == "Allow without dew point, user responsibility";
  }
  bool fallback_active() {
    return !dew_selected_valid() && id(cooling_without_dew_point_enabled).state && fallback_min_valid();
  }
  bool permitted_core() {
    const bool dew_ok = id(cooling_dew_point_available).state && dew_selected_valid() &&
                        id(minimum_safe_supply_temp).has_state() && finite(id(minimum_safe_supply_temp).state);
    const bool fallback_ok = id(cooling_without_dew_point_enabled).state && fallback_min_valid();
    const bool user_ok = id(cooling_without_dew_point_user_responsibility_enabled).state &&
                         id(cooling_min_supply_temp).has_state() && finite(id(cooling_min_supply_temp).state);
    return id(cooling_without_dew_point_user_responsibility_enabled).state ? user_ok
                                                                           : (dew_ok || fallback_ok || user_ok);
  }
  bool request_active() {
    if (!id(cooling_room_request_required).state) {
      id(oq_cooling_request_latched) = false;
      return id(cooling_enable_selected).has_state() && id(cooling_enable_selected).state;
    }
    if (!room_valid() || !setpoint_valid()) {
      id(oq_cooling_request_latched) = false;
      return false;
    }
    float on_delta_c = id(cooling_request_on_delta).state;
    float off_delta_c = id(cooling_request_off_delta).state;
    if (!finite(on_delta_c) || on_delta_c < 0.0f) on_delta_c = 0.4f;
    if (!finite(off_delta_c) || off_delta_c < 0.0f) off_delta_c = 0.1f;
    if (off_delta_c > on_delta_c) off_delta_c = on_delta_c;
    const float room_c = id(room_temp_selected).state;
    const float setpoint_c = id(room_setpoint_selected).state;
    if (!id(oq_cooling_request_latched) && room_c > (setpoint_c + on_delta_c)) {
      id(oq_cooling_request_latched) = true;
    } else if (id(oq_cooling_request_latched) && room_c < (setpoint_c + off_delta_c)) {
      id(oq_cooling_request_latched) = false;
    }
    return id(oq_cooling_request_latched);
  }
  bool permitted(float min_flow_lph) {
    return id(cooling_permitted_core).has_state() && id(cooling_permitted_core).state && flow_valid() &&
           id(flow_rate_selected).state >= min_flow_lph && !id(oq_lowflow_fault_active);
  }
  const char* guard_mode() {
    if (id(cooling_without_dew_point_user_responsibility_enabled).state) return "User responsibility";
    if (id(cooling_dew_point_available).state) {
      if (id(cooling_dew_point_source).has_state()) {
        if (id(cooling_dew_point_source).current_option() == "MQTT") return "Dew point (MQTT)";
        if (id(cooling_dew_point_source).current_option() == "API input") return "Dew point (API input)";
        if (id(cooling_dew_point_source).current_option() == "Home Assistant") return "Dew point (HA)";
      }
      return "Dew point";
    }
    if (!id(cooling_without_dew_point_enabled).state) return "Blocked";
    return fallback_min_valid() ? "Fallback" : "Fallback blocked";
  }
  const char* block_reason(float min_flow_lph) {
    if (!id(cooling_enable_selected).has_state() || !id(cooling_enable_selected).state) {
      return "Cooling disabled";
    }
    if (!id(oq_enabled).state) return "OpenQuatt paused";
    if (id(cooling_without_dew_point_user_responsibility_enabled).state &&
        (!id(cooling_min_supply_temp).has_state() || !finite(id(cooling_min_supply_temp).state))) {
      return "Cooling minimum unavailable";
    }
    if (!id(cooling_dew_point_available).state && !id(cooling_without_dew_point_user_responsibility_enabled).state) {
      return fallback_block_reason();
    }
    if (!id(cooling_permitted_core).has_state() || !id(cooling_permitted_core).state) {
      return "Safe supply floor unavailable";
    }
    if (!id(cooling_request_active).has_state() || !id(cooling_request_active).state) {
      return "Cooling enabled, waiting for room temperature above cooling setpoint";
    }
    if (!flow_valid()) return "Flow unavailable";
    if (id(oq_lowflow_fault_active) || id(flow_rate_selected).state < min_flow_lph) return "Flow too low";
    return "Ready";
  }
  float safety_margin_selected() { return clamp_float(id(cooling_safety_margin).state, 0.0f, 4.0f); }
  float selected_dew_point(uint32_t now_ms, uint32_t hold_ms) {
    const int source_mode_code = dew_source_mode_code();
    const bool ha_ok = ha_dew_valid();
    const bool api_ok = api_dew_valid();
    const bool mqtt_ok = mqtt_dew_valid();
    float selected_c = NAN;
    int selected_route_code = 0;
    if (source_mode_code == 2 && mqtt_ok) {
      selected_c = id(mqtt_cooling_dew_point).state;
      selected_route_code = 2;
    }
    if (source_mode_code == 4 && api_ok) {
      selected_c = id(api_input_cooling_dew_point).state;
      selected_route_code = 4;
    }
    if (source_mode_code == 1 && ha_ok) {
      selected_c = id(cooling_dew_point_ha).state;
      selected_route_code = 1;
    }
    if (source_mode_code == 3) {
      if (mqtt_ok) {
        selected_c = id(mqtt_cooling_dew_point).state;
        selected_route_code = 2;
      }
      if (api_ok && (!finite(selected_c) || id(api_input_cooling_dew_point).state > selected_c)) {
        selected_c = id(api_input_cooling_dew_point).state;
        selected_route_code = 4;
      }
      if (ha_ok && (!finite(selected_c) || id(cooling_dew_point_ha).state > selected_c)) {
        selected_c = id(cooling_dew_point_ha).state;
        selected_route_code = 1;
      }
    }
    if (finite(selected_c)) {
      id(oq_cooling_dew_point_selected_last_valid_c) = selected_c;
      id(oq_cooling_dew_point_selected_last_valid_ms) = now_ms;
      id(oq_cooling_dew_point_selected_last_mode_code) = source_mode_code;
      id(oq_cooling_dew_point_selected_last_route_code) = selected_route_code;
      id(oq_cooling_dew_point_selected_hold_active) = false;
      return selected_c;
    }
    id(oq_cooling_dew_point_selected_hold_active) = false;
    if (hold_ms > 0 && id(oq_cooling_dew_point_selected_last_route_code) == 1 &&
        id(oq_cooling_dew_point_selected_last_valid_ms) > 0 &&
        id(oq_cooling_dew_point_selected_last_mode_code) == source_mode_code &&
        (uint32_t)(now_ms - id(oq_cooling_dew_point_selected_last_valid_ms)) < hold_ms &&
        finite(id(oq_cooling_dew_point_selected_last_valid_c))) {
      id(oq_cooling_dew_point_selected_hold_active) = true;
      return id(oq_cooling_dew_point_selected_last_valid_c);
    }
    return NAN;
  }
  float minimum_safe_supply() {
    return dew_selected_valid() && id(cooling_safety_margin_selected).has_state() &&
                   finite(id(cooling_safety_margin_selected).state)
               ? id(cooling_dew_point_selected).state + id(cooling_safety_margin_selected).state
               : NAN;
  }
  float fallback_night_min_outdoor_temp() {
    if (!id(oq_time).now().is_valid()) return NAN;
    const auto now = id(oq_time).now();
    if (now.hour < 6 && id(oq_cooling_fallback_night_window_day_key) >= 0 &&
        finite(id(oq_cooling_fallback_night_min_current_c))) {
      return id(oq_cooling_fallback_night_min_current_c);
    }
    return finite(id(oq_cooling_fallback_night_min_last_c)) ? id(oq_cooling_fallback_night_min_last_c) : NAN;
  }
  float fallback_min_supply_temp() {
    if (!outside_valid() || id(outside_temp_selected).state < 20.0f) return NAN;
    const float outside_c = id(outside_temp_selected).state;
    float fallback_floor_c = clamp_float(19.0f + ((outside_c - 20.0f) * 0.25f), 19.0f, 22.0f);
    if (fallback_night_min_valid()) {
      const float night_min_c = id(cooling_fallback_night_min_outdoor_temp).state;
      if (night_min_c >= 20.0f)
        fallback_floor_c += 1.5f;
      else if (night_min_c >= 19.0f)
        fallback_floor_c += 1.0f;
      else if (night_min_c >= 18.0f)
        fallback_floor_c += 0.5f;
    }
    if (room_valid()) {
      const float room_usability_floor_c = id(room_temp_selected).state - 1.0f;
      if (room_usability_floor_c >= 20.0f) fallback_floor_c = fminf(fallback_floor_c, room_usability_floor_c);
    }
    return fallback_floor_c;
  }
  float effective_min_supply_temp() {
    if (id(cooling_without_dew_point_user_responsibility_enabled).state && id(cooling_min_supply_temp).has_state() &&
        finite(id(cooling_min_supply_temp).state)) {
      return id(cooling_min_supply_temp).state;
    }
    if (id(minimum_safe_supply_temp).has_state() && finite(id(minimum_safe_supply_temp).state)) {
      return id(minimum_safe_supply_temp).state;
    }
    return id(cooling_fallback_active).state && fallback_min_valid() ? id(cooling_fallback_min_supply_temp).state : NAN;
  }
  void update_fallback_night_min() {
    if (!id(oq_time).now().is_valid() || !outside_valid()) return;
    const auto now = id(oq_time).now();
    const int day_key = (now.year * 10000) + (now.month * 100) + now.day_of_month;
    if (now.hour < 6) {
      if (id(oq_cooling_fallback_night_window_day_key) != day_key ||
          !finite(id(oq_cooling_fallback_night_min_current_c))) {
        id(oq_cooling_fallback_night_window_day_key) = day_key;
        id(oq_cooling_fallback_night_min_current_c) = id(outside_temp_selected).state;
      } else {
        id(oq_cooling_fallback_night_min_current_c) =
            fminf(id(oq_cooling_fallback_night_min_current_c), id(outside_temp_selected).state);
      }
      return;
    }
    if (id(oq_cooling_fallback_night_window_day_key) >= 0 &&
        id(oq_cooling_fallback_night_window_day_key) != id(oq_cooling_fallback_night_min_last_day_key) &&
        finite(id(oq_cooling_fallback_night_min_current_c))) {
      id(oq_cooling_fallback_night_min_last_c) = id(oq_cooling_fallback_night_min_current_c);
      id(oq_cooling_fallback_night_min_last_day_key) = id(oq_cooling_fallback_night_window_day_key);
    }
  }

 private:
  bool finite(float value) const { return !isnan(value); }
  float clamp_float(float value, float min_value, float max_value) const {
    if (value < min_value) return min_value;
    return value > max_value ? max_value : value;
  }
  bool option_allows_fallback(const std::string& option) const {
    return option == "Allow without dew point, use dew point approximation" ||
           option == "Allow without dew point, use fallback" || option == "Allow without dew point";
  }
  int dew_source_mode_code() const {
    if (!id(cooling_dew_point_source).has_state()) return 3;
    const auto source = id(cooling_dew_point_source).current_option();
    if (source == "Home Assistant") return 1;
    if (source == "MQTT") return 2;
    if (source == "API input") return 4;
    return 3;
  }
  const char* fallback_block_reason() {
    if (!id(cooling_without_dew_point_enabled).state) return "No dew point source";
    if (!outside_valid()) return "Fallback outside temperature unavailable";
    if (id(outside_temp_selected).state < 20.0f)
      return "Fallback blocked below 20\xC2\xB0"
             "C outdoor";
    if (!fallback_min_valid()) return "Fallback minimum unavailable";
    if (fallback_night_min_valid() && id(cooling_fallback_night_min_outdoor_temp).state >= 18.0f) {
      if (id(cooling_fallback_night_min_outdoor_temp).state >= 20.0f) {
        return "Fallback active (+1.5\xC2\xB0"
               "C tropical night)";
      }
      if (id(cooling_fallback_night_min_outdoor_temp).state >= 19.0f) {
        return "Fallback active (+1.0\xC2\xB0"
               "C very warm night)";
      }
      return "Fallback active (+0.5\xC2\xB0"
             "C warm night)";
    }
    return "Fallback active";
  }
  bool dew_selected_valid() const {
    return id(cooling_dew_point_selected).has_state() && finite(id(cooling_dew_point_selected).state);
  }
  bool fallback_min_valid() const {
    return id(cooling_fallback_min_supply_temp).has_state() && finite(id(cooling_fallback_min_supply_temp).state);
  }
  bool fallback_night_min_valid() const {
    return id(cooling_fallback_night_min_outdoor_temp).has_state() &&
           finite(id(cooling_fallback_night_min_outdoor_temp).state);
  }
  bool outside_valid() const {
    return id(outside_temp_selected).has_state() && finite(id(outside_temp_selected).state);
  }
  bool room_valid() const { return id(room_temp_selected).has_state() && finite(id(room_temp_selected).state); }
  bool setpoint_valid() const {
    return id(room_setpoint_selected).has_state() && finite(id(room_setpoint_selected).state);
  }
  bool flow_valid() const { return id(flow_rate_selected).has_state() && finite(id(flow_rate_selected).state); }
  bool ha_dew_valid() const {
    return id(cooling_dew_point_valid_ha).has_state() && id(cooling_dew_point_valid_ha).state &&
           id(cooling_dew_point_ha).has_state() && finite(id(cooling_dew_point_ha).state);
  }
  bool mqtt_dew_valid() const {
    return id(mqtt_cooling_dew_point_valid).has_state() && id(mqtt_cooling_dew_point_valid).state &&
           id(mqtt_cooling_dew_point).has_state() && finite(id(mqtt_cooling_dew_point).state);
  }
  bool api_dew_valid() const {
    return id(api_input_cooling_dew_point_valid).has_state() && id(api_input_cooling_dew_point_valid).state &&
           id(api_input_cooling_dew_point).has_state() && finite(id(api_input_cooling_dew_point).state);
  }
};
inline CoolingSafetyRuntime& runtime() {
  static CoolingSafetyRuntime instance;
  return instance;
}

}  // namespace oq_cooling_safety
