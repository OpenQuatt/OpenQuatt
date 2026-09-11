#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::openquatt_incident_manager::restart_handoff {

constexpr uint32_t kRecordMagic = 0x4F515248UL;  // OQRH
constexpr uint16_t kRecordVersion = 1U;
constexpr uint8_t kRecordRestartArmed = 1U;
constexpr uint8_t kRecordOtaArmed = 2U;
// Backward-compatible name used by the existing controlled-restart tests.
constexpr uint8_t kRecordArmed = kRecordRestartArmed;
constexpr uint32_t kMaximumMinimumOffMs = 3600UL * 1000UL;
constexpr size_t kImageHashSize = 32U;

struct Record {
  uint32_t magic;
  uint16_t version;
  uint8_t state;
  uint8_t reserved;
  uint32_t minimum_off_ms;
  uint32_t config_hash;
  uint32_t boot_partition_address;
  uint32_t hp1_credit_ms;
  uint32_t hp2_credit_ms;
  uint8_t image_hash[kImageHashSize];
  uint32_t checksum;
};

static_assert(sizeof(Record) == 64U, "Restart handoff NVS record size changed");

inline uint32_t checksum(const void* data, size_t length) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  uint32_t hash = 2166136261UL;
  for (size_t index = 0U; index < length; ++index) {
    hash ^= bytes[index];
    hash *= 16777619UL;
  }
  return hash;
}

inline uint32_t configuration_hash(uint32_t minimum_off_ms, bool duo_topology) {
  uint32_t hash = 2166136261UL;
  const uint8_t bytes[] = {
      static_cast<uint8_t>(minimum_off_ms),         static_cast<uint8_t>(minimum_off_ms >> 8U),
      static_cast<uint8_t>(minimum_off_ms >> 16U),  static_cast<uint8_t>(minimum_off_ms >> 24U),
      static_cast<uint8_t>(duo_topology ? 1U : 0U),
  };
  for (uint8_t byte : bytes) {
    hash ^= byte;
    hash *= 16777619UL;
  }
  return hash;
}

inline bool valid_minimum_off_ms(uint32_t minimum_off_ms) {
  return minimum_off_ms > 0U && minimum_off_ms <= kMaximumMinimumOffMs;
}

inline bool has_image_hash(const uint8_t (&image_hash)[kImageHashSize]) {
  for (uint8_t byte : image_hash)
    if (byte != 0U) return true;
  return false;
}

inline bool valid_record_state(uint8_t state) { return state == kRecordRestartArmed || state == kRecordOtaArmed; }

inline bool valid_record(const Record& record) {
  return record.magic == kRecordMagic && record.version == kRecordVersion && valid_record_state(record.state) &&
         record.reserved == 0U && valid_minimum_off_ms(record.minimum_off_ms) &&
         record.hp1_credit_ms <= record.minimum_off_ms && record.hp2_credit_ms <= record.minimum_off_ms &&
         (record.hp1_credit_ms != 0U || record.hp2_credit_ms != 0U) && has_image_hash(record.image_hash) &&
         record.checksum == checksum(&record, offsetof(Record, checksum));
}

inline void finalize_record(Record* record) {
  record->checksum = 0U;
  record->checksum = checksum(record, offsetof(Record, checksum));
}

struct BootContext {
  bool software_reset{false};
  bool running_partition_matches_boot{false};
  bool image_valid{false};
  bool image_pending_verify{false};
  uint32_t minimum_off_ms{0U};
  uint32_t config_hash{0U};
  uint32_t boot_partition_address{0U};
  uint8_t image_hash[kImageHashSize]{};
};

enum class StorageReadResult : uint8_t { ABSENT, PRESENT, ERROR };

inline bool may_restore_credit(const Record& record, const BootContext& context) {
  if (!valid_record(record) || !context.software_reset || !context.running_partition_matches_boot ||
      context.minimum_off_ms != record.minimum_off_ms || context.config_hash != record.config_hash ||
      context.boot_partition_address != record.boot_partition_address || !has_image_hash(context.image_hash)) {
    return false;
  }

  if (record.state == kRecordRestartArmed) {
    return context.image_valid && std::memcmp(context.image_hash, record.image_hash, kImageHashSize) == 0;
  }

  if (record.state == kRecordOtaArmed) {
    // The OTA record already names the exact inactive partition selected before
    // flashing. A successful software reboot into that partition is sufficient,
    // even when the same binary was reinstalled. Failed/interrupted OTA stays on
    // the source partition and therefore cannot consume the credit.
    return context.image_pending_verify || context.image_valid;
  }

  return false;
}

// A matching record is still unsafe until its deletion has been committed and
// verified. Keep this separate so failure-path tests cannot accidentally grant
// the in-memory credit before durable consumption.
inline bool may_grant_after_durable_consume(bool consume_persisted, const Record& record, const BootContext& context) {
  return consume_persisted && may_restore_credit(record, context);
}

// Storage supplies only direct, durable operations. The same flow is used with
// NVS on-device and a fault-injecting host fake, so an optimistic cache read
// cannot accidentally turn a failed consume into a credit.
template <typename Storage>
inline bool consume_record(Storage& storage, const BootContext& context, uint32_t* hp1_credit_ms,
                           uint32_t* hp2_credit_ms) {
  if (hp1_credit_ms == nullptr || hp2_credit_ms == nullptr) return false;
  *hp1_credit_ms = 0U;
  *hp2_credit_ms = 0U;

  Record record{};
  const StorageReadResult read_result = storage.read(&record);
  if (read_result == StorageReadResult::ABSENT) return true;

  const bool eligible = read_result == StorageReadResult::PRESENT && may_restore_credit(record, context);
  // Covers corrupt records and ambiguous read failures. The adapter must prove
  // absence with a fresh direct read after its erase/commit operation.
  if (!storage.erase_and_verify_absent()) return false;
  if (!may_grant_after_durable_consume(eligible, record, context)) return true;

  *hp1_credit_ms = record.hp1_credit_ms;
  *hp2_credit_ms = record.hp2_credit_ms;
  return true;
}

template <typename Storage>
inline bool persist_record(Storage& storage, const Record& record) {
  return valid_record(record) && storage.write_commit_and_verify(record);
}

}  // namespace esphome::openquatt_incident_manager::restart_handoff
