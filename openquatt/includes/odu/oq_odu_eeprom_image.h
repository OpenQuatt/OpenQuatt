#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "oq_odu_bottom_plate_settings.h"
#include "../control/oq_defrost_logic.h"
#include "../experimental/oq_odu_runtime_frequency_table_logic.h"

namespace oq_eeprom {

inline constexpr size_t IMAGE_SIZE = 512U;
inline constexpr uint16_t IMAGE_ADDRESS = 2999U;
inline constexpr uint8_t BOTTOM = 1U, DEFROST = 2U, FREQUENCY = 4U, ALL = 7U;
using Image = std::array<uint8_t, IMAGE_SIZE>;
enum class State : uint8_t { WAIT_BASELINE = 1, BASELINE, PREPARED, WRITING, UNCERTAIN, WAIT_VERIFY, CANCELLED };

struct Identity {
  uint16_t board{0U}, program{0U};
  uint8_t variant{0U}, hp{0U};
  std::array<uint16_t, 20> serial{};
};

struct Record {
  uint32_t magic{0U}, generation{0U};
  uint16_t version{1U}, bytes{0U};
  State state{State::WAIT_BASELINE};
  uint8_t groups{0U}, restoring{0U}, verified{0U};
  Identity identity{};
  Image original{}, baseline{}, source{}, candidate{};
  uint32_t checksum{0U};
};
static_assert(sizeof(Record) < 4096U, "EEPROM journal must fit one flash sector");

inline uint16_t crc(const Image& image) {
  uint16_t value = 0xFFFFU;
  for (size_t index = 0U; index < IMAGE_SIZE - 2U; ++index) {
    value ^= image[index];
    for (uint8_t bit = 0U; bit < 8U; ++bit)
      value = (value & 1U) != 0U ? static_cast<uint16_t>((value >> 1U) ^ 0xA001U) : static_cast<uint16_t>(value >> 1U);
  }
  return value;
}
inline uint16_t stored_crc(const Image& image) { return image[510] | (static_cast<uint16_t>(image[511]) << 8U); }
inline bool valid_crc(const Image& image) { return crc(image) == stored_crc(image); }
inline void finalize_image(Image& image) {
  const uint16_t value = crc(image);
  image[510] = static_cast<uint8_t>(value);
  image[511] = static_cast<uint8_t>(value >> 8U);
}
inline uint32_t checksum(const Record& record) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t value = 2166136261U;
  for (size_t index = 0U; index < offsetof(Record, checksum); ++index) value = (value ^ bytes[index]) * 16777619U;
  return value;
}
inline void finalize_record(Record& record) {
  record.magic = 0x4F514545U;
  record.version = 1U;
  record.bytes = sizeof(record);
  record.checksum = checksum(record);
}
inline bool legacy_identity(oq_odu::Variant variant) {
  return variant == oq_odu::Variant::V1 || variant == oq_odu::Variant::V1_5;
}
inline bool supported_firmware(const Identity& identity) {
  if (identity.hp < 1U || identity.hp > 2U) return false;
  switch (static_cast<oq_odu::Variant>(identity.variant)) {
    case oq_odu::Variant::V1:
      return identity.board == oq_odu::CONTROL_BOARD_ITEM_V1 && identity.program == 0x0119U;
    case oq_odu::Variant::V1_5:
      return identity.board == oq_odu::CONTROL_BOARD_ITEM_V1_5_OR_V2_OLD_MODEL && identity.program == 0x011EU;
    case oq_odu::Variant::V2_OLD_MODEL:
      return identity.board == oq_odu::CONTROL_BOARD_ITEM_V1_5_OR_V2_OLD_MODEL && identity.program == 0x0122U;
    case oq_odu::Variant::V2_NEW_MODEL:
      return identity.board == oq_odu::CONTROL_BOARD_ITEM_V2_NEW_MODEL && identity.program == 0x0201U;
    default:
      return false;
  }
}
inline bool serial_bound(const Identity& identity) {
  for (const auto word : identity.serial)
    if (word != 0U && word != 0xFFFFU) return true;
  return false;
}
inline bool valid_identity(const Identity& identity) {
  return supported_firmware(identity) &&
         (legacy_identity(static_cast<oq_odu::Variant>(identity.variant)) || serial_bound(identity));
}
inline bool same_identity(const Identity& a, const Identity& b) {
  return a.board == b.board && a.program == b.program && a.variant == b.variant && a.hp == b.hp && a.serial == b.serial;
}
inline bool valid_record(const Record& record) {
  if (record.magic != 0x4F514545U || record.version != 1U || record.bytes != sizeof(record) ||
      record.generation == 0U || record.state < State::WAIT_BASELINE || record.state > State::CANCELLED ||
      record.groups > ALL || record.restoring > 1U || record.verified > 1U || !valid_identity(record.identity) ||
      record.checksum != checksum(record))
    return false;
  if (record.state == State::WAIT_BASELINE || record.state == State::CANCELLED) return true;
  if (!valid_crc(record.original) || !valid_crc(record.baseline)) return false;
  return record.state == State::BASELINE || valid_crc(record.candidate);
}
inline uint8_t group_for_address(uint16_t address, oq_odu::Variant variant) {
  if (address >= 3236U && address <= 3238U) return BOTTOM;
  if (address == oq_defrost::MODE_REGISTER) return DEFROST;
  if (address >= 3000U && address <= 3021U) return FREQUENCY;
  if (variant == oq_odu::Variant::V2_NEW_MODEL && address >= 3050U && address <= 3069U) return FREQUENCY;
  return 0U;
}
inline bool supported_values(const Image& image, oq_odu::Variant variant) {
  const oq_odu::BottomPlateSettings bottom{image[237], static_cast<int8_t>(static_cast<int>(image[238]) - 30),
                                           image[239]};
  if (!oq_odu::valid_bottom_plate_settings(bottom, variant) ||
      !oq_defrost::is_supported_defrost_mode(image[oq_defrost::MODE_REGISTER - IMAGE_ADDRESS], variant))
    return false;
  oq_odu_runtime_frequency::RuntimeFrequencyTables tables;
  tables.level_count =
      static_cast<uint8_t>(oq_odu_runtime_frequency::normalized_level_count(variant == oq_odu::Variant::V2_NEW_MODEL));
  for (size_t index = 0U; index < oq_odu_runtime_frequency::runtime_register_count(tables.level_count); ++index) {
    const auto target = oq_odu_runtime_frequency::runtime_write_register(tables, index);
    const uint8_t value = image[target.address - IMAGE_ADDRESS];
    if (index < 11U)
      tables.cooling[index] = value;
    else if (index < 22U)
      tables.heating[index - 11U] = value;
    else if (index < 32U)
      tables.heating[index - 11U] = value;
    else
      tables.cooling[index - 21U] = value;
  }
  return oq_odu_runtime_frequency::validate_monotonic_table(tables.heating, tables.level_count) &&
         oq_odu_runtime_frequency::validate_monotonic_table(tables.cooling, tables.level_count);
}
inline bool prepare(Record& record, const Image& source, uint8_t groups, bool restoring) {
  if (!valid_record(record) || record.state == State::WAIT_BASELINE || groups > ALL || (!restoring && groups == 0U))
    return false;
  const auto variant = static_cast<oq_odu::Variant>(record.identity.variant);
  Image candidate = restoring ? record.original : record.baseline;
  for (size_t index = 0U; index < IMAGE_SIZE - 2U; ++index) {
    const uint8_t group = group_for_address(static_cast<uint16_t>(IMAGE_ADDRESS + index), variant);
    if (source[index] != record.baseline[index] && group == 0U) return false;
    if (!restoring && (groups & group) != 0U) candidate[index] = source[index];
  }
  if (!supported_values(candidate, variant)) return false;
  // The generic editor ceiling (120 Hz) is not an ODU hardware rating. Keep
  // persistent tables inside the original unit's observed frequency envelope.
  const uint16_t first_addresses[]{3001U, 3012U};
  const size_t maximum_level = variant == oq_odu::Variant::V2_NEW_MODEL ? 20U : 10U;
  const uint16_t last_addresses[]{static_cast<uint16_t>(maximum_level == 20U ? 3069U : 3010U),
                                  static_cast<uint16_t>(maximum_level == 20U ? 3059U : 3021U)};
  for (size_t mode = 0U; mode < 2U; ++mode) {
    const uint8_t minimum = record.original[first_addresses[mode] - IMAGE_ADDRESS];
    const uint8_t maximum = record.original[last_addresses[mode] - IMAGE_ADDRESS];
    for (size_t index = 1U; index <= maximum_level; ++index) {
      const uint16_t address = index <= 10U ? static_cast<uint16_t>((mode == 0U ? 3000U : 3011U) + index)
                                            : static_cast<uint16_t>((mode == 0U ? 3060U : 3050U) + index - 11U);
      const uint8_t frequency = candidate[address - IMAGE_ADDRESS];
      if (frequency < minimum || frequency > maximum) return false;
    }
  }
  finalize_image(candidate);
  if (candidate == record.baseline) return false;
  record.source = source;
  record.candidate = candidate;
  record.groups = groups;
  record.restoring = restoring ? 1U : 0U;
  record.verified = 0U;
  record.state = State::PREPARED;
  return true;
}

}  // namespace oq_eeprom
