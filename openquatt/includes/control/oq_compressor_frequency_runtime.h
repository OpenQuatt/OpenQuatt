#pragma once

#include <math.h>

#include "oq_compressor_frequency_policy.h"

namespace oq_frequency_runtime {

struct Context {
  bool configured_v2{false};
  oq_odu::RuntimeFrequencySnapshot hp1_snapshot{};
  oq_odu::RuntimeFrequencySnapshot hp2_snapshot{};
  int cap_hz{-1};
  oq_frequency_policy::FrequencyRange hp1_excluded{};
  oq_frequency_policy::FrequencyRange hp2_excluded{};

  const oq_odu::RuntimeFrequencySnapshot& snapshot(bool is_hp1) const { return is_hp1 ? hp1_snapshot : hp2_snapshot; }

  const oq_frequency_policy::FrequencyRange& excluded_range(bool is_hp1) const {
    return is_hp1 ? hp1_excluded : hp2_excluded;
  }

  int automatic_frequency_hz(bool is_hp1, int mode_code, int level) const {
    return oq_frequency_policy::automatic_frequency_hz(configured_v2, snapshot(is_hp1), mode_code, level);
  }

  bool frequency_allowed(bool is_hp1, int mode_code, int level) const {
    return oq_frequency_policy::frequency_allowed(automatic_frequency_hz(is_hp1, mode_code, level), cap_hz,
                                                  excluded_range(is_hp1));
  }

  int pick_allowed_level(bool is_hp1, int mode_code, int requested_level, int min_level, int max_level) const {
    return oq_frequency_policy::pick_allowed_level(configured_v2, snapshot(is_hp1), mode_code, requested_level,
                                                   min_level, max_level, cap_hz, excluded_range(is_hp1));
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
#if OQ_TOPOLOGY_DUO
  const auto hp2_snapshot = oq_odu::decode_runtime_frequency_snapshot(id(hp2_runtime_frequency_snapshot_storage));
  const oq_frequency_policy::FrequencyRange hp2_excluded = {
      read_hz(id(hp2_excluded_frequency_min_hz)),
      read_hz(id(hp2_excluded_frequency_max_hz)),
  };
#else
  const auto hp2_snapshot = hp1_snapshot;
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
  };
}
#endif

}  // namespace oq_frequency_runtime
