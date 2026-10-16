#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::openquatt_incident_manager::restart_handoff {

constexpr uint32_t kRecordMagic = 0x4F515248UL;  // OQRH
constexpr uint16_t kRecordVersion = 2U;
constexpr uint8_t kRecordArmed = 1U;
constexpr uint8_t kRecordCompletedOta = 2U;
constexpr uint32_t kMaximumMinimumOffMs = 3600UL * 1000UL;
constexpr size_t kImageHashSize = 32U;
constexpr uint32_t kConsumeIncomplete = 0x4F514943UL;  // OQIC
// This RTC word can only deny restoration. It never carries positive intent.
inline bool begin_boot_consume(volatile uint32_t& marker) {
  const bool previous_incomplete = marker == kConsumeIncomplete;
  marker = kConsumeIncomplete;
  return !previous_incomplete;
}
inline void finish_boot_consume(volatile uint32_t& marker) { marker = 0U; }

// One-shot numeric Power House memory. Never CM or HP commands.
struct PowerHouseResumeState {
  float last_w;
  float comfort_memory_c;
  float setpoint_c;
};
inline bool valid_power_house_resume(const PowerHouseResumeState& state) {
  return std::isfinite(state.last_w) && state.last_w >= 0.0f && state.last_w <= 50000.0f &&
         std::isfinite(state.comfort_memory_c) && state.comfort_memory_c >= 0.0f && state.comfort_memory_c <= 0.20f &&
         std::isfinite(state.setpoint_c) && state.setpoint_c >= 5.0f && state.setpoint_c <= 35.0f;
}
inline bool may_restore_power_house(const PowerHouseResumeState& state, bool running_acquisition, float room_c,
                                    float setpoint_c) {
  return running_acquisition && valid_power_house_resume(state) && std::isfinite(room_c) && std::isfinite(setpoint_c) &&
         std::fabs(setpoint_c - state.setpoint_c) <= 0.01f;
}
struct ResumeState {
  uint8_t hp_mask{0U};
  bool completed_ota{false};
  PowerHouseResumeState power_house{};
};

// Main-loop-owned OTA session. A blocked transfer cannot service live guards;
// only short transfers may hand over. Background uploads check safety each tick.
class OtaResumePolicy {
 public:
  static constexpr uint32_t MAX_TRANSFER_MS = 45000U;
  void begin(uintptr_t transaction, uint32_t now, uint8_t hp_mask) {
    if (phase_ != Phase::IDLE) {
      cancel();
      return;
    }
    transaction_ = transaction;
    started_ms_ = now;
    mask_ = hp_mask & 3U;
    phase_ = Phase::UPLOADING;
  }
  void cancel() {
    mask_ = 0U;
    phase_ = Phase::CANCELLED;
  }
  bool owns(uintptr_t transaction) const { return phase_ != Phase::IDLE && transaction_ == transaction; }
  void reset() { *this = {}; }
  bool holding(uint8_t hp, uint32_t now, uint8_t live_mask) {
    if (phase_ != Phase::UPLOADING && phase_ != Phase::COMPLETED) return false;
    if (static_cast<uint32_t>(now - started_ms_) >= MAX_TRANSFER_MS || (live_mask & mask_) != mask_) cancel();
    return hp >= 1U && hp <= 2U && (mask_ & (1U << (hp - 1U))) != 0U;
  }
  uint8_t complete(uintptr_t transaction, uint32_t now) {
    if (phase_ != Phase::UPLOADING || transaction != transaction_ ||
        static_cast<uint32_t>(now - started_ms_) >= MAX_TRANSFER_MS) {
      cancel();
      return 0U;
    }
    phase_ = Phase::COMPLETED;
    return mask_;
  }

 private:
  enum class Phase : uint8_t { IDLE, UPLOADING, COMPLETED, CANCELLED };
  Phase phase_{Phase::IDLE};
  uintptr_t transaction_{0U};
  uint32_t started_ms_{0U};
  uint8_t mask_{0U};
};

struct Record {
  uint32_t magic;
  uint16_t version;
  uint8_t state;
  uint8_t resume_mask;
  uint32_t minimum_off_ms;
  uint32_t config_hash;
  uint32_t boot_partition_address;
  uint32_t hp1_credit_ms;
  uint32_t hp2_credit_ms;
  uint8_t image_hash[kImageHashSize];
  PowerHouseResumeState power_house;
  uint32_t checksum;
};

static_assert(sizeof(Record) == 76U, "Restart handoff NVS record size changed");

inline uint32_t checksum(const void* data, size_t length) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  uint32_t hash = 2166136261UL;
  for (size_t index = 0U; index < length; ++index) {
    hash ^= bytes[index];
    hash *= 16777619UL;
  }
  return hash;
}

inline uint32_t configuration_hash(uint32_t minimum_off_ms, bool duo_topology, bool controller_q = false) {
  uint32_t hash = 2166136261UL;
  const uint8_t bytes[] = {
      static_cast<uint8_t>(minimum_off_ms),         static_cast<uint8_t>(minimum_off_ms >> 8U),
      static_cast<uint8_t>(minimum_off_ms >> 16U),  static_cast<uint8_t>(minimum_off_ms >> 24U),
      static_cast<uint8_t>(duo_topology ? 1U : 0U), static_cast<uint8_t>(controller_q ? 1U : 0U),
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

inline bool valid_record(const Record& record) {
  return record.magic == kRecordMagic && record.version == kRecordVersion &&
         (record.state == kRecordArmed || record.state == kRecordCompletedOta) && (record.resume_mask & ~3U) == 0U &&
         valid_minimum_off_ms(record.minimum_off_ms) && record.hp1_credit_ms <= record.minimum_off_ms &&
         record.hp2_credit_ms <= record.minimum_off_ms &&
         (record.hp1_credit_ms != 0U || record.hp2_credit_ms != 0U || record.resume_mask != 0U) &&
         ((record.resume_mask & 1U) == 0U || record.hp1_credit_ms == 0U) &&
         ((record.resume_mask & 2U) == 0U || record.hp2_credit_ms == 0U) && has_image_hash(record.image_hash) &&
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
  bool image_confirmed{false};
  bool image_pending_verify{false};
  uint32_t minimum_off_ms{0U};
  uint32_t config_hash{0U};
  uint32_t boot_partition_address{0U};
  uint8_t image_hash[kImageHashSize]{};
};

enum class StorageReadResult : uint8_t { ABSENT, PRESENT, ERROR };

inline bool may_restore_credit(const Record& record, const BootContext& context) {
  return valid_record(record) && context.software_reset && context.running_partition_matches_boot &&
         (context.image_valid || context.image_pending_verify) && context.minimum_off_ms == record.minimum_off_ms &&
         context.config_hash == record.config_hash && context.boot_partition_address == record.boot_partition_address &&
         has_image_hash(context.image_hash) && std::memcmp(context.image_hash, record.image_hash, kImageHashSize) == 0;
}

inline bool may_restore_running(const Record& record, const BootContext& context) {
  return may_restore_credit(record, context) && record.resume_mask != 0U &&
         ((record.state == kRecordArmed && context.image_valid) || context.image_confirmed ||
          (record.state == kRecordCompletedOta && context.image_pending_verify));
}

inline bool may_adopt_running_image(const ResumeState& resume, const BootContext& context) {
  return resume.completed_ota ? context.image_confirmed : context.image_valid;
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
                           uint32_t* hp2_credit_ms, ResumeState* resume = nullptr) {
  if (hp1_credit_ms == nullptr || hp2_credit_ms == nullptr) return false;
  *hp1_credit_ms = 0U;
  *hp2_credit_ms = 0U;
  if (resume != nullptr) *resume = {};

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
  if (resume != nullptr && may_restore_running(record, context)) {
    resume->hp_mask = record.resume_mask;
    resume->completed_ota = record.state == kRecordCompletedOta;
    if (valid_power_house_resume(record.power_house)) resume->power_house = record.power_house;
  }
  return true;
}

template <typename Storage>
inline bool persist_record(Storage& storage, const Record& record) {
  return valid_record(record) && storage.write_commit_and_verify(record);
}

}  // namespace esphome::openquatt_incident_manager::restart_handoff
