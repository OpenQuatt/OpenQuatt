#include <assert.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../openquatt/includes/odu/oq_odu_frequency_table.h"

namespace {

template <size_t N>
std::array<uint8_t, N * 2U> encode_words(const std::array<uint16_t, N>& words) {
  std::array<uint8_t, N * 2U> data{};
  for (size_t index = 0; index < words.size(); ++index) {
    data[index * 2U] = static_cast<uint8_t>(words[index] >> 8U);
    data[index * 2U + 1U] = static_cast<uint8_t>(words[index] & 0xFFU);
  }
  return data;
}

oq_odu::RuntimeFrequencySnapshot new_v2_snapshot() {
  constexpr std::array<uint16_t, 22> base = {
      0, 20, 26, 30, 34, 36, 38, 40, 42, 44, 46, 0, 20, 26, 30, 36, 40, 45, 48, 52, 55, 60,
  };
  const auto response = encode_words(base);
  return oq_odu::parse_base_frequency_table_response(response.data(), response.size(), oq_odu::Variant::V2_NEW_MODEL);
}

}  // namespace

int main() {
  constexpr std::array<uint16_t, 22> v1_and_v1_5_reference = {
      0, 30, 36, 42, 47, 52, 56, 61, 66, 71, 74, 0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90,
  };
  const auto reference_response = encode_words(v1_and_v1_5_reference);
  const auto v1 = oq_odu::parse_base_frequency_table_response(reference_response.data(), reference_response.size(),
                                                              oq_odu::Variant::V1);
  const auto v1_5 = oq_odu::parse_base_frequency_table_response(reference_response.data(), reference_response.size(),
                                                                oq_odu::Variant::V1_5);
  assert(v1.cooling.valid && v1.heating.valid);
  assert(v1_5.cooling.valid && v1_5.heating.valid);
  assert(v1.cooling.hz == v1_5.cooling.hz);
  assert(v1.heating.hz == v1_5.heating.hz);

  // Runtime changes and duplicate frequencies are valid EEPROM content.
  constexpr std::array<uint16_t, 22> modified_v1 = {
      0, 20, 22, 24, 26, 28, 30, 30, 30, 30, 30, 0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90,
  };
  const auto modified_response = encode_words(modified_v1);
  const auto modified = oq_odu::parse_base_frequency_table_response(modified_response.data(), modified_response.size(),
                                                                    oq_odu::Variant::V1);
  assert(modified.cooling.valid);
  assert(modified.cooling.hz[6] == modified.cooling.hz[10]);

  constexpr std::array<uint16_t, 22> modified_v1_5 = {
      0, 26, 28, 30, 32, 34, 36, 38, 40, 71, 74, 0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90,
  };
  const auto modified_v1_5_response = encode_words(modified_v1_5);
  const auto modified_v1_5_snapshot = oq_odu::parse_base_frequency_table_response(
      modified_v1_5_response.data(), modified_v1_5_response.size(), oq_odu::Variant::V1_5);
  assert(modified_v1_5_snapshot.cooling.valid);
  assert(modified_v1_5_snapshot.heating.valid);

  auto new_v2 = new_v2_snapshot();
  assert(new_v2.cooling.valid && new_v2.heating.valid);
  assert(new_v2.cooling.level_count == 11U);
  assert(new_v2.heating.level_count == 11U);

  constexpr std::array<uint16_t, 20> extension = {
      65, 68, 72, 76, 82, 85, 90, 95, 102, 110, 48, 52, 54, 56, 58, 60, 64, 66, 68, 71,
  };
  const auto extension_response = encode_words(extension);
  const auto extension_result =
      oq_odu::apply_v2_extension_frequency_table_response(new_v2, extension_response.data(), extension_response.size());
  assert(extension_result.response_complete);
  assert(extension_result.heating_valid);
  assert(extension_result.cooling_valid);
  assert(new_v2.heating.level_count == 21U);
  assert(new_v2.cooling.level_count == 21U);
  assert(oq_odu::frequency_for_physical_level(new_v2.heating, 17) == 90);
  assert(oq_odu::frequency_for_physical_level(new_v2.cooling, 20) == 71);

  const auto stored_snapshot = oq_odu::encode_runtime_frequency_snapshot(new_v2);
  const auto restored_snapshot = oq_odu::decode_runtime_frequency_snapshot(stored_snapshot);
  assert(restored_snapshot.variant == oq_odu::Variant::V2_NEW_MODEL);
  assert(restored_snapshot.heating.valid && restored_snapshot.cooling.valid);
  assert(restored_snapshot.heating.hz == new_v2.heating.hz);
  assert(restored_snapshot.cooling.hz == new_v2.cooling.hz);
  auto corrupt_storage = stored_snapshot;
  corrupt_storage[1] = 22U;
  assert(!oq_odu::decode_runtime_frequency_snapshot(corrupt_storage).cooling.valid);

  // Extension addresses are never accepted for an older fingerprint.
  auto legacy = v1;
  const auto legacy_result =
      oq_odu::apply_v2_extension_frequency_table_response(legacy, extension_response.data(), extension_response.size());
  assert(!legacy_result.response_complete);
  assert(legacy.heating.level_count == 11U);
  assert(legacy.cooling.level_count == 11U);

  // Heating and cooling extension validity are independent.
  auto partially_valid = new_v2_snapshot();
  auto invalid_heating_extension = extension;
  invalid_heating_extension[3] = 60;  // Drops below the preceding 72 Hz.
  const auto partial_response = encode_words(invalid_heating_extension);
  const auto partial_result = oq_odu::apply_v2_extension_frequency_table_response(
      partially_valid, partial_response.data(), partial_response.size());
  assert(partial_result.response_complete);
  assert(!partial_result.heating_valid);
  assert(partial_result.cooling_valid);
  assert(partially_valid.heating.level_count == 11U);
  assert(partially_valid.cooling.level_count == 21U);

  auto invalid_extension_start = new_v2_snapshot();
  auto extension_below_f10 = extension;
  extension_below_f10[0] = 59;
  const auto invalid_extension_start_response = encode_words(extension_below_f10);
  const auto invalid_extension_start_result = oq_odu::apply_v2_extension_frequency_table_response(
      invalid_extension_start, invalid_extension_start_response.data(), invalid_extension_start_response.size());
  assert(!invalid_extension_start_result.heating_valid);
  assert(invalid_extension_start_result.cooling_valid);

  auto invalid_base = v1_and_v1_5_reference;
  invalid_base[0] = 1;
  invalid_base[16] = 54;  // Heating drops from 55 Hz to 54 Hz.
  const auto invalid_base_response = encode_words(invalid_base);
  const auto invalid = oq_odu::parse_base_frequency_table_response(invalid_base_response.data(),
                                                                   invalid_base_response.size(), oq_odu::Variant::V1_5);
  assert(!invalid.cooling.valid);
  assert(!invalid.heating.valid);

  auto out_of_range_base = v1_and_v1_5_reference;
  out_of_range_base[5] = 121;
  const auto out_of_range_response = encode_words(out_of_range_base);
  const auto out_of_range = oq_odu::parse_base_frequency_table_response(
      out_of_range_response.data(), out_of_range_response.size(), oq_odu::Variant::V1);
  assert(!out_of_range.cooling.valid);
  assert(out_of_range.heating.valid);

  const auto truncated = oq_odu::parse_base_frequency_table_response(
      reference_response.data(), reference_response.size() - 1U, oq_odu::Variant::V1);
  assert(truncated.cooling.valid);
  assert(!truncated.heating.valid);

  assert(oq_odu::resolve_nearest_physical_level(new_v2.heating, 80, 90) == 15);
  assert(oq_odu::resolve_nearest_physical_level(new_v2.heating, 90, 90) == 17);
  assert(oq_odu::resolve_nearest_physical_level(new_v2.heating, 110, 90) == 17);
  assert(oq_odu::resolve_nearest_physical_level(new_v2.cooling, 50, 71) == 11);

  return 0;
}
