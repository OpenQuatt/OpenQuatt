#pragma once
#include <math.h>
#include <stdint.h>
#include <string>
#include "oq_cooling_safety_policy.h"
#include "oq_sensor_source_runtime.h"
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
  BleGuard live_ble_guard(bool include_published = true) const {
#ifdef USE_OPENQUATT_BLE
    const int mode = dew_source_mode_code();
    if (mode == 3 || mode == 5) {
      const auto ble = esphome::openquatt_ble::dew_point(static_cast<uint32_t>(millis()));
      auto guard = ble_guard(mode, ble.value, ble.required, id(cooling_dew_point_selected).get_raw_state());
      if (include_published && finite_value(guard.dew))
        guard.dew = fmaxf(guard.dew, id(cooling_dew_point_selected).state);
      return guard;
    }
#endif
    return {};
  }
  float live_ble_floor(const BleGuard& guard) {
    const bool approximate = !finite_value(guard.dew) && without_dew_point_enabled();
    return guard.floor(safety_margin_selected(), without_dew_point_user_responsibility_enabled(),
                       id(cooling_min_supply_temp).state, approximate, approximate ? fallback_min_supply_temp() : NAN);
  }
  bool ble_cooling_must_stop() {
    const auto guard = live_ble_guard();
    if (!guard.active) return false;
    return guard.must_stop(live_ble_floor(guard), id(oq_system_supply_temp).state,
                           without_dew_point_user_responsibility_enabled());
  }
  bool ble_room_input_ready() const {
#ifdef USE_OPENQUATT_BLE
    if (id(cooling_room_request_required).state && id(room_temp_source).current_option() == "BLE")
      return finite_value(oq_sensor_source::runtime().room_control_temperature());
#endif
    return true;
  }
  bool dew_available() const {
    const auto guard = live_ble_guard();
    return guard.active ? finite_value(guard.dew) : dew_selected_valid();
  }
  bool fallback_active() {
    const auto guard = live_ble_guard();
    if (guard.active)
      return !finite_value(guard.dew) && without_dew_point_enabled() && finite_value(fallback_min_supply_temp());
    return !dew_selected_valid() && id(cooling_without_dew_point_enabled).state && fallback_min_valid();
  }
  bool permitted_core() {
    const auto guard = live_ble_guard();
    if (guard.active) return finite_value(live_ble_floor(guard));
    return core_permitted(id(cooling_without_dew_point_user_responsibility_enabled).state,
                          id(cooling_min_supply_temp).has_state() && finite_value(id(cooling_min_supply_temp).state),
                          id(cooling_dew_point_available).state && dew_selected_valid(),
                          id(minimum_safe_supply_temp).has_state() && finite_value(id(minimum_safe_supply_temp).state),
                          id(cooling_without_dew_point_enabled).state, fallback_min_valid());
  }
  bool request_active() {
    const RoomRequestInput input{id(cooling_room_request_required).state,
                                 id(cooling_enable_selected).has_state(),
                                 id(cooling_enable_selected).state,
                                 room_valid(),
                                 setpoint_valid(),
                                 id(room_temp_selected).state,
                                 id(room_setpoint_selected).state,
                                 id(cooling_request_on_delta).state,
                                 id(cooling_request_off_delta).state};
    return update_room_request(input, request_latched_);
  }
  bool permitted(float min_flow_lph) {
    return flow_permitted(id(cooling_permitted_core).has_state() && id(cooling_permitted_core).state, flow_valid(),
                          id(flow_rate_selected).state, min_flow_lph, id(oq_lowflow_fault_active));
  }
  const char* guard_mode() {
    if (id(cooling_without_dew_point_user_responsibility_enabled).state) return "User responsibility";
    if (dew_available()) {
      if (dew_selection_.last_route == 5 && live_ble_guard().active) return "Dew point (BLE)";
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
        (!id(cooling_min_supply_temp).has_state() || !finite_value(id(cooling_min_supply_temp).state))) {
      return "Cooling minimum unavailable";
    }
    if (!id(cooling_dew_point_available).state && !id(cooling_without_dew_point_user_responsibility_enabled).state) {
      if (live_ble_guard().active && !without_dew_point_enabled()) return "BLE dew point unavailable";
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
  float safety_margin_selected() { return clamp_finite(id(cooling_safety_margin).state, 0.0f, 4.0f); }
  float selected_dew_point(uint32_t now_ms, uint32_t hold_ms, uint32_t ha_stale_s) {
    const int source_mode_code = dew_source_mode_code();
    DewPointSources sources{id(cooling_dew_point_ha).state,
                            id(api_input_cooling_dew_point).state,
                            id(mqtt_cooling_dew_point).state,
                            ha_dew_valid(now_ms, ha_stale_s),
                            api_dew_valid(),
                            mqtt_dew_valid()};
#ifdef USE_OPENQUATT_BLE
    if (source_mode_code == 3 || source_mode_code == 5) {
      const auto ble = esphome::openquatt_ble::dew_point(now_ms);
      sources.ble = ble.value;
      sources.ble_required = ble.required;
    }
#endif
    const auto selected = select_dew_point(source_mode_code, sources, now_ms, hold_ms, dew_selection_);
    id(oq_cooling_dew_point_selected_hold_active) = selected.held;
    return selected.value;
  }
  float minimum_safe_supply() {
    const auto guard = live_ble_guard();
    if (guard.active) return guard.dew + safety_margin_selected();
    return dew_selected_valid() && id(cooling_safety_margin_selected).has_state() &&
                   finite_value(id(cooling_safety_margin_selected).state)
               ? id(cooling_dew_point_selected).state + id(cooling_safety_margin_selected).state
               : NAN;
  }
  float fallback_night_min_outdoor_temp() {
    if (!id(oq_time).now().is_valid()) return NAN;
    const auto now = id(oq_time).now();
    if (now.hour < 6 && fallback_night_window_day_key_ >= 0 && finite_value(fallback_night_min_current_c_)) {
      return fallback_night_min_current_c_;
    }
    return finite_value(id(oq_cooling_fallback_night_min_last_c)) ? id(oq_cooling_fallback_night_min_last_c) : NAN;
  }
  float fallback_min_supply_temp() {
    return fallback_minimum_supply(outside_valid(), outside_safety_temperature(), fallback_night_min_valid(),
                                   id(cooling_fallback_night_min_outdoor_temp).state, room_valid(),
                                   id(room_temp_selected).state);
  }
  float effective_min_supply_temp() {
    const auto guard = live_ble_guard();
    if (guard.active) return live_ble_floor(guard);
    if (id(cooling_without_dew_point_user_responsibility_enabled).state && id(cooling_min_supply_temp).has_state() &&
        finite_value(id(cooling_min_supply_temp).state)) {
      return id(cooling_min_supply_temp).state;
    }
    if (id(minimum_safe_supply_temp).has_state() && finite_value(id(minimum_safe_supply_temp).state)) {
      return id(minimum_safe_supply_temp).state;
    }
    return id(cooling_fallback_active).state && fallback_min_valid() ? id(cooling_fallback_min_supply_temp).state : NAN;
  }
  void update_fallback_night_min() {
    if (!id(oq_time).now().is_valid() || !outside_valid()) return;
    const auto now = id(oq_time).now();
    const int day_key = (now.year * 10000) + (now.month * 100) + now.day_of_month;
    if (now.hour < 6) {
      if (fallback_night_window_day_key_ != day_key || !finite_value(fallback_night_min_current_c_)) {
        fallback_night_window_day_key_ = day_key;
        fallback_night_min_current_c_ = outside_safety_temperature();
      } else {
        fallback_night_min_current_c_ = fminf(fallback_night_min_current_c_, outside_safety_temperature());
      }
      return;
    }
    if (fallback_night_window_day_key_ >= 0 &&
        fallback_night_window_day_key_ != id(oq_cooling_fallback_night_min_last_day_key) &&
        finite_value(fallback_night_min_current_c_)) {
      id(oq_cooling_fallback_night_min_last_c) = fallback_night_min_current_c_;
      id(oq_cooling_fallback_night_min_last_day_key) = fallback_night_window_day_key_;
    }
  }

 private:
  bool request_latched_ = false;
  float fallback_night_min_current_c_ = NAN;
  int fallback_night_window_day_key_ = -1;
  DewPointSelectionState dew_selection_;
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
    if (source == "BLE") return 5;
    return 3;
  }
  const char* fallback_block_reason() {
    if (!id(cooling_without_dew_point_enabled).state) return "No dew point source";
    if (!outside_valid()) return "Fallback outside temperature unavailable";
    if (outside_safety_temperature() < 20.0f)
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
    return id(cooling_dew_point_selected).has_state() && finite_value(id(cooling_dew_point_selected).state);
  }
  bool fallback_min_valid() const {
    return id(cooling_fallback_min_supply_temp).has_state() && finite_value(id(cooling_fallback_min_supply_temp).state);
  }
  bool fallback_night_min_valid() const {
    return id(cooling_fallback_night_min_outdoor_temp).has_state() &&
           finite_value(id(cooling_fallback_night_min_outdoor_temp).state);
  }
  static float outside_safety_temperature() { return oq_sensor_source::runtime().outside_safety_temperature(); }

  bool outside_valid() const { return finite_value(outside_safety_temperature()); }
  bool room_valid() const { return finite_value(oq_sensor_source::runtime().room_control_temperature()); }
  bool setpoint_valid() const {
    return id(room_setpoint_selected).has_state() && finite_value(id(room_setpoint_selected).state);
  }
  bool flow_valid() const { return id(flow_rate_selected).has_state() && finite_value(id(flow_rate_selected).state); }
  // Live HA input: shares the central HA ingress heartbeat, so a constant
  // dew point stays usable while the heartbeat arrives and goes stale when
  // the HA -> ESPHome link drops (issue #698).
  bool ha_dew_valid(uint32_t now_ms, uint32_t ha_stale_s) const {
    return oq_sensor_source::runtime().ha_live_valid(id(cooling_dew_point_valid_ha), id(cooling_dew_point_ha), now_ms,
                                                     ha_stale_s);
  }
  bool mqtt_dew_valid() const {
    return id(mqtt_cooling_dew_point_valid).has_state() && id(mqtt_cooling_dew_point_valid).state &&
           id(mqtt_cooling_dew_point).has_state() && finite_value(id(mqtt_cooling_dew_point).state);
  }
  bool api_dew_valid() const {
    return id(api_input_cooling_dew_point_valid).has_state() && id(api_input_cooling_dew_point_valid).state &&
           id(api_input_cooling_dew_point).has_state() && finite_value(id(api_input_cooling_dew_point).state);
  }
};
inline CoolingSafetyRuntime& runtime() {
  static CoolingSafetyRuntime instance;
  return instance;
}
}  // namespace oq_cooling_safety
