#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#ifdef USE_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
#include <mutex>
#endif

namespace esphome::openquatt_weather {

enum class SaveState : uint8_t { NONE, PENDING, STORED, FAILED };
enum class ConsentState : uint8_t { NONE, OFF, ON, PENDING, FAILED };

inline const char* consent_state_name(ConsentState state) {
  switch (state) {
    case ConsentState::OFF:
      return "off";
    case ConsentState::ON:
      return "on";
    case ConsentState::PENDING:
      return "pending";
    case ConsentState::FAILED:
      return "failed";
    case ConsentState::NONE:
      return nullptr;
  }
  return nullptr;
}

struct TextSnapshotValue {
  char text[128]{};
  char save_request[33]{};
  SaveState save_state = SaveState::NONE;
  ConsentState consent_state = ConsentState::NONE;
};

struct TextSnapshotMetadata {
  uint8_t category = 0;
  bool disabled_by_default = false;
};

// A fixed, allocation-free publication cache for the weather Text/TextSensors.
// The httpd task must never serialize their live, main-loop-owned std::strings.
class WeatherTextSnapshots {
 public:
  static constexpr size_t kSlots = 4;
  WeatherTextSnapshots() = default;
  WeatherTextSnapshots(const WeatherTextSnapshots&) = delete;
  WeatherTextSnapshots& operator=(const WeatherTextSnapshots&) = delete;

  // Bind once, before publishing the registry pointer to HTTP readers.
  void bind(size_t slot, const void* entity, TextSnapshotMetadata metadata = {}) {
    if (slot < kSlots) {
      entries_[slot].entity = entity;
      entries_[slot].metadata = metadata;
    }
  }

  void publish(const void* entity, const char* value, const char* request = "", SaveState state = SaveState::NONE,
               ConsentState consent = ConsentState::NONE) {
    Guard guard(*this);
    for (auto& entry : entries_) {
      if (!entity || entry.entity != entity) continue;
      size_t length = 0;
      while (length + 1 < sizeof(entry.value.text) && value[length]) ++length;
      std::memcpy(entry.value.text, value, length);
      entry.value.text[length] = '\0';
      std::strncpy(entry.value.save_request, request, sizeof(entry.value.save_request) - 1);
      entry.value.save_request[sizeof(entry.value.save_request) - 1] = '\0';
      entry.value.save_state = state;
      entry.value.consent_state = consent;
      return;
    }
  }

  bool copy(const void* entity, TextSnapshotValue& result) {
    Guard guard(*this);
    for (const auto& entry : entries_) {
      if (!entity || entry.entity != entity) continue;
      result = entry.value;
      return true;
    }
    return false;
  }

  bool copy_metadata(const void* entity, TextSnapshotMetadata& result) {
    // EntityBase identity and metadata are immutable after bind/release-store.
    // In particular, HTTP never reads the SDK bitfield also used for has_state.
    for (const auto& entry : entries_) {
      if (!entity || entry.entity != entity) continue;
      result = entry.metadata;
      return true;
    }
    return false;
  }

  using LocationHandler = void (*)(void*, const char*, const char*);
  void bind_location_handler(void* context, LocationHandler handler) {
    location_context_ = context;
    location_handler_ = handler;
  }
  bool is_location(const void* entity) const { return entity && entity == entries_[0].entity; }
  // Main-loop defer only; the immutable handler never runs on an HTTP task.
  void control_location(const void* entity, const char* value, const char* request) {
    if (is_location(entity) && location_handler_) location_handler_(location_context_, value, request);
  }

 private:
  class Guard {
   public:
    explicit Guard(WeatherTextSnapshots& owner) : owner_(owner) { owner_.lock_(); }
    ~Guard() { owner_.unlock_(); }

   private:
    WeatherTextSnapshots& owner_;
  };
  void lock_() {
#ifdef USE_ESP32
    xSemaphoreTake(mutex_, portMAX_DELAY);
#else
    mutex_.lock();
#endif
  }
  void unlock_() {
#ifdef USE_ESP32
    xSemaphoreGive(mutex_);
#else
    mutex_.unlock();
#endif
  }
#ifdef USE_ESP32
  // Static FreeRTOS mutex: no allocation/failure path at first HTTP read.
  StaticSemaphore_t mutex_storage_{};
  SemaphoreHandle_t mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
#else
  std::mutex mutex_;
#endif
  struct Entry {
    const void* entity = nullptr;
    TextSnapshotMetadata metadata;
    TextSnapshotValue value;
  } entries_[kSlots];
  void* location_context_ = nullptr;
  LocationHandler location_handler_ = nullptr;
};

// The component and this internal-RAM registry live for the firmware lifetime.
// Publication is release/acquire; bind() is never called after registration.
inline std::atomic<WeatherTextSnapshots*> published_weather_snapshots{nullptr};
inline bool copy_weather_text_snapshot(const void* entity, TextSnapshotValue& result) {
  auto* snapshots = published_weather_snapshots.load(std::memory_order_acquire);
  return snapshots && snapshots->copy(entity, result);
}
inline bool copy_weather_text_metadata(const void* entity, TextSnapshotMetadata& result) {
  auto* snapshots = published_weather_snapshots.load(std::memory_order_acquire);
  return snapshots && snapshots->copy_metadata(entity, result);
}
inline bool is_weather_location(const void* entity) {
  auto* snapshots = published_weather_snapshots.load(std::memory_order_acquire);
  return snapshots && snapshots->is_location(entity);
}
inline void control_weather_location(const void* entity, const char* value, const char* request) {
  auto* snapshots = published_weather_snapshots.load(std::memory_order_acquire);
  if (snapshots) snapshots->control_location(entity, value, request);
}
inline bool valid_save_request(const char* value, size_t length) {
  if (length != 32) return false;
  for (size_t i = 0; i < length; ++i)
    if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
  return true;
}

}  // namespace esphome::openquatt_weather
