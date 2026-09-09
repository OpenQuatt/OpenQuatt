#pragma once

#include "oq_hp_incident_types.h"

#include <array>

namespace oq_incidents {

constexpr IncidentDefinition make_status(uint16_t register_address, uint8_t bit, const char* key,
                                         const char* presentation_key) {
  return {incident_id(register_address, bit),
          register_address,
          bit,
          key,
          presentation_key,
          IncidentCategory::STATUS,
          IncidentSeverity::INFO,
          effect_mask(IncidentEffect::DISPLAY),
          1U,
          1U,
          ClearPolicy::AFTER_STABLE_READS,
          FallbackPolicy::NEVER,
          DocumentationConfidence::DESCRIBED,
          UserAction::NONE,
          RecoveryCondition::WHEN_BIT_CLEARS};
}

constexpr IncidentDefinition make_limit(uint16_t register_address, uint8_t bit, const char* key,
                                        const char* presentation_key) {
  return {incident_id(register_address, bit),
          register_address,
          bit,
          key,
          presentation_key,
          IncidentCategory::PROTECTION,
          IncidentSeverity::WARNING,
          IncidentEffect::DISPLAY | IncidentEffect::LIMIT_CAPACITY,
          1U,
          2U,
          ClearPolicy::AFTER_STABLE_READS,
          FallbackPolicy::NEVER,
          DocumentationConfidence::DESCRIBED,
          UserAction::WAIT_FOR_AUTOMATIC_RECOVERY,
          RecoveryCondition::AFTER_STABLE_READS};
}

constexpr IncidentDefinition make_diagnostic(uint16_t register_address, uint8_t bit, const char* key,
                                             const char* presentation_key) {
  IncidentDefinition definition = make_status(register_address, bit, key, presentation_key);
  definition.effects = effect_mask(IncidentEffect::NONE);
  definition.documentation_confidence = DocumentationConfidence::NAME_ONLY;
  return definition;
}

constexpr IncidentDefinition make_start_block(uint16_t register_address, uint8_t bit, const char* key,
                                              const char* presentation_key) {
  return {incident_id(register_address, bit),
          register_address,
          bit,
          key,
          presentation_key,
          IncidentCategory::PROTECTION,
          IncidentSeverity::WARNING,
          IncidentEffect::DISPLAY | IncidentEffect::BLOCK_START,
          1U,
          2U,
          ClearPolicy::AFTER_STABLE_READS,
          FallbackPolicy::NEVER,
          DocumentationConfidence::DESCRIBED,
          UserAction::WAIT_FOR_AUTOMATIC_RECOVERY,
          RecoveryCondition::PREHEAT_COMPLETE};
}

constexpr IncidentDefinition make_hard_fault(uint16_t register_address, uint8_t bit, const char* key,
                                             const char* presentation_key,
                                             DocumentationConfidence confidence = DocumentationConfidence::DESCRIBED,
                                             EffectMask additional_effects = effect_mask(IncidentEffect::NONE),
                                             ClearPolicy clear_policy = ClearPolicy::AFTER_STABLE_READS) {
  return {
      incident_id(register_address, bit),
      register_address,
      bit,
      key,
      presentation_key,
      IncidentCategory::FAULT,
      IncidentSeverity::FAULT,
      static_cast<EffectMask>((IncidentEffect::DISPLAY | IncidentEffect::BLOCK_START | IncidentEffect::STOP_COMPRESSOR |
                               IncidentEffect::MARK_HP_UNAVAILABLE | IncidentEffect::ALLOW_CM4) |
                              additional_effects),
      2U,
      3U,
      clear_policy,
      FallbackPolicy::AFTER_SYSTEM_GUARDS,
      confidence,
      UserAction::CONTACT_INSTALLER,
      clear_policy == ClearPolicy::AFTER_CONFIRMED_ODU_POWER_CYCLE
          ? RecoveryCondition::CONFIRMED_ODU_POWER_CYCLE
          : RecoveryCondition::STABLE_READS_AND_RECOVERY_WINDOW};
}

// Stable IDs are derived from register and bit:
// R2119.b0 = 1, R2120.b0 = 17, R2121.b0 = 33.
// Existing entries must not be renumbered when the catalog is extended.
static constexpr std::array<IncidentDefinition, 41U> kHpIncidentCatalog{{
    make_hard_fault(2119U, 0U, "main_line_current", "hp.main_line_current_fault"),
    make_hard_fault(2119U, 1U, "compressor_phase_current", "hp.compressor_phase_current_fault"),
    make_hard_fault(2119U, 2U, "ipm_module", "hp.ipm_module_fault"),
    make_status(2119U, 3U, "compressor_oil_return", "hp.compressor_oil_return_status"),
    make_hard_fault(2119U, 4U, "high_pressure_switch", "hp.high_pressure_switch_fault"),
    make_limit(2119U, 5U, "high_pressure_speed_limit", "hp.high_pressure_speed_limit"),
    make_start_block(2119U, 6U, "first_start_preheat", "hp.first_start_preheat"),
    make_hard_fault(2119U, 7U, "gas_discharge_temperature", "hp.gas_discharge_temperature_fault"),
    make_hard_fault(2119U, 8U, "evaporator_coil_temperature", "hp.evaporator_coil_temperature_fault"),
    make_hard_fault(2119U, 9U, "ac_voltage", "hp.ac_voltage_fault"),
    make_hard_fault(2119U, 10U, "ambient_temperature_range", "hp.ambient_temperature_range_fault"),
    make_limit(2119U, 11U, "ambient_temperature_frequency_limit", "hp.ambient_temperature_frequency_limit"),
    make_hard_fault(2119U, 12U, "low_pressure_switch", "hp.low_pressure_switch_fault"),
    make_limit(2119U, 13U, "low_pressure_speed_limit", "hp.low_pressure_speed_limit"),

    make_hard_fault(2120U, 0U, "ambient_temperature_sensor", "hp.ambient_temperature_sensor_fault"),
    make_hard_fault(2120U, 1U, "evaporator_coil_temperature_sensor", "hp.evaporator_coil_temperature_sensor_fault"),
    make_hard_fault(2120U, 2U, "gas_discharge_temperature_sensor", "hp.gas_discharge_temperature_sensor_fault"),
    make_hard_fault(2120U, 3U, "gas_return_temperature_sensor", "hp.gas_return_temperature_sensor_fault"),
    make_hard_fault(2120U, 4U, "evaporator_pressure_sensor_lock", "hp.evaporator_pressure_sensor_lock",
                    DocumentationConfidence::DESCRIBED, effect_mask(IncidentEffect::REQUIRE_CONFIRMED_ODU_POWER_CYCLE),
                    ClearPolicy::AFTER_CONFIRMED_ODU_POWER_CYCLE),
    make_hard_fault(2120U, 5U, "condenser_pressure_sensor", "hp.condenser_pressure_sensor_fault"),
    make_hard_fault(2120U, 6U, "high_pressure_switch_lock", "hp.high_pressure_switch_lock"),
    make_hard_fault(2120U, 7U, "low_pressure_switch_lock", "hp.low_pressure_switch_lock"),
    make_hard_fault(2120U, 8U, "fan", "hp.fan_fault"),
    make_hard_fault(2120U, 10U, "evaporating_pressure_lock", "hp.evaporating_pressure_lock"),
    make_hard_fault(2120U, 11U, "condenser_pressure_lock", "hp.condenser_pressure_lock"),
    make_hard_fault(2120U, 13U, "evi_pressure_sensor", "hp.evi_pressure_sensor_fault",
                    DocumentationConfidence::NAME_ONLY),
    make_hard_fault(2120U, 14U, "evi_inlet_temperature_sensor", "hp.evi_inlet_temperature_sensor_fault",
                    DocumentationConfidence::NAME_ONLY),
    make_hard_fault(2120U, 15U, "evi_outlet_temperature_sensor", "hp.evi_outlet_temperature_sensor_fault",
                    DocumentationConfidence::NAME_ONLY),

    make_hard_fault(2121U, 0U, "odu_master_slave_communication", "hp.odu_master_slave_communication_fault",
                    DocumentationConfidence::NAME_ONLY),
    make_hard_fault(2121U, 1U, "odu_control_pcb_communication", "hp.odu_control_pcb_communication_fault",
                    DocumentationConfidence::NAME_ONLY),
    make_hard_fault(2121U, 2U, "compressor_phase_current_failure", "hp.compressor_phase_current_failure",
                    DocumentationConfidence::NAME_ONLY),
    make_hard_fault(2121U, 3U, "compressor_phase_current_overload", "hp.compressor_phase_current_overload",
                    DocumentationConfidence::NAME_ONLY),
    make_hard_fault(2121U, 4U, "compressor_driver", "hp.compressor_driver_fault", DocumentationConfidence::NAME_ONLY),
    make_hard_fault(2121U, 5U, "module_vdc_voltage", "hp.module_vdc_voltage_fault", DocumentationConfidence::NAME_ONLY),
    make_hard_fault(2121U, 6U, "ac_current", "hp.ac_current_fault", DocumentationConfidence::NAME_ONLY),
    make_hard_fault(2121U, 7U, "eeprom", "hp.eeprom_fault", DocumentationConfidence::NAME_ONLY),
    make_hard_fault(2121U, 8U, "fan_drive_pcb", "hp.fan_drive_pcb_fault", DocumentationConfidence::NAME_ONLY),
    make_hard_fault(2121U, 9U, "inlet_water_temperature_sensor", "hp.inlet_water_temperature_sensor_fault",
                    DocumentationConfidence::NAME_ONLY),
    make_hard_fault(2121U, 10U, "outlet_water_temperature_sensor", "hp.outlet_water_temperature_sensor_fault",
                    DocumentationConfidence::NAME_ONLY),
    make_hard_fault(2121U, 11U, "inner_coil_temperature_sensor", "hp.inner_coil_temperature_sensor_fault",
                    DocumentationConfidence::NAME_ONLY),
    // Some ODU/pump combinations assert this bit during normal pump standby.
    // The pump type cannot be inferred from the ODU generation. Keep the raw
    // diagnostic, but never turn this bit alone into an alarm or control gate.
    make_diagnostic(2121U, 13U, "dc_water_pump", "hp.dc_water_pump_fault"),
}};

inline const IncidentDefinition* catalog_definition(uint16_t register_address, uint8_t bit) {
  for (const IncidentDefinition& definition : kHpIncidentCatalog) {
    if (definition.register_address == register_address && definition.bit == bit) {
      return &definition;
    }
  }
  return nullptr;
}

inline IncidentDefinition definition_for(uint16_t register_address, uint8_t bit) {
  const IncidentDefinition* definition = catalog_definition(register_address, bit);
  if (definition != nullptr) {
    return *definition;
  }

  // Unknown active bits are deliberately not ignored. Until they are
  // classified for the installed ODU firmware they block a new start, but do
  // not stop a running compressor or authorize boiler fallback.
  return {incident_id(register_address, bit),
          register_address,
          bit,
          "unclassified_odu_fault",
          "hp.unclassified_odu_fault",
          IncidentCategory::FAULT,
          IncidentSeverity::WARNING,
          IncidentEffect::DISPLAY | IncidentEffect::BLOCK_START,
          2U,
          3U,
          ClearPolicy::AFTER_STABLE_READS,
          FallbackPolicy::NEVER,
          DocumentationConfidence::REVIEW_REQUIRED,
          UserAction::CONTACT_INSTALLER,
          RecoveryCondition::REVIEW_REQUIRED};
}

inline IncidentDefinition definition_for_id(IncidentId id) {
  if (id == kNoIncident || id > static_cast<IncidentId>(kRawIncidentSlotCount)) {
    return {};
  }
  const size_t slot = static_cast<size_t>(id - 1U);
  return definition_for(register_for_slot(slot), bit_for_slot(slot));
}

}  // namespace oq_incidents
