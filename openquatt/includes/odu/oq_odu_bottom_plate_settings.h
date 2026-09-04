#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "oq_odu_generation.h"

namespace oq_odu {

inline constexpr uint16_t BOTTOM_PLATE_START_ADDRESS = 3236U;
inline constexpr uint16_t BOTTOM_PLATE_REGISTER_COUNT = 3U;
inline constexpr int8_t BOTTOM_PLATE_MIN_START_TEMPERATURE_C = -30;
inline constexpr int8_t BOTTOM_PLATE_MAX_START_TEMPERATURE_C = 30;
inline constexpr uint8_t BOTTOM_PLATE_MAX_STOP_DELTA_C = 30U;

struct BottomPlateSettings {
  uint8_t mode{0U};
  int8_t start_temperature_c{0};
  uint8_t stop_delta_c{0U};
};

struct BottomPlateWriteTarget {
  uint16_t address{0U};
  uint16_t value{0U};
};

struct BottomPlateProfileStorage {
  uint32_t magic{0U};
  uint8_t version{0U};
  uint8_t flags{0U};
  uint8_t variant{0U};
  uint8_t mode{0U};
  uint16_t control_board_item{0U};
  int8_t start_temperature_c{0};
  uint8_t stop_delta_c{0U};
  uint32_t checksum{0U};
};

static_assert(sizeof(BottomPlateProfileStorage) == 16U, "Bottom-plate preference budget changed");

inline constexpr uint32_t BOTTOM_PLATE_PROFILE_MAGIC = 0x4F514250U;
inline constexpr uint8_t BOTTOM_PLATE_PROFILE_VERSION = 1U;
inline constexpr uint8_t BOTTOM_PLATE_PROFILE_AUTO_REAPPLY = 0x01U;

inline bool valid_bottom_plate_settings(const BottomPlateSettings& settings) {
  return settings.mode <= 3U && settings.start_temperature_c >= BOTTOM_PLATE_MIN_START_TEMPERATURE_C &&
         settings.start_temperature_c <= BOTTOM_PLATE_MAX_START_TEMPERATURE_C &&
         settings.stop_delta_c <= BOTTOM_PLATE_MAX_STOP_DELTA_C;
}

inline constexpr uint16_t encode_bottom_plate_start_temperature(int8_t temperature_c) {
  return static_cast<uint16_t>(static_cast<int16_t>(temperature_c) + 30);
}

inline bool decode_bottom_plate_settings(const uint8_t* data, size_t size, BottomPlateSettings& settings) {
  if (data == nullptr || size < BOTTOM_PLATE_REGISTER_COUNT * 2U) return false;
  const uint16_t mode = read_word(data, 0U);
  const uint16_t start_raw = read_word(data, 1U);
  const uint16_t stop_delta = read_word(data, 2U);
  if (mode > 3U || start_raw > 60U || stop_delta > BOTTOM_PLATE_MAX_STOP_DELTA_C) return false;
  settings = {static_cast<uint8_t>(mode), static_cast<int8_t>(static_cast<int16_t>(start_raw) - 30),
              static_cast<uint8_t>(stop_delta)};
  return valid_bottom_plate_settings(settings);
}

inline bool bottom_plate_settings_match(const BottomPlateSettings& lhs, const BottomPlateSettings& rhs) {
  return lhs.mode == rhs.mode && lhs.start_temperature_c == rhs.start_temperature_c &&
         lhs.stop_delta_c == rhs.stop_delta_c;
}

inline BottomPlateSettings default_bottom_plate_settings(Variant variant) {
  return {static_cast<uint8_t>(variant == Variant::V1 ? 1U : 3U), 4, 3U};
}

inline constexpr std::array<BottomPlateWriteTarget, BOTTOM_PLATE_REGISTER_COUNT> bottom_plate_write_targets(
    const BottomPlateSettings& settings) {
  if (settings.mode == 0U) {
    // Disable first so an interrupted sequence cannot leave heating active
    // while only part of the new thresholds has been written.
    return {{{BOTTOM_PLATE_START_ADDRESS, settings.mode},
             {BOTTOM_PLATE_START_ADDRESS + 1U, encode_bottom_plate_start_temperature(settings.start_temperature_c)},
             {BOTTOM_PLATE_START_ADDRESS + 2U, settings.stop_delta_c}}};
  }
  // Thresholds are written first. Mode is committed last so a partially
  // completed sequence cannot activate a new mode with stale thresholds.
  return {{{BOTTOM_PLATE_START_ADDRESS + 1U, encode_bottom_plate_start_temperature(settings.start_temperature_c)},
           {BOTTOM_PLATE_START_ADDRESS + 2U, settings.stop_delta_c},
           {BOTTOM_PLATE_START_ADDRESS, settings.mode}}};
}

inline uint32_t bottom_plate_profile_checksum(const BottomPlateProfileStorage& storage) {
  uint32_t hash = 2166136261U;
  const auto add_byte = [&hash](uint8_t value) {
    hash ^= value;
    hash *= 16777619U;
  };
  for (size_t shift = 0U; shift < 32U; shift += 8U) add_byte(static_cast<uint8_t>(storage.magic >> shift));
  add_byte(storage.version);
  add_byte(storage.flags);
  add_byte(storage.variant);
  add_byte(storage.mode);
  add_byte(static_cast<uint8_t>(storage.control_board_item));
  add_byte(static_cast<uint8_t>(storage.control_board_item >> 8U));
  add_byte(static_cast<uint8_t>(storage.start_temperature_c));
  add_byte(storage.stop_delta_c);
  return hash;
}

inline BottomPlateProfileStorage make_bottom_plate_profile(const BottomPlateSettings& settings, Variant variant,
                                                           uint16_t control_board_item, bool auto_reapply) {
  BottomPlateProfileStorage storage{BOTTOM_PLATE_PROFILE_MAGIC,
                                    BOTTOM_PLATE_PROFILE_VERSION,
                                    static_cast<uint8_t>(auto_reapply ? BOTTOM_PLATE_PROFILE_AUTO_REAPPLY : 0U),
                                    static_cast<uint8_t>(variant),
                                    settings.mode,
                                    control_board_item,
                                    settings.start_temperature_c,
                                    settings.stop_delta_c,
                                    0U};
  storage.checksum = bottom_plate_profile_checksum(storage);
  return storage;
}

inline bool valid_bottom_plate_profile(const BottomPlateProfileStorage& storage) {
  const auto variant = static_cast<Variant>(storage.variant);
  const BottomPlateSettings settings{storage.mode, storage.start_temperature_c, storage.stop_delta_c};
  return storage.magic == BOTTOM_PLATE_PROFILE_MAGIC && storage.version == BOTTOM_PLATE_PROFILE_VERSION &&
         (storage.flags & static_cast<uint8_t>(~BOTTOM_PLATE_PROFILE_AUTO_REAPPLY)) == 0U && variant >= Variant::V1 &&
         variant <= Variant::V2_NEW_MODEL && storage.control_board_item != 0U &&
         valid_bottom_plate_settings(settings) && storage.checksum == bottom_plate_profile_checksum(storage);
}

inline bool bottom_plate_profile_matches_identity(const BottomPlateProfileStorage& storage, uint16_t control_board_item,
                                                  Variant variant) {
  return valid_bottom_plate_profile(storage) && storage.control_board_item == control_board_item &&
         storage.variant == static_cast<uint8_t>(variant);
}

}  // namespace oq_odu
