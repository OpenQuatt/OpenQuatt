#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace oq_odu {

inline constexpr uint16_t CONTROL_BOARD_ITEM_REGISTER = 2127U;

enum class Generation : uint8_t {
  UNKNOWN = 0,
  V1,
  V1_5,
  V2,
};

struct Detection {
  Generation generation{Generation::UNKNOWN};
  bool control_board_item_available{false};
  uint16_t control_board_item{0};
};

inline Generation detect_control_board_item(uint16_t control_board_item) {
  switch (control_board_item) {
    case 0x0037U:
      return Generation::V1;
    case 0x0E37U:
      return Generation::V1_5;
    case 0x1037U:
      return Generation::V2;
    default:
      return Generation::UNKNOWN;
  }
}

inline Detection detect_control_board_item(bool available, float control_board_item) {
  if (!available || !std::isfinite(control_board_item) || control_board_item < 0.0f || control_board_item > 65535.0f ||
      std::round(control_board_item) != control_board_item) {
    return {};
  }

  const uint16_t raw = static_cast<uint16_t>(control_board_item);
  return {detect_control_board_item(raw), true, raw};
}

inline Detection detect_control_board_item_response(const uint8_t* data, size_t size) {
  if (data == nullptr || size < 2U) {
    return {};
  }

  const uint16_t raw = static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) | data[1]);
  return {detect_control_board_item(raw), true, raw};
}

inline uint32_t next_request_token(uint32_t current) {
  const uint32_t next = current + 1U;
  return next == 0U ? 1U : next;
}

inline const char* generation_label(Generation generation) {
  switch (generation) {
    case Generation::V1:
      return "V1";
    case Generation::V1_5:
      return "V1.5";
    case Generation::V2:
      return "V2";
    default:
      return "Unknown";
  }
}

}  // namespace oq_odu
