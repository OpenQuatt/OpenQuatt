#pragma once

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace oq_cic {

inline constexpr uint16_t UNAVAILABLE = 0xFFFFu;
inline constexpr uint16_t FIRMWARE_FALLBACK = 0x011Eu;

constexpr uint16_t gated(bool enabled, uint16_t value) { return enabled ? value : 0u; }

inline uint16_t scaled(float value, float factor = 1.0f, uint16_t nan_value = 0u) {
  return isnan(value) ? nan_value : static_cast<uint16_t>(lroundf(value * factor));
}

inline uint16_t temperature(float value) {
  return isnan(value) ? 0u : static_cast<uint16_t>(lroundf((value * 100.0f) + 3000.0f));
}

inline uint16_t flow(float value) { return isnan(value) ? 0u : static_cast<uint16_t>(lroundf(value / 0.618f)); }

inline uint16_t on_option(const char* option, uint16_t on_value = 1u) {
  return option != nullptr && strcmp(option, "On") == 0 ? on_value : 0u;
}

constexpr uint16_t boolean(bool value, uint16_t true_value = 1u) { return value ? true_value : 0u; }

inline uint16_t working_mode(const char* option) {
  if (option != nullptr && strcmp(option, "Cooling") == 0) return 1u;
  if (option != nullptr && strcmp(option, "Heating") == 0) return 2u;
  return 0u;
}

inline uint16_t compressor_level(const char* option) {
  return option == nullptr || option[0] == '\0' ? 0u : static_cast<uint16_t>(atoi(option));
}

constexpr uint16_t status_flags(bool fan_low, bool bottom_plate, bool crankcase, bool fan_defrost, bool fan_high,
                                bool four_way, bool pump_relay) {
  return (fan_low ? 0x0001u : 0u) | (bottom_plate ? 0x0004u : 0u) | (crankcase ? 0x0008u : 0u) |
         (fan_defrost ? 0x0010u : 0u) | (fan_high ? 0x0020u : 0u) | (four_way ? 0x0040u : 0u) |
         (pump_relay ? 0x0800u : 0u);
}

}  // namespace oq_cic
