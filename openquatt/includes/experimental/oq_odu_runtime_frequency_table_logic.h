#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace oq_odu_runtime_frequency {

inline constexpr uint16_t BASE_TABLE_START_ADDRESS = 3000U;
inline constexpr size_t BASE_LEVEL_COUNT = 11U;
inline constexpr size_t BASE_TABLE_REGISTER_COUNT = 22U;
inline constexpr uint16_t EXTENDED_TABLE_START_ADDRESS = 3050U;
inline constexpr size_t EXTENDED_LEVEL_COUNT = 21U;
inline constexpr size_t EXTENDED_TABLE_REGISTER_COUNT = 20U;
inline constexpr size_t MAX_TABLE_REGISTER_COUNT = BASE_TABLE_REGISTER_COUNT + EXTENDED_TABLE_REGISTER_COUNT;
inline constexpr uint8_t MIN_FREQUENCY_HZ = 0U;
inline constexpr uint8_t MAX_FREQUENCY_HZ = 120U;

using FrequencyValues = std::array<uint8_t, EXTENDED_LEVEL_COUNT>;

struct RuntimeFrequencyTables {
  FrequencyValues cooling{};
  FrequencyValues heating{};
  uint8_t level_count{BASE_LEVEL_COUNT};
};

struct RuntimeWriteRegister {
  uint16_t address{0U};
  uint16_t value{0U};
  bool valid{false};
};

inline size_t normalized_level_count(bool extended_layout) {
  return extended_layout ? EXTENDED_LEVEL_COUNT : BASE_LEVEL_COUNT;
}

inline size_t runtime_register_count(size_t level_count) {
  if (level_count == EXTENDED_LEVEL_COUNT) return MAX_TABLE_REGISTER_COUNT;
  if (level_count == BASE_LEVEL_COUNT) return BASE_TABLE_REGISTER_COUNT;
  return 0U;
}

inline bool valid_frequency(uint16_t value) { return value <= MAX_FREQUENCY_HZ; }

inline bool validate_monotonic_table(const FrequencyValues& values, size_t level_count) {
  if (level_count != BASE_LEVEL_COUNT && level_count != EXTENDED_LEVEL_COUNT) return false;
  if (values[0] != 0U) return false;
  for (size_t index = 0; index < level_count; ++index) {
    if (!valid_frequency(values[index])) return false;
    if (index > 0U && values[index] == 0U) return false;
    if (index > 0U && values[index] < values[index - 1U]) return false;
  }
  return true;
}

inline bool read_u16_word(const uint8_t* data, size_t size, size_t index, uint16_t& value) {
  const size_t offset = index * 2U;
  if (data == nullptr || size < offset + 2U) return false;
  value = (uint16_t(data[offset]) << 8U) | uint16_t(data[offset + 1U]);
  return true;
}

inline bool read_word_as_frequency(const uint8_t* data, size_t size, size_t index, uint8_t& value) {
  uint16_t raw = 0U;
  if (!read_u16_word(data, size, index, raw)) return false;
  if (!valid_frequency(raw)) return false;
  value = static_cast<uint8_t>(raw);
  return true;
}

inline bool parse_base_runtime_table(const uint8_t* data, size_t size, RuntimeFrequencyTables& tables, size_t& loaded) {
  loaded = 0U;
  tables.level_count = BASE_LEVEL_COUNT;
  for (size_t level = 0; level < BASE_LEVEL_COUNT; ++level) {
    if (!read_word_as_frequency(data, size, level, tables.cooling[level])) return false;
    ++loaded;
  }
  for (size_t level = 0; level < BASE_LEVEL_COUNT; ++level) {
    if (!read_word_as_frequency(data, size, BASE_LEVEL_COUNT + level, tables.heating[level])) return false;
    ++loaded;
  }
  return true;
}

inline bool parse_extended_runtime_table(const uint8_t* data, size_t size, RuntimeFrequencyTables& tables,
                                         size_t& loaded) {
  loaded = 0U;
  const size_t extension_levels = EXTENDED_LEVEL_COUNT - BASE_LEVEL_COUNT;
  for (size_t offset = 0; offset < extension_levels; ++offset) {
    const size_t level = BASE_LEVEL_COUNT + offset;
    if (!read_word_as_frequency(data, size, offset, tables.heating[level])) return false;
    ++loaded;
  }
  for (size_t offset = 0; offset < extension_levels; ++offset) {
    const size_t level = BASE_LEVEL_COUNT + offset;
    if (!read_word_as_frequency(data, size, extension_levels + offset, tables.cooling[level])) return false;
    ++loaded;
  }
  tables.level_count = EXTENDED_LEVEL_COUNT;
  return true;
}

inline bool tables_match(const RuntimeFrequencyTables& actual, const RuntimeFrequencyTables& expected) {
  if (actual.level_count != expected.level_count) return false;
  for (size_t level = 0; level < expected.level_count; ++level) {
    if (actual.cooling[level] != expected.cooling[level]) return false;
    if (actual.heating[level] != expected.heating[level]) return false;
  }
  return true;
}

inline uint16_t frequency_to_register(uint8_t value) { return value; }

inline RuntimeWriteRegister runtime_write_register(const RuntimeFrequencyTables& tables, size_t write_index) {
  if (write_index >= runtime_register_count(tables.level_count)) return {};

  if (write_index < BASE_LEVEL_COUNT) {
    return {static_cast<uint16_t>(BASE_TABLE_START_ADDRESS + write_index),
            frequency_to_register(tables.cooling[write_index]), true};
  }
  if (write_index < BASE_TABLE_REGISTER_COUNT) {
    const size_t level = write_index - BASE_LEVEL_COUNT;
    return {static_cast<uint16_t>(BASE_TABLE_START_ADDRESS + write_index), frequency_to_register(tables.heating[level]),
            true};
  }

  const size_t extension_index = write_index - BASE_TABLE_REGISTER_COUNT;
  const size_t extension_levels = EXTENDED_LEVEL_COUNT - BASE_LEVEL_COUNT;
  if (extension_index < extension_levels) {
    const size_t level = BASE_LEVEL_COUNT + extension_index;
    return {static_cast<uint16_t>(EXTENDED_TABLE_START_ADDRESS + extension_index),
            frequency_to_register(tables.heating[level]), true};
  }

  const size_t cooling_offset = extension_index - extension_levels;
  const size_t level = BASE_LEVEL_COUNT + cooling_offset;
  return {static_cast<uint16_t>(EXTENDED_TABLE_START_ADDRESS + extension_index),
          frequency_to_register(tables.cooling[level]), true};
}

}  // namespace oq_odu_runtime_frequency
