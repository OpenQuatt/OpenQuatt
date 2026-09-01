#pragma once

#ifndef OPENQUATT_OQ_ODU_RUNTIME_FREQUENCY_TABLE_H
#define OPENQUATT_OQ_ODU_RUNTIME_FREQUENCY_TABLE_H

#include <array>
#include <cstdio>
#include <cstdint>
#include <span>
#include <utility>

#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/number/number.h"
#include "esphome/components/openquatt_odu_eeprom_dump/OpenQuattOduEepromDump.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/log.h"
#include "oq_odu_runtime_frequency_table_logic.h"

namespace oq_odu_runtime_frequency {

static const char* const TAG = "oq_odu_eeprom";
static constexpr uint16_t GUARD_START_ADDRESS = 2099U;
static constexpr uint16_t GUARD_REGISTER_COUNT = 5U;
static constexpr size_t GUARD_WORKING_MODE_INDEX = 0U;
static constexpr size_t GUARD_COMPRESSOR_FREQUENCY_INDEX = 4U;

struct RuntimeFrequencyTableRefs {
  esphome::modbus_controller::ModbusController* controller;
  esphome::openquatt_odu_eeprom_dump::OpenQuattOduEepromDump* eeprom_dump;
  esphome::switch_::Switch* enable_switch;
  esphome::text_sensor::TextSensor* status;
  const char* prefix;
  bool extended_layout;
  std::array<esphome::number::Number*, EXTENDED_LEVEL_COUNT> cooling_desired;
  std::array<esphome::number::Number*, EXTENDED_LEVEL_COUNT> heating_desired;
  bool* table_loaded;
  uint32_t* write_operation_token;
};

inline void publish_status(const RuntimeFrequencyTableRefs& refs, const char* message) {
  refs.status->publish_state(message);
  ESP_LOGW(TAG, "%s%s", refs.prefix, message);
}

inline void publish_runtime_table(const RuntimeFrequencyTableRefs& refs, const RuntimeFrequencyTables& tables) {
  for (size_t level = 0; level < tables.level_count; ++level) {
    refs.cooling_desired[level]->publish_state(tables.cooling[level]);
    refs.heating_desired[level]->publish_state(tables.heating[level]);
  }
}

inline void publish_register_progress(const RuntimeFrequencyTableRefs& refs, const char* prefix, size_t loaded,
                                      size_t expected) {
  char status[64];
  snprintf(status, sizeof(status), "%s: %u/%u runtime registers", prefix, static_cast<unsigned>(loaded),
           static_cast<unsigned>(expected));
  publish_status(refs, status);
}

inline void queue_apply_readback(RuntimeFrequencyTableRefs refs, RuntimeFrequencyTables expected,
                                 uint32_t operation_token);

inline void queue_runtime_write_register(RuntimeFrequencyTableRefs refs, RuntimeFrequencyTables tables,
                                         size_t write_index, uint32_t operation_token) {
  if (*refs.write_operation_token != operation_token) return;
  const size_t register_count = runtime_register_count(tables.level_count);
  if (write_index >= register_count) {
    publish_status(refs, "WRITE_CONFIRMED: runtime writes acknowledged");
    queue_apply_readback(refs, tables, operation_token);
    return;
  }

  const auto target = runtime_write_register(tables, write_index);
  if (!target.valid) {
    publish_status(refs, "VERIFY_FAILED: invalid runtime register mapping");
    return;
  }
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_write_single_command(refs.controller, target.address,
                                                                                        target.value);
  cmd.on_data_func = [refs, tables, write_index, operation_token](esphome::modbus::EntityType, uint16_t,
                                                                  std::span<const uint8_t>) {
    queue_runtime_write_register(refs, tables, write_index + 1U, operation_token);
  };
  refs.controller->queue_command(std::move(cmd));
}

inline void queue_runtime_write(RuntimeFrequencyTableRefs refs, RuntimeFrequencyTables tables,
                                uint32_t operation_token) {
  if (*refs.write_operation_token != operation_token) return;
  refs.enable_switch->turn_off();
  publish_status(refs, "WRITE_QUEUED: runtime table write requested");
  queue_runtime_write_register(refs, tables, 0U, operation_token);
}

inline void queue_guarded_runtime_write(RuntimeFrequencyTableRefs refs, RuntimeFrequencyTables tables,
                                        uint32_t operation_token) {
  if (*refs.write_operation_token != operation_token) return;
  publish_status(refs, "GUARD_READ_REQUESTED: checking ODU state");
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_read_command(
      refs.controller, esphome::modbus::EntityType::HOLDING, GUARD_START_ADDRESS, GUARD_REGISTER_COUNT,
      [refs, tables, operation_token](esphome::modbus::EntityType, uint16_t, std::span<const uint8_t> data) {
        if (*refs.write_operation_token != operation_token) return;
        uint16_t working_mode = 0U;
        uint16_t compressor_hz = 0U;
        if (!read_u16_word(data.data(), data.size(), GUARD_WORKING_MODE_INDEX, working_mode)) {
          publish_status(refs, "BLOCKED: ODU mode unknown");
          return;
        }
        if (!read_u16_word(data.data(), data.size(), GUARD_COMPRESSOR_FREQUENCY_INDEX, compressor_hz)) {
          publish_status(refs, "BLOCKED: compressor frequency unknown");
          return;
        }
        if (working_mode != 0U) {
          publish_status(refs, "BLOCKED: ODU is not in standby");
          return;
        }
        if (compressor_hz > 0U) {
          publish_status(refs, "BLOCKED: compressor is running");
          return;
        }
        queue_runtime_write(refs, tables, operation_token);
      });
  refs.controller->queue_command(std::move(cmd));
}

inline void finish_apply_readback(const RuntimeFrequencyTableRefs& refs, const RuntimeFrequencyTables& actual,
                                  const RuntimeFrequencyTables& expected, uint32_t operation_token) {
  if (*refs.write_operation_token != operation_token) return;
  if (!tables_match(actual, expected)) {
    publish_status(refs, "VERIFY_FAILED: readback mismatch");
    return;
  }
  *refs.table_loaded = true;
  publish_runtime_table(refs, actual);
  publish_status(refs, "APPLIED: runtime table written and read back");
}

inline void queue_apply_extension_readback(RuntimeFrequencyTableRefs refs, RuntimeFrequencyTables actual,
                                           RuntimeFrequencyTables expected, uint32_t operation_token) {
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_read_command(
      refs.controller, esphome::modbus::EntityType::HOLDING, EXTENDED_TABLE_START_ADDRESS,
      EXTENDED_TABLE_REGISTER_COUNT,
      [refs, actual, expected, operation_token](esphome::modbus::EntityType, uint16_t start_address,
                                                std::span<const uint8_t> data) mutable {
        if (*refs.write_operation_token != operation_token || start_address != EXTENDED_TABLE_START_ADDRESS) return;
        size_t loaded = 0U;
        if (!parse_extended_runtime_table(data.data(), data.size(), actual, loaded)) {
          publish_register_progress(refs, "VERIFY_FAILED", BASE_TABLE_REGISTER_COUNT + loaded,
                                    MAX_TABLE_REGISTER_COUNT);
          return;
        }
        finish_apply_readback(refs, actual, expected, operation_token);
      });
  refs.controller->queue_command(std::move(cmd));
}

inline void queue_apply_readback(RuntimeFrequencyTableRefs refs, RuntimeFrequencyTables expected,
                                 uint32_t operation_token) {
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_read_command(
      refs.controller, esphome::modbus::EntityType::HOLDING, BASE_TABLE_START_ADDRESS, BASE_TABLE_REGISTER_COUNT,
      [refs, expected, operation_token](esphome::modbus::EntityType, uint16_t start_address,
                                        std::span<const uint8_t> data) {
        if (*refs.write_operation_token != operation_token || start_address != BASE_TABLE_START_ADDRESS) return;
        RuntimeFrequencyTables actual;
        size_t loaded = 0U;
        if (!parse_base_runtime_table(data.data(), data.size(), actual, loaded)) {
          publish_register_progress(refs, "VERIFY_FAILED", loaded, runtime_register_count(expected.level_count));
          return;
        }
        if (expected.level_count == EXTENDED_LEVEL_COUNT) {
          queue_apply_extension_readback(refs, actual, expected, operation_token);
          return;
        }
        finish_apply_readback(refs, actual, expected, operation_token);
      });
  refs.controller->queue_command(std::move(cmd));
}

inline void finish_load(const RuntimeFrequencyTableRefs& refs, const RuntimeFrequencyTables& tables) {
  *refs.table_loaded = true;
  publish_runtime_table(refs, tables);
  publish_register_progress(refs, "LOADED", runtime_register_count(tables.level_count),
                            runtime_register_count(tables.level_count));
}

inline void queue_extension_load(RuntimeFrequencyTableRefs refs, RuntimeFrequencyTables tables,
                                 uint32_t operation_token) {
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_read_command(
      refs.controller, esphome::modbus::EntityType::HOLDING, EXTENDED_TABLE_START_ADDRESS,
      EXTENDED_TABLE_REGISTER_COUNT,
      [refs, tables, operation_token](esphome::modbus::EntityType, uint16_t start_address,
                                      std::span<const uint8_t> data) mutable {
        if (*refs.write_operation_token != operation_token || start_address != EXTENDED_TABLE_START_ADDRESS) return;
        size_t loaded = 0U;
        if (!parse_extended_runtime_table(data.data(), data.size(), tables, loaded)) {
          publish_register_progress(refs, "LOAD_FAILED", BASE_TABLE_REGISTER_COUNT + loaded, MAX_TABLE_REGISTER_COUNT);
          return;
        }
        finish_load(refs, tables);
      });
  refs.controller->queue_command(std::move(cmd));
}

inline void load_runtime_table(RuntimeFrequencyTableRefs refs) {
  if (refs.eeprom_dump != nullptr && refs.eeprom_dump->is_active()) {
    publish_status(refs, "BLOCKED: EEPROM dump active");
    return;
  }
  *refs.table_loaded = false;
  publish_status(refs, "LOAD_REQUESTED");
  const uint32_t operation_token = ++*refs.write_operation_token;
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_read_command(
      refs.controller, esphome::modbus::EntityType::HOLDING, BASE_TABLE_START_ADDRESS, BASE_TABLE_REGISTER_COUNT,
      [refs, operation_token](esphome::modbus::EntityType, uint16_t start_address, std::span<const uint8_t> data) {
        if (*refs.write_operation_token != operation_token || start_address != BASE_TABLE_START_ADDRESS) return;
        RuntimeFrequencyTables tables;
        size_t loaded = 0U;
        if (!parse_base_runtime_table(data.data(), data.size(), tables, loaded)) {
          publish_register_progress(refs, "LOAD_FAILED", loaded,
                                    refs.extended_layout ? MAX_TABLE_REGISTER_COUNT : BASE_TABLE_REGISTER_COUNT);
          return;
        }
        if (refs.extended_layout) {
          queue_extension_load(refs, tables, operation_token);
          return;
        }
        finish_load(refs, tables);
      });
  refs.controller->queue_command(std::move(cmd));
}

inline bool read_desired_values(const std::array<esphome::number::Number*, EXTENDED_LEVEL_COUNT>& entities,
                                size_t level_count, FrequencyValues& values) {
  for (size_t level = 0; level < level_count; ++level) values[level] = entities[level]->state;
  return validate_monotonic_table(values, level_count);
}

inline void apply_runtime_table(RuntimeFrequencyTableRefs refs, bool enabled) {
  if (refs.eeprom_dump != nullptr && refs.eeprom_dump->is_active()) {
    publish_status(refs, "BLOCKED: EEPROM dump active");
    return;
  }
  if (!enabled) {
    publish_status(refs, "BLOCKED: enable switch is off");
    return;
  }
  if (!*refs.table_loaded) {
    publish_status(refs, "BLOCKED: load ODU runtime table first");
    return;
  }

  RuntimeFrequencyTables tables;
  tables.level_count = normalized_level_count(refs.extended_layout);
  if (!read_desired_values(refs.cooling_desired, tables.level_count, tables.cooling)) {
    publish_status(refs, "BLOCKED: invalid cooling table");
    return;
  }
  if (!read_desired_values(refs.heating_desired, tables.level_count, tables.heating)) {
    publish_status(refs, "BLOCKED: invalid heating table");
    return;
  }

  const uint32_t operation_token = ++*refs.write_operation_token;
  queue_guarded_runtime_write(refs, tables, operation_token);
}

}  // namespace oq_odu_runtime_frequency

#endif  // OPENQUATT_OQ_ODU_RUNTIME_FREQUENCY_TABLE_H
