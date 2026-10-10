#pragma once

#include <cstdint>

#include "esp_efuse.h"
#include "esp_efuse_table.h"

namespace oq_hardware {

struct HardwareRevision {
  esp_err_t error;
  bool programmed;
  uint8_t major;
  uint8_t minor;
  uint8_t batch;
  uint16_t raw;
};

inline HardwareRevision read_hardware_revision_efuse() {
  uint16_t raw = 0;
  const esp_err_t error = esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, &raw, 14);
  raw &= 0x3FFFU;

  return {
      error,
      error == ESP_OK && raw != 0U,
      static_cast<uint8_t>(raw & 0x0FU),
      static_cast<uint8_t>((raw >> 4U) & 0x0FU),
      static_cast<uint8_t>((raw >> 8U) & 0x3FU),
      raw,
  };
}

}  // namespace oq_hardware
