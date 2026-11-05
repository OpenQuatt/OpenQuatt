#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace {
uint64_t now_ms = 0;
int task = 1;
uint64_t esp_timer_get_time() { return now_ms * 1000U; }
int xTaskGetCurrentTaskHandle() { return task; }

// Compile the real publication and snapshot methods, with the SDK cache
// storage, task identity and clock replaced by deterministic test doubles.
struct OpenQuattHouseLearningStatus {
  bool publish_status_json(const char* json, size_t length, size_t offset = 0, uint64_t expiry = 0,
                           uint32_t generation = 0);
  bool snapshot_status(size_t* length) const;
  uint32_t shadow_generation() const { return shadow_generation_.load(std::memory_order_acquire); }
  void invalidate_shadow();
  bool storage_available() const { return true; }
  bool cache_lock_() const {
    assert(!locked);
    locked = true;
    return true;
  }
  void cache_unlock_() const {
    assert(locked);
    locked = false;
  }
  std::array<char, 4096> status_buffer_{};
  mutable std::array<char, 37 * 1024> request_buffer_{};
  int main_loop_task_ = 1;
  mutable bool locked = false;
  size_t status_length_ = 0, shadow_offset_ = 0;
  uint64_t shadow_expires_ms_ = 0;
  uint32_t status_shadow_generation_ = 0;
  std::atomic<uint32_t> shadow_generation_{1};
};

#include "production_shadow_status.inc"
}  // namespace

int main() {
  OpenQuattHouseLearningStatus cache;
  constexpr std::string_view ready = R"({"records":18,"shadow":{"available":true,"candidate_w":2750}})";
  constexpr auto offset = ready.find(",\"shadow\"");
  constexpr std::string_view expired = R"({"records":18,"shadow_expired":true})";
  const auto read = [&cache]() {
    size_t length = 0;
    assert(cache.snapshot_status(&length) && !cache.locked && cache.request_buffer_[length] == '\0');
    return std::string_view(cache.request_buffer_.data(), length);
  };
  const auto publish = [&](uint32_t generation) {
    assert(cache.publish_status_json(ready.data(), ready.size(), offset, 20000, generation));
  };

  // Frame t=10s, published nine seconds into its original 10s cadence.
  now_ms = 19000;
  publish(cache.shadow_generation());
  assert(read() == R"({"records":18,"shadow":{"available":true,"candidate_w":2750},"shadow_ttl_ms":1000})");
  now_ms = 19999;
  assert(read().find("\"shadow_ttl_ms\":1}") != std::string_view::npos);
  now_ms = 20000;
  assert(read() == expired);
  assert(read() == expired);  // Repeated GET never renews the lifetime.

  now_ms = 19200;
  const auto old_generation = cache.shadow_generation();
  publish(old_generation);
  task = 2;
  cache.invalidate_shadow();
  assert(read() == expired);
  assert(!cache.publish_status_json(ready.data(), ready.size(), offset, 20000, old_generation));
  task = 1;
  publish(old_generation);  // An invalidation may win between evaluation and publication.
  assert(read() == expired);
  publish(cache.shadow_generation());
  assert(read().find("\"shadow_ttl_ms\":800}") != std::string_view::npos);
  assert(!cache.publish_status_json(ready.data(), ready.size(), ready.size(), 20000, cache.shadow_generation()));
  constexpr std::string_view plain = R"({"records":18})";
  assert(cache.publish_status_json(plain.data(), plain.size()));
  assert(read() == plain);
}
