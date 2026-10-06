#include <cassert>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../fixtures/v1_eeprom.h"
#include "../fixtures/v1_5_eeprom.h"
#include "../fixtures/v2_old_amh6_eeprom.h"
#include "../fixtures/v2_new_amh6_eeprom.h"
#include "includes/odu/oq_odu_bottom_plate_settings.h"
#include "includes/odu/oq_odu_compressor_levels.h"
#include "includes/odu/oq_odu_defrost_diagnostics.h"
#include "includes/protocol/oq_modbus_recovery.h"

namespace {

using EepromWords = std::array<uint16_t, 512>;

struct Reference {
  const char* name;
  const std::array<uint16_t, 14>& core_words;
  const std::array<uint16_t, 2>& customer_words;
  bool customer_available;
  const EepromWords& eeprom;
  uint16_t crc;
  oq_odu::Generation generation;
  oq_odu::Variant variant;
  std::array<uint8_t, 21> cooling_hz;
  std::array<uint8_t, 21> heating_hz;
  std::array<int, 11> heating_mapping;
  oq_odu::CompressorLevelProfile profile;
  int defrost_mode;
  std::array<float, 7> mode4_delta_k;
};

constexpr std::array<uint8_t, 21> LEGACY_COOLING_HZ = {0, 30, 36, 42, 47, 52, 56, 61, 66, 71, 74};
constexpr std::array<uint8_t, 21> LEGACY_HEATING_HZ = {0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90};
constexpr std::array<int, 11> LEGACY_HEATING_MAPPING = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

const std::array<Reference, 4> REFERENCES = {{
    {"V1 (normalized cooling baseline)",
     oq_test::v1::CORE_IDENTITY_WORDS,
     oq_test::v1::CUSTOMER_MODEL_WORDS,
     false,
     oq_test::v1::EEPROM_WORDS,
     oq_test::v1::EEPROM_CRC,
     oq_odu::Generation::V1,
     oq_odu::Variant::V1,
     LEGACY_COOLING_HZ,
     LEGACY_HEATING_HZ,
     LEGACY_HEATING_MAPPING,
     oq_odu::CompressorLevelProfile::UNKNOWN,
     0,
     {}},
    {"V1.5",
     oq_test::v1_5::CORE_IDENTITY_WORDS,
     oq_test::v1_5::CUSTOMER_MODEL_WORDS,
     true,
     oq_test::v1_5::EEPROM_WORDS,
     oq_test::v1_5::EEPROM_CRC,
     oq_odu::Generation::V1_5,
     oq_odu::Variant::V1_5,
     LEGACY_COOLING_HZ,
     LEGACY_HEATING_HZ,
     LEGACY_HEATING_MAPPING,
     oq_odu::CompressorLevelProfile::UNKNOWN,
     0,
     {12, 11, 11, 10, 9, 7, 6}},
    {"V2 old model",
     oq_test::v2_old_amh6::CORE_IDENTITY_WORDS,
     oq_test::v2_old_amh6::CUSTOMER_MODEL_WORDS,
     true,
     oq_test::v2_old_amh6::EEPROM_WORDS,
     oq_test::v2_old_amh6::EEPROM_CRC,
     oq_odu::Generation::V2,
     oq_odu::Variant::V2_OLD_MODEL,
     {0, 30, 36, 42, 46, 48, 52, 56, 61, 66, 71},
     {0, 20, 26, 30, 48, 55, 61, 72, 80, 85, 90},
     LEGACY_HEATING_MAPPING,
     oq_odu::CompressorLevelProfile::V2_LEGACY,
     4,
     {12, 11, 10, 10, 9, 6, 5}},
    {"V2 new model",
     oq_test::v2_new_amh6::CORE_IDENTITY_WORDS,
     oq_test::v2_new_amh6::CUSTOMER_MODEL_WORDS,
     true,
     oq_test::v2_new_amh6::EEPROM_WORDS,
     oq_test::v2_new_amh6::EEPROM_CRC,
     oq_odu::Generation::V2,
     oq_odu::Variant::V2_NEW_MODEL,
     {0, 20, 26, 30, 34, 36, 38, 40, 42, 44, 46, 48, 52, 54, 56, 58, 60, 64, 66, 68, 71},
     {0, 20, 26, 30, 36, 40, 45, 48, 52, 55, 60, 65, 68, 72, 76, 82, 85, 90, 95, 102, 110},
     {0, 1, 2, 3, 7, 9, 10, 13, 15, 16, 17},
     oq_odu::CompressorLevelProfile::V2_EXTENDED,
     4,
     {8, 11, 9, 9, 8, 7, 6}},
}};

template <size_t N>
std::array<uint16_t, N> eeprom_block(const EepromWords& eeprom, uint16_t modbus_address) {
  assert(modbus_address >= 2999U);
  const size_t offset = modbus_address - 2999U;
  assert(offset + N <= eeprom.size());
  std::array<uint16_t, N> words{};
  for (size_t index = 0; index < N; ++index) words[index] = eeprom[offset + index];
  return words;
}

template <size_t N>
std::array<uint8_t, N * 2U> encode_words(const std::array<uint16_t, N>& words) {
  std::array<uint8_t, N * 2U> bytes{};
  for (size_t index = 0; index < N; ++index) {
    bytes[index * 2U] = static_cast<uint8_t>(words[index] >> 8U);
    bytes[index * 2U + 1U] = static_cast<uint8_t>(words[index]);
  }
  return bytes;
}

std::array<uint8_t, 512> eeprom_low_bytes(const EepromWords& eeprom) {
  std::array<uint8_t, 512> low_bytes{};
  for (size_t index = 0; index < eeprom.size(); ++index) {
    assert(eeprom[index] <= 255U);
    low_bytes[index] = static_cast<uint8_t>(eeprom[index]);
  }
  return low_bytes;
}

void check_fixture_integrity(const Reference& reference) {
  const auto low_bytes = eeprom_low_bytes(reference.eeprom);
  const uint16_t stored_crc = static_cast<uint16_t>(low_bytes[510] | (low_bytes[511] << 8U));
  assert(stored_crc == reference.crc);
  assert(oq_modbus_recovery::crc16_modbus(low_bytes.data(), 510U) == reference.crc);
  assert(oq_modbus_recovery::crc16_modbus(low_bytes.data(), low_bytes.size()) == 0U);
}

oq_odu::Detection check_identity(const Reference& reference) {
  const auto core_bytes = encode_words(reference.core_words);
  const auto customer_bytes = encode_words(reference.customer_words);
  const auto core = oq_odu::parse_core_identity_response(core_bytes.data(), core_bytes.size());
  const auto customer = reference.customer_available
                            ? oq_odu::parse_customer_model_response(customer_bytes.data(), customer_bytes.size())
                            : oq_odu::CustomerModelPrefix{};
  assert(core.available);
  assert(customer.available == reference.customer_available);
  const char* model_label = reference.variant == oq_odu::Variant::V1     ? "Unknown"
                            : reference.variant == oq_odu::Variant::V1_5 ? "Missing"
                                                                         : "AMH6";
  assert(std::strcmp(oq_odu::customer_model_label(customer), model_label) == 0);
  const auto detection = oq_odu::detect_generation(core, customer);
  assert(detection.generation == reference.generation);
  assert(detection.variant == reference.variant);
  if (oq_odu::requires_customer_model(core)) {
    assert(oq_odu::detect_generation(core).variant == oq_odu::Variant::UNKNOWN);
  }
  return detection;
}

void check_frequency_tables(const Reference& reference, oq_odu::Variant variant) {
  const auto base_bytes = encode_words(eeprom_block<22>(reference.eeprom, oq_odu::BASE_FREQUENCY_TABLE_REGISTER));
  auto snapshot = oq_odu::parse_base_frequency_table_response(base_bytes.data(), base_bytes.size(), variant);
  assert(snapshot.cooling.valid && snapshot.heating.valid);
  assert(snapshot.cooling.level_count == 11U && snapshot.heating.level_count == 11U);
  const auto original = oq_odu::encode_runtime_frequency_snapshot(snapshot);
  const auto extension_words = eeprom_block<20>(reference.eeprom, oq_odu::EXTENDED_FREQUENCY_TABLE_REGISTER);
  const auto extension_bytes = encode_words(extension_words);
  const auto extension =
      oq_odu::apply_v2_extension_frequency_table_response(snapshot, extension_bytes.data(), extension_bytes.size());
  const bool extended = variant == oq_odu::Variant::V2_NEW_MODEL;
  assert(extension.response_complete == extended);
  assert(extension.heating_valid == extended && extension.cooling_valid == extended);
  if (!extended) {
    // V1 has nonzero words at these addresses; its fingerprint must still reject extension parsing.
    assert(oq_odu::encode_runtime_frequency_snapshot(snapshot) == original);
  }
  assert(snapshot.cooling.level_count == (extended ? 21U : 11U));
  assert(snapshot.heating.level_count == (extended ? 21U : 11U));
  assert(snapshot.cooling.hz == reference.cooling_hz && snapshot.heating.hz == reference.heating_hz);
  const bool configured_v2 = reference.generation == oq_odu::Generation::V2;
  for (int level = 0; level <= 10; ++level) {
    const auto heating_command = oq_odu::resolve_automatic_level(configured_v2, snapshot, 2, level);
    assert(heating_command.control_level == level);
    assert(heating_command.physical_level == reference.heating_mapping[static_cast<size_t>(level)]);
    assert(oq_odu::resolve_automatic_level(configured_v2, snapshot, 1, level).physical_level == level);
  }

  const auto profile = oq_odu::compressor_level_profile(snapshot);
  assert(profile == reference.profile);
  const char* profile_label = extended                                   ? "V2 F0-F20"
                              : variant == oq_odu::Variant::V2_OLD_MODEL ? "V2 F0-F10"
                                                                         : "Unknown / F0-F10 safe";
  assert(std::strcmp(oq_odu::compressor_level_profile_label(profile), profile_label) == 0);
  for (int mode = 1; mode <= 2; ++mode) {
    assert(oq_odu::has_extended_compressor_levels(profile, mode) == extended);
    assert(oq_odu::has_extended_frequency_table(snapshot, mode) == extended);
    assert(oq_odu::physical_level_limit(configured_v2, snapshot, mode) == (extended ? 20 : 10));
    assert(oq_odu::resolve_manual_level(configured_v2, snapshot, mode, 20).physical_level == (extended ? 20 : 10));
    assert(oq_odu::resolve_manual_level(false, snapshot, mode, 20).physical_level == 10);
  }
}

void check_bottom_plate(const Reference& reference, oq_odu::Variant variant) {
  const auto bytes = encode_words(eeprom_block<3>(reference.eeprom, oq_odu::BOTTOM_PLATE_START_ADDRESS));
  oq_odu::BottomPlateSettings settings;
  assert(oq_odu::decode_bottom_plate_settings(bytes.data(), bytes.size(), settings));
  assert(oq_odu::valid_bottom_plate_settings(settings, variant));
  assert(settings.mode == (variant == oq_odu::Variant::V1 ? 1U : 3U));
  assert(settings.start_temperature_c == 4 && settings.stop_delta_c == 3U);
  assert(oq_odu::bottom_plate_settings_match(settings, oq_odu::default_bottom_plate_settings(variant)));
}

void check_mode4_profile(const Reference& reference, const oq_defrost::Parameters& parameters) {
  // Exercise each side of the real mode-4 ambient band boundaries.
  constexpr std::array<float, 14> ambient = {8.999f, 0,        -0.001f, -5,       -5.001f, -10,      -10.001f,
                                             -15,    -15.001f, -20,     -20.001f, -23,     -23.001f, -30};
  constexpr std::array<size_t, 14> band = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6};
  for (size_t index = 0; index < ambient.size(); ++index) {
    assert(oq_defrost::delta_threshold(parameters, ambient[index]) == reference.mode4_delta_k[band[index]]);
  }
  assert(std::isnan(oq_defrost::delta_threshold(parameters, 9)));

  // Check diagnostic interpretation, without predicting autonomous ODU starts.
  oq_defrost::Diagnostics diagnostics;
  const float evap_at_threshold = 3.0f - reference.mode4_delta_k[0];
  diagnostics.sample(parameters, true, false, 3, -9, evap_at_threshold, 20, 1000);
  assert(diagnostics.delta_k == reference.mode4_delta_k[0]);
  assert(diagnostics.interval_s == 2700 && diagnostics.minimum_runtime_s == 300);
  assert(diagnostics.max_duration_s == 480 && diagnostics.confirm_required_s == 60);
  assert(diagnostics.exit_c == 17 && diagnostics.alternate_exit_c == 25);
  assert(diagnostics.exit_required_s == -1);  // Exit task-tick units remain unproven.
  assert(diagnostics.confirm_s == 0);
  diagnostics.sample(parameters, true, false, 3, -9, evap_at_threshold - 0.1f, 20, 2000);
  diagnostics.sample(parameters, true, false, 3, -9, evap_at_threshold - 0.1f, 20, 3000);
  assert(diagnostics.confirm_s == 1);
  diagnostics.sample(parameters, true, false, -25, -9, -40, 20, 4000);
  assert(diagnostics.exit_c == 17);
  diagnostics.sample(parameters, true, false, -25.001f, -9, -40, 20, 5000);
  assert(diagnostics.exit_c == 25);
  diagnostics.sample(parameters, false, false, 3, -9, -10, 20, 6000);
  assert(std::isnan(diagnostics.delta_k) && diagnostics.confirm_s == -1);
}

void check_defrost(const Reference& reference, oq_odu::Variant variant) {
  oq_defrost::Parameters parameters;
  parameters.variant = variant;
  parameters.base = eeprom_block<11>(reference.eeprom, 3270U);   // P271..P281
  parameters.timing = eeprom_block<9>(reference.eeprom, 3307U);  // P308..P316
  parameters.coil = eeprom_block<6>(reference.eeprom, 3336U);    // P337..P342
  parameters.delta = eeprom_block<14>(reference.eeprom, 3414U);  // P415..P428
  parameters.loaded = true;
  assert(parameters.automatic() && parameters.mode() == reference.defrost_mode);
  assert(oq_defrost::is_supported_defrost_mode(parameters.mode(), variant));
  if (parameters.mode() == 0) {
    constexpr std::array<float, 5> ambient = {3, -3, -3.001f, -10, -10.001f};
    constexpr std::array<float, 5> start_c = {-3, -3, -5, -5, -12};
    for (size_t index = 0; index < ambient.size(); ++index) {
      assert(oq_defrost::start_threshold(parameters, ambient[index]) == start_c[index]);
    }
    assert(std::isnan(oq_defrost::delta_threshold(parameters, 3)));
    oq_defrost::Diagnostics diagnostics;
    diagnostics.sample(parameters, true, false, 3, -3, -10, 30, 1000);
    assert(diagnostics.start_c == -3 && std::isnan(diagnostics.delta_k));
    assert(diagnostics.interval_s == -1 && diagnostics.minimum_runtime_s == 300);
    assert(diagnostics.confirm_required_s == 180 && diagnostics.max_duration_s == 480);
    assert(diagnostics.exit_c == 17 && diagnostics.alternate_exit_c == 25 && diagnostics.exit_required_s == -1);
    diagnostics.sample(parameters, true, false, 3, -3.1f, -10, 30, 2000);
    diagnostics.sample(parameters, true, false, 3, -3.1f, -10, 30, 3000);
    assert(diagnostics.confirm_s == 1);
    diagnostics.sample(parameters, false, false, 3, -4, -10, 30, 4000);
    assert(std::isnan(diagnostics.start_c) && diagnostics.confirm_s == -1);
  }
  // V1.5 was captured in mode 0; also test its real extension if mode 4 is selected.
  parameters.base[5] = 4;
  if (oq_defrost::has_mode4_defrost(variant)) {
    check_mode4_profile(reference, parameters);
  } else {
    assert(std::isnan(oq_defrost::delta_threshold(parameters, 3)));
    oq_defrost::Diagnostics diagnostics;
    diagnostics.sample(parameters, true, false, 3, -10, -20, 30, 1000);
    assert(std::isnan(diagnostics.delta_k) && diagnostics.confirm_required_s == -1);
  }
}

void check_captured_runtime_cooling(const Reference& reference, const std::array<uint16_t, 11>& cooling,
                                    uint16_t captured_crc) {
  auto captured = reference.eeprom;
  for (size_t index = 0; index < cooling.size(); ++index) captured[1U + index] = cooling[index];
  const auto low_bytes = eeprom_low_bytes(captured);
  assert(oq_modbus_recovery::crc16_modbus(low_bytes.data(), 510U) == captured_crc);
  assert(captured_crc != reference.crc);
  assert((captured[510] | (captured[511] << 8U)) == reference.crc);
  const auto response = encode_words(eeprom_block<22>(captured, oq_odu::BASE_FREQUENCY_TABLE_REGISTER));
  const auto snapshot =
      oq_odu::parse_base_frequency_table_response(response.data(), response.size(), reference.variant);
  assert(snapshot.cooling.valid && snapshot.heating.valid);
  for (size_t level = 0; level < cooling.size(); ++level) {
    assert(oq_odu::frequency_for_physical_level(snapshot.cooling, static_cast<int>(level)) == cooling[level]);
  }
  assert(snapshot.heating.hz == reference.heating_hz);
  assert(oq_odu::compressor_level_profile(snapshot) == reference.profile);
  assert(oq_odu::resolve_manual_level(false, snapshot, 1, 20).physical_level == 10);
}

}  // namespace

int main() {
  for (const auto& reference : REFERENCES) {
    std::puts(reference.name);
    check_fixture_integrity(reference);
    const auto detection = check_identity(reference);
    check_frequency_tables(reference, detection.variant);
    check_bottom_plate(reference, detection.variant);
    check_defrost(reference, detection.variant);
  }
  check_captured_runtime_cooling(REFERENCES[0], oq_test::v1::CAPTURED_COOLING_WORDS,
                                 oq_test::v1::CAPTURED_CALCULATED_CRC);
  check_captured_runtime_cooling(REFERENCES[1], oq_test::v1_5::CAPTURED_COOLING_WORDS,
                                 oq_test::v1_5::CAPTURED_CALCULATED_CRC);
  return 0;
}
