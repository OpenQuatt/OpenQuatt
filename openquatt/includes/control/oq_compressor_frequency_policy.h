#pragma once

#include <algorithm>
#include "../odu/oq_odu_compressor_levels.h"

namespace oq_frequency_policy {

inline constexpr int MAX_POLICY_FREQUENCY_HZ = 120;

struct FrequencyRange {
  int min_hz{0};
  int max_hz{0};
};

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

inline bool frequency_allowed(int frequency_hz, int cap_hz, const FrequencyRange& excluded) {
  if (frequency_hz <= 0 || cap_hz < 0 || cap_hz > MAX_POLICY_FREQUENCY_HZ) return false;
  if (!valid_frequency_range(excluded)) return false;
  if (frequency_hz > cap_hz) return false;
  return !frequency_in_range(frequency_hz, excluded);
}

inline int pick_allowed_level(bool configured_v2, const oq_odu::RuntimeFrequencySnapshot& snapshot, int mode_code,
                              int requested_level, int min_level, int max_level, int cap_hz,
                              const FrequencyRange& excluded) {
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
