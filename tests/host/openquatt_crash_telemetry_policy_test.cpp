#include <cassert>

#include "components/openquatt_crash_telemetry/OpenQuattCrashTelemetryPolicy.h"

using esphome::openquatt_crash_telemetry::crash_data_may_be_published;
using esphome::openquatt_crash_telemetry::crash_publication_is_retained;
using esphome::openquatt_crash_telemetry::CrashPublishKind;
using esphome::openquatt_crash_telemetry::flash_sequence_is_newer;
using esphome::openquatt_crash_telemetry::persisted_consent_blocks_crash;
using esphome::openquatt_crash_telemetry::reported_at_is_usable;
using esphome::openquatt_crash_telemetry::select_crash_publish_kind;
using esphome::openquatt_crash_telemetry::should_request_tombstone;
using esphome::openquatt_crash_telemetry::should_wait_for_time_sync;

int main() {
  assert(select_crash_publish_kind(true, false, false, false) == CrashPublishKind::TOMBSTONE);
  assert(select_crash_publish_kind(false, true, true, true) == CrashPublishKind::CRASH);
  assert(select_crash_publish_kind(false, true, false, true) == CrashPublishKind::NONE);
  assert(select_crash_publish_kind(false, false, true, true) == CrashPublishKind::NONE);
  assert(select_crash_publish_kind(false, true, true, false) == CrashPublishKind::NONE);

  assert(should_request_tombstone(true, true, false, true));
  assert(!should_request_tombstone(false, true, false, true));
  assert(!should_request_tombstone(true, false, false, true));
  assert(!should_request_tombstone(true, true, true, true));
  assert(!should_request_tombstone(true, true, false, false));

  assert(persisted_consent_blocks_crash(true, false));
  assert(!persisted_consent_blocks_crash(true, true));
  assert(!persisted_consent_blocks_crash(false, false));
  assert(!persisted_consent_blocks_crash(false, true));

  assert(flash_sequence_is_newer(2U, 1U));
  assert(!flash_sequence_is_newer(1U, 2U));
  assert(!flash_sequence_is_newer(7U, 7U));
  assert(flash_sequence_is_newer(1U, UINT32_MAX));
  assert(!flash_sequence_is_newer(UINT32_MAX, 1U));

  assert(crash_data_may_be_published(CrashPublishKind::CRASH, true, true));
  assert(!crash_data_may_be_published(CrashPublishKind::CRASH, false, true));
  assert(!crash_data_may_be_published(CrashPublishKind::CRASH, true, false));
  assert(crash_data_may_be_published(CrashPublishKind::TOMBSTONE, false, false));
  assert(!crash_data_may_be_published(CrashPublishKind::NONE, true, true));

  assert(!crash_publication_is_retained(CrashPublishKind::CRASH));
  assert(crash_publication_is_retained(CrashPublishKind::TOMBSTONE));
  assert(!crash_publication_is_retained(CrashPublishKind::NONE));

  assert(should_wait_for_time_sync(CrashPublishKind::CRASH, false, 100U, 200U));
  assert(!should_wait_for_time_sync(CrashPublishKind::CRASH, true, 100U, 200U));
  assert(!should_wait_for_time_sync(CrashPublishKind::TOMBSTONE, false, 100U, 200U));
  assert(!should_wait_for_time_sync(CrashPublishKind::CRASH, false, 200U, 200U));
  assert(!should_wait_for_time_sync(CrashPublishKind::CRASH, false, 201U, 200U));
  assert(should_wait_for_time_sync(CrashPublishKind::CRASH, false, UINT32_MAX - 10U, 20U));

  assert(reported_at_is_usable(true, true, false, 200U, 0U));
  assert(reported_at_is_usable(true, true, true, 200U, 200U));
  assert(reported_at_is_usable(true, true, true, 201U, 200U));
  assert(!reported_at_is_usable(false, true, true, 201U, 200U));
  assert(!reported_at_is_usable(true, false, true, 201U, 200U));
  assert(!reported_at_is_usable(true, true, true, 199U, 200U));

  return 0;
}
