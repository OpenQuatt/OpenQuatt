#include <assert.h>

#include <array>
#include <limits>
#include <string>

#include "../../openquatt/includes/odu/oq_odu_generation.h"

namespace {

using oq_odu::CoreIdentity;
using oq_odu::customer_model_label;
using oq_odu::CustomerModelPrefix;
using oq_odu::detect_generation;
using oq_odu::Detection;
using oq_odu::Generation;
using oq_odu::generation_label;
using oq_odu::next_request_token;
using oq_odu::parse_core_identity_response;
using oq_odu::parse_customer_model_response;
using oq_odu::requires_customer_model;
using oq_odu::Variant;
using oq_odu::variant_label;

void write_word(uint8_t* data, size_t index, uint16_t value) {
  data[index * 2U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  data[index * 2U + 1U] = static_cast<uint8_t>(value & 0xFFU);
}

std::array<uint8_t, oq_odu::CORE_IDENTITY_REGISTER_COUNT * 2U> core_response(uint16_t compressor_code,
                                                                             uint16_t pcb_program,
                                                                             uint16_t control_board_item) {
  std::array<uint8_t, oq_odu::CORE_IDENTITY_REGISTER_COUNT * 2U> data{};
  write_word(data.data(), oq_odu::COMPRESSOR_CODE_WORD_OFFSET, compressor_code);
  write_word(data.data(), oq_odu::PCB_PROGRAM_WORD_OFFSET, pcb_program);
  write_word(data.data(), oq_odu::CONTROL_BOARD_ITEM_WORD_OFFSET, control_board_item);
  return data;
}

CustomerModelPrefix customer_model(uint16_t first, uint16_t second) {
  std::array<uint8_t, oq_odu::CUSTOMER_MODEL_REGISTER_COUNT * 2U> data{};
  write_word(data.data(), 0U, first);
  write_word(data.data(), 1U, second);
  return parse_customer_model_response(data.data(), data.size());
}

void assert_detection(const Detection& detection, Generation generation, Variant variant) {
  assert(detection.generation == generation);
  assert(detection.variant == variant);
}

void test_core_identity_offsets_and_length_checks() {
  const auto response = core_response(2825U, 0x0122U, 0x0E37U);
  const auto core = parse_core_identity_response(response.data(), response.size());
  assert(core.available);
  assert(core.compressor_code == 2825U);
  assert(core.pcb_program == 0x0122U);
  assert(core.control_board_item == 0x0E37U);
  assert(requires_customer_model(core));

  assert(!parse_core_identity_response(response.data(), response.size() - 1U).available);
  assert(!parse_core_identity_response(nullptr, 0U).available);
}

void test_customer_model_parsing() {
  const auto amh6 = customer_model(0x414DU, 0x4836U);
  assert(amh6.available);
  assert(!amh6.missing);
  assert(amh6.printable);
  assert(std::string(customer_model_label(amh6)) == "AMH6");

  for (const auto missing : {
           customer_model(0x0000U, 0x0000U),
           customer_model(0xFFFFU, 0xFFFFU),
           customer_model(0x0000U, 0xFFFFU),
       }) {
    assert(missing.available);
    assert(missing.missing);
    assert(std::string(customer_model_label(missing)) == "Missing");
  }

  const auto invalid = customer_model(0x414DU, 0xFF36U);
  assert(invalid.available);
  assert(!invalid.missing);
  assert(!invalid.printable);
  assert(std::string(customer_model_label(invalid)) == "Invalid");

  const uint8_t short_response[] = {0x41U, 0x4DU, 0x48U};
  const auto unavailable = parse_customer_model_response(short_response, sizeof(short_response));
  assert(!unavailable.available);
  assert(std::string(customer_model_label(unavailable)) == "Unknown");
}

void test_reference_fingerprints() {
  const auto v1_bytes = core_response(0U, 0x0119U, 0x0037U);
  const auto v1 = parse_core_identity_response(v1_bytes.data(), v1_bytes.size());
  assert_detection(detect_generation(v1), Generation::V1, Variant::V1);
  assert(!requires_customer_model(v1));

  const auto v1_5_bytes = core_response(0U, 0x011EU, 0x0E37U);
  const auto v1_5 = parse_core_identity_response(v1_5_bytes.data(), v1_5_bytes.size());
  assert_detection(detect_generation(v1_5, customer_model(0xFFFFU, 0xFFFFU)), Generation::V1_5, Variant::V1_5);

  const auto v2_old_model_bytes = core_response(2825U, 0x0122U, 0x0E37U);
  const auto v2_old_model = parse_core_identity_response(v2_old_model_bytes.data(), v2_old_model_bytes.size());
  assert_detection(detect_generation(v2_old_model, customer_model(0x414DU, 0x4836U)), Generation::V2,
                   Variant::V2_OLD_MODEL);
  assert_detection(detect_generation(v2_old_model, customer_model(0xFFFFU, 0xFFFFU)), Generation::V2,
                   Variant::V2_OLD_MODEL);

  const auto v2_current_bytes = core_response(2825U, 0x0201U, 0x1037U);
  const auto v2_current = parse_core_identity_response(v2_current_bytes.data(), v2_current_bytes.size());
  assert_detection(detect_generation(v2_current), Generation::V2, Variant::V2_NEW_MODEL);
  assert(!requires_customer_model(v2_current));
}

void test_ambiguous_or_conflicting_fingerprints_fail_closed() {
  const auto missing_customer = customer_model(0xFFFFU, 0xFFFFU);
  const auto amh6 = customer_model(0x414DU, 0x4836U);
  const auto other_model = customer_model(0x414DU, 0x4837U);

  assert_detection(detect_generation({true, 0U, 0x011EU, 0x0E37U}), Generation::UNKNOWN, Variant::UNKNOWN);
  assert_detection(detect_generation({true, 0U, 0x011EU, 0x0E37U}, amh6), Generation::UNKNOWN, Variant::UNKNOWN);
  assert_detection(detect_generation({true, 2825U, 0x0122U, 0x0E37U}, other_model), Generation::UNKNOWN,
                   Variant::UNKNOWN);
  assert_detection(detect_generation({true, 0U, 0x0122U, 0x0E37U}, missing_customer), Generation::UNKNOWN,
                   Variant::UNKNOWN);
  assert_detection(detect_generation({true, 2825U, 0x011EU, 0x0E37U}, missing_customer), Generation::UNKNOWN,
                   Variant::UNKNOWN);
  assert_detection(detect_generation({true, 2825U, 0x9999U, 0x0E37U}, missing_customer), Generation::UNKNOWN,
                   Variant::UNKNOWN);
  assert_detection(detect_generation({true, 2825U, 0x0122U, 0x9999U}, amh6), Generation::UNKNOWN, Variant::UNKNOWN);
  assert_detection(detect_generation(CoreIdentity{}), Generation::UNKNOWN, Variant::UNKNOWN);
}

void test_labels_and_request_tokens() {
  assert(std::string(generation_label(Generation::V1)) == "V1");
  assert(std::string(generation_label(Generation::V1_5)) == "V1.5");
  assert(std::string(generation_label(Generation::V2)) == "V2");
  assert(std::string(generation_label(Generation::UNKNOWN)) == "Unknown");
  assert(std::string(variant_label(Variant::V2_OLD_MODEL)) == "V2 old model");
  assert(std::string(variant_label(Variant::V2_NEW_MODEL)) == "V2 new model");
  assert(std::string(variant_label(Variant::UNKNOWN)) == "Unknown");

  assert(next_request_token(0U) == 1U);
  assert(next_request_token(41U) == 42U);
  assert(next_request_token(std::numeric_limits<uint32_t>::max()) == 1U);
}

}  // namespace

int main() {
  test_core_identity_offsets_and_length_checks();
  test_customer_model_parsing();
  test_reference_fingerprints();
  test_ambiguous_or_conflicting_fingerprints_fail_closed();
  test_labels_and_request_tokens();
  return 0;
}
