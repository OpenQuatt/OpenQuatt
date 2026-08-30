#pragma once
#include <math.h>
#include <stdint.h>
namespace oq_cooling_safety {
inline bool finite_value(float value) { return isfinite(value); }
inline float clamp_finite(float value, float low, float high) {
  if (!finite_value(value)) return NAN;
  return value < low ? low : (value > high ? high : value);
}
struct DewPointSources {
  float ha = NAN, api = NAN, mqtt = NAN;
  bool ha_valid = false, api_valid = false, mqtt_valid = false;
};
struct DewPointSelectionState {
  float last_valid = NAN;
  uint32_t last_valid_ms = 0;
  int last_mode = 0, last_route = 0;
};
struct DewPointSelection {
  float value = NAN;
  int route = 0;
  bool held = false;
};
inline DewPointSelection select_dew_point(int mode, const DewPointSources& sources, uint32_t now_ms, uint32_t hold_ms,
                                          DewPointSelectionState& state) {
  DewPointSelection out;
  auto take = [&](float value, bool valid, int route) {
    if (valid && finite_value(value) && (!finite_value(out.value) || value > out.value)) {
      out.value = value;
      out.route = route;
    }
  };
  if (mode == 1)
    take(sources.ha, sources.ha_valid, 1);
  else if (mode == 2)
    take(sources.mqtt, sources.mqtt_valid, 2);
  else if (mode == 4)
    take(sources.api, sources.api_valid, 4);
  else {
    take(sources.mqtt, sources.mqtt_valid, 2);
    take(sources.api, sources.api_valid, 4);
    take(sources.ha, sources.ha_valid, 1);
  }
  if (finite_value(out.value)) {
    state = {out.value, now_ms, mode, out.route};
    return out;
  }
  const bool may_hold = hold_ms > 0 && state.last_route == 1 && state.last_mode == mode &&
                        now_ms - state.last_valid_ms < hold_ms && finite_value(state.last_valid);
  if (may_hold) return {state.last_valid, 1, true};
  return out;
}
struct RoomRequestInput {
  bool room_required = true, enabled_valid = false, enabled = false, room_valid = false, setpoint_valid = false;
  float room_c = NAN, setpoint_c = NAN, on_delta_c = NAN, off_delta_c = NAN;
};
inline bool update_room_request(const RoomRequestInput& in, bool& latched) {
  if (!in.room_required) {
    latched = false;
    return in.enabled_valid && in.enabled;
  }
  if (!in.room_valid || !in.setpoint_valid || !finite_value(in.room_c) || !finite_value(in.setpoint_c)) {
    latched = false;
    return false;
  }
  const float on = finite_value(in.on_delta_c) && in.on_delta_c >= 0 ? in.on_delta_c : 0.4f;
  float off = finite_value(in.off_delta_c) && in.off_delta_c >= 0 ? in.off_delta_c : 0.1f;
  if (off > on) off = on;
  if (!latched && in.room_c > in.setpoint_c + on)
    latched = true;
  else if (latched && in.room_c < in.setpoint_c + off)
    latched = false;
  return latched;
}
inline bool core_permitted(bool user_responsibility, bool cooling_min_valid, bool dew_available,
                           bool minimum_safe_valid, bool fallback_enabled, bool fallback_min_valid) {
  return user_responsibility ? cooling_min_valid
                             : ((dew_available && minimum_safe_valid) || (fallback_enabled && fallback_min_valid));
}
inline bool flow_permitted(bool core_valid, bool flow_valid, float flow_lph, float minimum_flow_lph,
                           bool lowflow_fault) {
  return core_valid && flow_valid && finite_value(flow_lph) && finite_value(minimum_flow_lph) &&
         flow_lph >= minimum_flow_lph && !lowflow_fault;
}
inline float fallback_minimum_supply(bool outside_valid, float outside_c, bool night_valid, float night_min_c,
                                     bool room_valid, float room_c) {
  if (!outside_valid || !finite_value(outside_c) || outside_c < 20.0f) return NAN;
  float floor = clamp_finite(19.0f + (outside_c - 20.0f) * 0.25f, 19.0f, 22.0f);
  if (night_valid && finite_value(night_min_c))
    floor += night_min_c >= 20.0f ? 1.5f : (night_min_c >= 19.0f ? 1.0f : (night_min_c >= 18.0f ? 0.5f : 0));
  if (room_valid && finite_value(room_c) && room_c - 1.0f >= 20.0f && room_c - 1.0f < floor) floor = room_c - 1.0f;
  return floor;
}
}  // namespace oq_cooling_safety
