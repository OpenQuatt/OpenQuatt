#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::openquatt_ble {

enum class AdvertisementKind : uint8_t { ADVERTISEMENT, SCAN_RESPONSE, COMBINED };

// Borrowed raw AD structures. Keep manufacturer data and service data available
// to protocol decoders, including data delivered only in a scan response.
struct Advertisement {
  uint64_t address;
  const uint8_t* data;
  size_t length;
  int rssi;
  uint32_t received_at;
  AdvertisementKind kind;
};

}  // namespace esphome::openquatt_ble
