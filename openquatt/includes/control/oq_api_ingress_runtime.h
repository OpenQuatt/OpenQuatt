#pragma once

#include <array>
#include <math.h>
#include <stdint.h>

#include "oq_input_source_logic.h"

namespace oq_api_ingress {

enum class Slot : uint8_t {
  COOLING_DEW_POINT = 0,
  OUTSIDE_TEMPERATURE,
  ROOM_TEMPERATURE,
  ROOM_SETPOINT,
  HEATING_ENABLE,
  COOLING_ENABLE,
  EXTERNAL_HEAT_DEMAND,
  COUNT,
};

struct Config {
  uint32_t cooling_dew_point_stale_s = 0;
  uint32_t outside_temperature_stale_s = 0;
  uint32_t room_temperature_stale_s = 0;
  uint32_t room_setpoint_stale_s = 0;
  uint32_t heating_enable_stale_s = 0;
  uint32_t cooling_enable_stale_s = 0;
  uint32_t external_heat_demand_stale_s = 0;
};

class Runtime {
 public:
  void reset() {
    accept_updates_ = false;
    for (auto& state : states_) state.reset();
    for (uint8_t index = 0; index < slot_count(); index++) publish(static_cast<Slot>(index), {});
  }

  void set_accept_updates(bool accept) { accept_updates_ = accept; }

  void observe(Slot slot, uint32_t now_ms) {
    if (!accept_updates_) return;
    auto& state = states_[index(slot)];
    if (!entity_valid(slot)) {
      state.reset();
      publish(slot, {});
      return;
    }
    state.observe(now_ms);
    publish(slot, {0.0f, true});
  }

  void tick(uint32_t now_ms, const Config& config) {
    for (uint8_t raw_slot = 0; raw_slot < slot_count(); raw_slot++) {
      const auto slot = static_cast<Slot>(raw_slot);
      publish(slot, oq_input_source::evaluate_freshness(states_[raw_slot], now_ms, stale_seconds(slot, config),
                                                        entity_valid(slot)));
    }
  }

 private:
  static constexpr uint8_t kSlotCount = static_cast<uint8_t>(Slot::COUNT);
  static constexpr uint8_t slot_count() { return kSlotCount; }
  static constexpr uint8_t index(Slot slot) { return static_cast<uint8_t>(slot); }

  bool accept_updates_ = false;
  std::array<oq_input_source::TimedState, kSlotCount> states_{};

  static uint32_t stale_seconds(Slot slot, const Config& config) {
    switch (slot) {
      case Slot::COOLING_DEW_POINT:
        return config.cooling_dew_point_stale_s;
      case Slot::OUTSIDE_TEMPERATURE:
        return config.outside_temperature_stale_s;
      case Slot::ROOM_TEMPERATURE:
        return config.room_temperature_stale_s;
      case Slot::ROOM_SETPOINT:
        return config.room_setpoint_stale_s;
      case Slot::HEATING_ENABLE:
        return config.heating_enable_stale_s;
      case Slot::COOLING_ENABLE:
        return config.cooling_enable_stale_s;
      case Slot::EXTERNAL_HEAT_DEMAND:
        return config.external_heat_demand_stale_s;
      default:
        return 0;
    }
  }

  static bool entity_valid(Slot slot) {
    switch (slot) {
      case Slot::COOLING_DEW_POINT:
        return id(api_input_cooling_dew_point).has_state() && isfinite(id(api_input_cooling_dew_point).state);
      case Slot::OUTSIDE_TEMPERATURE:
        return id(api_input_outside_temperature).has_state() && isfinite(id(api_input_outside_temperature).state);
      case Slot::ROOM_TEMPERATURE:
        return id(api_input_room_temperature).has_state() && isfinite(id(api_input_room_temperature).state);
      case Slot::ROOM_SETPOINT:
        return id(api_input_room_setpoint).has_state() && isfinite(id(api_input_room_setpoint).state);
      case Slot::EXTERNAL_HEAT_DEMAND:
        return id(api_input_external_heat_demand).has_state() && isfinite(id(api_input_external_heat_demand).state);
      case Slot::HEATING_ENABLE:
      case Slot::COOLING_ENABLE:
        return true;
      default:
        return false;
    }
  }

  static void publish(Slot slot, const oq_input_source::Freshness& freshness) {
    switch (slot) {
      case Slot::COOLING_DEW_POINT:
        publish_to(id(api_input_cooling_dew_point_age), id(api_input_cooling_dew_point_valid), freshness);
        break;
      case Slot::OUTSIDE_TEMPERATURE:
        publish_to(id(api_input_outside_temperature_age), id(api_input_outside_temperature_valid), freshness);
        break;
      case Slot::ROOM_TEMPERATURE:
        publish_to(id(api_input_room_temperature_age), id(api_input_room_temperature_valid), freshness);
        break;
      case Slot::ROOM_SETPOINT:
        publish_to(id(api_input_room_setpoint_age), id(api_input_room_setpoint_valid), freshness);
        break;
      case Slot::HEATING_ENABLE:
        publish_to(id(api_input_heating_enable_age), id(api_input_heating_enable_valid), freshness);
        break;
      case Slot::COOLING_ENABLE:
        publish_to(id(api_input_cooling_enable_age), id(api_input_cooling_enable_valid), freshness);
        break;
      case Slot::EXTERNAL_HEAT_DEMAND:
        publish_to(id(api_input_external_heat_demand_age), id(api_input_external_heat_demand_valid), freshness);
        break;
      default:
        break;
    }
  }

  template <typename A, typename V>
  static void publish_to(A& age, V& valid, const oq_input_source::Freshness& freshness) {
    age.publish_state(freshness.age_s);
    valid.publish_state(freshness.valid);
  }
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_api_ingress
