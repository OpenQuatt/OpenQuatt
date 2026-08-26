#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "components/openquatt_crash_telemetry/OpenQuattCrashTelemetryRecord.h"
#include "components/openquatt_log_history/OpenQuattCrashTimeBreadcrumb.h"

using esphome::openquatt_crash_telemetry::detail::crash_record_checksum;
using esphome::openquatt_crash_telemetry::detail::CRASH_RECORD_MAGIC;
using esphome::openquatt_crash_telemetry::detail::CRASH_RECORD_VERSION;
using esphome::openquatt_crash_telemetry::detail::CRASH_REPORT_CAPACITY;
using esphome::openquatt_crash_telemetry::detail::CrashRecord;
using esphome::openquatt_crash_telemetry::detail::LEGACY_CRASH_REPORT_CAPACITY;
using esphome::openquatt_crash_telemetry::detail::migrate_crash_record;
using esphome::openquatt_crash_telemetry::detail::valid_stored_crash_record;
using esphome::openquatt_log_history::consume_crash_time_breadcrumb_state;
using esphome::openquatt_log_history::crash_time_breadcrumb_checksum;
using esphome::openquatt_log_history::crash_time_breadcrumb_is_valid;
using esphome::openquatt_log_history::CRASH_TIME_BREADCRUMB_MAGIC;
using esphome::openquatt_log_history::CRASH_TIME_BREADCRUMB_VERSION;
using esphome::openquatt_log_history::crash_uptime_seconds_from_microseconds;
using esphome::openquatt_log_history::CrashTimeBreadcrumb;
using esphome::openquatt_log_history::CrashTimeBreadcrumbBootCache;
using esphome::openquatt_log_history::CrashTimeBreadcrumbSnapshot;
using esphome::openquatt_log_history::MAX_VALID_CRASH_EPOCH_S;
using esphome::openquatt_log_history::MIN_VALID_CRASH_EPOCH_S;

namespace {

struct LegacyCrashRecord {
  uint32_t magic;
  uint16_t version;
  uint8_t pending;
  uint8_t truncated;
  uint8_t captured_by_reporting_build;
  uint8_t reserved[3];
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
  char report[LEGACY_CRASH_REPORT_CAPACITY];
  uint32_t checksum;
};

static_assert(sizeof(LegacyCrashRecord) == sizeof(CrashRecord));
static_assert(offsetof(LegacyCrashRecord, report) == offsetof(CrashRecord, report));
static_assert(offsetof(LegacyCrashRecord, checksum) == offsetof(CrashRecord, checksum));

void finalize_record(CrashRecord* record) {
  record->checksum = 0U;
  record->checksum = crash_record_checksum(record, offsetof(CrashRecord, checksum));
}

CrashTimeBreadcrumb make_breadcrumb(uint32_t epoch_s) {
  CrashTimeBreadcrumb breadcrumb{};
  breadcrumb.magic = CRASH_TIME_BREADCRUMB_MAGIC;
  breadcrumb.version = CRASH_TIME_BREADCRUMB_VERSION;
  breadcrumb.epoch_s = epoch_s;
  breadcrumb.uptime_s = 123U;
  breadcrumb.sequence = 7U;
  breadcrumb.crc = crash_time_breadcrumb_checksum(breadcrumb);
  return breadcrumb;
}

}  // namespace

int main() {
  CrashTimeBreadcrumb breadcrumb = make_breadcrumb(MIN_VALID_CRASH_EPOCH_S);
  assert(crash_time_breadcrumb_is_valid(breadcrumb));
  breadcrumb.uptime_s++;
  assert(!crash_time_breadcrumb_is_valid(breadcrumb));
  assert(!crash_time_breadcrumb_is_valid(make_breadcrumb(MIN_VALID_CRASH_EPOCH_S - 1U)));
  assert(!crash_time_breadcrumb_is_valid(make_breadcrumb(MAX_VALID_CRASH_EPOCH_S)));
  assert(crash_uptime_seconds_from_microseconds(4294967296000ULL) == 4294967U);

  CrashTimeBreadcrumb retained = make_breadcrumb(MIN_VALID_CRASH_EPOCH_S);
  CrashTimeBreadcrumbBootCache boot_cache{};
  CrashTimeBreadcrumbSnapshot first_consumer{};
  CrashTimeBreadcrumbSnapshot second_consumer{};
  assert(consume_crash_time_breadcrumb_state(&retained, &boot_cache, &first_consumer));
  assert(retained.magic == 0U);
  assert(consume_crash_time_breadcrumb_state(&retained, &boot_cache, &second_consumer));
  assert(std::memcmp(&first_consumer, &second_consumer, sizeof(first_consumer)) == 0);
  CrashTimeBreadcrumbBootCache next_boot_cache{};
  CrashTimeBreadcrumbSnapshot next_boot_consumer{};
  assert(!consume_crash_time_breadcrumb_state(&retained, &next_boot_cache, &next_boot_consumer));

  CrashRecord current{};
  current.magic = CRASH_RECORD_MAGIC;
  current.version = CRASH_RECORD_VERSION;
  current.pending = 1U;
  current.captured_by_reporting_build = 1U;
  current.crash_time_valid = 1U;
  current.report_length = 4U;
  current.sequence = 5U;
  current.crash_timestamp = MIN_VALID_CRASH_EPOCH_S;
  current.crash_uptime_s = 16U;
  std::memcpy(current.report, "test", 5U);
  finalize_record(&current);
  assert(valid_stored_crash_record(current));
  assert(migrate_crash_record(&current));
  assert(current.crash_timestamp == MIN_VALID_CRASH_EPOCH_S);
  LegacyCrashRecord rollback_record{};
  std::memcpy(&rollback_record, &current, sizeof(current));
  assert(rollback_record.version == CRASH_RECORD_VERSION);
  assert(rollback_record.report_length < LEGACY_CRASH_REPORT_CAPACITY);
  assert(rollback_record.report[rollback_record.report_length] == '\0');
  assert(rollback_record.checksum == crash_record_checksum(&rollback_record, offsetof(LegacyCrashRecord, checksum)));

  CrashRecord legacy{};
  legacy.magic = CRASH_RECORD_MAGIC;
  legacy.version = CRASH_RECORD_VERSION;
  legacy.pending = 1U;
  legacy.captured_by_reporting_build = 1U;
  legacy.report_length = LEGACY_CRASH_REPORT_CAPACITY - 1U;
  legacy.sequence = 9U;
  auto* legacy_bytes = reinterpret_cast<uint8_t*>(&legacy);
  std::memset(legacy_bytes + offsetof(CrashRecord, report), 'x', LEGACY_CRASH_REPORT_CAPACITY);
  legacy_bytes[offsetof(CrashRecord, report) + legacy.report_length] = '\0';
  finalize_record(&legacy);
  assert(valid_stored_crash_record(legacy));
  assert(migrate_crash_record(&legacy));
  assert(legacy.version == CRASH_RECORD_VERSION);
  assert(legacy.report_length == CRASH_REPORT_CAPACITY - 1U);
  assert(legacy.report[legacy.report_length] == '\0');
  assert(legacy.truncated == 1U);
  assert(legacy.crash_time_valid == 0U);
  assert(legacy.crash_timestamp == 0U);
  assert(legacy.crash_uptime_s == 0U);
  assert(valid_stored_crash_record(legacy));

  legacy.checksum++;
  assert(!valid_stored_crash_record(legacy));
  return 0;
}
