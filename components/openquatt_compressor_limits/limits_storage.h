#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome {
namespace openquatt_compressor_limits {

// Fixed v1 layout; no pointers or padding are persisted. The key remains
// stable across schema versions. NVS already checks the blob's CRC.
struct LimitsRecord {
  uint32_t magic{0x4F51434CU};  // OQCL
  uint16_t version{1U};
  uint16_t bytes{16U};
  float limit_2h{6.0f};
  float limit_72h{40.0f};
};
static_assert(sizeof(LimitsRecord) == 16U);
static_assert(offsetof(LimitsRecord, limit_2h) == 8U);
static_assert(offsetof(LimitsRecord, limit_72h) == 12U);

enum class ReadResult : uint8_t { FOUND, MISSING, ERROR };

class LimitsStorage {
 public:
  virtual ~LimitsStorage() = default;
  virtual ReadResult read_bundle(LimitsRecord& record) = 0;
  virtual ReadResult read_legacy(uint8_t index, float& value) = 0;
  virtual bool write_bundle(const LimitsRecord& record) = 0;
  virtual bool erase_legacy() = 0;
};

inline bool valid_limit(uint8_t index, float value) {
  return index < 2U && std::isfinite(value) && value >= 1.0f && value <= (index == 0U ? 20.0f : 120.0f);
}

inline bool valid_record(const LimitsRecord& record) {
  return record.magic == 0x4F51434CU && record.version == 1U && record.bytes == sizeof(LimitsRecord) &&
         valid_limit(0U, record.limit_2h) && valid_limit(1U, record.limit_72h);
}

// All methods run on the ESPHome main loop. Failed reads never become an
// absent record; failed writes/readback never authorize legacy deletion.
class LimitsState {
 public:
  bool restore(LimitsStorage& storage) {
    if (ready_) return true;
    LimitsRecord candidate{};
    const ReadResult result = storage.read_bundle(candidate);
    if (result == ReadResult::ERROR || (result == ReadResult::FOUND && !valid_record(candidate))) return false;
    if (result == ReadResult::MISSING) {
      candidate = LimitsRecord{};
      for (uint8_t index = 0U; index < 2U; ++index) {
        float value{};
        const ReadResult legacy = storage.read_legacy(index, value);
        if (legacy == ReadResult::ERROR || (legacy == ReadResult::FOUND && !valid_limit(index, value))) return false;
        if (legacy == ReadResult::FOUND) {
          if (index == 0U)
            candidate.limit_2h = value;
          else
            candidate.limit_72h = value;
        }
      }
      dirty_ = true;
    }
    record_ = candidate;
    ready_ = true;
    cleanup_pending_ = true;
    return true;
  }

  bool set(uint8_t index, float value) {
    if (!ready_ || !valid_limit(index, value)) return false;
    float& target = index == 0U ? record_.limit_2h : record_.limit_72h;
    if (target != value) {
      target = value;
      dirty_ = true;
    }
    return true;
  }

  bool flush(LimitsStorage& storage) {
    if (!ready_) return false;
    if (dirty_) {
      const LimitsRecord snapshot = record_;
      if (!storage.write_bundle(snapshot)) return false;
      LimitsRecord readback{};
      if (storage.read_bundle(readback) != ReadResult::FOUND ||
          std::memcmp(&snapshot, &readback, sizeof(snapshot)) != 0)
        return false;
      dirty_ = false;
    }
    if (cleanup_pending_) {
      if (!storage.erase_legacy()) return false;
      cleanup_pending_ = false;
    }
    return true;
  }

  float value(uint8_t index) const { return index == 0U ? record_.limit_2h : record_.limit_72h; }
  bool ready() const { return ready_; }
  bool pending() const { return dirty_ || cleanup_pending_; }

 protected:
  LimitsRecord record_{};
  bool ready_{false};
  bool dirty_{false};
  bool cleanup_pending_{false};
};

}  // namespace openquatt_compressor_limits
}  // namespace esphome
