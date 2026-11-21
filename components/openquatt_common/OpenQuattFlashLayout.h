#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::openquatt_common {

struct OpenQuattFlashLayout {
  static constexpr size_t SECTOR_SIZE = 4096;

  static constexpr uint32_t TRENDS_OFFSET = 0;
  static constexpr size_t TRENDS_SECTOR_COUNT = 90;
  static constexpr uint32_t TRENDS_END_OFFSET = TRENDS_OFFSET + (TRENDS_SECTOR_COUNT * SECTOR_SIZE);

  static constexpr uint32_t ENERGY_DAILY_OFFSET = TRENDS_END_OFFSET;
  static constexpr size_t ENERGY_DAILY_MAX_SECTOR_COUNT = 256;
  static constexpr uint32_t ENERGY_HOURLY_OFFSET = ENERGY_DAILY_OFFSET + (ENERGY_DAILY_MAX_SECTOR_COUNT * SECTOR_SIZE);
  static constexpr size_t ENERGY_HOURLY_SLOT_SIZE = 1024;
  static constexpr uint16_t ENERGY_HOURLY_MAX_RETENTION_DAYS = 365;
  static constexpr size_t ENERGY_HOURLY_SLOTS_PER_SECTOR = SECTOR_SIZE / ENERGY_HOURLY_SLOT_SIZE;
  static constexpr size_t ENERGY_HOURLY_MAX_SECTOR_COUNT =
      (ENERGY_HOURLY_MAX_RETENTION_DAYS + ENERGY_HOURLY_SLOTS_PER_SECTOR - 1U) / ENERGY_HOURLY_SLOTS_PER_SECTOR;
  static constexpr uint32_t ENERGY_END_OFFSET = ENERGY_HOURLY_OFFSET + (ENERGY_HOURLY_MAX_SECTOR_COUNT * SECTOR_SIZE);

  static constexpr uint32_t DECISION_LOG_OFFSET = ENERGY_END_OFFSET;
  static constexpr size_t DECISION_LOG_SECTOR_COUNT = 32;
  static constexpr uint32_t DECISION_LOG_END_OFFSET = DECISION_LOG_OFFSET + (DECISION_LOG_SECTOR_COUNT * SECTOR_SIZE);

  static constexpr uint32_t CRASH_TELEMETRY_OFFSET = DECISION_LOG_END_OFFSET;
  static constexpr size_t CRASH_TELEMETRY_SECTOR_COUNT = 2;
  static constexpr uint32_t CRASH_TELEMETRY_END_OFFSET =
      CRASH_TELEMETRY_OFFSET + (CRASH_TELEMETRY_SECTOR_COUNT * SECTOR_SIZE);

  // Two 8 KiB slots; existing archive offsets and the partition table stay stable.
  static constexpr uint32_t HOUSE_LEARNING_OFFSET = CRASH_TELEMETRY_END_OFFSET;
  static constexpr size_t HOUSE_LEARNING_SLOT_SIZE = 2U * SECTOR_SIZE;
  static constexpr size_t HOUSE_LEARNING_SLOT_COUNT = 2U;
  static constexpr uint32_t HOUSE_LEARNING_END_OFFSET =
      HOUSE_LEARNING_OFFSET + HOUSE_LEARNING_SLOT_COUNT * HOUSE_LEARNING_SLOT_SIZE;

  // Ring of three sectors for the settings change log; one 4 KiB sector stays unassigned.
  static constexpr uint32_t CHANGE_LOG_OFFSET = HOUSE_LEARNING_END_OFFSET;
  static constexpr size_t CHANGE_LOG_SECTOR_COUNT = 3U;
  static constexpr uint32_t CHANGE_LOG_END_OFFSET = CHANGE_LOG_OFFSET + (CHANGE_LOG_SECTOR_COUNT * SECTOR_SIZE);
  // Optional measurement archive: context registry and separate data rings.
  static constexpr uint32_t MEASUREMENT_ARCHIVE_OFFSET = 0x1E0000;
  static constexpr size_t MEASUREMENT_ARCHIVE_SECTOR_COUNT = 16U + 359U + 207U + 87U;
  static constexpr uint32_t MEASUREMENT_ARCHIVE_END_OFFSET =
      MEASUREMENT_ARCHIVE_OFFSET + MEASUREMENT_ARCHIVE_SECTOR_COUNT * SECTOR_SIZE;
};

static_assert(OpenQuattFlashLayout::CHANGE_LOG_END_OFFSET <= OpenQuattFlashLayout::MEASUREMENT_ARCHIVE_OFFSET,
              "Measurement archive must start after the existing persistent archives");
static_assert(OpenQuattFlashLayout::MEASUREMENT_ARCHIVE_END_OFFSET <= 0x5E0000,
              "Measurement archive requires the 16 MiB Q data partition");

static_assert(OpenQuattFlashLayout::DECISION_LOG_OFFSET % OpenQuattFlashLayout::SECTOR_SIZE == 0,
              "Persistent archives must start on erase-sector boundaries");
static_assert(OpenQuattFlashLayout::CRASH_TELEMETRY_OFFSET % OpenQuattFlashLayout::SECTOR_SIZE == 0,
              "Crash telemetry storage must start on an erase-sector boundary");
static_assert(OpenQuattFlashLayout::CRASH_TELEMETRY_END_OFFSET <= 0x1E0000,
              "Persistent archives must fit in the smallest openquatt_data partition");
static_assert(OpenQuattFlashLayout::HOUSE_LEARNING_OFFSET % OpenQuattFlashLayout::SECTOR_SIZE == 0 &&
                  OpenQuattFlashLayout::HOUSE_LEARNING_SLOT_SIZE % OpenQuattFlashLayout::SECTOR_SIZE == 0,
              "Passive learning journal slots must use complete erase sectors");
static_assert(OpenQuattFlashLayout::HOUSE_LEARNING_END_OFFSET <= 0x1E0000,
              "Passive learning journal must fit without moving existing archives");
static_assert(OpenQuattFlashLayout::CHANGE_LOG_OFFSET % OpenQuattFlashLayout::SECTOR_SIZE == 0,
              "Change log storage must start on an erase-sector boundary");
static_assert(OpenQuattFlashLayout::CHANGE_LOG_END_OFFSET <= 0x1E0000,
              "Change log storage must fit without moving existing archives");

}  // namespace esphome::openquatt_common
