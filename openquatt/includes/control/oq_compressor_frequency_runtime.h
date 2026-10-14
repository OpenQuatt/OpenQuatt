#pragma once

#include <math.h>

#include "oq_compressor_frequency_policy.h"
#include "oq_compressor_temperature_runtime.h"
#include "oq_thermal_request_logic.h"

namespace oq_frequency_runtime {

struct Context {
  bool configured_v2{false};
  oq_odu::RuntimeFrequencySnapshot hp1_snapshot{};
  oq_odu::RuntimeFrequencySnapshot hp2_snapshot{};
  int cap_hz{-1};
  oq_frequency_policy::FrequencyRange hp1_excluded{};
  oq_frequency_policy::FrequencyRange hp2_excluded{};
  oq_odu::Variant hp1_variant{oq_odu::Variant::UNKNOWN};
  oq_odu::Variant hp2_variant{oq_odu::Variant::UNKNOWN};

  // Default contexts fail closed. Isolated frequency-only host tests explicitly
  // disable the temperature layer; production capture always enforces it.
  bool temperature_enforced{true};
  oq_temperature::Evaluation hp1_temperature{};
  oq_temperature::Evaluation hp2_temperature{};

  bool temperature_allowed(bool is_hp1, int mode_code, int level) const {
    return mode_code != 2 ||
           (!temperature_enforced ? true : (is_hp1 ? hp1_temperature : hp2_temperature).allows(level));
  }

  const oq_odu::RuntimeFrequencySnapshot& snapshot(bool is_hp1) const { return is_hp1 ? hp1_snapshot : hp2_snapshot; }

  const oq_frequency_policy::FrequencyRange& excluded_range(bool is_hp1) const {
    return is_hp1 ? hp1_excluded : hp2_excluded;
  }

  oq_odu::Variant performance_variant(bool is_hp1) const { return is_hp1 ? hp1_variant : hp2_variant; }

  int automatic_level_limit(bool is_hp1, int mode_code) const {
    return oq_odu::automatic_level_limit(configured_v2, snapshot(is_hp1), mode_code);
  }

  int automatic_frequency_hz(bool is_hp1, int mode_code, int level) const {
    return oq_frequency_policy::automatic_frequency_hz(configured_v2, snapshot(is_hp1), mode_code, level);
  }

  bool frequency_allowed(bool is_hp1, int mode_code, int level) const {
    return temperature_allowed(is_hp1, mode_code, level) &&
           oq_frequency_policy::frequency_allowed(automatic_frequency_hz(is_hp1, mode_code, level), cap_hz,
                                                  excluded_range(is_hp1));
  }

  int pick_allowed_level(bool is_hp1, int mode_code, int requested_level, int min_level, int max_level) const {
    if (requested_level <= 0) return 0;
    min_level = std::max(1, min_level);
    max_level = std::min(automatic_level_limit(is_hp1, mode_code), max_level);
    if (min_level > max_level) return 0;
    requested_level = std::max(min_level, std::min(max_level, requested_level));
    if (frequency_allowed(is_hp1, mode_code, requested_level)) return requested_level;
    for (int level = requested_level - 1; level >= min_level; --level)
      if (frequency_allowed(is_hp1, mode_code, level)) return level;
    // Temperature removal of a low stand must not itself command more heat.
    // The demand selector can choose a higher allowed entry on its next cycle.
    if (mode_code == 2 && temperature_enforced && !temperature_allowed(is_hp1, mode_code, requested_level)) return 0;
    for (int level = requested_level + 1; level <= max_level; ++level)
      if (frequency_allowed(is_hp1, mode_code, level)) return level;
    return 0;
  }

  int pick_temperature_level(bool is_hp1, int mode_code, int requested_level) const {
    if (requested_level <= 0) return 0;
    if (mode_code != 2) return requested_level;
    for (int level = std::min(requested_level, oq_odu::EXTENDED_PHYSICAL_LEVEL_MAX); level >= 1; --level)
      if (temperature_allowed(is_hp1, mode_code, level)) return level;
    return 0;
  }

  int limit_heating_slew(bool is_hp1, int requested, int previous, uint32_t now_ms, uint32_t last_change_ms,
                         uint32_t up_hold_ms, uint32_t down_hold_ms) const {
    const int maximum = this->automatic_level_limit(is_hp1, 2);
    if (previous > 0 && !this->frequency_allowed(is_hp1, 2, previous))
      return this->pick_allowed_level(is_hp1, 2, previous, 1, std::min(maximum, previous));
    // Slew one allowed entry at a time. Excluded intermediate entries must
    // neither strand a running HP nor bypass an upward/downward wait window.
    std::array<uint8_t, oq_odu::EXTENDED_PHYSICAL_LEVEL_MAX + 1> entries{};
    int count = 0, previous_rank = 0, requested_rank = 0;
    const int target = this->pick_allowed_level(is_hp1, 2, requested, 1, maximum);
    for (int physical = 1; physical <= maximum; ++physical) {
      if (!this->frequency_allowed(is_hp1, 2, physical)) continue;
      entries[++count] = static_cast<uint8_t>(physical);
      if (physical == previous) previous_rank = count;
      if (physical == target) requested_rank = count;
    }
    const int rank = oq_request::limit_level_slew(requested_rank, previous_rank, false, now_ms, last_change_ms,
                                                  up_hold_ms, down_hold_ms);
    return rank >= 0 && rank <= count ? entries[rank] : 0;
  }
};

#if defined(OQ_TOPOLOGY_DUO)
template <typename Number>
inline int read_hz(const Number& number) {
  return number.has_state() && !isnan(number.state) ? static_cast<int>(lroundf(number.state)) : -1;
}

inline Context capture() {
  const bool configured_v2 = id(hp_generation).has_state() && id(hp_generation).current_option() == "V2";
  const auto hp1_snapshot = oq_odu::decode_runtime_frequency_snapshot(id(hp1_runtime_frequency_snapshot_storage));
  const auto hp1_variant =
      oq_odu::confirmed_variant(id(hp1_odu_generation_detection_complete), id(hp1_generation_variant_code));
#if OQ_TOPOLOGY_DUO
  const auto hp2_snapshot = oq_odu::decode_runtime_frequency_snapshot(id(hp2_runtime_frequency_snapshot_storage));
  const auto hp2_variant =
      oq_odu::confirmed_variant(id(hp2_odu_generation_detection_complete), id(hp2_generation_variant_code));
  const oq_frequency_policy::FrequencyRange hp2_excluded = {
      read_hz(id(hp2_excluded_frequency_min_hz)),
      read_hz(id(hp2_excluded_frequency_max_hz)),
  };
#else
  const auto hp2_snapshot = hp1_snapshot;
  const auto hp2_variant = oq_odu::Variant::UNKNOWN;
  const oq_frequency_policy::FrequencyRange hp2_excluded{};
#endif
  const auto& cap = id(oq_silent_active).state ? id(oq_silent_max_frequency_hz) : id(oq_day_max_frequency_hz);
  return {
      configured_v2,
      hp1_snapshot,
      hp2_snapshot,
      read_hz(cap),
      {read_hz(id(hp1_excluded_frequency_min_hz)), read_hz(id(hp1_excluded_frequency_max_hz))},
      hp2_excluded,
      hp1_variant,
      hp2_variant,
      true,
      oq_temperature_runtime::capture(true, hp1_snapshot),
#if OQ_TOPOLOGY_DUO
      oq_temperature_runtime::capture(false, hp2_snapshot),
#else
      {},
#endif
  };
}
#endif

}  // namespace oq_frequency_runtime
