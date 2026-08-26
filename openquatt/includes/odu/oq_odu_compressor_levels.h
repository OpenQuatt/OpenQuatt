#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "oq_odu_frequency_table.h"

namespace oq_odu {

inline constexpr int MODEL_LEVEL_MAX = 10;
inline constexpr int LEGACY_PHYSICAL_LEVEL_MAX = 10;
inline constexpr int EXTENDED_PHYSICAL_LEVEL_MAX = 20;

// The V2 performance model remains a ten-level model in step 1. Its heating
// anchors are resolved against the validated runtime frequency table instead
// of a fixed physical F-level table.
inline constexpr std::array<uint8_t, 11> V2_HEATING_MODEL_FREQUENCIES_HZ = {
    0, 20, 26, 30, 48, 55, 61, 72, 80, 85, 90,
};

enum class CompressorLevelProfile : uint8_t {
  UNKNOWN = 0,
  V2_EXTENDED = 1,
  V2_HEATING_EXTENDED = 2,
  V2_COOLING_EXTENDED = 3,
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
    default:
      return "Unknown / F0-F10 safe";
  }
}

inline int physical_level_limit(bool configured_v2, const RuntimeFrequencySnapshot& snapshot, int mode_code) {
  return configured_v2 && has_extended_frequency_table(snapshot, mode_code) ? EXTENDED_PHYSICAL_LEVEL_MAX
                                                                            : LEGACY_PHYSICAL_LEVEL_MAX;
}

inline bool automatic_v2_heating_mapping_available(bool configured_v2, const RuntimeFrequencySnapshot& snapshot) {
  const bool detected_v2 = snapshot.variant == Variant::V2_OLD_MODEL || snapshot.variant == Variant::V2_NEW_MODEL;
  return configured_v2 && detected_v2 && snapshot.heating.valid;
}

inline LevelCommand resolve_automatic_level(bool configured_v2, const RuntimeFrequencySnapshot& snapshot, int mode_code,
                                            int model_level) {
  const int bounded_model = std::max(0, std::min(MODEL_LEVEL_MAX, model_level));
  if (bounded_model == 0) return {};

  // Step 1 deliberately preserves automatic cooling, non-V2 layouts and the
  // F0-F10 fallback while no valid V2 heating table is available.
  if (mode_code != 2 || !automatic_v2_heating_mapping_available(configured_v2, snapshot)) {
    return {bounded_model, bounded_model};
  }

  const int requested_frequency = V2_HEATING_MODEL_FREQUENCIES_HZ[static_cast<size_t>(bounded_model)];
  const int physical_level =
      resolve_nearest_physical_level(snapshot.heating, requested_frequency, V2_HEATING_MODEL_FREQUENCIES_HZ.back());
  if (physical_level <= 0) return {};
  return {bounded_model, physical_level};
}

inline LevelCommand resolve_manual_level(bool configured_v2, const RuntimeFrequencySnapshot& snapshot, int mode_code,
                                         int requested_physical_level) {
  const int bounded_physical =
      std::max(0, std::min(physical_level_limit(configured_v2, snapshot, mode_code), requested_physical_level));
  return {std::min(MODEL_LEVEL_MAX, bounded_physical), bounded_physical};
}

inline int model_level_for_physical_heating_level(bool configured_v2, const RuntimeFrequencySnapshot& snapshot,
                                                  int physical_level) {
  const int bounded_physical = std::max(0, std::min(EXTENDED_PHYSICAL_LEVEL_MAX, physical_level));
  if (!automatic_v2_heating_mapping_available(configured_v2, snapshot)) {
    return std::min(MODEL_LEVEL_MAX, bounded_physical);
  }

  const int physical_frequency = frequency_for_physical_level(snapshot.heating, bounded_physical);
  if (physical_frequency <= 0) return 0;

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
                                            int previous_control_level, int previous_physical_level, bool configured_v2,
                                            const RuntimeFrequencySnapshot& snapshot) {
  if (!hold_active || cooling_mode_active) return {};

  int physical_level = selected_physical_level > 0 ? selected_physical_level : previous_physical_level;
  const int physical_max = physical_level_limit(configured_v2, snapshot, 2);
  physical_level = std::max(0, std::min(physical_max, physical_level));
  if (physical_level <= 0 && previous_control_level <= 0) return {};

  int control_level = std::max(0, std::min(MODEL_LEVEL_MAX, previous_control_level));
  if (control_level <= 0 && physical_level > 0) {
    control_level = model_level_for_physical_heating_level(configured_v2, snapshot, physical_level);
  }
  if (physical_level <= 0 && control_level > 0) {
    physical_level = resolve_automatic_level(configured_v2, snapshot, 2, control_level).physical_level;
  }
  return {control_level, physical_level};
}

}  // namespace oq_odu
