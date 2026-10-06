#include <cassert>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../fixtures/v2_old_amh6_eeprom.h"
#include "includes/odu/oq_odu_bottom_plate_settings.h"
#include "includes/odu/oq_odu_compressor_levels.h"
#include "includes/odu/oq_odu_defrost_diagnostics.h"
#include "includes/protocol/oq_modbus_recovery.h"

namespace {

namespace fixture = oq_test::v2_old_amh6;

template <size_t N>
std::array<uint16_t, N> eeprom_block(uint16_t modbus_address) {
  assert(modbus_address >= fixture::EEPROM_MODBUS_START);
  const size_t offset = modbus_address - fixture::EEPROM_MODBUS_START;
  assert(offset + N <= fixture::EEPROM_WORDS.size());
  std::array<uint16_t, N> words{};
  for (size_t index = 0; index < N; ++index) words[index] = fixture::EEPROM_WORDS[offset + index];
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

void check_fixture_integrity() {
  std::array<uint8_t, 512> low_bytes{};
  for (size_t index = 0; index < fixture::EEPROM_WORDS.size(); ++index) {
    assert(fixture::EEPROM_WORDS[index] <= 255U);
    low_bytes[index] = static_cast<uint8_t>(fixture::EEPROM_WORDS[index]);
  }
  const uint16_t stored_crc = static_cast<uint16_t>(low_bytes[510] | (low_bytes[511] << 8U));
  assert(stored_crc == fixture::EEPROM_CRC);
  assert(oq_modbus_recovery::crc16_modbus(low_bytes.data(), 510U) == fixture::EEPROM_CRC);
  assert(oq_modbus_recovery::crc16_modbus(low_bytes.data(), low_bytes.size()) == 0U);
}

oq_odu::Detection check_identity() {
  const auto core_bytes = encode_words(fixture::CORE_IDENTITY_WORDS);
  const auto customer_bytes = encode_words(fixture::CUSTOMER_MODEL_WORDS);
  const auto core = oq_odu::parse_core_identity_response(core_bytes.data(), core_bytes.size());
  const auto customer = oq_odu::parse_customer_model_response(customer_bytes.data(), customer_bytes.size());
  assert(core.available && core.compressor_code == 2825U);
  assert(core.control_board_item == 0x0E37U && core.pcb_program == 0x0122U);
  assert(customer.available && customer.printable && !customer.missing);
  assert(std::strcmp(oq_odu::customer_model_label(customer), "AMH6") == 0);
  assert(oq_odu::requires_customer_model(core));
  const auto detection = oq_odu::detect_generation(core, customer);
  assert(detection.generation == oq_odu::Generation::V2);
  assert(detection.variant == oq_odu::Variant::V2_OLD_MODEL);
  return detection;
}

void check_frequency_tables(oq_odu::Variant variant) {
  const auto base_bytes = encode_words(eeprom_block<22>(oq_odu::BASE_FREQUENCY_TABLE_REGISTER));
  auto snapshot = oq_odu::parse_base_frequency_table_response(base_bytes.data(), base_bytes.size(), variant);
  assert(snapshot.cooling.valid && snapshot.heating.valid);
  assert(snapshot.cooling.level_count == 11U && snapshot.heating.level_count == 11U);
  constexpr std::array<uint8_t, 11> cooling = {0, 30, 36, 42, 46, 48, 52, 56, 61, 66, 71};
  constexpr std::array<uint8_t, 11> heating = {0, 20, 26, 30, 48, 55, 61, 72, 80, 85, 90};
  for (size_t level = 0; level < heating.size(); ++level) {
    assert(snapshot.cooling.hz[level] == cooling[level]);
    assert(snapshot.heating.hz[level] == heating[level]);
    const auto command = oq_odu::resolve_automatic_level(true, snapshot, 2, static_cast<int>(level));
    assert(command.control_level == static_cast<int>(level));
    assert(command.physical_level == static_cast<int>(level));
  }

  const auto original = oq_odu::encode_runtime_frequency_snapshot(snapshot);
  const auto extension_words = eeprom_block<20>(oq_odu::EXTENDED_FREQUENCY_TABLE_REGISTER);
  for (const auto word : extension_words) assert(word == 0U);
  const auto extension_bytes = encode_words(extension_words);
  const auto extension =
      oq_odu::apply_v2_extension_frequency_table_response(snapshot, extension_bytes.data(), extension_bytes.size());
  assert(!extension.response_complete && !extension.heating_valid && !extension.cooling_valid);
  assert(oq_odu::encode_runtime_frequency_snapshot(snapshot) == original);

  const auto profile = oq_odu::compressor_level_profile(snapshot);
  assert(profile == oq_odu::CompressorLevelProfile::V2_LEGACY);
  assert(std::strcmp(oq_odu::compressor_level_profile_label(profile), "V2 F0-F10") == 0);
  for (int mode = 1; mode <= 2; ++mode) {
    assert(!oq_odu::has_extended_compressor_levels(profile, mode));
    assert(!oq_odu::has_extended_frequency_table(snapshot, mode));
    assert(oq_odu::physical_level_limit(true, snapshot, mode) == 10);
    assert(oq_odu::resolve_manual_level(true, snapshot, mode, 20).physical_level == 10);
  }
}

void check_bottom_plate(oq_odu::Variant variant) {
  const auto bytes = encode_words(eeprom_block<3>(oq_odu::BOTTOM_PLATE_START_ADDRESS));
  oq_odu::BottomPlateSettings settings;
  assert(oq_odu::decode_bottom_plate_settings(bytes.data(), bytes.size(), settings));
  assert(oq_odu::valid_bottom_plate_settings(settings, variant));
  assert(settings.mode == 3U && settings.start_temperature_c == 4 && settings.stop_delta_c == 3U);
  assert(oq_odu::bottom_plate_settings_match(settings, oq_odu::default_bottom_plate_settings(variant)));
}

void check_defrost(oq_odu::Variant variant) {
  oq_defrost::Parameters parameters;
  parameters.variant = variant;
  parameters.base = eeprom_block<11>(3270U);   // P271..P281
  parameters.timing = eeprom_block<9>(3307U);  // P308..P316
  parameters.coil = eeprom_block<6>(3336U);    // P337..P342
  parameters.delta = eeprom_block<14>(3414U);  // P415..P428
  parameters.loaded = true;
  assert(parameters.automatic() && parameters.mode() == 4);
  assert(oq_defrost::is_supported_defrost_mode(parameters.mode(), variant));

  // Exercise each side of the real mode-4 ambient band boundaries.
  constexpr std::array<float, 14> ambient = {8.999f, 0,        -0.001f, -5,       -5.001f, -10,      -10.001f,
                                             -15,    -15.001f, -20,     -20.001f, -23,     -23.001f, -30};
  constexpr std::array<float, 14> delta = {12, 12, 11, 11, 10, 10, 10, 10, 9, 9, 6, 6, 5, 5};
  for (size_t index = 0; index < ambient.size(); ++index) {
    assert(oq_defrost::delta_threshold(parameters, ambient[index]) == delta[index]);
  }
  assert(std::isnan(oq_defrost::delta_threshold(parameters, 9)));

  // Check diagnostic interpretation, without predicting autonomous ODU starts.
  oq_defrost::Diagnostics diagnostics;
  diagnostics.sample(parameters, true, false, 3, -9, -9, 20, 1000);
  assert(diagnostics.delta_k == 12);
  assert(diagnostics.interval_s == 2700 && diagnostics.minimum_runtime_s == 300);
  assert(diagnostics.max_duration_s == 480 && diagnostics.confirm_required_s == 60);
  assert(diagnostics.exit_c == 17 && diagnostics.alternate_exit_c == 25);
  assert(diagnostics.exit_required_s == -1);  // Exit task-tick units remain unproven.
  assert(diagnostics.confirm_s == 0);
  diagnostics.sample(parameters, true, false, 3, -9, -9.1f, 20, 2000);
  diagnostics.sample(parameters, true, false, 3, -9, -9.1f, 20, 3000);
  assert(diagnostics.confirm_s == 1);
  diagnostics.sample(parameters, true, false, -25, -9, -40, 20, 4000);
  assert(diagnostics.exit_c == 17);
  diagnostics.sample(parameters, true, false, -25.001f, -9, -40, 20, 5000);
  assert(diagnostics.exit_c == 25);
  diagnostics.sample(parameters, false, false, 3, -9, -10, 20, 6000);
  assert(std::isnan(diagnostics.delta_k) && diagnostics.confirm_s == -1);
}

}  // namespace

int main() {
  check_fixture_integrity();
  const auto detection = check_identity();
  check_frequency_tables(detection.variant);
  check_bottom_plate(detection.variant);
  check_defrost(detection.variant);
  return 0;
}
