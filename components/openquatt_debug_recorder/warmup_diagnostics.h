#pragma once

#include <cmath>
#include <cstdint>

namespace esphome::openquatt_debug_recorder {

enum class WarmupPath : uint8_t {
  INACTIVE,
  NO_BOOST,
  APPLIED,
  FALLING,
  NO_RECOVERY,
  NO_MODEL,
  WATER_LIMIT,
  EXTERNAL,
  UNAVAILABLE
};

inline const char* warmup_path_name(WarmupPath path) {
  static constexpr const char* names[] = {"inactive", "no_boost",    "applied",  "falling",    "no_recovery",
                                          "no_model", "water_limit", "external", "unavailable"};
  const auto index = static_cast<unsigned>(path);
  return index < sizeof(names) / sizeof(names[0]) ? names[index] : "unknown";
}

struct WarmupPowerSnapshot {
  float base_w = NAN, minimum_w = NAN, extra_w = NAN, before_caps_w = NAN, after_caps_w = NAN;
  WarmupPath path = WarmupPath::INACTIVE;
};

// Only semantic changes trigger a log, not room noise, elapsed time or changing
// Pmin. Normalize missing values: NaN != NaN must never produce a log storm.
inline uint32_t warmup_log_key(bool warming, float intermediate_c, float boost_c, WarmupPath path) {
  if (!warming) return 0;
  const auto centi = [](float value, uint32_t maximum, uint32_t missing) {
    return std::isfinite(value) && value >= 0 && value <= static_cast<float>(maximum) / 100.0f
               ? static_cast<uint32_t>(std::lround(value * 100.0f))
               : missing;
  };
  return (centi(intermediate_c, 65534, 65535) << 16U) | (centi(boost_c, 254, 255) << 8U) |
         (static_cast<uint32_t>(path) + 1U);
}

}  // namespace esphome::openquatt_debug_recorder
