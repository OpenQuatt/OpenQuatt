#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

#include "../odu/oq_odu_compressor_levels.h"

namespace oq_frequency_policy {

inline constexpr int MAX_POLICY_FREQUENCY_HZ = 120;
inline constexpr uint8_t STORAGE_VERSION = 1;
inline constexpr uint8_t DAY_HEATING_CAP_MIGRATED = 0x01U;
inline constexpr uint8_t DAY_COOLING_CAP_MIGRATED = 0x02U;
inline constexpr uint8_t SILENT_HEATING_CAP_MIGRATED = 0x04U;
inline constexpr uint8_t SILENT_COOLING_CAP_MIGRATED = 0x08U;
inline constexpr uint8_t CAPS_MIGRATED =
    DAY_HEATING_CAP_MIGRATED | DAY_COOLING_CAP_MIGRATED | SILENT_HEATING_CAP_MIGRATED | SILENT_COOLING_CAP_MIGRATED;
inline constexpr uint8_t HP1_EXCLUSIONS_MIGRATED = 0x10U;
inline constexpr uint8_t HP2_EXCLUSIONS_MIGRATED = 0x20U;

enum StorageIndex : size_t {
  VERSION = 0,
  MIGRATION_FLAGS = 1,
  DAY_HEATING_MAX_HZ = 2,
  DAY_COOLING_MAX_HZ = 3,
  SILENT_HEATING_MAX_HZ = 4,
  SILENT_COOLING_MAX_HZ = 5,
  HP1_HEATING_A_MIN_HZ = 6,
  HP1_HEATING_A_MAX_HZ = 7,
  HP1_HEATING_B_MIN_HZ = 8,
  HP1_HEATING_B_MAX_HZ = 9,
  HP1_COOLING_A_MIN_HZ = 10,
  HP1_COOLING_A_MAX_HZ = 11,
  HP1_COOLING_B_MIN_HZ = 12,
  HP1_COOLING_B_MAX_HZ = 13,
  HP2_HEATING_A_MIN_HZ = 14,
  HP2_HEATING_A_MAX_HZ = 15,
  HP2_HEATING_B_MIN_HZ = 16,
  HP2_HEATING_B_MAX_HZ = 17,
  HP2_COOLING_A_MIN_HZ = 18,
  HP2_COOLING_A_MAX_HZ = 19,
  HP2_COOLING_B_MIN_HZ = 20,
  HP2_COOLING_B_MAX_HZ = 21,
  STORAGE_SIZE = 22,
};

using Storage = std::array<uint8_t, STORAGE_SIZE>;

inline bool storage_has_migration(const Storage& storage, uint8_t flag) {
  return storage[VERSION] == STORAGE_VERSION && (storage[MIGRATION_FLAGS] & flag) == flag;
}

inline uint8_t exclusions_migrated_flag(int hp_index) {
  return hp_index == 2 ? HP2_EXCLUSIONS_MIGRATED : HP1_EXCLUSIONS_MIGRATED;
}

inline StorageIndex storage_index_for_hp(int hp_index, StorageIndex hp1_index) {
  const size_t index = static_cast<size_t>(hp1_index) + (hp_index == 2 ? 8U : 0U);
  return static_cast<StorageIndex>(index);
}

inline int stored_frequency_hz(const Storage& storage, StorageIndex index, int fallback_hz) {
  if (storage[VERSION] != STORAGE_VERSION || index <= MIGRATION_FLAGS || index >= STORAGE_SIZE) return fallback_hz;
  return storage[index];
}

inline void store_frequency_hz(Storage& storage, StorageIndex index, int frequency_hz) {
  if (index <= MIGRATION_FLAGS || index >= STORAGE_SIZE) return;
  if (storage[VERSION] != STORAGE_VERSION) storage.fill(0);
  storage[VERSION] = STORAGE_VERSION;
  storage[index] = static_cast<uint8_t>(std::max(0, std::min(MAX_POLICY_FREQUENCY_HZ, frequency_hz)));
}

inline void mark_migrated(Storage& storage, uint8_t flag) {
  if (storage[VERSION] != STORAGE_VERSION) storage.fill(0);
  storage[VERSION] = STORAGE_VERSION;
  storage[MIGRATION_FLAGS] |= flag;
}

inline void clear_migrated(Storage& storage, uint8_t flag) {
  if (storage[VERSION] != STORAGE_VERSION) storage.fill(0);
  storage[VERSION] = STORAGE_VERSION;
  storage[MIGRATION_FLAGS] &= static_cast<uint8_t>(~flag);
}

inline void store_configured_frequency_hz(Storage& storage, StorageIndex index, int frequency_hz,
                                          uint8_t migration_flag) {
  store_frequency_hz(storage, index, frequency_hz);
  mark_migrated(storage, migration_flag);
}

struct FrequencyRange {
  int min_hz{0};
  int max_hz{0};
};

struct ExcludedFrequencyRanges {
  FrequencyRange a{};
  FrequencyRange b{};
};

inline bool configuration_matches_variant(bool configured_v2, oq_odu::Variant variant) {
  if (variant == oq_odu::Variant::V2_OLD_MODEL || variant == oq_odu::Variant::V2_NEW_MODEL) return configured_v2;
  if (variant == oq_odu::Variant::V1 || variant == oq_odu::Variant::V1_5) return !configured_v2;
  return false;
}

inline int conservative_shared_cap_hz(int hp1_hz, int hp2_hz, bool has_hp2) {
  if (hp1_hz < 0 || (has_hp2 && hp2_hz < 0)) return -1;
  return has_hp2 ? std::min(hp1_hz, hp2_hz) : hp1_hz;
}

inline bool frequency_range_disabled(const FrequencyRange& range) { return range.min_hz == 0 || range.max_hz == 0; }

inline bool valid_frequency_range(const FrequencyRange& range) {
  if (range.min_hz < 0 || range.max_hz < 0) return false;
  if (frequency_range_disabled(range)) return true;
  return range.min_hz >= 1 && range.max_hz >= range.min_hz && range.max_hz <= MAX_POLICY_FREQUENCY_HZ;
}

inline bool frequency_in_range(int frequency_hz, const FrequencyRange& range) {
  return !frequency_range_disabled(range) && frequency_hz >= range.min_hz && frequency_hz <= range.max_hz;
}

inline int automatic_frequency_hz(bool configured_v2, const oq_odu::RuntimeFrequencySnapshot& snapshot, int mode_code,
                                  int model_level) {
  if (model_level <= 0) return 0;
  const auto command = oq_odu::resolve_automatic_level(configured_v2, snapshot, mode_code, model_level);
  if (command.control_level <= 0 || command.physical_level <= 0) return -1;
  if (!oq_odu::has_valid_frequency_table(snapshot, mode_code)) return -1;
  const auto& table = oq_odu::frequency_table_for_mode(snapshot, mode_code);
  const int frequency_hz = oq_odu::frequency_for_physical_level(table, command.physical_level);
  return frequency_hz > 0 ? frequency_hz : -1;
}

struct ModeFrequencies {
  int heating_hz{-1};
  int cooling_hz{-1};

  bool valid() const { return heating_hz >= 0 && cooling_hz >= 0; }
};

inline ModeFrequencies automatic_mode_frequencies(bool configured_v2, const oq_odu::RuntimeFrequencySnapshot& snapshot,
                                                  int model_level) {
  if (!configuration_matches_variant(configured_v2, snapshot.variant)) return {};
  return {
      automatic_frequency_hz(configured_v2, snapshot, 2, model_level),
      automatic_frequency_hz(configured_v2, snapshot, 1, model_level),
  };
}

inline ModeFrequencies conservative_mode_frequencies(bool configured_v2,
                                                     const oq_odu::RuntimeFrequencySnapshot& primary,
                                                     const oq_odu::RuntimeFrequencySnapshot& secondary,
                                                     bool has_secondary, int model_level) {
  const auto primary_frequencies = automatic_mode_frequencies(configured_v2, primary, model_level);
  if (!has_secondary) return primary_frequencies;
  const auto secondary_frequencies = automatic_mode_frequencies(configured_v2, secondary, model_level);
  return {
      conservative_shared_cap_hz(primary_frequencies.heating_hz, secondary_frequencies.heating_hz, true),
      conservative_shared_cap_hz(primary_frequencies.cooling_hz, secondary_frequencies.cooling_hz, true),
  };
}

inline bool frequency_allowed(int frequency_hz, int cap_hz, const ExcludedFrequencyRanges& excluded) {
  if (frequency_hz <= 0 || cap_hz < 0 || cap_hz > MAX_POLICY_FREQUENCY_HZ) return false;
  if (!valid_frequency_range(excluded.a) || !valid_frequency_range(excluded.b)) return false;
  if (frequency_hz > cap_hz) return false;
  return !frequency_in_range(frequency_hz, excluded.a) && !frequency_in_range(frequency_hz, excluded.b);
}

inline int pick_allowed_level(bool configured_v2, const oq_odu::RuntimeFrequencySnapshot& snapshot, int mode_code,
                              int requested_level, int min_level, int max_level, int cap_hz,
                              const ExcludedFrequencyRanges& excluded) {
  if (requested_level <= 0) return 0;
  min_level = std::max(1, min_level);
  max_level = std::min(oq_odu::MODEL_LEVEL_MAX, max_level);
  if (min_level > max_level) return 0;
  requested_level = std::max(min_level, std::min(max_level, requested_level));

  auto allowed = [&](int level) {
    return frequency_allowed(automatic_frequency_hz(configured_v2, snapshot, mode_code, level), cap_hz, excluded);
  };
  if (allowed(requested_level)) return requested_level;
  for (int level = requested_level - 1; level >= min_level; --level) {
    if (allowed(level)) return level;
  }
  for (int level = requested_level + 1; level <= max_level; ++level) {
    if (allowed(level)) return level;
  }
  return 0;
}

}  // namespace oq_frequency_policy
