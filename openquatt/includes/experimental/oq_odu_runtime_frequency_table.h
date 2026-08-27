#pragma once

#ifndef OPENQUATT_OQ_ODU_RUNTIME_FREQUENCY_TABLE_H
#define OPENQUATT_OQ_ODU_RUNTIME_FREQUENCY_TABLE_H

#include <array>
#include <cmath>
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

namespace oq_odu_runtime_frequency {

static const char* const TAG = "oq_odu_eeprom";
static constexpr uint16_t RUNTIME_TABLE_START_ADDRESS = 3000;
static constexpr uint16_t RUNTIME_TABLE_REGISTER_COUNT = 22;
static constexpr uint16_t GUARD_START_ADDRESS = 2099;
static constexpr uint16_t GUARD_REGISTER_COUNT = 5;
static constexpr size_t GUARD_WORKING_MODE_INDEX = 0;
static constexpr size_t GUARD_COMPRESSOR_FREQUENCY_INDEX = 4;
static constexpr float MIN_FREQUENCY_HZ = 0.0f;
static constexpr float MAX_FREQUENCY_HZ = 120.0f;

struct RuntimeFrequencyTableRefs {
  esphome::modbus_controller::ModbusController* controller;
  esphome::openquatt_odu_eeprom_dump::OpenQuattOduEepromDump* eeprom_dump;
  esphome::switch_::Switch* enable_switch;
  esphome::text_sensor::TextSensor* status;
  const char* prefix;
  std::array<esphome::number::Number*, 11> cooling_desired;
  std::array<esphome::number::Number*, 11> heating_desired;
  uint32_t* write_operation_token;
};

inline void publish_status(const RuntimeFrequencyTableRefs& refs, const char* message) {
  refs.status->publish_state(message);
  ESP_LOGW(TAG, "%s%s", refs.prefix, message);
}

inline bool valid_frequency(float value) {
  return !std::isnan(value) && value >= MIN_FREQUENCY_HZ && value <= MAX_FREQUENCY_HZ;
}

inline bool validate_monotonic_table(const std::array<float, 11>& values) {
  for (size_t i = 0; i < values.size(); i++) {
    if (!valid_frequency(values[i])) return false;
    if (i > 0 && values[i] < values[i - 1]) return false;
  }
  return true;
}

inline bool read_u16_word(std::span<const uint8_t> data, size_t index, uint16_t& value) {
  const size_t offset = index * 2U;
  if (data.size() < offset + 2U) return false;
  value = (uint16_t(data[offset]) << 8) | uint16_t(data[offset + 1U]);
  return true;
}

inline bool read_word_as_frequency(std::span<const uint8_t> data, size_t index, float& value) {
  uint16_t raw = 0;
  if (!read_u16_word(data, index, raw)) return false;
  value = float(raw);
  return valid_frequency(value);
}

inline void publish_loaded_value(esphome::number::Number* target, float value) { target->publish_state(value); }

inline bool parse_runtime_table(std::span<const uint8_t> data, std::array<float, 11>& cooling,
                                std::array<float, 11>& heating, int& loaded) {
  loaded = 0;
  float value = NAN;
  for (size_t i = 0; i < cooling.size(); i++) {
    if (!read_word_as_frequency(data, i, value)) return false;
    cooling[i] = value;
    loaded++;
  }
  for (size_t i = 0; i < heating.size(); i++) {
    if (!read_word_as_frequency(data, i + cooling.size(), value)) return false;
    heating[i] = value;
    loaded++;
  }
  return true;
}

inline void publish_runtime_table(const RuntimeFrequencyTableRefs& refs, const std::array<float, 11>& cooling,
                                  const std::array<float, 11>& heating) {
  for (size_t i = 0; i < cooling.size(); i++) publish_loaded_value(refs.cooling_desired[i], cooling[i]);
  for (size_t i = 0; i < heating.size(); i++) publish_loaded_value(refs.heating_desired[i], heating[i]);
}

inline bool tables_match(const std::array<float, 11>& actual, const std::array<float, 11>& expected) {
  for (size_t i = 0; i < actual.size(); i++) {
    if (lroundf(actual[i]) != lroundf(expected[i])) return false;
  }
  return true;
}

inline uint16_t frequency_to_register(float value) { return static_cast<uint16_t>(lroundf(value)); }

inline uint16_t runtime_write_value(const std::array<float, 11>& cooling, const std::array<float, 11>& heating,
                                    size_t index) {
  if (index < cooling.size()) return frequency_to_register(cooling[index]);
  return frequency_to_register(heating[index - cooling.size()]);
}

inline void queue_apply_readback(RuntimeFrequencyTableRefs refs, std::array<float, 11> expected_cooling,
                                 std::array<float, 11> expected_heating, uint32_t operation_token);

inline void queue_runtime_write_register(RuntimeFrequencyTableRefs refs, std::array<float, 11> cooling,
                                         std::array<float, 11> heating, size_t index, uint32_t operation_token) {
  if (*refs.write_operation_token != operation_token) return;
  if (index >= RUNTIME_TABLE_REGISTER_COUNT) {
    publish_status(refs, "WRITE_CONFIRMED: runtime writes acknowledged");
    queue_apply_readback(refs, cooling, heating, operation_token);
    return;
  }

  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_write_single_command(
      refs.controller, static_cast<uint16_t>(RUNTIME_TABLE_START_ADDRESS + index),
      runtime_write_value(cooling, heating, index));
  cmd.on_data_func = [refs, cooling, heating, index, operation_token](esphome::modbus::EntityType register_type,
                                                                      uint16_t start_address,
                                                                      std::span<const uint8_t> data) {
    queue_runtime_write_register(refs, cooling, heating, index + 1U, operation_token);
  };
  refs.controller->queue_command(std::move(cmd));
}

inline void queue_runtime_write(RuntimeFrequencyTableRefs refs, std::array<float, 11> cooling,
                                std::array<float, 11> heating) {
  refs.enable_switch->turn_off();
  publish_status(refs, "WRITE_QUEUED: runtime table write requested");
  const uint32_t operation_token = ++*refs.write_operation_token;
  queue_runtime_write_register(refs, cooling, heating, 0U, operation_token);
}

inline void queue_guarded_runtime_write(RuntimeFrequencyTableRefs refs, std::array<float, 11> cooling,
                                        std::array<float, 11> heating) {
  publish_status(refs, "GUARD_READ_REQUESTED: checking ODU state");
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_read_command(
      refs.controller, esphome::modbus::EntityType::HOLDING, GUARD_START_ADDRESS, GUARD_REGISTER_COUNT,
      [refs, cooling, heating](esphome::modbus::EntityType register_type, uint16_t start_address,
                               std::span<const uint8_t> data) {
        uint16_t working_mode = 0;
        uint16_t compressor_hz = 0;
        if (!read_u16_word(data, GUARD_WORKING_MODE_INDEX, working_mode)) {
          publish_status(refs, "BLOCKED: ODU mode unknown");
          return;
        }
        if (!read_u16_word(data, GUARD_COMPRESSOR_FREQUENCY_INDEX, compressor_hz)) {
          publish_status(refs, "BLOCKED: compressor frequency unknown");
          return;
        }
        if (working_mode != 0) {
          publish_status(refs, "BLOCKED: ODU is not in standby");
          return;
        }
        if (compressor_hz > 0) {
          publish_status(refs, "BLOCKED: compressor is running");
          return;
        }
        queue_runtime_write(refs, cooling, heating);
      });
  refs.controller->queue_command(std::move(cmd));
}

inline void queue_apply_readback(RuntimeFrequencyTableRefs refs, std::array<float, 11> expected_cooling,
                                 std::array<float, 11> expected_heating, uint32_t operation_token) {
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_read_command(
      refs.controller, esphome::modbus::EntityType::HOLDING, RUNTIME_TABLE_START_ADDRESS, RUNTIME_TABLE_REGISTER_COUNT,
      [refs, expected_cooling, expected_heating, operation_token](
          esphome::modbus::EntityType register_type, uint16_t start_address, std::span<const uint8_t> data) {
        if (*refs.write_operation_token != operation_token) return;
        std::array<float, 11> cooling{};
        std::array<float, 11> heating{};
        int loaded = 0;
        if (!parse_runtime_table(data, cooling, heating, loaded)) {
          char status[56];
          snprintf(status, sizeof(status), "VERIFY_FAILED: %d/22 runtime registers", loaded);
          publish_status(refs, status);
          return;
        }
        publish_runtime_table(refs, cooling, heating);
        if (!tables_match(cooling, expected_cooling) || !tables_match(heating, expected_heating)) {
          publish_status(refs, "VERIFY_FAILED: readback mismatch");
          return;
        }
        publish_status(refs, "APPLIED: runtime table written and read back");
      });
  refs.controller->queue_command(std::move(cmd));
}

inline void load_runtime_table(RuntimeFrequencyTableRefs refs) {
  if (refs.eeprom_dump != nullptr && refs.eeprom_dump->is_active()) {
    publish_status(refs, "BLOCKED: EEPROM dump active");
    return;
  }
  publish_status(refs, "LOAD_REQUESTED");
  auto cmd = esphome::modbus_controller::ModbusCommandItem::create_read_command(
      refs.controller, esphome::modbus::EntityType::HOLDING, RUNTIME_TABLE_START_ADDRESS, RUNTIME_TABLE_REGISTER_COUNT,
      [refs](esphome::modbus::EntityType register_type, uint16_t start_address, std::span<const uint8_t> data) {
        std::array<float, 11> cooling{};
        std::array<float, 11> heating{};
        int loaded = 0;
        if (!parse_runtime_table(data, cooling, heating, loaded)) {
          char status[52];
          snprintf(status, sizeof(status), "LOAD_FAILED: %d/22 runtime registers", loaded);
          publish_status(refs, status);
          return;
        }
        publish_runtime_table(refs, cooling, heating);

        char status[48];
        snprintf(status, sizeof(status), "LOADED: %d/22 runtime registers", loaded);
        publish_status(refs, status);
      });
  refs.controller->queue_command(std::move(cmd));
}

inline bool read_desired_values(const std::array<esphome::number::Number*, 11>& entities,
                                std::array<float, 11>& values) {
  for (size_t i = 0; i < entities.size(); i++) values[i] = entities[i]->state;
  return validate_monotonic_table(values);
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

  std::array<float, 11> cooling{};
  std::array<float, 11> heating{};
  if (!read_desired_values(refs.cooling_desired, cooling)) {
    publish_status(refs, "BLOCKED: invalid cooling table");
    return;
  }
  if (!read_desired_values(refs.heating_desired, heating)) {
    publish_status(refs, "BLOCKED: invalid heating table");
    return;
  }

  queue_guarded_runtime_write(refs, cooling, heating);
}

}  // namespace oq_odu_runtime_frequency

#endif  // OPENQUATT_OQ_ODU_RUNTIME_FREQUENCY_TABLE_H
