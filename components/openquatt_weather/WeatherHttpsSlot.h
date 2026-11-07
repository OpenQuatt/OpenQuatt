#pragma once

#include <atomic>

namespace esphome::openquatt_weather {

// One slot for manifest checks and Open-Meteo, including location lookup.
class HttpsSlot {
 public:
  bool try_acquire() { return !busy_.exchange(true, std::memory_order_acquire); }
  bool try_acquire_weather() { return !manifest_waiting_.load(std::memory_order_acquire) && try_acquire(); }
  void set_manifest_waiting(bool waiting) { manifest_waiting_.store(waiting, std::memory_order_release); }
  void release() { busy_.store(false, std::memory_order_release); }

 private:
  std::atomic<bool> busy_{false};
  std::atomic<bool> manifest_waiting_{false};
};

inline HttpsSlot https_slot;

}  // namespace esphome::openquatt_weather
