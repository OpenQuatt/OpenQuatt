#pragma once

#include <atomic>
#include <cstdint>

namespace esphome::openquatt_service_status {

// The loop publishes status and duration together; HTTP reads one coherent word.
// No allocation, persistent storage or access to the mutable control runtime.
class WarmupProgressSnapshot {
 public:
  void publish(uint8_t status, uint32_t elapsed_s) {
    const uint32_t bounded_s = elapsed_s > 28800U ? 28800U : elapsed_s;
    this->value_.store((static_cast<uint32_t>(status) << 24U) | bounded_s, std::memory_order_relaxed);
  }
  uint32_t read() const { return this->value_.load(std::memory_order_relaxed); }
  static constexpr uint32_t UNAVAILABLE = UINT32_MAX;

 private:
  std::atomic<uint32_t> value_{UNAVAILABLE};
};

// Xtensa reports is_always_lock_free=false for the full atomic operation set.
// The relaxed 32-bit load/store used here compile to aligned l32i/s32i accesses
// (ESP32 and ESP32-S3), without helper calls or heap allocations.
static_assert(sizeof(WarmupProgressSnapshot) == sizeof(uint32_t));
static_assert(alignof(WarmupProgressSnapshot) >= alignof(uint32_t));

}  // namespace esphome::openquatt_service_status
