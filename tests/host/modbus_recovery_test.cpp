#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "openquatt/includes/protocol/oq_modbus_recovery.h"

namespace {

void append_crc(std::vector<uint8_t>& frame) {
  const uint16_t crc = oq_modbus_recovery::crc16_modbus(frame.data(), frame.size());
  frame.push_back(static_cast<uint8_t>(crc & 0xFFU));
  frame.push_back(static_cast<uint8_t>(crc >> 8U));
}

std::vector<uint8_t> read_response(uint8_t address, uint16_t value) {
  std::vector<uint8_t> frame{address, 0x03U, 0x02U, static_cast<uint8_t>(value >> 8U),
                             static_cast<uint8_t>(value & 0xFFU)};
  append_crc(frame);
  return frame;
}

std::vector<uint8_t> write_single_response(uint8_t address, uint16_t register_address, uint16_t value) {
  std::vector<uint8_t> frame{address,
                             0x06U,
                             static_cast<uint8_t>(register_address >> 8U),
                             static_cast<uint8_t>(register_address & 0xFFU),
                             static_cast<uint8_t>(value >> 8U),
                             static_cast<uint8_t>(value & 0xFFU)};
  append_crc(frame);
  return frame;
}

}  // namespace

int main() {
  const uint8_t read_request[] = {0x03U, 0x07U, 0xDFU, 0x00U, 0x01U};
  const auto valid_read = read_response(0x01U, 1000U);
  assert(oq_modbus_recovery::crc16_modbus(valid_read.data(), valid_read.size()) == 0U);

  std::vector<uint8_t> leading_ff{0xFFU};
  leading_ff.insert(leading_ff.end(), valid_read.begin(), valid_read.end());
  assert(oq_modbus_recovery::find_expected_response_offset(leading_ff.data(), leading_ff.size(), 0x01U, read_request,
                                                           sizeof(read_request)) == 1U);

  std::vector<uint8_t> two_leading_bytes{0xFEU, 0xFFU};
  two_leading_bytes.insert(two_leading_bytes.end(), valid_read.begin(), valid_read.end());
  assert(oq_modbus_recovery::find_expected_response_offset(two_leading_bytes.data(), two_leading_bytes.size(), 0x01U,
                                                           read_request, sizeof(read_request)) == 2U);

  std::vector<uint8_t> trailing_ff = valid_read;
  trailing_ff.push_back(0xFFU);
  assert(oq_modbus_recovery::find_expected_response_offset(trailing_ff.data(), trailing_ff.size(), 0x01U, read_request,
                                                           sizeof(read_request)) == 0U);

  std::vector<uint8_t> corrupt = leading_ff;
  corrupt[corrupt.size() - 1U] ^= 0x01U;
  assert(oq_modbus_recovery::find_expected_response_offset(corrupt.data(), corrupt.size(), 0x01U, read_request,
                                                           sizeof(read_request)) == 0U);

  const uint8_t two_register_request[] = {0x03U, 0x07U, 0xDFU, 0x00U, 0x02U};
  assert(oq_modbus_recovery::find_expected_response_offset(leading_ff.data(), leading_ff.size(), 0x01U,
                                                           two_register_request, sizeof(two_register_request)) == 0U);

  std::vector<uint8_t> too_much_garbage{0x11U, 0x22U, 0x33U, 0x44U, 0xFFU};
  too_much_garbage.insert(too_much_garbage.end(), valid_read.begin(), valid_read.end());
  assert(oq_modbus_recovery::find_expected_response_offset(too_much_garbage.data(), too_much_garbage.size(), 0x01U,
                                                           read_request, sizeof(read_request)) == 0U);

  const uint8_t write_request[] = {0x06U, 0x07U, 0xDAU, 0x00U, 0x00U};
  const auto valid_write = write_single_response(0x01U, 0x07DAU, 0x0000U);
  std::vector<uint8_t> leading_write_ff{0xFFU};
  leading_write_ff.insert(leading_write_ff.end(), valid_write.begin(), valid_write.end());
  assert(oq_modbus_recovery::find_expected_response_offset(leading_write_ff.data(), leading_write_ff.size(), 0x01U,
                                                           write_request, sizeof(write_request)) == 1U);

  const uint8_t wrong_write_request[] = {0x06U, 0x07U, 0xDAU, 0x00U, 0x01U};
  assert(oq_modbus_recovery::find_expected_response_offset(leading_write_ff.data(), leading_write_ff.size(), 0x01U,
                                                           wrong_write_request, sizeof(wrong_write_request)) == 0U);

  return 0;
}
