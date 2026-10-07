#pragma once

#include <stdint.h>
#include <string.h>

#include "../control/oq_supply_calibration_logic.h"

namespace oq_supply_calibration_migration {

// Restoring globals: 1944399030U XOR the first 32 bits of MD5(id).
static constexpr uint32_t kLegacySourceKey = 2609287369U;
static constexpr uint32_t kLegacyFingerprintKey = 3358605580U;
static constexpr uint32_t kLegacyChecksumKey = 909863605U;

inline uint32_t record_key(int32_t source_code) {
  switch (source_code) {
    case oq_supply_calibration::SOURCE_LOCAL_PT1000:
      return 291797858U;
    case oq_supply_calibration::SOURCE_LOCAL_DS18B20:
      return 3766055637U;
    case oq_supply_calibration::SOURCE_CIC:
      return 3464536190U;
    case oq_supply_calibration::SOURCE_HA_INPUT:
      return 3192510929U;
    default:
      return 0U;
  }
}

enum class ReadResult : uint8_t { FOUND, ABSENT, UNEXPECTED, ERROR };
enum class Result : uint8_t { COMPLETE, KEPT, RETRY };

// Storage implements bounded, exact-size raw ESPHome NVS blobs. A missing key
// differs from a read failure: only actual absence permits torn-cleanup recovery.
template <typename Storage>
inline Result migrate(Storage& storage, uint32_t (&target)[oq_supply_calibration::kRecordStorageWords],
                      uint32_t source_bits, ReadResult fingerprint_result, uint32_t fingerprint,
                      ReadResult checksum_result, uint32_t checksum) {
  using namespace oq_supply_calibration;
  const int32_t code = static_cast<int32_t>(source_bits);
  if (fingerprint_result == ReadResult::ERROR || checksum_result == ReadResult::ERROR) return Result::RETRY;
  if (fingerprint_result == ReadResult::UNEXPECTED || checksum_result == ReadResult::UNEXPECTED) return Result::KEPT;

  if (code == SOURCE_NONE) {
    // Old firmware also persisted its three initial zero values. There is no
    // calibration to migrate; keep nonzero or malformed records fail-closed.
    if ((fingerprint_result == ReadResult::FOUND && fingerprint != 0U) ||
        (checksum_result == ReadResult::FOUND && checksum != 0U)) {
      return Result::KEPT;
    }
  } else {
    if (!source_supported(code)) return Result::KEPT;
    // Live RAM is authoritative, including a newer user calibration awaiting
    // persistence. Never load a stale durable target over that live value.
    if (!record_valid(load_record(target), code)) {
      uint32_t existing[kRecordStorageWords]{};
      const ReadResult existing_result = storage.read_record(record_key(code), existing);
      if (existing_result == ReadResult::ERROR) return Result::RETRY;
      // Do not seed RAM (whose restoring-global writer would then save it) over
      // an unexpected type/size at this key. That could be a hash collision.
      if (existing_result == ReadResult::UNEXPECTED) return Result::KEPT;
      if (existing_result == ReadResult::FOUND && record_valid(load_record(existing), code)) {
        // RestoringGlobals::setup may have suffered a transient read failure.
        // The valid durable per-source record still supersedes the old format.
        memcpy(target, existing, sizeof(existing));
      } else {
        if (fingerprint_result != ReadResult::FOUND || checksum_result != ReadResult::FOUND) return Result::KEPT;
        float offset = 0.0f;
        const ReadResult offset_result = storage.read_offset(offset);
        if (offset_result == ReadResult::ERROR) return Result::RETRY;
        if (offset_result != ReadResult::FOUND || !migrate_legacy_record(target, code, fingerprint, checksum, offset)) {
          return Result::KEPT;
        }
      }
    }
    uint32_t expected[kRecordStorageWords];
    memcpy(expected, target, sizeof(expected));
    // Drain queued ESPHome writes first, then inspect the durable target blob.
    // Updating RAM alone or an ESPHome load (which can see pending writes) is
    // never sufficient proof that the new record survived to flash.
    if (!storage.sync()) return Result::RETRY;
    uint32_t durable[kRecordStorageWords]{};
    ReadResult durable_result = storage.read_record(record_key(code), durable);
    if (durable_result == ReadResult::ERROR) return Result::RETRY;
    if (durable_result == ReadResult::UNEXPECTED) return Result::KEPT;
    // A full store must still allow cleanup if the valid new record is already
    // durable. Rewriting it would unnecessarily require replacement entries.
    if (durable_result != ReadResult::FOUND || memcmp(durable, expected, sizeof(durable)) != 0) {
      if (!storage.write_record(record_key(code), expected)) return Result::RETRY;
      durable_result = storage.read_record(record_key(code), durable);
    }
    if (durable_result != ReadResult::FOUND || memcmp(durable, expected, sizeof(durable)) != 0 ||
        !record_valid(load_record(durable), code)) {
      return Result::RETRY;
    }
  }

  // Erase the source code LAST. A restart halfway through cleanup can still
  // identify and verify the durable target even if the other old words vanished.
  if (!storage.erase_word(kLegacyFingerprintKey, fingerprint_result, fingerprint) ||
      !storage.erase_word(kLegacyChecksumKey, checksum_result, checksum) ||
      !storage.erase_word(kLegacySourceKey, ReadResult::FOUND, source_bits)) {
    return Result::RETRY;
  }
  return Result::COMPLETE;
}

}  // namespace oq_supply_calibration_migration
