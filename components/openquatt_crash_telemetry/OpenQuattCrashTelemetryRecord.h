#pragma once

#include <cstddef>
#include <cstdint>

#include "../openquatt_log_history/OpenQuattCrashTimeBreadcrumb.h"

namespace esphome::openquatt_crash_telemetry::detail {

static constexpr uint32_t CRASH_RECORD_MAGIC = 0x4F514352UL;  // OQCR
static constexpr uint16_t CRASH_RECORD_VERSION = 1U;
static constexpr size_t LEGACY_CRASH_REPORT_CAPACITY = 2048U;
static constexpr size_t CRASH_REPORT_CAPACITY = 2040U;

struct CrashRecord {
  uint32_t magic;
  uint16_t version;
  uint8_t pending;
  uint8_t truncated;
  uint8_t captured_by_reporting_build;
  uint8_t crash_time_valid;
  uint8_t reserved[2];
  uint16_t report_length;
  uint16_t reserved2;
  uint32_t sequence;
  uint32_t build_epoch;
  uint32_t config_hash;
  uint32_t reset_reason;
  char crash_id[37];
  char build_id[65];
  char source_repository[98];
  char source_commit[41];
  char build_target[97];
  char release_manifest_url[257];
  char firmware_version[33];
  char release_channel[17];
  char esphome_version[17];
  char hardware_profile[33];
  char topology[17];
  char connection[17];
  char report[CRASH_REPORT_CAPACITY];
  uint8_t reserved_report_tail[3];
  uint32_t crash_timestamp;
  uint32_t crash_uptime_s;
  uint32_t checksum;
};

static_assert(offsetof(CrashRecord, checksum) ==
                  ((offsetof(CrashRecord, report) + LEGACY_CRASH_REPORT_CAPACITY + alignof(uint32_t) - 1U) &
                   ~(alignof(uint32_t) - 1U)),
              "Crash timestamp fields must preserve the original checksum and flash layout");
static_assert(sizeof(CrashRecord) < 3072U, "Crash record should remain a small bounded blob");

inline uint32_t crash_record_checksum(const void* data, size_t length) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  uint32_t hash = 2166136261UL;
  for (size_t index = 0U; index < length; ++index) {
    hash ^= bytes[index];
    hash *= 16777619UL;
  }
  return hash;
}

inline bool valid_stored_crash_record(const CrashRecord& record) {
  if (record.magic != CRASH_RECORD_MAGIC || record.version != CRASH_RECORD_VERSION || record.pending > 1U ||
      record.truncated > 1U || record.captured_by_reporting_build > 1U || record.crash_time_valid > 1U ||
      record.sequence == 0U) {
    return false;
  }
  const size_t report_capacity = record.crash_time_valid != 0U ? CRASH_REPORT_CAPACITY : LEGACY_CRASH_REPORT_CAPACITY;
  if (record.report_length >= report_capacity) return false;
  const auto* record_bytes = reinterpret_cast<const uint8_t*>(&record);
  if (record_bytes[offsetof(CrashRecord, report) + record.report_length] != '\0' ||
      record.checksum != crash_record_checksum(&record, offsetof(CrashRecord, checksum))) {
    return false;
  }
  if (record.crash_time_valid != 0U &&
      (record.reserved_report_tail[0] != 0U || record.reserved_report_tail[1] != 0U ||
       record.reserved_report_tail[2] != 0U || !openquatt_log_history::crash_epoch_is_sane(record.crash_timestamp))) {
    return false;
  }
  return true;
}

inline bool migrate_crash_record(CrashRecord* record) {
  if (record == nullptr || !valid_stored_crash_record(*record)) return false;
  if (record->crash_time_valid != 0U) return true;
  if (record->report_length >= CRASH_REPORT_CAPACITY) {
    record->report_length = CRASH_REPORT_CAPACITY - 1U;
    record->report[record->report_length] = '\0';
    record->truncated = 1U;
  }
  record->crash_time_valid = 0U;
  record->reserved_report_tail[0] = 0U;
  record->reserved_report_tail[1] = 0U;
  record->reserved_report_tail[2] = 0U;
  record->crash_timestamp = 0U;
  record->crash_uptime_s = 0U;
  record->checksum = 0U;
  record->checksum = crash_record_checksum(record, offsetof(CrashRecord, checksum));
  return true;
}

}  // namespace esphome::openquatt_crash_telemetry::detail
