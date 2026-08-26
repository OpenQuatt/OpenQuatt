#include <assert.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../openquatt/includes/odu/oq_odu_compressor_levels.h"

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

oq_odu::RuntimeFrequencySnapshot extended_v2_snapshot() {
  constexpr std::array<uint16_t, 22> base = {
      0, 20, 26, 30, 34, 36, 38, 40, 42, 44, 46, 0, 20, 26, 30, 36, 40, 45, 48, 52, 55, 60,
  };
  constexpr std::array<uint16_t, 20> extension = {
      65, 68, 72, 76, 82, 85, 90, 95, 102, 110, 48, 52, 54, 56, 58, 60, 64, 66, 68, 71,
  };
  const auto base_response = encode_words(base);
  const auto extension_response = encode_words(extension);
  auto snapshot = oq_odu::parse_base_frequency_table_response(base_response.data(), base_response.size(),
                                                              oq_odu::Variant::V2_NEW_MODEL);
  const auto result = oq_odu::apply_v2_extension_frequency_table_response(snapshot, extension_response.data(),
                                                                          extension_response.size());
  assert(result.heating_valid && result.cooling_valid);
  return snapshot;
}

oq_odu::RuntimeFrequencySnapshot old_v2_snapshot() {
  constexpr std::array<uint16_t, 22> base = {
      0, 30, 36, 42, 46, 48, 52, 56, 61, 66, 71, 0, 20, 26, 30, 48, 55, 61, 72, 80, 85, 90,
  };
  const auto response = encode_words(base);
  return oq_odu::parse_base_frequency_table_response(response.data(), response.size(), oq_odu::Variant::V2_OLD_MODEL);
}

}  // namespace

int main() {
  const auto extended = extended_v2_snapshot();
  const auto old_v2 = old_v2_snapshot();
  const oq_odu::RuntimeFrequencySnapshot unknown{};

  constexpr std::array<int, 11> expected_mapping = {0, 1, 2, 3, 7, 9, 10, 13, 15, 16, 17};
  for (int model_level = 0; model_level <= oq_odu::MODEL_LEVEL_MAX; ++model_level) {
    const auto command = oq_odu::resolve_automatic_level(true, extended, 2, model_level);
    assert(command.control_level == model_level);
    assert(command.physical_level == expected_mapping[static_cast<size_t>(model_level)]);
  }
  // A model-domain Day/Silent cap at level 6 resolves afterwards to physical F10.
  assert(oq_odu::resolve_automatic_level(true, extended, 2, 6).physical_level == 10);

  // Known V2-old factory data and cooling retain their current physical outcomes.
  assert(oq_odu::resolve_automatic_level(true, old_v2, 2, 10).physical_level == 10);
  auto modified_old_v2 = old_v2;
  modified_old_v2.heating.hz = {0, 20, 26, 30, 36, 40, 48, 55, 61, 72, 80};
  assert(oq_odu::validate_frequency_table(modified_old_v2.heating));
  assert(oq_odu::resolve_automatic_level(true, modified_old_v2, 2, 4).physical_level == 6);
  assert(oq_odu::resolve_automatic_level(false, extended, 2, 10).physical_level == 10);
  assert(oq_odu::resolve_automatic_level(true, extended, 1, 10).physical_level == 10);
  assert(oq_odu::resolve_automatic_level(true, extended, 2, 99).physical_level == 17);
  assert(oq_odu::resolve_automatic_level(true, extended, 2, -1).physical_level == 0);

  // Missing runtime data preserves the legacy F0-F10 automatic fallback.
  auto invalid_new_v2 = unknown;
  invalid_new_v2.variant = oq_odu::Variant::V2_NEW_MODEL;
  assert(oq_odu::resolve_automatic_level(true, invalid_new_v2, 2, 10).physical_level == 10);
  assert(oq_odu::resolve_automatic_level(false, unknown, 2, 10).physical_level == 10);
  assert(oq_odu::resolve_automatic_level(false, unknown, 1, 10).physical_level == 10);

  assert(oq_odu::physical_level_limit(true, extended, 1) == 20);
  assert(oq_odu::physical_level_limit(true, extended, 2) == 20);
  assert(oq_odu::physical_level_limit(true, old_v2, 2) == 10);
  assert(oq_odu::physical_level_limit(false, extended, 2) == 10);
  assert(oq_odu::resolve_manual_level(true, extended, 2, 20).physical_level == 20);
  assert(oq_odu::resolve_manual_level(true, extended, 2, 20).control_level == 10);
  assert(oq_odu::resolve_manual_level(false, extended, 2, 20).physical_level == 10);
  assert(oq_odu::resolve_manual_level(true, unknown, 2, 20).physical_level == 10);

  assert(oq_odu::compressor_level_profile(extended) == oq_odu::CompressorLevelProfile::V2_EXTENDED);
  assert(oq_odu::compressor_level_profile(old_v2) == oq_odu::CompressorLevelProfile::UNKNOWN);

  const auto retained = oq_odu::resolve_retained_level(true, false, 17, 10, 17, true, extended);
  assert(retained.control_level == 10);
  assert(retained.physical_level == 17);
  const auto retained_from_readback = oq_odu::resolve_retained_level(true, false, 17, 0, 0, true, extended);
  assert(retained_from_readback.control_level == 10);
  assert(retained_from_readback.physical_level == 17);
  const auto retained_from_off_anchor_readback = oq_odu::resolve_retained_level(true, false, 11, 0, 0, true, extended);
  assert(retained_from_off_anchor_readback.control_level == 6);
  assert(retained_from_off_anchor_readback.physical_level == 11);
  const auto retained_from_f20_readback = oq_odu::resolve_retained_level(true, false, 20, 0, 0, true, extended);
  assert(retained_from_f20_readback.control_level == 10);
  assert(retained_from_f20_readback.physical_level == 20);
  const auto retained_fail_closed = oq_odu::resolve_retained_level(true, false, 17, 10, 17, false, extended);
  assert(retained_fail_closed.control_level == 10);
  assert(retained_fail_closed.physical_level == 10);
  const auto no_cooling_hold = oq_odu::resolve_retained_level(true, true, 17, 10, 17, true, extended);
  assert(no_cooling_hold.control_level == 0);
  assert(no_cooling_hold.physical_level == 0);

  return 0;
}
