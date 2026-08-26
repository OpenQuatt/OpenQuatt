#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace oq_odu {

inline constexpr int MODEL_LEVEL_MAX = 10;
inline constexpr int LEGACY_PHYSICAL_LEVEL_MAX = 10;
inline constexpr int EXTENDED_PHYSICAL_LEVEL_MAX = 20;
inline constexpr uint16_t EXTENDED_HEATING_TABLE_REGISTER = 3050U;
inline constexpr uint16_t EXTENDED_HEATING_TABLE_REGISTER_COUNT = 10U;

inline constexpr std::array<uint8_t, 21> V2_HEATING_PHYSICAL_FREQUENCIES_HZ = {
    0, 20, 26, 30, 36, 40, 45, 48, 52, 55, 60, 65, 68, 72, 76, 82, 85, 90, 95, 102, 110,
};

inline constexpr std::array<uint8_t, 21> V2_COOLING_PHYSICAL_FREQUENCIES_HZ = {
    0, 20, 26, 30, 34, 36, 38, 40, 42, 44, 46, 48, 52, 54, 56, 58, 60, 64, 66, 68, 71,
};

// The V2 performance model remains a ten-level model. These are its heating
// frequency anchors, mapped to the nearest exact F-level in the extended table.
inline constexpr std::array<uint8_t, 11> V2_HEATING_MODEL_FREQUENCIES_HZ = {
    0, 20, 26, 30, 48, 55, 61, 72, 80, 85, 90,
};

inline constexpr std::array<uint8_t, 11> V2_HEATING_MODEL_TO_PHYSICAL = {
    0, 1, 2, 3, 7, 9, 10, 13, 15, 16, 17,
};

enum class CompressorLevelProfile : uint8_t {
  UNKNOWN = 0,
  V2_EXTENDED = 1,
};

struct LevelCommand {
  int control_level{0};
  int physical_level{0};
};

struct RetainedLevel {
  int control_level{0};
  int physical_level{0};
};

inline bool has_extended_compressor_levels(CompressorLevelProfile profile) {
  return profile == CompressorLevelProfile::V2_EXTENDED;
}

inline const char* compressor_level_profile_label(CompressorLevelProfile profile) {
  return has_extended_compressor_levels(profile) ? "V2 F0-F20" : "Unknown / F0-F10 safe";
}

inline CompressorLevelProfile detect_extended_heating_table_response(const uint8_t* data, size_t size) {
  if (data == nullptr || size < EXTENDED_HEATING_TABLE_REGISTER_COUNT * 2U) {
    return CompressorLevelProfile::UNKNOWN;
  }

  for (size_t index = 0; index < EXTENDED_HEATING_TABLE_REGISTER_COUNT; ++index) {
    const size_t offset = index * 2U;
    const uint16_t raw = static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8U) | data[offset + 1U]);
    if (raw != V2_HEATING_PHYSICAL_FREQUENCIES_HZ[index + 11U]) {
      return CompressorLevelProfile::UNKNOWN;
    }
  }
  return CompressorLevelProfile::V2_EXTENDED;
}

inline int physical_level_limit(bool configured_v2, CompressorLevelProfile profile) {
  return configured_v2 && has_extended_compressor_levels(profile) ? EXTENDED_PHYSICAL_LEVEL_MAX
                                                                  : LEGACY_PHYSICAL_LEVEL_MAX;
}

inline LevelCommand resolve_automatic_level(bool extended_v2_mapping_allowed, int mode_code, int model_level) {
  const int bounded_model = std::max(0, std::min(MODEL_LEVEL_MAX, model_level));
  if (extended_v2_mapping_allowed && mode_code == 2) {
    return {bounded_model, V2_HEATING_MODEL_TO_PHYSICAL[static_cast<size_t>(bounded_model)]};
  }
  return {bounded_model, bounded_model};
}

inline LevelCommand resolve_manual_level(bool configured_v2, CompressorLevelProfile profile,
                                         int requested_physical_level) {
  const int bounded_physical =
      std::max(0, std::min(physical_level_limit(configured_v2, profile), requested_physical_level));
  return {std::min(MODEL_LEVEL_MAX, bounded_physical), bounded_physical};
}

inline int model_level_for_physical_heating_level(bool extended_v2_mapping_allowed, int physical_level) {
  const int bounded_physical = std::max(0, std::min(EXTENDED_PHYSICAL_LEVEL_MAX, physical_level));
  if (!extended_v2_mapping_allowed) {
    return std::min(MODEL_LEVEL_MAX, bounded_physical);
  }

  // Recover model bookkeeping from physical readback after logical state was
  // lost. Off-anchor CM100 levels retain their physical value and use the
  // nearest model-frequency anchor, so an active hold never becomes level 0.
  const int physical_frequency = V2_HEATING_PHYSICAL_FREQUENCIES_HZ[static_cast<size_t>(bounded_physical)];
  int nearest_model_level = 0;
  int nearest_delta = 256;
  for (size_t model_level = 0; model_level < V2_HEATING_MODEL_FREQUENCIES_HZ.size(); ++model_level) {
    const int delta_signed = physical_frequency - V2_HEATING_MODEL_FREQUENCIES_HZ[model_level];
    const int delta = delta_signed < 0 ? -delta_signed : delta_signed;
    if (delta < nearest_delta) {
      nearest_delta = delta;
      nearest_model_level = static_cast<int>(model_level);
    }
  }
  return nearest_model_level;
}

inline RetainedLevel resolve_retained_level(bool hold_active, bool cooling_mode_active, int selected_physical_level,
                                            int previous_control_level, int previous_physical_level,
                                            bool extended_v2_mapping_allowed) {
  if (!hold_active || cooling_mode_active) return {};

  int physical_level = selected_physical_level > 0 ? selected_physical_level : previous_physical_level;
  const int physical_max = extended_v2_mapping_allowed ? EXTENDED_PHYSICAL_LEVEL_MAX : LEGACY_PHYSICAL_LEVEL_MAX;
  physical_level = std::max(0, std::min(physical_max, physical_level));
  if (physical_level <= 0 && previous_control_level <= 0) return {};

  int control_level = std::max(0, std::min(MODEL_LEVEL_MAX, previous_control_level));
  if (control_level <= 0 && physical_level > 0) {
    control_level = model_level_for_physical_heating_level(extended_v2_mapping_allowed, physical_level);
  }
  if (physical_level <= 0 && control_level > 0) {
    physical_level = resolve_automatic_level(extended_v2_mapping_allowed, 2, control_level).physical_level;
  }
  return {control_level, physical_level};
}

}  // namespace oq_odu
