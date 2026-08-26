#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oq_odu_generation.h"

namespace oq_odu {

inline constexpr uint16_t BASE_FREQUENCY_TABLE_REGISTER = 3000U;
inline constexpr uint16_t BASE_FREQUENCY_TABLE_REGISTER_COUNT = 22U;
inline constexpr uint16_t EXTENDED_FREQUENCY_TABLE_REGISTER = 3050U;
inline constexpr uint16_t EXTENDED_FREQUENCY_TABLE_REGISTER_COUNT = 20U;
inline constexpr size_t LEGACY_FREQUENCY_LEVEL_COUNT = 11U;
inline constexpr size_t EXTENDED_FREQUENCY_LEVEL_COUNT = 21U;
inline constexpr uint16_t MAX_RUNTIME_FREQUENCY_HZ = 120U;
inline constexpr size_t RUNTIME_FREQUENCY_SNAPSHOT_STORAGE_SIZE = 46U;

struct FrequencyTable {
  std::array<uint8_t, EXTENDED_FREQUENCY_LEVEL_COUNT> hz{};
  uint8_t level_count{0};
  bool valid{false};
};

struct RuntimeFrequencySnapshot {
  Variant variant{Variant::UNKNOWN};
  FrequencyTable cooling{};
  FrequencyTable heating{};
};

struct ExtensionParseResult {
  bool response_complete{false};
  bool heating_valid{false};
  bool cooling_valid{false};
};

inline bool validate_frequency_table(const FrequencyTable& table);

using RuntimeFrequencySnapshotStorage = std::array<uint8_t, RUNTIME_FREQUENCY_SNAPSHOT_STORAGE_SIZE>;

inline RuntimeFrequencySnapshotStorage encode_runtime_frequency_snapshot(const RuntimeFrequencySnapshot& snapshot) {
  RuntimeFrequencySnapshotStorage storage{};
  storage[0] = static_cast<uint8_t>(snapshot.variant);
  storage[1] = snapshot.cooling.level_count;
  storage[2] = snapshot.heating.level_count;
  storage[3] = static_cast<uint8_t>((snapshot.cooling.valid ? 0x01U : 0U) | (snapshot.heating.valid ? 0x02U : 0U));
  for (size_t index = 0; index < EXTENDED_FREQUENCY_LEVEL_COUNT; ++index) {
    storage[4U + index] = snapshot.cooling.hz[index];
    storage[25U + index] = snapshot.heating.hz[index];
  }
  return storage;
}

inline RuntimeFrequencySnapshot decode_runtime_frequency_snapshot(const RuntimeFrequencySnapshotStorage& storage) {
  RuntimeFrequencySnapshot snapshot;
  if (storage[0] <= static_cast<uint8_t>(Variant::V2_NEW_MODEL)) {
    snapshot.variant = static_cast<Variant>(storage[0]);
  }
  snapshot.cooling.level_count = storage[1];
  snapshot.heating.level_count = storage[2];
  for (size_t index = 0; index < EXTENDED_FREQUENCY_LEVEL_COUNT; ++index) {
    snapshot.cooling.hz[index] = storage[4U + index];
    snapshot.heating.hz[index] = storage[25U + index];
  }
  snapshot.cooling.valid = (storage[3] & 0x01U) != 0U && validate_frequency_table(snapshot.cooling);
  snapshot.heating.valid = (storage[3] & 0x02U) != 0U && validate_frequency_table(snapshot.heating);
  return snapshot;
}

inline bool read_frequency_word(const uint8_t* data, size_t size, size_t word_index, uint16_t& value) {
  const size_t offset = word_index * 2U;
  if (data == nullptr || size < offset + 2U) return false;
  value = static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8U) | data[offset + 1U]);
  return true;
}

inline bool validate_frequency_table(const FrequencyTable& table) {
  if (table.level_count != LEGACY_FREQUENCY_LEVEL_COUNT && table.level_count != EXTENDED_FREQUENCY_LEVEL_COUNT) {
    return false;
  }
  if (table.hz[0] != 0U) return false;

  uint8_t previous = 0U;
  for (size_t level = 1; level < table.level_count; ++level) {
    const uint8_t frequency = table.hz[level];
    if (frequency == 0U || frequency > MAX_RUNTIME_FREQUENCY_HZ || frequency < previous) return false;
    previous = frequency;
  }
  return true;
}

inline FrequencyTable parse_frequency_table(const uint8_t* data, size_t size, size_t word_offset, size_t level_count) {
  FrequencyTable table;
  if (level_count != LEGACY_FREQUENCY_LEVEL_COUNT && level_count != EXTENDED_FREQUENCY_LEVEL_COUNT) return table;

  table.level_count = static_cast<uint8_t>(level_count);
  for (size_t level = 0; level < level_count; ++level) {
    uint16_t frequency = 0U;
    if (!read_frequency_word(data, size, word_offset + level, frequency) || frequency > MAX_RUNTIME_FREQUENCY_HZ) {
      table.valid = false;
      return table;
    }
    table.hz[level] = static_cast<uint8_t>(frequency);
  }
  table.valid = validate_frequency_table(table);
  return table;
}

inline RuntimeFrequencySnapshot parse_base_frequency_table_response(const uint8_t* data, size_t size, Variant variant) {
  RuntimeFrequencySnapshot snapshot;
  snapshot.variant = variant;
  snapshot.cooling = parse_frequency_table(data, size, 0U, LEGACY_FREQUENCY_LEVEL_COUNT);
  snapshot.heating = parse_frequency_table(data, size, LEGACY_FREQUENCY_LEVEL_COUNT, LEGACY_FREQUENCY_LEVEL_COUNT);
  return snapshot;
}

inline bool extend_frequency_table(FrequencyTable& table, const uint8_t* data, size_t size, size_t word_offset) {
  if (!table.valid || table.level_count != LEGACY_FREQUENCY_LEVEL_COUNT) return false;

  FrequencyTable extended = table;
  extended.level_count = static_cast<uint8_t>(EXTENDED_FREQUENCY_LEVEL_COUNT);
  for (size_t index = 0; index < EXTENDED_FREQUENCY_LEVEL_COUNT - LEGACY_FREQUENCY_LEVEL_COUNT; ++index) {
    uint16_t frequency = 0U;
    if (!read_frequency_word(data, size, word_offset + index, frequency) || frequency > MAX_RUNTIME_FREQUENCY_HZ) {
      return false;
    }
    extended.hz[LEGACY_FREQUENCY_LEVEL_COUNT + index] = static_cast<uint8_t>(frequency);
  }
  if (!validate_frequency_table(extended)) return false;
  table = extended;
  return true;
}

inline ExtensionParseResult apply_v2_extension_frequency_table_response(RuntimeFrequencySnapshot& snapshot,
                                                                        const uint8_t* data, size_t size) {
  ExtensionParseResult result;
  if (snapshot.variant != Variant::V2_NEW_MODEL || data == nullptr ||
      size < static_cast<size_t>(EXTENDED_FREQUENCY_TABLE_REGISTER_COUNT) * 2U) {
    return result;
  }

  result.response_complete = true;
  result.heating_valid = extend_frequency_table(snapshot.heating, data, size, 0U);
  result.cooling_valid = extend_frequency_table(snapshot.cooling, data, size,
                                                EXTENDED_FREQUENCY_LEVEL_COUNT - LEGACY_FREQUENCY_LEVEL_COUNT);
  return result;
}

inline const FrequencyTable& frequency_table_for_mode(const RuntimeFrequencySnapshot& snapshot, int mode_code) {
  return mode_code == 1 ? snapshot.cooling : snapshot.heating;
}

inline bool has_valid_frequency_table(const RuntimeFrequencySnapshot& snapshot, int mode_code) {
  if (mode_code != 1 && mode_code != 2) return false;
  return frequency_table_for_mode(snapshot, mode_code).valid;
}

inline bool has_extended_frequency_table(const RuntimeFrequencySnapshot& snapshot, int mode_code) {
  if (snapshot.variant != Variant::V2_NEW_MODEL || !has_valid_frequency_table(snapshot, mode_code)) return false;
  return frequency_table_for_mode(snapshot, mode_code).level_count == EXTENDED_FREQUENCY_LEVEL_COUNT;
}

inline int resolve_nearest_physical_level(const FrequencyTable& table, int requested_frequency_hz,
                                          int maximum_frequency_hz) {
  if (!table.valid || requested_frequency_hz <= 0 || maximum_frequency_hz <= 0) return 0;

  int selected_level = 0;
  int selected_frequency = 0;
  int selected_delta = 256;
  for (size_t level = 1; level < table.level_count; ++level) {
    const int frequency = table.hz[level];
    if (frequency > maximum_frequency_hz) break;
    const int delta_signed = frequency - requested_frequency_hz;
    const int delta = delta_signed < 0 ? -delta_signed : delta_signed;
    if (delta < selected_delta || (delta == selected_delta && frequency < selected_frequency)) {
      selected_level = static_cast<int>(level);
      selected_frequency = frequency;
      selected_delta = delta;
    }
  }
  return selected_level;
}

inline int frequency_for_physical_level(const FrequencyTable& table, int physical_level) {
  if (!table.valid || physical_level < 0 || physical_level >= table.level_count) return 0;
  return table.hz[static_cast<size_t>(physical_level)];
}

}  // namespace oq_odu
