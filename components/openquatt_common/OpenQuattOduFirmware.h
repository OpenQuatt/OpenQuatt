#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace esphome::openquatt_common {

// R2122 contains a binary major/minor pair, not a decimal version number.
inline uint16_t odu_firmware_raw(float value) {
  if (!std::isfinite(value) || value <= 0.0f || value >= 65535.0f || std::floor(value) != value) return 0U;
  return static_cast<uint16_t>(value);
}

inline void format_odu_firmware(uint16_t raw, char (&output)[8]) {
  if (raw == 0U || raw == 0xFFFFU) {
    output[0] = '\0';
    return;
  }
  std::snprintf(output, sizeof(output), "%u.%02u", static_cast<unsigned>(raw >> 8U),
                static_cast<unsigned>(raw & 0xFFU));
}

template <typename Writer>
void append_odu_firmware(Writer& output, const std::array<uint16_t, 2U>& firmware, size_t hp_count) {
  output += '[';
  for (size_t index = 0U; index < hp_count; ++index) {
    if (index != 0U) output += ',';
    char version[8];
    format_odu_firmware(firmware[index], version);
    if (version[0] == '\0') {
      output += "null";
    } else {
      output += '"';
      output += version;
      output += '"';
    }
  }
  output += ']';
}

}  // namespace esphome::openquatt_common
