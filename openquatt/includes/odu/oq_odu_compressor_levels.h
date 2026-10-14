#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "oq_odu_frequency_table.h"

namespace oq_odu {

inline constexpr int LEGACY_PHYSICAL_LEVEL_MAX = 10;
inline constexpr int EXTENDED_PHYSICAL_LEVEL_MAX = 20;

// Automatic heating uses physical table entries; the model domain remains 90 Hz.
inline constexpr int MAX_AUTOMATIC_HEATING_FREQUENCY_HZ = 90;
// Reference axes are used only to normalize feedback into the existing demand
// scale, never to restrict or translate the automatic heating candidates.
inline constexpr std::array<uint8_t, 11> V2_HEATING_DEMAND_REFERENCE_HZ = {
    0, 20, 26, 30, 48, 55, 61, 72, 80, 85, 90,
};
inline constexpr std::array<uint8_t, 11> V1_HEATING_DEMAND_REFERENCE_HZ = {
    0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90,
};

enum class CompressorLevelProfile : uint8_t {
  UNKNOWN = 0,
  V2_EXTENDED = 1,
  V2_HEATING_EXTENDED = 2,
  V2_COOLING_EXTENDED = 3,
  V2_LEGACY = 4,
};

struct LevelCommand {
  int control_level{0};
  int physical_level{0};
};

struct RetainedLevel {
  int control_level{0};
  int physical_level{0};
};

inline bool has_extended_compressor_levels(CompressorLevelProfile profile, int mode_code) {
  if (profile == CompressorLevelProfile::V2_EXTENDED) return mode_code == 1 || mode_code == 2;
  if (profile == CompressorLevelProfile::V2_HEATING_EXTENDED) return mode_code == 2;
  if (profile == CompressorLevelProfile::V2_COOLING_EXTENDED) return mode_code == 1;
  return false;
}

inline CompressorLevelProfile compressor_level_profile(const RuntimeFrequencySnapshot& snapshot) {
  const bool cooling_extended = has_extended_frequency_table(snapshot, 1);
  const bool heating_extended = has_extended_frequency_table(snapshot, 2);
  if (cooling_extended && heating_extended) return CompressorLevelProfile::V2_EXTENDED;
  if (heating_extended) return CompressorLevelProfile::V2_HEATING_EXTENDED;
  if (cooling_extended) return CompressorLevelProfile::V2_COOLING_EXTENDED;
  if (snapshot.variant == Variant::V2_OLD_MODEL && snapshot.cooling.valid && snapshot.heating.valid &&
      snapshot.cooling.level_count == LEGACY_FREQUENCY_LEVEL_COUNT &&
      snapshot.heating.level_count == LEGACY_FREQUENCY_LEVEL_COUNT) {
    return CompressorLevelProfile::V2_LEGACY;
  }
  return CompressorLevelProfile::UNKNOWN;
}

inline const char* compressor_level_profile_label(CompressorLevelProfile profile) {
  switch (profile) {
    case CompressorLevelProfile::V2_EXTENDED:
      return "V2 F0-F20";
    case CompressorLevelProfile::V2_HEATING_EXTENDED:
      return "V2 heating F0-F20";
    case CompressorLevelProfile::V2_COOLING_EXTENDED:
      return "V2 cooling F0-F20";
    case CompressorLevelProfile::V2_LEGACY:
      return "V2 F0-F10";
    default:
      return "Unknown / F0-F10 safe";
  }
}

inline int physical_level_limit(bool configured_v2, const RuntimeFrequencySnapshot& snapshot, int mode_code) {
  (void)configured_v2;
  if (snapshot.variant != Variant::UNKNOWN && has_valid_frequency_table(snapshot, mode_code)) {
    const auto& table = frequency_table_for_mode(snapshot, mode_code);
    if (validate_frequency_table(table)) return static_cast<int>(table.level_count) - 1;
  }
  return LEGACY_PHYSICAL_LEVEL_MAX;
}

inline int automatic_level_limit(bool configured_v2, const RuntimeFrequencySnapshot& snapshot, int mode_code) {
  if (mode_code == 1)
    return std::min(LEGACY_PHYSICAL_LEVEL_MAX, physical_level_limit(configured_v2, snapshot, mode_code));
  if (mode_code != 2 || snapshot.variant == Variant::UNKNOWN || !has_valid_frequency_table(snapshot, mode_code))
    return 0;
  const auto& table = snapshot.heating;
  if (!validate_frequency_table(table)) return 0;
  int maximum = 0;
  for (size_t level = 1; level < table.level_count; ++level) {
    if (table.hz[level] > MAX_AUTOMATIC_HEATING_FREQUENCY_HZ) break;
    maximum = static_cast<int>(level);
  }
  return maximum;
}

inline LevelCommand resolve_automatic_level(bool configured_v2, const RuntimeFrequencySnapshot& snapshot, int mode_code,
                                            int physical_level) {
  if (physical_level <= 0 || physical_level > automatic_level_limit(configured_v2, snapshot, mode_code)) return {};
  // The optimizer selected this physical entry and evaluated its actual Hz.
  // Never remap an accepted request to another table entry at the actuator.
  return {physical_level, physical_level};
}

inline LevelCommand resolve_manual_level(bool configured_v2, const RuntimeFrequencySnapshot& snapshot, int mode_code,
                                         int requested_physical_level) {
  const int physical =
      std::max(0, std::min(physical_level_limit(configured_v2, snapshot, mode_code), requested_physical_level));
  return {physical, physical};
}

inline int normalized_heating_demand_level(bool configured_v2, const RuntimeFrequencySnapshot& snapshot,
                                           int physical_level) {
  (void)configured_v2;
  if (physical_level <= 0) return 0;
  // Preserve active/off semantics while a table is being reacquired. Only a
  // minimal demand indication is known; do not invent its operating frequency.
  if (snapshot.variant == Variant::UNKNOWN || !snapshot.heating.valid || !validate_frequency_table(snapshot.heating))
    return 1;
  const int frequency = frequency_for_physical_level(snapshot.heating, physical_level);
  if (frequency <= 0) return 1;
  const bool v2 = snapshot.variant == Variant::V2_OLD_MODEL || snapshot.variant == Variant::V2_NEW_MODEL;
  const auto& axis = v2 ? V2_HEATING_DEMAND_REFERENCE_HZ : V1_HEATING_DEMAND_REFERENCE_HZ;
  int selected = 0;
  int delta = 256;
  for (size_t level = 1; level < axis.size(); ++level) {
    const int distance = std::abs(frequency - axis[level]);
    if (distance < delta) {
      selected = static_cast<int>(level);
      delta = distance;
    }
  }
  return selected;
}

inline RetainedLevel resolve_retained_level(bool hold_active, bool cooling_mode_active, int selected_physical_level,
                                            int previous_control_level, int previous_physical_level, bool configured_v2,
                                            const RuntimeFrequencySnapshot& snapshot) {
  if (!hold_active || cooling_mode_active) return {};

  int physical_level = selected_physical_level > 0 ? selected_physical_level : previous_physical_level;
  const int physical_max = physical_level_limit(configured_v2, snapshot, 2);
  physical_level = std::max(0, std::min(physical_max, physical_level));
  if (physical_level <= 0 && previous_control_level <= 0) return {};

  // Requests, readbacks and retained commands share the physical level domain.
  if (physical_level <= 0) physical_level = std::max(0, std::min(physical_max, previous_control_level));
  return {physical_level, physical_level};
}

inline RetainedLevel update_retained_level_snapshot(const RetainedLevel& retained, bool hold_active,
                                                    bool cooling_mode_active, int selected_physical_level,
                                                    int previous_control_level, int previous_physical_level,
                                                    bool configured_v2, const RuntimeFrequencySnapshot& snapshot) {
  if (!hold_active || cooling_mode_active) return {};
  const int physical_max = physical_level_limit(configured_v2, snapshot, 2);
  if (retained.control_level > 0 && retained.physical_level > 0 && retained.physical_level <= physical_max) {
    return retained;
  }
  return resolve_retained_level(hold_active, cooling_mode_active, selected_physical_level, previous_control_level,
                                previous_physical_level, configured_v2, snapshot);
}

inline bool retained_level_should_override_request(const RetainedLevel& retained, int requested_level,
                                                   bool requested_level_is_physical = false) {
  const int retained_request_level = requested_level_is_physical ? retained.physical_level : retained.control_level;
  return retained.control_level > 0 && retained.physical_level > 0 && requested_level < retained_request_level;
}

}  // namespace oq_odu
