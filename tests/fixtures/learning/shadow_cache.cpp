#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>

#include "../../../openquatt/includes/control/oq_ph_shadow_demand_types.h"
#include "../../../openquatt/includes/control/oq_ph_learning_json.h"

namespace {
uint64_t now_ms = 0;
int task = 1;
uint64_t esp_timer_get_time() { return now_ms * 1000U; }
int xTaskGetCurrentTaskHandle() { return task; }

// Compile the real publication and snapshot methods, with the SDK cache
// storage, task identity and clock replaced by deterministic test doubles.
struct OpenQuattHouseLearningStatus {
  bool publish_status_json(const char* json, size_t length, size_t offset = 0, uint64_t expiry = 0,
                           uint32_t generation = 0, uint32_t context_revision = 0);
  bool publish_shadow_json(const char* json, size_t length, uint64_t expiry, uint32_t generation,
                           uint32_t context_revision);
  bool snapshot_status(size_t* length) const;
  uint32_t shadow_generation() const { return shadow_generation_.load(std::memory_order_acquire); }
  void invalidate_shadow();
  bool storage_available() const { return storage_ready; }
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
  bool storage_ready = true;
  size_t status_length_ = 0, shadow_prefix_length_ = 0;
  uint64_t shadow_expires_ms_ = 0;
  uint32_t status_shadow_generation_ = 0;
  uint32_t status_context_revision_ = 0;
  std::atomic<uint32_t> shadow_generation_{1};
};

#include "production_shadow_status.inc"

// Exercise the production post-control publisher against the actual cache.
// Only its already separately tested model/frame calculation is supplied here.
struct PublisherStorage {
  char json[37 * 1024]{};
  struct {
    uint32_t context_revision = 1;
  } input;
};
struct Publisher {
  PublisherStorage* storage_;
  OpenQuattHouseLearningStatus& oq_ph_learning_status_endpoint;
  oq_power_house::ShadowDemandResult result{};
  using JsonWriter = oq_ph_learning::JsonWriter;
  oq_power_house::ShadowDemandResult shadow_demand_(PublisherStorage&) const { return result; }
#define id(entity) entity
#include "production_shadow_refresh.inc"
#undef id
};

using oq_power_house::ShadowDemandReason;

namespace oq_power_house_runtime {
struct Runtime {
  OpenQuattHouseLearningStatus& oq_ph_learning_status_endpoint;
  std::atomic<ShadowDemandReason> frame_invalid_reason_{ShadowDemandReason::SETTINGS_CHANGED};

#define id(entity) entity
#include "production_shadow_invalidation.inc"
#undef id
};

Runtime* active = nullptr;
Runtime& runtime() { return *active; }
}  // namespace oq_power_house_runtime

struct ExternalDemandSensor {
  void add_on_state_callback(std::function<void(float)> callback) { on_state = callback; }
  std::function<void(float)> on_state;
};

struct ShadowLifecycle {
  void invalidate_shadow_(ShadowDemandReason reason) {
    oq_power_house_runtime::runtime().invalidate_demand_evaluation(reason);
  }

  void connect(ExternalDemandSensor& external_heat_demand_selected) {
#define id(entity) entity
#include "production_external_override.inc"
#undef id
  }

  static void polling_paused(bool x) {
#include "production_shadow_pause.inc"
  }
};
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
    assert(cache.publish_status_json(ready.data(), ready.size(), offset, 20000, generation, 1));
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
  assert(!cache.publish_status_json(ready.data(), ready.size(), offset, 20000, old_generation, 1));
  task = 1;
  publish(old_generation);  // An invalidation may win between evaluation and publication.
  assert(read() == expired);
  publish(cache.shadow_generation());
  assert(read().find("\"shadow_ttl_ms\":800}") != std::string_view::npos);

  oq_power_house_runtime::Runtime owner{cache};
  oq_power_house_runtime::active = &owner;
  ExternalDemandSensor external;
  ShadowLifecycle lifecycle;
  lifecycle.connect(external);
  const auto live_generation = cache.shadow_generation();
  external.on_state(NAN);
  ShadowLifecycle::polling_paused(false);
  assert(cache.shadow_generation() == live_generation);
  assert(read().find("\"candidate_w\":2750") != std::string_view::npos);
  external.on_state(0.0f);  // A finite zero is an active external override.
  assert(owner.frame_invalid_reason_.load() == ShadowDemandReason::EXTERNAL_OVERRIDE);
  assert(read() == expired);  // No status tick has intervened.
  publish(cache.shadow_generation());
  ShadowLifecycle::polling_paused(true);
  assert(owner.frame_invalid_reason_.load() == ShadowDemandReason::SERVICE_OR_OTA);
  assert(read() == expired);
  ShadowLifecycle::polling_paused(false);
  assert(read() == expired);  // Resuming cannot revive old watts.

  assert(!cache.publish_status_json(ready.data(), ready.size(), ready.size(), 20000, cache.shadow_generation(), 1));

  // The actual 4/14/24/34s control phase must not leave the 30/40s status
  // publication with an expired comparison between normal status ticks.
  constexpr std::string_view unavailable = R"({"records":18,"shadow":{"available":false,"reason":"stale_frame"}})";
  constexpr std::string_view tail = R"(,"shadow":{"available":true,"candidate_w":2750}})";
  now_ms = 30000;
  assert(cache.publish_status_json(ready.data(), ready.size(), offset, 34000, cache.shadow_generation(), 1));
  const auto prefix = std::string_view(cache.status_buffer_.data(), offset);
  const auto saved_prefix = std::string(prefix);
  now_ms = 33999;
  assert(read().find("\"shadow_ttl_ms\":1}") != std::string_view::npos);
  now_ms = 34000;
  assert(read() == expired);
  assert(cache.publish_shadow_json(tail.data(), tail.size(), 44000, cache.shadow_generation(), 1));
  for (now_ms = 34000; now_ms < 44000; now_ms += 1000) {
    assert(read().find("\"candidate_w\":2750") != std::string_view::npos);
    assert(std::string_view(cache.status_buffer_.data(), offset) == saved_prefix);
  }
  // Republishing the same frame never extends its original lifetime.
  assert(cache.publish_shadow_json(tail.data(), tail.size(), 44000, cache.shadow_generation(), 1));
  assert(read() == expired);

  // A status published before a valid control frame retains its prefix so
  // the next post-control publication can become ready without a learner tick.
  assert(cache.publish_status_json(unavailable.data(), unavailable.size(), offset, 0, 0, 1));
  assert(read() == unavailable);
  PublisherStorage storage;
  Publisher publisher{&storage, cache};
  publisher.result.reason = ShadowDemandReason::READY;
  publisher.result.expires_ms = 54000;
  publisher.result.generation = cache.shadow_generation();
  publisher.result.candidate_w = 2750;
  publisher.refresh_shadow();
  assert(read().find("\"candidate_w\":2750") != std::string_view::npos);
  assert(read().find("\"shadow_ttl_ms\":10000}") != std::string_view::npos);
  assert(std::string_view(cache.status_buffer_.data(), offset) == saved_prefix);
  cache.invalidate_shadow();
  assert(read() == expired);
  publisher.refresh_shadow();  // Invalidation before publication also wins.
  assert(read() == expired);

  // The suffix can never attach to a status from another learning context.
  assert(!cache.publish_shadow_json(tail.data(), tail.size(), 54000, cache.shadow_generation(), 2));
  assert(read() == expired);
  std::array<char, 4096> oversized;
  oversized.fill('x');
  oversized.front() = ',';
  oversized[oversized.size() - offset - 1] = '}';
  for (const int failure : {0, 1, 2, 3}) {
    assert(cache.publish_shadow_json(tail.data(), tail.size(), 54000, cache.shadow_generation(), 1));
    if (failure == 0) task = 2;
    if (failure == 1) cache.storage_ready = false;
    const char* data = failure == 3 ? oversized.data() : failure == 2 ? "invalid}" : tail.data();
    const size_t size = failure == 3 ? cache.status_buffer_.size() - offset : tail.size();
    assert(!cache.publish_shadow_json(data, size, 54000, cache.shadow_generation(), 1));
    task = 1;
    cache.storage_ready = true;
    assert(read() == expired);
  }
  constexpr std::string_view plain = R"({"records":18})";
  assert(cache.publish_status_json(plain.data(), plain.size()));
  assert(read() == plain);
  assert(!cache.publish_shadow_json(tail.data(), tail.size(), 54000, cache.shadow_generation(), 1));
  assert(read() == plain);
}
