#include <assert.h>

#include <limits>
#include <string>

#include "../../openquatt/includes/odu/oq_odu_generation.h"

namespace {

using oq_odu::detect_control_board_item;
using oq_odu::detect_control_board_item_response;
using oq_odu::Generation;
using oq_odu::generation_label;
using oq_odu::next_request_token;

void test_known_control_board_items_map_exactly() {
  assert(detect_control_board_item(0x0037U) == Generation::V1);
  assert(detect_control_board_item(0x0E37U) == Generation::V1_5);
  assert(detect_control_board_item(0x1037U) == Generation::V2);

  assert(std::string(generation_label(Generation::V1)) == "V1");
  assert(std::string(generation_label(Generation::V1_5)) == "V1.5");
  assert(std::string(generation_label(Generation::V2)) == "V2");
}

void test_missing_or_unrecognized_values_fail_closed() {
  assert(detect_control_board_item(0x0000U) == Generation::UNKNOWN);
  assert(detect_control_board_item(0x0038U) == Generation::UNKNOWN);
  assert(detect_control_board_item(0xFFFFU) == Generation::UNKNOWN);

  assert(detect_control_board_item(false, 0x0037).generation == Generation::UNKNOWN);
  assert(detect_control_board_item(true, -1.0f).generation == Generation::UNKNOWN);
  assert(detect_control_board_item(true, 65536.0f).generation == Generation::UNKNOWN);
  assert(detect_control_board_item(true, 55.5f).generation == Generation::UNKNOWN);
  assert(detect_control_board_item(true, std::numeric_limits<float>::quiet_NaN()).generation == Generation::UNKNOWN);
  assert(std::string(generation_label(Generation::UNKNOWN)) == "Unknown");
}

void test_float_detection_preserves_raw_diagnostic_value() {
  const auto detection = detect_control_board_item(true, 0x1037);
  assert(detection.generation == Generation::V2);
  assert(detection.control_board_item_available);
  assert(detection.control_board_item == 0x1037U);

  const auto missing = detect_control_board_item(false, 0x1037);
  assert(!missing.control_board_item_available);
  assert(missing.control_board_item == 0U);
}

void test_modbus_response_is_big_endian_and_length_checked() {
  const uint8_t v1_response[] = {0x00U, 0x37U};
  const uint8_t v1_5_response[] = {0x0EU, 0x37U, 0xAAU, 0x55U};
  const uint8_t v2_response[] = {0x10U, 0x37U};
  const uint8_t unknown_response[] = {0x12U, 0x34U};
  const uint8_t short_response[] = {0x10U};

  assert(detect_control_board_item_response(v1_response, sizeof(v1_response)).generation == Generation::V1);
  assert(detect_control_board_item_response(v1_5_response, sizeof(v1_5_response)).generation == Generation::V1_5);
  assert(detect_control_board_item_response(v2_response, sizeof(v2_response)).generation == Generation::V2);
  const auto unknown = detect_control_board_item_response(unknown_response, sizeof(unknown_response));
  assert(unknown.generation == Generation::UNKNOWN);
  assert(unknown.control_board_item_available);
  assert(unknown.control_board_item == 0x1234U);

  const auto short_detection = detect_control_board_item_response(short_response, sizeof(short_response));
  assert(short_detection.generation == Generation::UNKNOWN);
  assert(!short_detection.control_board_item_available);
  assert(!detect_control_board_item_response(nullptr, 0U).control_board_item_available);
}

void test_request_tokens_never_use_zero() {
  assert(next_request_token(0U) == 1U);
  assert(next_request_token(41U) == 42U);
  assert(next_request_token(std::numeric_limits<uint32_t>::max()) == 1U);
}

}  // namespace

int main() {
  test_known_control_board_items_map_exactly();
  test_missing_or_unrecognized_values_fail_closed();
  test_float_detection_preserves_raw_diagnostic_value();
  test_modbus_response_is_big_endian_and_length_checked();
  test_request_tokens_never_use_zero();
  return 0;
}
