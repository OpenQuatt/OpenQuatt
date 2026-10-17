#pragma once
#include <mutex>
#include <atomic>
#include <cstdint>
namespace esphome {
using Mutex = std::mutex;
using LockGuard = std::lock_guard<Mutex>;
inline std::atomic<uint32_t> test_millis{100};
inline uint32_t millis() { return test_millis.load(); }
inline uint32_t esp_random() { return 42; }
inline uint32_t fnv1_hash(const char*) { return 123; }
}  // namespace esphome
