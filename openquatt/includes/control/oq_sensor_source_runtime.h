#pragma once

#include <math.h>
#include <stdint.h>
#include <string>

#include "oq_flow_pump_logic.h"
#include "oq_input_source_logic.h"
#include "oq_schedule_runtime.h"
#include "../sources/oq_resolved_learning_source.h"
#include "../sources/oq_source_receipt_runtime.h"
#include "oq_supply_calibration_logic.h"
#include "oq_supply_hold_logic.h"

namespace oq_sensor_source {

class Runtime {
 public:
  oq_sources::ResolvedLearningSource resolved_room_temperature() const {
    return validate_cached_receipt(this->resolved_room_);
  }
  oq_sources::ResolvedLearningSource resolved_room_setpoint() const {
    return validate_cached_receipt(this->resolved_setpoint_);
  }
  oq_sources::ResolvedLearningSource resolved_outside_temperature() const {
    return validate_cached_receipt(this->resolved_outside_);
  }
  oq_sources::ResolvedLearningSource resolved_flow_rate() const {
    return validate_cached_receipt(this->resolved_flow_);
  }

  // Register this on every source-selector and CIC URL on_value callback. It
  // records even A->B->A changes that occur between periodic sensor updates and
  // invalidates the old selected-source snapshot until control resolves again.
  void source_configuration_changed() {
    this->flow_generation_.observe(flow_configuration_key());
    this->outside_generation_.observe(outside_configuration_key());
    this->room_generation_.observe(room_configuration_key(false));
    this->setpoint_generation_.observe(room_configuration_key(true));
    this->resolved_flow_ = {};
    this->resolved_outside_ = {};
    this->resolved_room_ = {};
    this->resolved_setpoint_ = {};
  }

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
    if (!id(flow_source).has_state()) {
      this->resolved_flow_ = {};
      return NAN;
    }
    oq_input_source::FlowInputs input;
    input.selected = parse_source(id(flow_source).current_option());
    input.cic = sample(cic_feed_valid(), id(flow_rate_cic));
    input.aggregate = sample(true, id(flow_rate_hp_avg));
    input.q_hardware = OQ_HARDWARE_HEATPUMP_CONTROLLER_Q;
    input.duo = OQ_TOPOLOGY_DUO;
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    const bool hp1_uses_controller = id(hp_generation).has_state() && id(hp_generation).current_option() == "V1";
    input.controller_mode = controller_flow_mode_();
    input.hp_generation_v1 = hp1_uses_controller;
    input.controller = sample(true, id(flow_rate_controller));
#endif
    const oq_flow::PumpRelayState hp1{id(hp1_is_online) && id(hp1_pump_relay).has_state(), id(hp1_pump_relay).state};
#if OQ_TOPOLOGY_DUO
    const oq_flow::PumpRelayState hp2{id(hp2_is_online) && id(hp2_pump_relay).has_state(), id(hp2_pump_relay).state};
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    input.hp1 = hp1_uses_controller ? input.controller : sample(true, id(hp1_flow));
#else
    input.hp1 = sample(true, id(hp1_flow));
#endif
    input.hp2 = sample(true, id(hp2_flow));
    input.outdoor_mode = outdoor_flow_mode_();
#else
    const oq_flow::PumpRelayState hp2{};
#endif
    input.all_relevant_pumps_stopped = oq_flow::all_relevant_pumps_stopped(OQ_TOPOLOGY_DUO, hp1, hp2);
    const auto selected = oq_input_source::select_flow(input);
    const oq_sources::SourceConfigurationKey configuration{
        static_cast<uint8_t>(input.selected), static_cast<uint8_t>(input.controller_mode),
        static_cast<uint8_t>(input.outdoor_mode),
        input.selected == oq_input_source::Source::CIC ? id(cic_component).source_generation() : 0U};
    const uint32_t generation = this->flow_generation_.observe(configuration);
    this->resolved_flow_ = resolve_flow(selected, input, generation);
    this->resolved_flow_.configuration = configuration;
    this->resolved_flow_.configuration_generation = this->flow_generation_.observe_resolution(this->resolved_flow_);
    return selected.valid ? selected.value : NAN;
  }

  float outside(uint32_t now_ms, uint32_t hold_ms) {
    if (!id(outside_temp_source).has_state()) {
      this->resolved_outside_ = {};
      return NAN;
    }
    oq_input_source::NumericSources sources;
    sources.outdoor = sample(true, id(outside_temp_hp_avg));
    sources.ha = sample(ha_valid(id(outside_temp_valid_ha), id(outside_temp_ha)), id(outside_temp_ha));
    sources.api = sample(api_valid(id(api_input_outside_temperature_valid), id(api_input_outside_temperature)),
                         id(api_input_outside_temperature));
    sources.mqtt = sample(mqtt_valid(id(mqtt_outside_temperature_valid), id(mqtt_outside_temperature)),
                          id(mqtt_outside_temperature));
    const auto configured = parse_source(id(outside_temp_source).current_option());
    const auto selected = oq_input_source::select_outside(configured, sources, now_ms, hold_ms, outside_hold_);
    id(oq_outside_temp_selected_hold_active) = selected.held;
    const oq_sources::SourceConfigurationKey configuration{static_cast<uint8_t>(configured), 0U, 0U, 0U};
    const uint32_t generation = this->outside_generation_.observe(configuration);
    this->resolved_outside_ = resolve_outside(selected, generation);
    this->resolved_outside_.configuration = configuration;
    this->resolved_outside_.configuration_generation =
        this->outside_generation_.observe_resolution(this->resolved_outside_);
    return selected.valid ? selected.value : NAN;
  }

  float room_temperature(uint32_t now_ms, uint32_t hold_ms, bool opentherm_fresh) {
    if (!id(room_temp_source).has_state()) {
      this->resolved_room_ = {};
      return NAN;
    }
    const auto configured = parse_source(id(room_temp_source).current_option());
    const auto sources = room_sources(opentherm_fresh, false);
    const auto selected = oq_input_source::select_direct(configured, sources, true, now_ms, hold_ms, room_hold_);
    id(oq_room_temp_selected_hold_active) = selected.held;
    const oq_sources::SourceConfigurationKey configuration{
        static_cast<uint8_t>(configured), 0U, 0U,
        configured == oq_input_source::Source::CIC ? id(cic_component).source_generation() : 0U};
    const uint32_t generation = this->room_generation_.observe(configuration);
    this->resolved_room_ = resolve_room(selected, false, generation);
    this->resolved_room_.configuration = configuration;
    this->resolved_room_.configuration_generation = this->room_generation_.observe_resolution(this->resolved_room_);
    return selected.valid ? selected.value : NAN;
  }

  float room_setpoint(uint32_t now_ms, uint32_t hold_ms, bool opentherm_fresh) {
    if (!id(room_setpoint_source).has_state()) {
      this->resolved_setpoint_ = {};
      return NAN;
    }
    const auto configured = parse_source(id(room_setpoint_source).current_option());
    const auto sources = room_sources(opentherm_fresh, true);
    const auto selected = oq_input_source::select_direct(configured, sources, true, now_ms, hold_ms, setpoint_hold_);
    id(oq_room_setpoint_selected_hold_active) = selected.held;
    const oq_sources::SourceConfigurationKey configuration{
        static_cast<uint8_t>(configured), 0U, 0U,
        configured == oq_input_source::Source::CIC ? id(cic_component).source_generation() : 0U};
    const uint32_t generation = this->setpoint_generation_.observe(configuration);
    this->resolved_setpoint_ = resolve_room(selected, true, generation);
    this->resolved_setpoint_.configuration = configuration;
    this->resolved_setpoint_.configuration_generation =
        this->setpoint_generation_.observe_resolution(this->resolved_setpoint_);
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
  mutable oq_sources::SourceConfigurationGeneration flow_generation_;
  oq_sources::SourceConfigurationGeneration outside_generation_;
  oq_sources::SourceConfigurationGeneration room_generation_;
  oq_sources::SourceConfigurationGeneration setpoint_generation_;
  mutable oq_sources::ResolvedLearningSource resolved_flow_;
  oq_sources::ResolvedLearningSource resolved_outside_;
  oq_sources::ResolvedLearningSource resolved_room_;
  oq_sources::ResolvedLearningSource resolved_setpoint_;

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

  static oq_sources::RawFloatReceipt raw_receipt(float value, uint64_t received_ms, bool received, bool valid) {
    return {value, received_ms, received, valid};
  }

  static oq_input_source::ControllerFlowMode controller_flow_mode_() {
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    if (id(oq_q_flow_source).has_state()) {
      const auto option = id(oq_q_flow_source).current_option();
      if (option == "Local") return oq_input_source::ControllerFlowMode::LOCAL;
      if (option == "Auto") return oq_input_source::ControllerFlowMode::AUTO;
    }
#endif
    return oq_input_source::ControllerFlowMode::OTHER;
  }

  static oq_input_source::OutdoorFlowMode outdoor_flow_mode_() {
#if OQ_TOPOLOGY_DUO
    if (id(oq_duo_outdoor_flow_mode).has_state()) {
      const auto option = id(oq_duo_outdoor_flow_mode).current_option();
      if (option == "Flowmeter HP1") return oq_input_source::OutdoorFlowMode::HP1;
      if (option == "Flowmeter HP2") return oq_input_source::OutdoorFlowMode::HP2;
    }
#endif
    return oq_input_source::OutdoorFlowMode::AGGREGATE;
  }

  static oq_sources::SourceConfigurationKey room_configuration_key(bool setpoint) {
    const auto& selector = setpoint ? id(room_setpoint_source) : id(room_temp_source);
    const auto configured =
        selector.has_state() ? parse_source(selector.current_option()) : oq_input_source::Source::NONE;
    return {static_cast<uint8_t>(configured), 0U, 0U,
            configured == oq_input_source::Source::CIC ? id(cic_component).source_generation() : 0U};
  }

  static oq_sources::SourceConfigurationKey outside_configuration_key() {
    const auto configured = id(outside_temp_source).has_state() ? parse_source(id(outside_temp_source).current_option())
                                                                : oq_input_source::Source::NONE;
    return {static_cast<uint8_t>(configured), 0U, 0U, 0U};
  }

  static oq_sources::SourceConfigurationKey flow_configuration_key() {
    const auto configured =
        id(flow_source).has_state() ? parse_source(id(flow_source).current_option()) : oq_input_source::Source::NONE;
    const auto controller_mode = controller_flow_mode_();
    const auto outdoor_mode = outdoor_flow_mode_();
    return {static_cast<uint8_t>(configured), static_cast<uint8_t>(controller_mode), static_cast<uint8_t>(outdoor_mode),
            configured == oq_input_source::Source::CIC ? id(cic_component).source_generation() : 0U};
  }

  static oq_sources::RawFloatReceipt current_receipt(oq_sources::LearningSourceRoute route) {
    using oq_sources::LearningSourceRoute;
    switch (route) {
      case LearningSourceRoute::OPENTHERM_ROOM:
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
      {
        const auto receipt = id(oq_ot_slave_hub).master_room_temperature_receipt();
        return raw_receipt(receipt.value, receipt.received_ms, receipt.received, receipt.valid);
      }
#else
        return {};
#endif
      case LearningSourceRoute::OPENTHERM_SETPOINT:
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
      {
        const auto receipt = id(oq_ot_slave_hub).master_room_setpoint_receipt();
        return raw_receipt(receipt.value, receipt.received_ms, receipt.received, receipt.valid);
      }
#else
        return {};
#endif
      case LearningSourceRoute::CIC_ROOM:
        return id(cic_component).room_temperature_receipt();
      case LearningSourceRoute::CIC_SETPOINT:
        return id(cic_component).room_setpoint_receipt();
      case LearningSourceRoute::CIC_FLOW:
        return id(cic_component).flow_rate_receipt();
      case LearningSourceRoute::HP1_OUTSIDE:
        return oq_sources::hp1.outside;
      case LearningSourceRoute::HP2_OUTSIDE:
#if OQ_TOPOLOGY_DUO
        return oq_sources::hp2.outside;
#else
        return {};
#endif
      case LearningSourceRoute::CONTROLLER_FLOW:
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
        return oq_sources::controller_flow;
#else
        return {};
#endif
      case LearningSourceRoute::HP1_FLOW:
        return oq_sources::hp1.flow;
      case LearningSourceRoute::HP2_FLOW:
#if OQ_TOPOLOGY_DUO
        return oq_sources::hp2.flow;
#else
        return {};
#endif
      default:
        return {};
    }
  }

  static oq_sources::ResolvedLearningSource validate_cached_receipt(const oq_sources::ResolvedLearningSource& cached) {
    if (cached.provenance == oq_sources::LearningSourceProvenance::PHYSICAL_RECEIPT) {
      return oq_sources::validate_current_receipts(cached, current_receipt(cached.route));
    }
    if (cached.route != oq_sources::LearningSourceRoute::OUTSIDE_AGGREGATE &&
        cached.route != oq_sources::LearningSourceRoute::FLOW_AGGREGATE)
      return cached;
    return oq_sources::validate_current_receipts(cached, current_receipt(cached.component_route),
                                                 current_receipt(cached.secondary_route));
  }

  static oq_sources::ResolvedLearningSource resolve_room(const oq_input_source::NumericSelection& selected,
                                                         bool setpoint, uint32_t generation) {
    using oq_sources::LearningSourceProvenance;
    using oq_sources::LearningSourceRoute;
    if (selected.held) {
      return oq_sources::unsupported_source(selected.value, selected.valid,
                                            setpoint ? LearningSourceRoute::HA_SETPOINT : LearningSourceRoute::HA_ROOM,
                                            generation, LearningSourceProvenance::HELD);
    }
    switch (selected.route) {
      case oq_input_source::Source::OPENTHERM: {
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
        const auto receipt = setpoint ? id(oq_ot_slave_hub).master_room_setpoint_receipt()
                                      : id(oq_ot_slave_hub).master_room_temperature_receipt();
        return oq_sources::physical_source(
            setpoint ? LearningSourceRoute::OPENTHERM_SETPOINT : LearningSourceRoute::OPENTHERM_ROOM,
            raw_receipt(receipt.value, receipt.received_ms, receipt.received, receipt.valid), generation,
            selected.valid);
#else
        return oq_sources::unsupported_source(
            selected.value, selected.valid,
            setpoint ? LearningSourceRoute::OPENTHERM_SETPOINT : LearningSourceRoute::OPENTHERM_ROOM, generation);
#endif
      }
      case oq_input_source::Source::CIC:
        return oq_sources::physical_source(
            setpoint ? LearningSourceRoute::CIC_SETPOINT : LearningSourceRoute::CIC_ROOM,
            setpoint ? id(cic_component).room_setpoint_receipt() : id(cic_component).room_temperature_receipt(),
            generation, selected.valid);
      case oq_input_source::Source::HA:
        return oq_sources::unsupported_source(
            selected.value, selected.valid, setpoint ? LearningSourceRoute::HA_SETPOINT : LearningSourceRoute::HA_ROOM,
            generation);
      case oq_input_source::Source::API:
        return oq_sources::unsupported_source(
            selected.value, selected.valid,
            setpoint ? LearningSourceRoute::API_SETPOINT : LearningSourceRoute::API_ROOM, generation);
      case oq_input_source::Source::MQTT:
        return oq_sources::unsupported_source(
            selected.value, selected.valid,
            setpoint ? LearningSourceRoute::MQTT_SETPOINT : LearningSourceRoute::MQTT_ROOM, generation);
      default:
        return {};
    }
  }

  static oq_sources::ResolvedLearningSource resolve_outside(const oq_input_source::NumericSelection& selected,
                                                            uint32_t generation) {
    using oq_sources::LearningSourceProvenance;
    using oq_sources::LearningSourceRoute;
    if (selected.held) {
      return oq_sources::unsupported_source(selected.value, selected.valid, LearningSourceRoute::HA_OUTSIDE, generation,
                                            LearningSourceProvenance::HELD);
    }
    switch (selected.route) {
      case oq_input_source::Source::OUTDOOR: {
#if OQ_TOPOLOGY_DUO
        const auto& local = oq_sources::local_outside_selection;
        if (selected.valid && local.valid && local.route == oq_sources::LocalOutsideRoute::HP1)
          return oq_sources::physical_source(LearningSourceRoute::HP1_OUTSIDE, local.hp1_receipt, generation, true);
        if (selected.valid && local.valid && local.route == oq_sources::LocalOutsideRoute::HP2)
          return oq_sources::physical_source(LearningSourceRoute::HP2_OUTSIDE, local.hp2_receipt, generation, true);
        if (selected.valid && local.valid && local.route == oq_sources::LocalOutsideRoute::COMPOSITE) {
          const auto operation = local.operation == oq_sources::LocalOutsideOperation::MINIMUM
                                     ? oq_sources::LearningCompositeOperation::MINIMUM
                                     : oq_sources::LearningCompositeOperation::ARITHMETIC_MEAN;
          return oq_sources::unsupported_composite_source(
              selected.value, true, LearningSourceRoute::OUTSIDE_AGGREGATE, LearningSourceRoute::HP1_OUTSIDE,
              LearningSourceRoute::HP2_OUTSIDE, local.hp1_receipt, local.hp2_receipt, operation, {}, generation);
        }
        return oq_sources::unsupported_source(selected.value, selected.valid, LearningSourceRoute::OUTSIDE_AGGREGATE,
                                              generation);
#else
        return oq_sources::physical_source(LearningSourceRoute::HP1_OUTSIDE, oq_sources::hp1.outside, generation,
                                           selected.valid);
#endif
      }
      case oq_input_source::Source::HA:
        return oq_sources::unsupported_source(selected.value, selected.valid, LearningSourceRoute::HA_OUTSIDE,
                                              generation);
      case oq_input_source::Source::API:
        return oq_sources::unsupported_source(selected.value, selected.valid, LearningSourceRoute::API_OUTSIDE,
                                              generation);
      case oq_input_source::Source::MQTT:
        return oq_sources::unsupported_source(selected.value, selected.valid, LearningSourceRoute::MQTT_OUTSIDE,
                                              generation);
      default:
        return {};
    }
  }

  static oq_sources::ResolvedLearningSource resolve_flow(const oq_input_source::FlowSelection& selected,
                                                         const oq_input_source::FlowInputs& input,
                                                         uint32_t generation) {
    using oq_sources::LearningSourceProvenance;
    using oq_sources::LearningSourceRoute;
    switch (selected.route) {
      case oq_input_source::FlowRoute::CIC:
        return oq_sources::physical_source(LearningSourceRoute::CIC_FLOW, id(cic_component).flow_rate_receipt(),
                                           generation, selected.valid);
      case oq_input_source::FlowRoute::CONTROLLER:
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
        return oq_sources::physical_source(LearningSourceRoute::CONTROLLER_FLOW, oq_sources::controller_flow,
                                           generation, selected.valid);
#else
        return {};
#endif
      case oq_input_source::FlowRoute::HP1:
        return oq_sources::physical_source(LearningSourceRoute::HP1_FLOW, oq_sources::hp1.flow, generation,
                                           selected.valid);
      case oq_input_source::FlowRoute::HP2:
#if OQ_TOPOLOGY_DUO
        return oq_sources::physical_source(LearningSourceRoute::HP2_FLOW, oq_sources::hp2.flow, generation,
                                           selected.valid);
#else
        return {};
#endif
      case oq_input_source::FlowRoute::AGGREGATE: {
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
        const bool hp1_uses_controller = id(hp_generation).has_state() && id(hp_generation).current_option() == "V1";
        const auto hp1_receipt = hp1_uses_controller ? oq_sources::controller_flow : oq_sources::hp1.flow;
        const auto hp1_route =
            hp1_uses_controller ? LearningSourceRoute::CONTROLLER_FLOW : LearningSourceRoute::HP1_FLOW;
#else
        const auto hp1_receipt = oq_sources::hp1.flow;
        constexpr auto hp1_route = LearningSourceRoute::HP1_FLOW;
#endif
#if OQ_TOPOLOGY_DUO
        if (selected.valid && input.hp1.valid && !input.hp2.valid)
          return oq_sources::physical_source(hp1_route, hp1_receipt, generation, true);
        if (selected.valid && !input.hp1.valid && input.hp2.valid)
          return oq_sources::physical_source(LearningSourceRoute::HP2_FLOW, oq_sources::hp2.flow, generation, true);
        const auto operation = selected.aggregate_operation == oq_input_source::FlowAggregateOperation::ARITHMETIC_MEAN
                                   ? oq_sources::LearningCompositeOperation::ARITHMETIC_MEAN
                               : selected.aggregate_operation == oq_input_source::FlowAggregateOperation::MAXIMUM
                                   ? oq_sources::LearningCompositeOperation::MAXIMUM
                                   : oq_sources::LearningCompositeOperation::NONE;
        return oq_sources::unsupported_composite_source(
            selected.value, selected.valid, LearningSourceRoute::FLOW_AGGREGATE, hp1_route,
            LearningSourceRoute::HP2_FLOW, hp1_receipt, oq_sources::hp2.flow, operation, {}, generation);
#else
        return oq_sources::physical_source(hp1_route, hp1_receipt, generation, selected.valid);
#endif
      }
      case oq_input_source::FlowRoute::PUMPS_STOPPED:
        return oq_sources::unsupported_source(selected.value, selected.valid,
                                              LearningSourceRoute::SYNTHESIZED_ZERO_FLOW, generation,
                                              LearningSourceProvenance::SYNTHESIZED);
      default:
        return {};
    }
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
