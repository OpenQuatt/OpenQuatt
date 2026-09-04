#pragma once

#include <math.h>
#include <stdint.h>
#include <string>

#include "oq_flow_pump_logic.h"
#include "oq_input_source_logic.h"
#include "oq_schedule_runtime.h"
#include "oq_supply_calibration_logic.h"
#include "oq_supply_hold_logic.h"

namespace oq_sensor_source {

class Runtime {
 public:
  void water_source_changed(const std::string& option) {
    if (!id(oq_water_supply_temp_current_source_ready)) return;
    const int32_t current = id(oq_water_supply_temp_current_source_code);
    const bool unchanged = (option == "Local" && (current == oq_supply_calibration::SOURCE_LOCAL_PT1000 ||
                                                  current == oq_supply_calibration::SOURCE_LOCAL_DS18B20)) ||
                           (option == "CIC" && current == oq_supply_calibration::SOURCE_CIC) ||
                           (option == "HA input" && current == oq_supply_calibration::SOURCE_HA_INPUT);
    if (unchanged) return;
    id(oq_water_supply_temp_current_source_ready) = false;
    if (id(oq_hp_water_calibration_active)) id(oq_hp_water_calibration_abort) = true;
    id(water_supply_temp_selected).update();
  }

  bool heating_enable_valid() const {
    if (!id(heating_enable_source).has_state()) return false;
    return oq_input_source::select_heating_enable(parse_source(id(heating_enable_source).current_option()),
                                                  heating_enable_sources())
        .valid;
  }

  bool heating_enable_value() const {
    if (!id(heating_enable_source).has_state()) return false;
    return oq_input_source::select_heating_enable(parse_source(id(heating_enable_source).current_option()),
                                                  heating_enable_sources())
        .value;
  }

  bool cooling_enable_valid() const {
    if (!id(cooling_enable_source).has_state()) return false;
    return oq_input_source::select_cooling_enable(parse_source(id(cooling_enable_source).current_option()),
                                                  cooling_enable_sources(), false)
        .valid;
  }

  bool cooling_enable_selected() const {
    if (!id(cooling_enable_source).has_state()) return false;
    return oq_input_source::select_cooling_enable(parse_source(id(cooling_enable_source).current_option()),
                                                  cooling_enable_sources(), id(oq_manual_cooling_enable).state)
        .value;
  }

  bool heating_blocked_by_thermostat() const {
    if (!id(heating_enable_source).has_state() ||
        parse_source(id(heating_enable_source).current_option()) == oq_input_source::Source::DISABLED) {
      return false;
    }
    return id(oq_enabled).state && id(oq_strategy_heat_request_active) && !id(heating_enable_selected).state;
  }

  std::string calibration_status() const {
    if (!id(oq_water_supply_temp_current_source_ready)) return "Calibration inactive: source not ready";
    const auto source = current_calibration_source();
    const auto record = calibration_record(source.code);
    if (!oq_supply_calibration::record_present(record)) return "Not calibrated";
    if (!oq_supply_calibration::record_matches(record, source)) {
      return std::string("Recalibration required: ") +
             oq_supply_calibration::source_label(static_cast<int32_t>(source.code));
    }
    return std::string("Calibrated: ") + oq_supply_calibration::source_label(static_cast<int32_t>(source.code));
  }

  std::string heating_effective_source() const {
    if (!id(heating_enable_source).has_state()) return "Unknown";
    const std::string option = id(heating_enable_source).current_option();
    if (parse_source(option) == oq_input_source::Source::DISABLED) return "None";
    return heating_enable_valid() ? option : "None";
  }

  std::string configured_room_source() const {
    return id(room_temp_source).has_state() ? std::string(id(room_temp_source).current_option()) : "Unknown";
  }

  std::string configured_setpoint_source() const {
    return id(room_setpoint_source).has_state() ? std::string(id(room_setpoint_source).current_option()) : "Unknown";
  }

  std::string cooling_effective_source() const {
    if (!id(cooling_enable_source).has_state()) return "Unknown";
    const std::string option = id(cooling_enable_source).current_option();
    const auto selected = parse_source(option);
    const bool manual = id(oq_manual_cooling_enable).state;
    const auto sources = cooling_enable_sources();
    std::string automatic = "None";
    if (selected == oq_input_source::Source::CIC_OR_HA) {
      const bool cic = sources.cic.valid && sources.cic.value;
      const bool ha = sources.ha.valid && sources.ha.value;
      automatic = cic && ha ? "CIC + HA input" : (cic ? "CIC" : (ha ? "HA input" : "None"));
    } else if (selected != oq_input_source::Source::DISABLED) {
      const auto value = oq_input_source::binary_for(selected, sources);
      if (value.valid && value.value) automatic = option;
    }
    if (!manual) return automatic;
    return automatic == "None" ? "Manual" : automatic + " + Manual";
  }

  std::string selected_hold_status() const {
    std::string active;
    add_hold(active, id(oq_water_supply_temp_selected_hold_active), "Water supply");
    add_hold(active, id(oq_outside_temp_selected_hold_active), "Outside temp");
    add_hold(active, id(oq_room_temp_selected_hold_active), "Room temp");
    add_hold(active, id(oq_room_setpoint_selected_hold_active), "Room setpoint");
    add_hold(active, id(oq_external_heat_demand_selected_hold_active), "External heat demand");
    add_hold(active, id(oq_cooling_dew_point_selected_hold_active), "Cooling dew point");
    return active.empty() ? "None" : active;
  }

  float water_supply(uint32_t now_ms, uint32_t local_cic_hold_ms, uint32_t ha_hold_ms, uint32_t fallback_stale_ms,
                     const char* ha_entity_id) {
    const std::string option =
        id(water_supply_source).has_state() ? id(water_supply_source).current_option() : std::string();
    const auto source = supply_source(option, ha_entity_id);
    migrate_legacy_calibration();
    if (selected_supply_hold_.has_value() && !selected_supply_hold_.matches_source(source)) clear_supply_hold();

    bool calibration_required = false;
    if (source.ready) {
      const auto record = calibration_record(source.code);
      calibration_required = oq_supply_calibration::calibration_required(record, source);
      if (calibration_required) clear_supply_hold();
      id(oq_water_supply_temp_current_source_code) = static_cast<int32_t>(source.code);
      id(oq_water_supply_temp_current_source_fingerprint) = source.fingerprint;
      id(oq_water_supply_temp_current_source_ready) = true;
      const float offset = oq_supply_calibration::record_matches(record, source) ? record.offset_c : 0.0f;
      if (!id(water_supply_temp_calibration_offset).has_state() ||
          fabsf(id(water_supply_temp_calibration_offset).state - offset) > 0.0001f) {
        id(water_supply_temp_calibration_offset).publish_state(offset);
      }
    } else {
      id(oq_water_supply_temp_current_source_code) = 0;
      id(oq_water_supply_temp_current_source_fingerprint) = 0;
      id(oq_water_supply_temp_current_source_ready) = false;
    }
    id(oq_water_supply_temp_calibration_required).publish_state(calibration_required);
    id(oq_water_supply_temp_calibration_status).update();

    float selected_c = NAN;
    if (option == "CIC" && cic_feed_valid() && id(water_supply_temp_cic).has_state())
      selected_c = id(water_supply_temp_cic).state;
    else if (option == "HA input" && ha_valid(id(water_supply_temp_valid_ha), id(water_supply_temp_ha)))
      selected_c = id(water_supply_temp_ha).state;
    else if (option == "Local" && id(water_supply_temp_esp).has_state())
      selected_c = id(water_supply_temp_esp).state;

    if (isfinite(selected_c)) {
      const auto record = calibration_record(source.code);
      if (oq_supply_calibration::record_matches(record, source)) selected_c += record.offset_c;
      if (source.ready)
        selected_supply_hold_.remember(selected_c, now_ms, source);
      else
        clear_supply_hold();
      id(oq_water_supply_temp_selected_hold_active) = false;
      id(oq_water_supply_temp_fallback_runtime_active) = false;
      publish_supply_source(local_supply_label(option));
      return selected_c;
    }

    id(oq_water_supply_temp_selected_hold_active) = false;
    const uint32_t hold_ms = oq_supply_hold::timeout_ms(source, local_cic_hold_ms, ha_hold_ms);
    const bool may_hold = selected_supply_hold_.available(source, now_ms, hold_ms);
    if (!may_hold) clear_supply_hold();
    const auto fallback = fallback_supply(now_ms, fallback_stale_ms);
    if (fallback.valid) {
      id(oq_water_supply_temp_fallback_runtime_active) = true;
      publish_supply_source(fallback.label);
      return fallback.value;
    }
    id(oq_water_supply_temp_fallback_runtime_active) = false;
    if (may_hold) {
      id(oq_water_supply_temp_selected_hold_active) = true;
      publish_supply_source(std::string(oq_supply_calibration::source_label(selected_supply_hold_.source_code)) +
                            " (held)");
      return selected_supply_hold_.last_valid_c;
    }
    publish_supply_source("Unavailable");
    return NAN;
  }

  float flow() const {
    if (!id(flow_source).has_state()) return NAN;
    oq_input_source::FlowInputs input;
    input.selected = parse_source(id(flow_source).current_option());
    input.cic = sample(cic_feed_valid(), id(flow_rate_cic));
    input.aggregate = sample(true, id(flow_rate_hp_avg));
    input.q_hardware = OQ_HARDWARE_HEATPUMP_CONTROLLER_Q;
    input.duo = OQ_TOPOLOGY_DUO;
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    if (id(oq_q_flow_source).has_state()) {
      const auto option = id(oq_q_flow_source).current_option();
      input.controller_mode = option == "Local"  ? oq_input_source::ControllerFlowMode::LOCAL
                              : option == "Auto" ? oq_input_source::ControllerFlowMode::AUTO
                                                 : oq_input_source::ControllerFlowMode::OTHER;
    }
    input.hp_generation_v1 = id(hp_generation).has_state() && id(hp_generation).current_option() == "V1";
    input.controller = sample(true, id(flow_rate_controller));
#endif
    const oq_flow::PumpRelayState hp1{id(hp1_is_online) && id(hp1_pump_relay).has_state(), id(hp1_pump_relay).state};
#if OQ_TOPOLOGY_DUO
    const oq_flow::PumpRelayState hp2{id(hp2_is_online) && id(hp2_pump_relay).has_state(), id(hp2_pump_relay).state};
    input.hp1 = sample(true, id(hp1_flow));
    input.hp2 = sample(true, id(hp2_flow));
    if (id(oq_duo_outdoor_flow_mode).has_state()) {
      const auto mode = id(oq_duo_outdoor_flow_mode).current_option();
      input.outdoor_mode = mode == "Flowmeter HP1"   ? oq_input_source::OutdoorFlowMode::HP1
                           : mode == "Flowmeter HP2" ? oq_input_source::OutdoorFlowMode::HP2
                                                     : oq_input_source::OutdoorFlowMode::AGGREGATE;
    }
#else
    const oq_flow::PumpRelayState hp2{};
#endif
    input.all_relevant_pumps_stopped = oq_flow::all_relevant_pumps_stopped(OQ_TOPOLOGY_DUO, hp1, hp2);
    const auto selected = oq_input_source::select_flow(input);
    return selected.valid ? selected.value : NAN;
  }

  float outside(uint32_t now_ms, uint32_t hold_ms) {
    if (!id(outside_temp_source).has_state()) return NAN;
    oq_input_source::NumericSources sources;
    sources.outdoor = sample(true, id(outside_temp_hp_avg));
    sources.ha = sample(ha_valid(id(outside_temp_valid_ha), id(outside_temp_ha)), id(outside_temp_ha));
    sources.api = sample(api_valid(id(api_input_outside_temperature_valid), id(api_input_outside_temperature)),
                         id(api_input_outside_temperature));
    sources.mqtt = sample(mqtt_valid(id(mqtt_outside_temperature_valid), id(mqtt_outside_temperature)),
                          id(mqtt_outside_temperature));
    const auto selected = oq_input_source::select_outside(parse_source(id(outside_temp_source).current_option()),
                                                          sources, now_ms, hold_ms, outside_hold_);
    id(oq_outside_temp_selected_hold_active) = selected.held;
    return selected.valid ? selected.value : NAN;
  }

  float room_temperature(uint32_t now_ms, uint32_t hold_ms, bool opentherm_fresh) {
    if (!id(room_temp_source).has_state()) return NAN;
    const auto sources = room_sources(opentherm_fresh, false);
    const auto selected = oq_input_source::select_direct(parse_source(id(room_temp_source).current_option()), sources,
                                                         true, now_ms, hold_ms, room_hold_);
    id(oq_room_temp_selected_hold_active) = selected.held;
    return selected.valid ? selected.value : NAN;
  }

  float room_setpoint(uint32_t now_ms, uint32_t hold_ms, bool opentherm_fresh) {
    if (!id(room_setpoint_source).has_state()) return NAN;
    const auto sources = room_sources(opentherm_fresh, true);
    const auto selected = oq_input_source::select_direct(parse_source(id(room_setpoint_source).current_option()),
                                                         sources, true, now_ms, hold_ms, setpoint_hold_);
    id(oq_room_setpoint_selected_hold_active) = selected.held;
    return selected.valid ? selected.value : NAN;
  }

  float external_heat_demand(uint32_t now_ms, uint32_t hold_ms) {
    if (!id(external_heat_demand_source).has_state()) return NAN;
    oq_input_source::NumericSources sources;
    sources.ha =
        sample(ha_valid(id(external_heat_demand_valid_ha), id(external_heat_demand_ha)), id(external_heat_demand_ha));
    sources.api = sample(api_valid(id(api_input_external_heat_demand_valid), id(api_input_external_heat_demand)),
                         id(api_input_external_heat_demand));
    const auto selected = oq_input_source::select_direct(parse_source(id(external_heat_demand_source).current_option()),
                                                         sources, true, now_ms, hold_ms, demand_hold_);
    id(oq_external_heat_demand_selected_hold_active) = selected.held;
    return selected.valid ? selected.value : NAN;
  }

 private:
  struct SupplyFallback {
    bool valid = false;
    float value = NAN;
    const char* label = "Unavailable";
  };

  oq_supply_hold::State selected_supply_hold_;
  oq_input_source::HoldState outside_hold_;
  oq_input_source::HoldState room_hold_;
  oq_input_source::HoldState setpoint_hold_;
  oq_input_source::HoldState demand_hold_;

  template <typename T>
  static oq_input_source::Source parse_source(const T& option) {
    if (option == "Auto" || option == "Lowest valid") return oq_input_source::Source::AUTO;
    if (option == "Local") return oq_input_source::Source::LOCAL;
    if (option == "Outdoor unit") return oq_input_source::Source::OUTDOOR;
    if (option == "CIC") return oq_input_source::Source::CIC;
    if (option == "HA input") return oq_input_source::Source::HA;
    if (option == "API input") return oq_input_source::Source::API;
    if (option == "MQTT") return oq_input_source::Source::MQTT;
    if (option == "OT thermostat") return oq_input_source::Source::OPENTHERM;
    if (option == "Disabled") return oq_input_source::Source::DISABLED;
    if (option == "CIC or HA input") return oq_input_source::Source::CIC_OR_HA;
    if (option == "Schedule") return oq_input_source::Source::SCHEDULE;
    return oq_input_source::Source::NONE;
  }

  template <typename T>
  static oq_input_source::NumericSample sample(bool enabled, const T& entity) {
    return oq_input_source::numeric_sample(enabled, entity.has_state(), entity.state);
  }

  template <typename B, typename S>
  static bool ha_valid(const B& valid, const S& value) {
    return valid.has_state() && valid.state && value.has_state() && isfinite(value.state);
  }

  template <typename B, typename S>
  static bool api_valid(const B& valid, const S& value) {
    return valid.has_state() && valid.state && value.has_state() && isfinite(value.state);
  }

  template <typename B, typename S>
  static bool mqtt_valid(const B& valid, const S& value) {
    return valid.has_state() && valid.state && value.has_state() && isfinite(value.state);
  }

  static bool cic_feed_valid() {
    return id(feed_ok).has_state() && id(feed_ok).state && id(cic_data_stale).has_state() && !id(cic_data_stale).state;
  }

  static oq_input_source::EnableSources heating_enable_sources() {
    return {{id(ot_thermostat_status_valid).has_state() && id(ot_thermostat_status_valid).state &&
                 id(ot_thermostat_ch_enable).has_state() && id(ot_thermostat_ch_enable).state,
             id(ot_thermostat_status_valid).has_state() && id(ot_thermostat_status_valid).state &&
                 id(ot_thermostat_ch_enable).has_state()},
            {id(cic_ch_enabled).has_state() && id(cic_ch_enabled).state,
             cic_feed_valid() && id(cic_ch_enable_valid).has_state() && id(cic_ch_enable_valid).state &&
                 id(cic_ch_enabled).has_state()},
            {id(heating_enable_ha).has_state() && id(heating_enable_ha).state,
             id(heating_enable_valid_ha).has_state() && id(heating_enable_valid_ha).state &&
                 id(heating_enable_ha).has_state()},
            {id(api_input_heating_enable).state,
             id(api_input_heating_enable_valid).has_state() && id(api_input_heating_enable_valid).state},
            {id(mqtt_heating_enable).has_state() && id(mqtt_heating_enable).state,
             id(mqtt_heating_enable_valid).has_state() && id(mqtt_heating_enable_valid).state &&
                 id(mqtt_heating_enable).has_state()}};
  }

  static oq_input_source::EnableSources cooling_enable_sources() {
    const auto schedule = oq_schedule::cooling_window();
    return {{id(ot_thermostat_status_valid).has_state() && id(ot_thermostat_status_valid).state &&
                 id(ot_thermostat_cooling_enable).has_state() && id(ot_thermostat_cooling_enable).state,
             id(ot_thermostat_status_valid).has_state() && id(ot_thermostat_status_valid).state &&
                 id(ot_thermostat_cooling_enable).has_state()},
            {id(cic_cooling_enabled).has_state() && id(cic_cooling_enabled).state,
             cic_feed_valid() && id(cic_cooling_enabled).has_state()},
            {id(cooling_enable_ha).has_state() && id(cooling_enable_ha).state,
             id(cooling_enable_valid_ha).has_state() && id(cooling_enable_valid_ha).state &&
                 id(cooling_enable_ha).has_state()},
            {id(api_input_cooling_enable).state,
             id(api_input_cooling_enable_valid).has_state() && id(api_input_cooling_enable_valid).state},
            {id(mqtt_cooling_enable).has_state() && id(mqtt_cooling_enable).state,
             id(mqtt_cooling_enable_valid).has_state() && id(mqtt_cooling_enable_valid).state &&
                 id(mqtt_cooling_enable).has_state()},
            {schedule.active, schedule.valid}};
  }

  static oq_input_source::NumericSources room_sources(bool opentherm_fresh, bool setpoint) {
    oq_input_source::NumericSources sources;
    if (setpoint) {
      sources.ha = sample(ha_valid(id(room_setpoint_valid_ha), id(thermostat_setpoint_ha)), id(thermostat_setpoint_ha));
      sources.opentherm = sample(opentherm_fresh, id(ot_thermostat_room_setpoint));
      sources.cic = sample(cic_feed_valid(), id(cic_room_setpoint));
      sources.api = sample(api_valid(id(api_input_room_setpoint_valid), id(api_input_room_setpoint)),
                           id(api_input_room_setpoint));
      sources.mqtt = sample(mqtt_valid(id(mqtt_room_setpoint_valid), id(mqtt_room_setpoint)), id(mqtt_room_setpoint));
    } else {
      sources.ha = sample(ha_valid(id(room_temp_valid_ha), id(thermostat_room_temp_ha)), id(thermostat_room_temp_ha));
      sources.opentherm = sample(opentherm_fresh, id(ot_thermostat_room_temp));
      sources.cic = sample(cic_feed_valid(), id(cic_room_temp));
      sources.api = sample(api_valid(id(api_input_room_temperature_valid), id(api_input_room_temperature)),
                           id(api_input_room_temperature));
      sources.mqtt =
          sample(mqtt_valid(id(mqtt_room_temperature_valid), id(mqtt_room_temperature)), id(mqtt_room_temperature));
    }
    return sources;
  }

  static oq_supply_calibration::SourceIdentity current_calibration_source() {
    return {static_cast<oq_supply_calibration::SourceCode>(id(oq_water_supply_temp_current_source_code)),
            id(oq_water_supply_temp_current_source_fingerprint), true};
  }

  static oq_supply_calibration::CalibrationRecord calibration_record(oq_supply_calibration::SourceCode code) {
    switch (code) {
      case oq_supply_calibration::SOURCE_LOCAL_PT1000:
        return oq_supply_calibration::load_record(id(oq_water_supply_temp_calibration_pt1000_record));
      case oq_supply_calibration::SOURCE_LOCAL_DS18B20:
        return oq_supply_calibration::load_record(id(oq_water_supply_temp_calibration_ds18b20_record));
      case oq_supply_calibration::SOURCE_CIC:
        return oq_supply_calibration::load_record(id(oq_water_supply_temp_calibration_cic_record));
      case oq_supply_calibration::SOURCE_HA_INPUT:
        return oq_supply_calibration::load_record(id(oq_water_supply_temp_calibration_ha_input_record));
      default:
        return {};
    }
  }

  static void migrate_legacy_calibration() {
    if (!id(water_supply_temp_calibration_offset).has_state()) return;
    const int32_t code = id(oq_water_supply_temp_calibration_source_code);
    switch (code) {
      case oq_supply_calibration::SOURCE_LOCAL_PT1000:
        oq_supply_calibration::migrate_legacy_record(id(oq_water_supply_temp_calibration_pt1000_record), code,
                                                     id(oq_water_supply_temp_calibration_source_fingerprint),
                                                     id(oq_water_supply_temp_calibration_checksum),
                                                     id(water_supply_temp_calibration_offset).state);
        break;
      case oq_supply_calibration::SOURCE_LOCAL_DS18B20:
        oq_supply_calibration::migrate_legacy_record(id(oq_water_supply_temp_calibration_ds18b20_record), code,
                                                     id(oq_water_supply_temp_calibration_source_fingerprint),
                                                     id(oq_water_supply_temp_calibration_checksum),
                                                     id(water_supply_temp_calibration_offset).state);
        break;
      case oq_supply_calibration::SOURCE_CIC:
        oq_supply_calibration::migrate_legacy_record(id(oq_water_supply_temp_calibration_cic_record), code,
                                                     id(oq_water_supply_temp_calibration_source_fingerprint),
                                                     id(oq_water_supply_temp_calibration_checksum),
                                                     id(water_supply_temp_calibration_offset).state);
        break;
      case oq_supply_calibration::SOURCE_HA_INPUT:
        oq_supply_calibration::migrate_legacy_record(id(oq_water_supply_temp_calibration_ha_input_record), code,
                                                     id(oq_water_supply_temp_calibration_source_fingerprint),
                                                     id(oq_water_supply_temp_calibration_checksum),
                                                     id(water_supply_temp_calibration_offset).state);
        break;
      default:
        break;
    }
  }

  static oq_supply_calibration::SourceIdentity supply_source(const std::string& option, const char* ha_entity_id) {
    std::string local_source;
    bool local_ready = false;
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    local_ready = id(oq_local_supply_temp_source).has_state();
    if (local_ready) local_source = id(oq_local_supply_temp_source).current_option();
#endif
    const bool cic_configured = id(cic_feed_url).has_state();
    const std::string cic_url = cic_configured ? id(cic_feed_url).state : std::string();
    return oq_supply_calibration::source_identity(
        option.c_str(), OQ_HARDWARE_HEATPUMP_CONTROLLER_Q, local_source.c_str(), local_ready, cic_url.c_str(),
        cic_configured && id(cic_component).is_url_ready(cic_url), ha_entity_id);
  }

  static std::string local_supply_label(const std::string& option) {
    if (option != "Local") return option;
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    return id(oq_local_supply_temp_source).has_state()
               ? std::string("Local - ") + id(oq_local_supply_temp_source).current_option()
               : "Local";
#else
    return "Local - DS18B20";
#endif
  }

  static SupplyFallback fallback_supply(uint32_t now_ms, uint32_t stale_ms) {
#if OQ_TOPOLOGY_DUO
    const uint32_t last_update_ms = id(hp2_water_out_temp_last_update_ms);
    const bool valid = id(hp2_is_online) && last_update_ms > 0 && now_ms - last_update_ms <= stale_ms &&
                       id(hp2_water_out_temp).has_state() && isfinite(id(hp2_water_out_temp).state);
    return {valid, valid ? id(hp2_water_out_temp).state : NAN, "hp2 water out (fallback)"};
#else
    const uint32_t last_update_ms = id(hp1_water_out_temp_last_update_ms);
    const bool valid = id(hp1_is_online) && last_update_ms > 0 && now_ms - last_update_ms <= stale_ms &&
                       id(hp1_water_out_temp).has_state() && isfinite(id(hp1_water_out_temp).state);
    return {valid, valid ? id(hp1_water_out_temp).state : NAN, "hp1 water out (fallback)"};
#endif
  }

  void clear_supply_hold() {
    selected_supply_hold_.reset();
    id(oq_water_supply_temp_selected_hold_active) = false;
  }

  static void publish_supply_source(const std::string& source) {
    if (!id(oq_water_supply_temp_effective_source).has_state() ||
        id(oq_water_supply_temp_effective_source).state != source) {
      id(oq_water_supply_temp_effective_source).publish_state(source);
    }
  }

  static void add_hold(std::string& active, bool enabled, const char* label) {
    if (!enabled) return;
    if (!active.empty()) active += ", ";
    active += label;
  }
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_sensor_source
