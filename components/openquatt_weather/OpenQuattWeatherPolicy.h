#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>

namespace oq_weather {

constexpr uint64_t kRequestDeadlineMs = 20000;
constexpr uint64_t kOtaQuietTimeoutMs = 180000;
enum class WorkGate : uint8_t { QUEUED, CANCELLED, FLASH_STARTED };

inline void cancel_queued_work(std::atomic<WorkGate>& gate) {
  auto queued = WorkGate::QUEUED;
  gate.compare_exchange_strong(queued, WorkGate::CANCELLED, std::memory_order_acq_rel);
}

inline bool begin_flash_work(std::atomic<WorkGate>& gate) {
  auto queued = WorkGate::QUEUED;
  return gate.compare_exchange_strong(queued, WorkGate::FLASH_STARTED, std::memory_order_acq_rel);
}

inline bool request_expired(uint64_t started, uint64_t now) {
  return now < started || now - started >= kRequestDeadlineMs;
}

// All calls belong to the main loop. Concurrent OTA owners cannot release each
// other's hold; missing terminal events expire only when the global pause is off.
class OtaPause {
 public:
  void progress(const void* owner, uint64_t now) {
    for (auto& slot : slots_)
      if (slot.owner == owner && slot.active) {
        slot.progress = now;
        return;
      }
    for (auto& slot : slots_)
      if (!slot.active) {
        slot = {owner, now, true};
        return;
      }
    overflow_ = true;  // An unknown fifth owner fails closed until reboot.
  }
  void failed(const void* owner) {
    for (auto& slot : slots_)
      if (slot.owner == owner) slot.active = false;
  }
  bool active(uint64_t now, bool global_pause) {
    bool hold = overflow_;
    for (auto& slot : slots_) {
      if (slot.active && !global_pause && now >= slot.progress && now - slot.progress >= kOtaQuietTimeoutMs)
        slot.active = false;
      hold |= slot.active;
    }
    return hold;
  }

 private:
  struct Slot {
    const void* owner = nullptr;
    uint64_t progress = 0;
    bool active = false;
  } slots_[4];
  bool overflow_ = false;
};

}  // namespace oq_weather
