#include <assert.h>

#include <array>

#include "../../openquatt/includes/odu/oq_odu_compressor_levels.h"

int main() {
  using oq_odu::CompressorLevelProfile;

  constexpr std::array<uint8_t, 21> expected_heating_frequencies = {
      0, 20, 26, 30, 36, 40, 45, 48, 52, 55, 60, 65, 68, 72, 76, 82, 85, 90, 95, 102, 110,
  };
  constexpr std::array<uint8_t, 21> expected_cooling_frequencies = {
      0, 20, 26, 30, 34, 36, 38, 40, 42, 44, 46, 48, 52, 54, 56, 58, 60, 64, 66, 68, 71,
  };
  assert(oq_odu::V2_HEATING_PHYSICAL_FREQUENCIES_HZ == expected_heating_frequencies);
  assert(oq_odu::V2_COOLING_PHYSICAL_FREQUENCIES_HZ == expected_cooling_frequencies);

  constexpr std::array<int, 11> expected_mapping = {0, 1, 2, 3, 7, 9, 10, 13, 15, 16, 17};
  for (int model_level = 0; model_level <= oq_odu::MODEL_LEVEL_MAX; ++model_level) {
    const auto command = oq_odu::resolve_automatic_level(true, 2, model_level);
    assert(command.control_level == model_level);
    assert(command.physical_level == expected_mapping[static_cast<size_t>(model_level)]);
  }

  assert(oq_odu::resolve_automatic_level(false, 2, 10).physical_level == 10);
  assert(oq_odu::resolve_automatic_level(true, 1, 10).physical_level == 10);
  assert(oq_odu::resolve_automatic_level(true, 2, 6).physical_level == 10);
  assert(oq_odu::resolve_automatic_level(true, 2, 99).physical_level == 17);
  assert(oq_odu::resolve_automatic_level(true, 2, -1).physical_level == 0);

  assert(oq_odu::resolve_manual_level(true, CompressorLevelProfile::V2_EXTENDED, 20).physical_level == 20);
  assert(oq_odu::resolve_manual_level(true, CompressorLevelProfile::V2_EXTENDED, 20).control_level == 10);
  assert(oq_odu::resolve_manual_level(false, CompressorLevelProfile::V2_EXTENDED, 20).physical_level == 10);
  assert(oq_odu::resolve_manual_level(true, CompressorLevelProfile::UNKNOWN, 20).physical_level == 10);

  std::array<uint8_t, 20> extended_response{};
  for (size_t index = 0; index < 10; ++index) {
    const uint16_t value = oq_odu::V2_HEATING_PHYSICAL_FREQUENCIES_HZ[index + 11U];
    extended_response[index * 2U] = static_cast<uint8_t>(value >> 8U);
    extended_response[index * 2U + 1U] = static_cast<uint8_t>(value & 0xFFU);
  }
  assert(oq_odu::detect_extended_heating_table_response(extended_response.data(), extended_response.size()) ==
         CompressorLevelProfile::V2_EXTENDED);
  extended_response[3]++;
  assert(oq_odu::detect_extended_heating_table_response(extended_response.data(), extended_response.size()) ==
         CompressorLevelProfile::UNKNOWN);
  assert(oq_odu::detect_extended_heating_table_response(nullptr, 0) == CompressorLevelProfile::UNKNOWN);

  const auto retained = oq_odu::resolve_retained_level(true, false, 17, 10, 17, true);
  assert(retained.control_level == 10);
  assert(retained.physical_level == 17);
  const auto retained_from_readback = oq_odu::resolve_retained_level(true, false, 17, 0, 0, true);
  assert(retained_from_readback.control_level == 10);
  assert(retained_from_readback.physical_level == 17);
  const auto retained_fail_closed = oq_odu::resolve_retained_level(true, false, 17, 10, 17, false);
  assert(retained_fail_closed.control_level == 10);
  assert(retained_fail_closed.physical_level == 10);
  const auto no_cooling_hold = oq_odu::resolve_retained_level(true, true, 17, 10, 17, true);
  assert(no_cooling_hold.control_level == 0);
  assert(no_cooling_hold.physical_level == 0);

  return 0;
}
