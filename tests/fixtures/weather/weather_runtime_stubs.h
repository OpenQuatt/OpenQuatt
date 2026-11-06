#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace weather_fixture {
struct WorkerParked {};
struct Environment {
  uint64_t now_ms = 1000;
  uint32_t epoch = 1800000000;
  bool clock_valid = true;
  bool forbid_clock_read = false;
  bool namespace_exists = false;
  bool network_connected = false;
  bool fail_task_creation = false, fail_external_allocation = false;
  bool fail_write_open = false, fail_write = false, fail_commit = false, fail_read = false, corrupt_readback = false;
  unsigned task_creations = 0, notifications = 0, external_allocations = 0, internal_allocations = 0;
  unsigned write_opens = 0, write_calls = 0, commit_calls = 0, verified_reads = 0, fetch_calls = 0;
  unsigned next_handle = 1, write_handle = 0;
  std::vector<unsigned char> committed, staged;
  int committed_consent = -1, staged_consent = -1;
  std::map<unsigned, int> handles;
  std::function<void()> on_write;
  std::function<void(const char*)> observe_nvs;
};
inline Environment env;
template <typename... Args>
inline void log(Args&&...) {}
}  // namespace weather_fixture

namespace esphome {
class EntityBase {
 public:
  uint8_t get_entity_category() const { return 0; }
  bool is_disabled_by_default() const { return false; }
};
inline uint32_t millis() { return static_cast<uint32_t>(weather_fixture::env.now_ms); }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual void on_shutdown() {}
  void mark_failed() { failed = true; }
  bool failed = false;
};
class PollingComponent : public Component {
 public:
  virtual void update() {}
};
template <typename T>
class RAMAllocator {
 public:
  enum { ALLOC_EXTERNAL = 1, ALLOC_INTERNAL = 2 };
  explicit RAMAllocator(int mode = ALLOC_INTERNAL) : mode_(mode) {}
  T* allocate(size_t count) {
    if (mode_ == ALLOC_EXTERNAL) {
      ++weather_fixture::env.external_allocations;
      if (weather_fixture::env.fail_external_allocation) return nullptr;
    } else {
      ++weather_fixture::env.internal_allocations;
    }
    return static_cast<T*>(std::calloc(count, sizeof(T)));
  }

 private:
  int mode_;
};
namespace binary_sensor {
class BinarySensor {
 public:
  bool state = false;
  std::vector<std::function<void(bool)>> callbacks;
  void add_on_state_callback(std::function<void(bool)> callback) { callbacks.push_back(std::move(callback)); }
  void publish_state(bool value) {
    state = value;
    for (const auto& callback : callbacks) callback(value);
  }
};
}  // namespace binary_sensor
namespace switch_ {
class Switch : public binary_sensor::BinarySensor {
 public:
  virtual ~Switch() = default;
  void turn_on() { write_state(true); }
  void turn_off() { write_state(false); }

 protected:
  virtual void write_state(bool) = 0;
};
}  // namespace switch_
namespace sensor {
class Sensor {
 public:
  float state = 0;
  void publish_state(float value) { state = value; }
};
}  // namespace sensor
namespace text {
class Text : public EntityBase {
 public:
  virtual ~Text() = default;
  std::string state;
  std::vector<std::string> publications;
  std::function<void()> frontend;
  void publish_state(const std::string& value) {
    state = value;
    publications.push_back(value);
    if (frontend) frontend();
  }

 protected:
  virtual void control(const std::string&) = 0;
};
}  // namespace text
namespace text_sensor {
class TextSensor : public EntityBase {
 public:
  std::string state;
  bool state_present = false;
  std::vector<std::function<void(const std::string&)>> callbacks;
  std::function<void()> frontend;
  bool has_state() const { return state_present; }
  template <typename F>
  void add_on_state_callback(F&& callback) {
    callbacks.emplace_back(std::forward<F>(callback));
  }
  void publish_state(const std::string& value) {
    state = value;
    state_present = true;
    for (const auto& callback : callbacks) callback(state);
    if (frontend) frontend();
  }
};
}  // namespace text_sensor
namespace time {
struct ClockValue {
  uint32_t timestamp = 1800000000;
  bool valid = true;
  bool is_valid() const { return valid; }
};
class RealTimeClock {
 public:
  ClockValue now() const {
    assert(!weather_fixture::env.forbid_clock_read);
    return {weather_fixture::env.epoch, weather_fixture::env.clock_valid};
  }
};
}  // namespace time
namespace ota {
enum OTAState { OTA_STARTED, OTA_IN_PROGRESS, OTA_COMPLETED, OTA_ERROR, OTA_ABORT };
class OTAComponent {};
class OTAGlobalStateListener {
 public:
  virtual ~OTAGlobalStateListener() = default;
  virtual void on_ota_global_state(OTAState, float, uint8_t, OTAComponent*) = 0;
};
struct GlobalOtaCallback {
  void add_global_state_listener(OTAGlobalStateListener*) {}
};
inline GlobalOtaCallback* get_global_ota_callback() {
  static GlobalOtaCallback callback;
  return &callback;
}
}  // namespace ota
namespace network {
inline bool is_connected() { return weather_fixture::env.network_connected; }
}  // namespace network
}  // namespace esphome

#define ESP_LOGE(...) ::weather_fixture::log(__VA_ARGS__)
#define ESP_LOGW(...) ::weather_fixture::log(__VA_ARGS__)
#define ESP_LOGI(...) ::weather_fixture::log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) ::weather_fixture::log(__VA_ARGS__)

using TaskHandle_t = void*;
using BaseType_t = int;
using TickType_t = uint32_t;
inline constexpr BaseType_t pdPASS = 1, pdTRUE = 1, tskIDLE_PRIORITY = 0;
inline constexpr TickType_t portMAX_DELAY = UINT32_MAX;
inline BaseType_t xTaskCreate(void (*)(void*), const char*, unsigned stack_bytes, void*, unsigned,
                              TaskHandle_t* handle) {
  assert(stack_bytes == 8192);
  ++weather_fixture::env.task_creations;
  if (weather_fixture::env.fail_task_creation) return 0;
  *handle = reinterpret_cast<TaskHandle_t>(uintptr_t{1});
  return pdPASS;
}
inline void xTaskNotifyGive(TaskHandle_t handle) {
  assert(handle != nullptr);
  ++weather_fixture::env.notifications;
}
inline uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t wait) {
  assert(clear_on_exit == pdTRUE && wait == portMAX_DELAY);
  if (weather_fixture::env.notifications == 0) throw weather_fixture::WorkerParked{};
  return std::exchange(weather_fixture::env.notifications, 0);
}
inline uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 4096; }

struct ip_addr_t {
  uint32_t address = 0;
};
inline int64_t esp_timer_get_time() { return static_cast<int64_t>(weather_fixture::env.now_ms * 1000); }
inline constexpr int MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2;
inline size_t heap_caps_get_free_size(int) { return 65536; }
inline size_t heap_caps_get_minimum_free_size(int) { return 60000; }
inline size_t heap_caps_get_largest_free_block(int) { return 50000; }

using esp_err_t = int;
using nvs_handle_t = unsigned;
inline constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NVS_NOT_FOUND = 0x1102,
                           ESP_ERR_NVS_INVALID_LENGTH = 0x110c;
inline constexpr int NVS_READONLY = 0, NVS_READWRITE = 1;

inline esp_err_t nvs_open(const char* name, int mode, nvs_handle_t* handle) {
  auto& env = weather_fixture::env;
  assert(std::strcmp(name, "oq_weather") == 0);
  if (mode == NVS_READWRITE) {
    ++env.write_opens;
    if (env.fail_write_open) return ESP_FAIL;
    env.namespace_exists = true;
  } else if (!env.namespace_exists) {
    return ESP_ERR_NVS_NOT_FOUND;
  }
  *handle = env.next_handle++;
  env.handles[*handle] = mode;
  if (mode == NVS_READWRITE) {
    env.write_handle = *handle;
    env.staged.clear();
    env.staged_consent = -1;
  }
  return ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t handle, const char* key, void* value, size_t* size) {
  auto& env = weather_fixture::env;
  assert(std::strcmp(key, "location") == 0 && env.handles.at(handle) == NVS_READONLY);
  if (env.observe_nvs) env.observe_nvs("read");
  if (env.fail_read) return ESP_FAIL;
  if (env.committed.empty()) return ESP_ERR_NVS_NOT_FOUND;
  if (*size < env.committed.size()) {
    *size = env.committed.size();
    return ESP_ERR_NVS_INVALID_LENGTH;
  }
  std::memcpy(value, env.committed.data(), env.committed.size());
  *size = env.committed.size();
  if (env.commit_calls != 0) {
    assert(handle != env.write_handle);
    ++env.verified_reads;
    if (env.corrupt_readback) static_cast<unsigned char*>(value)[0] ^= 1;
  }
  return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t size) {
  auto& env = weather_fixture::env;
  assert(std::strcmp(key, "location") == 0 && env.handles.at(handle) == NVS_READWRITE);
  ++env.write_calls;
  if (env.observe_nvs) env.observe_nvs("write");
  auto callback = std::move(env.on_write);
  env.on_write = {};
  if (callback) callback();
  if (env.fail_write) return ESP_FAIL;
  const auto* bytes = static_cast<const unsigned char*>(value);
  env.staged.assign(bytes, bytes + size);
  return ESP_OK;
}
inline esp_err_t nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* value) {
  auto& env = weather_fixture::env;
  assert(std::strcmp(key, "enabled") == 0 && env.handles.at(handle) == NVS_READONLY);
  if (env.observe_nvs) env.observe_nvs("read");
  if (env.fail_read) return ESP_FAIL;
  if (env.committed_consent < 0) return ESP_ERR_NVS_NOT_FOUND;
  *value = static_cast<uint8_t>(env.committed_consent);
  if (env.commit_calls != 0) {
    assert(handle != env.write_handle);
    ++env.verified_reads;
    if (env.corrupt_readback) *value ^= 1;
  }
  return ESP_OK;
}
inline esp_err_t nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value) {
  auto& env = weather_fixture::env;
  assert(std::strcmp(key, "enabled") == 0 && env.handles.at(handle) == NVS_READWRITE);
  ++env.write_calls;
  if (env.observe_nvs) env.observe_nvs("write");
  auto callback = std::exchange(env.on_write, {});
  if (callback) callback();
  if (env.fail_write) return ESP_FAIL;
  env.staged_consent = value;
  return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t handle) {
  auto& env = weather_fixture::env;
  assert(env.handles.at(handle) == NVS_READWRITE);
  ++env.commit_calls;
  if (env.observe_nvs) env.observe_nvs("commit");
  if (env.fail_commit) return ESP_FAIL;
  if (env.staged_consent >= 0)
    env.committed_consent = env.staged_consent;
  else
    env.committed = env.staged;
  return ESP_OK;
}
inline void nvs_close(nvs_handle_t handle) { assert(weather_fixture::env.handles.erase(handle) == 1); }
