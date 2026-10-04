#pragma once

#include "oq_heat_intent_runtime.h"
#include "oq_heating_curve_logic.h"
#include "oq_sensor_source_runtime.h"
#include "oq_warmup_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_warmup_runtime {

class Runtime {
 public:
  void cancel(oq_warmup::Status reason) { this->state_ = oq_warmup::cancel(reason); }

  float target(bool ot_room_fresh, bool ot_setpoint_fresh) {
    const bool curve = id(oq_heat_mode_code) == 1;
    const int cm = id(oq_control_mode_code);
    const bool external = curve ? id(heating_supply_target_source).current_option() != "Heating curve"
                                : id(external_heat_demand_source).current_option() != "Disabled";
    // Keep observing setpoint edges while thermostat CH permission is off.
    // This limiter never grants permission; the downstream heat gates own it.
    const bool automatic = id(oq_warmup_settings_ready) && cm >= 0 && cm <= 3 &&
                           id(oq_cm_override).current_option() == "Auto" && id(oq_enabled).state &&
                           !id(oq_manual_hp_active) && !id(oq_manual_flow_active) && !external;
    const auto room = oq_sensor_source::runtime().resolved_room_temperature();
    const auto setpoint = oq_sensor_source::runtime().resolved_room_setpoint();
    const auto profile = oq_curve::control_profile(id(oq_curve_control_profile).current_option());
    const float below_c = curve ? profile.room_resume_heat_c : id(ph_comfort_band_below_c).state;
    const oq_warmup::Settings settings{id(oq_warmup_trigger_c).state, id(oq_warmup_step_c).state,
                                       oq_warmup::duration_ms(id(oq_warmup_step_minutes).state, 60000.0f, 5.0f, 120.0f),
                                       id(oq_warmup_max_offset_c).state,
                                       oq_warmup::duration_ms(id(oq_warmup_max_hours).state, 3600000.0f, 1.0f, 24.0f)};
    const bool cic_room_current =
        room.route != oq_sources::LearningSourceRoute::CIC_ROOM ||
        oq_warmup::current_receipt_matches(id(cic_component).room_temperature_receipt(), id(cic_room_temp).state);
    const bool cic_setpoint_current =
        setpoint.route != oq_sources::LearningSourceRoute::CIC_SETPOINT ||
        oq_warmup::current_receipt_matches(id(cic_component).room_setpoint_receipt(), id(cic_room_setpoint).state);
    // Compare the receipt with its producer, not the selected sensor: the
    // latter updates every 10s and may legitimately lag a new valid payload.
    const bool fresh = cic_room_current && cic_setpoint_current &&
                       oq_heat_intent_runtime::room_temperature_fresh(ot_room_fresh) &&
                       oq_heat_intent_runtime::room_setpoint_fresh(ot_setpoint_fresh) && room.valid && setpoint.valid &&
                       room.provenance != oq_sources::LearningSourceProvenance::HELD &&
                       setpoint.provenance != oq_sources::LearningSourceProvenance::HELD &&
                       room.value == id(room_temp_selected).state && setpoint.value == id(room_setpoint_selected).state;
    this->state_ = oq_warmup::evaluate(
        {static_cast<uint32_t>(millis()), id(oq_warmup_enabled).state, automatic, fresh, room.configuration_generation,
         setpoint.configuration_generation, static_cast<uint8_t>(curve ? 2 : 3), id(room_temp_selected).state,
         id(room_setpoint_selected).state, below_c},
        settings, this->state_);
    return oq_warmup::effective_target(this->state_, id(room_setpoint_selected).state);
  }

  bool active() const { return this->state_.active; }
  float effective_target() const { return oq_warmup::effective_target(this->state_, id(room_setpoint_selected).state); }
  float offset() const { return this->state_.offset_c; }
  const char* status() const { return oq_warmup::status_name(this->state_.status); }

 private:
  oq_warmup::State state_;
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_warmup_runtime
#endif
