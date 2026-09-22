#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace oq_modbus_recovery {

constexpr size_t MAX_LEADING_GARBAGE_BYTES = 4U;
constexpr size_t MIN_RTU_FRAME_SIZE = 5U;
constexpr uint8_t FUNCTION_CODE_MASK = 0x7FU;
constexpr uint8_t FUNCTION_CODE_EXCEPTION_MASK = 0x80U;

inline uint16_t crc16_modbus(const uint8_t* data, size_t size) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < size; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8U; bit++) {
      if ((crc & 0x0001U) != 0U) {
        crc = static_cast<uint16_t>((crc >> 1U) ^ 0xA001U);
      } else {
        crc = static_cast<uint16_t>(crc >> 1U);
      }
    }
  }
  return crc;
}

inline uint16_t read_be_u16(const uint8_t* data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) | data[1]);
}

inline bool bytes_equal(const uint8_t* lhs, const uint8_t* rhs, size_t size) {
  for (size_t i = 0; i < size; i++) {
    if (lhs[i] != rhs[i]) {
      return false;
    }
  }
  return true;
}

inline bool response_matches_request(const uint8_t* frame, size_t available, uint8_t expected_address,
                                     const uint8_t* request_pdu, size_t request_pdu_size,
                                     size_t* frame_size = nullptr) {
  if (frame == nullptr || request_pdu == nullptr || request_pdu_size == 0U || available < MIN_RTU_FRAME_SIZE) {
    return false;
  }
  if (frame[0] != expected_address) {
    return false;
  }

  const uint8_t expected_function = request_pdu[0] & FUNCTION_CODE_MASK;
  const uint8_t response_function = frame[1];
  if ((response_function & FUNCTION_CODE_MASK) != expected_function) {
    return false;
  }

  size_t expected_frame_size = 0U;
  if ((response_function & FUNCTION_CODE_EXCEPTION_MASK) != 0U) {
    expected_frame_size = MIN_RTU_FRAME_SIZE;
  } else {
    switch (expected_function) {
      case 0x01U:    // Read coils
      case 0x02U: {  // Read discrete inputs
        if (request_pdu_size < 5U || available < 3U) {
          return false;
        }
        const uint16_t quantity = read_be_u16(request_pdu + 3U);
        const size_t expected_bytes = (static_cast<size_t>(quantity) + 7U) / 8U;
        if (expected_bytes > 0xFFU || frame[2] != expected_bytes) {
          return false;
        }
        expected_frame_size = 5U + expected_bytes;
        break;
      }
      case 0x03U:    // Read holding registers
      case 0x04U:    // Read input registers
      case 0x17U: {  // Read/write multiple registers
        if (request_pdu_size < 5U || available < 3U) {
          return false;
        }
        const uint16_t quantity = read_be_u16(request_pdu + 3U);
        const size_t expected_bytes = static_cast<size_t>(quantity) * 2U;
        if (expected_bytes > 0xFFU || frame[2] != expected_bytes) {
          return false;
        }
        expected_frame_size = 5U + expected_bytes;
        break;
      }
      case 0x05U:  // Write single coil
      case 0x06U:  // Write single register
        if (request_pdu_size < 5U) {
          return false;
        }
        expected_frame_size = 8U;
        if (available < expected_frame_size || !bytes_equal(frame + 1U, request_pdu, 5U)) {
          return false;
        }
        break;
      case 0x0FU:  // Write multiple coils
      case 0x10U:  // Write multiple registers
        if (request_pdu_size < 5U) {
          return false;
        }
        expected_frame_size = 8U;
        if (available < expected_frame_size || !bytes_equal(frame + 1U, request_pdu, 5U)) {
          return false;
        }
        break;
      case 0x16U:  // Mask write register
        if (request_pdu_size < 7U) {
          return false;
        }
        expected_frame_size = 10U;
        if (available < expected_frame_size || !bytes_equal(frame + 1U, request_pdu, 7U)) {
          return false;
        }
        break;
      default:
        // Stay conservative: only recover standard function codes OpenQuatt can
        // validate against the outstanding request shape.
        return false;
    }
  }

  if (expected_frame_size > available || crc16_modbus(frame, expected_frame_size) != 0U) {
    return false;
  }
  if (frame_size != nullptr) {
    *frame_size = expected_frame_size;
  }
  return true;
}

inline size_t find_expected_response_offset(const uint8_t* buffer, size_t size, uint8_t expected_address,
                                            const uint8_t* request_pdu, size_t request_pdu_size) {
  if (buffer == nullptr || size <= MIN_RTU_FRAME_SIZE || request_pdu == nullptr || request_pdu_size == 0U) {
    return 0U;
  }

  const size_t max_offset = std::min(MAX_LEADING_GARBAGE_BYTES, size - MIN_RTU_FRAME_SIZE);
  for (size_t offset = 1U; offset <= max_offset; offset++) {
    if (response_matches_request(buffer + offset, size - offset, expected_address, request_pdu, request_pdu_size)) {
      return offset;
    }
  }
  return 0U;
}

}  // namespace oq_modbus_recovery
