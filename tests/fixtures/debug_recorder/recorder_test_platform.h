#pragma once

// Only ESPHome/IDF peripherals are adapted. The recorder and PsramBuffer are
// compiled unchanged from components/, including allocation and lock ownership.
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <sys/types.h>

namespace recorder_test {
inline uint32_t now_ms{0};
inline size_t allocation_calls{0}, internal_allocations{0}, fail_allocation_at{0};
inline bool fail_mutex_creation{false};
inline std::function<void()> allocation_hook, chunk_hook;
inline size_t held_mutexes{0};
inline size_t entity_reads{0};
inline bool allow_entity_reads{false};
inline void reset_allocation_faults() {
  allocation_calls = internal_allocations = fail_allocation_at = 0;
  allocation_hook = {};
}
template <typename... Args>
inline void log(Args&&...) {}
inline void entity_read() {
  assert(allow_entity_reads);
  ++entity_reads;
}
}  // namespace recorder_test

using TickType_t = uint32_t;
inline constexpr TickType_t portMAX_DELAY = UINT32_MAX;
inline constexpr int pdTRUE = 1, pdFALSE = 0;
struct StaticSemaphore_t {
  std::timed_mutex mutex;
};
using SemaphoreHandle_t = StaticSemaphore_t*;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* storage) {
  return recorder_test::fail_mutex_creation ? nullptr : storage;
}
inline int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
  bool locked;
  if (ticks == portMAX_DELAY) {
    semaphore->mutex.lock();
    locked = true;
  } else if (ticks == 0) {
    locked = semaphore->mutex.try_lock();
  } else {
    locked = semaphore->mutex.try_lock_for(std::chrono::milliseconds(ticks));
  }
  if (locked) ++recorder_test::held_mutexes;
  return locked ? pdTRUE : pdFALSE;
}
inline void xSemaphoreGive(SemaphoreHandle_t semaphore) {
  assert(recorder_test::held_mutexes > 0);
  --recorder_test::held_mutexes;
  semaphore->mutex.unlock();
}

using esp_err_t = int;
inline constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = -2;
inline constexpr const char* HTTPD_200 = "200 OK";
struct httpd_req_t {
  std::string status{"200 OK"}, response, content_type;
  std::map<std::string, std::string> headers;
  int chunks{0}, fail_chunk{-1};
  bool finished{false};
};
inline esp_err_t httpd_resp_set_status(httpd_req_t* req, const char* status) {
  req->status = status;
  return ESP_OK;
}
inline esp_err_t httpd_resp_set_type(httpd_req_t* req, const char* type) {
  req->content_type = type;
  return ESP_OK;
}
inline esp_err_t httpd_resp_set_hdr(httpd_req_t* req, const char* name, const char* value) {
  req->headers[name] = value;
  return ESP_OK;
}
inline esp_err_t httpd_resp_send(httpd_req_t* req, const char* data, ssize_t length) {
  req->response.assign(data == nullptr ? "" : data, static_cast<size_t>(length));
  req->finished = true;
  return ESP_OK;
}
inline esp_err_t httpd_resp_sendstr(httpd_req_t* req, const char* data) {
  return httpd_resp_send(req, data, static_cast<ssize_t>(std::strlen(data)));
}
inline esp_err_t httpd_resp_send_chunk(httpd_req_t* req, const char* data, ssize_t length) {
  assert(recorder_test::held_mutexes == 0);  // Network I/O must happen outside the state lock.
  if (recorder_test::chunk_hook) {
    auto hook = std::move(recorder_test::chunk_hook);
    recorder_test::chunk_hook = {};
    hook();
  }
  if (req->fail_chunk >= 0 && req->chunks++ >= req->fail_chunk) return ESP_FAIL;
  if (data != nullptr)
    req->response.append(data, static_cast<size_t>(length));
  else
    req->finished = true;
  return ESP_OK;
}
inline int httpd_req_to_sockfd(httpd_req_t*) { return -1; }
inline constexpr int SHUT_RDWR = 2;
inline int shutdown(int, int) { return 0; }

inline constexpr int HTTP_GET = 0, HTTP_POST = 1, HTTP_PUT = 2;
class AsyncWebServerRequest {
 public:
  static constexpr size_t URL_BUF_SIZE = 256;
  std::string path;
  int verb{HTTP_GET};
  std::map<std::string, std::string> arguments, headers;
  httpd_req_t response;
  void url_to(char* out) const { std::snprintf(out, URL_BUF_SIZE, "%s", path.c_str()); }
  int method() const { return verb; }
  std::string arg(const char* name) const {
    auto it = arguments.find(name);
    return it == arguments.end() ? "" : it->second;
  }
  std::optional<std::string> get_header(const char* name) const {
    auto it = headers.find(name);
    return it == headers.end() ? std::nullopt : std::optional<std::string>(it->second);
  }
  void send(int status, const char* type, const char* body) {
    response.status = std::to_string(status);
    httpd_resp_set_type(&response, type);
    httpd_resp_sendstr(&response, body);
  }
  operator httpd_req_t*() { return &response; }
};
class AsyncWebHandler {
 public:
  virtual ~AsyncWebHandler() = default;
  virtual bool canHandle(AsyncWebServerRequest*) const = 0;
  virtual void handleRequest(AsyncWebServerRequest*) = 0;
};

inline constexpr int MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_SPIRAM = 2;
inline uint32_t heap_caps_get_free_size(int) { return 100000; }
inline uint32_t heap_caps_get_minimum_free_size(int) { return 90000; }
inline uint32_t heap_caps_get_largest_free_block(int) { return 80000; }
inline uint32_t esp_random() {
  static uint32_t value = 0x10203040;
  return ++value;
}
#define ESP_LOGE(...) recorder_test::log(__VA_ARGS__)
#define ESP_LOGW(...) recorder_test::log(__VA_ARGS__)
#define ESP_LOGI(...) recorder_test::log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) recorder_test::log(__VA_ARGS__)

namespace esphome {
inline uint32_t millis() { return recorder_test::now_ms; }
using StringRef = std::string;
namespace setup_priority {
inline constexpr float WIFI = 250;
}
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  void mark_failed() { failed_ = true; }
  bool is_failed() const { return failed_; }

 private:
  bool failed_{false};
};
template <typename T>
class RAMAllocator {
 public:
  enum Flags { ALLOC_EXTERNAL, ALLOC_INTERNAL };
  explicit RAMAllocator(Flags flags) : flags_(flags) {}
  T* allocate(size_t count) {
    if (flags_ == ALLOC_INTERNAL) ++recorder_test::internal_allocations;
    const size_t call = ++recorder_test::allocation_calls;
    if (recorder_test::allocation_hook) recorder_test::allocation_hook();
    if (recorder_test::fail_allocation_at == call) return nullptr;
    void* storage = std::malloc(count * sizeof(T));
    if (storage != nullptr) std::memset(storage, 0xa5, count * sizeof(T));
    return static_cast<T*>(storage);
  }

 private:
  Flags flags_;
};
inline uint32_t fnv1_hash(const char* text) {
  uint32_t value = 2166136261U;
  while (*text != '\0') {
    value *= 16777619U;
    value ^= static_cast<uint8_t>(*text++);
  }
  return value;
}
namespace time {
struct ESPTime {
  uint32_t timestamp{0};
  bool valid{false};
  bool is_valid() const { return valid; }
};
class RealTimeClock {
 public:
  ESPTime value;
  ESPTime now() const { return value; }
};
}  // namespace time
class ESPPreferences;
class ESPPreferenceObject {
 public:
  ESPPreferences* owner{nullptr};
  uint32_t key{0};
  template <typename T>
  bool load(T* value) const;
  template <typename T>
  bool save(const T* value) const;
};
class ESPPreferences {
 public:
  bool fail_save{false}, fail_sync{false};
  size_t saves{0}, save_attempts{0}, syncs{0};
  size_t fail_save_at{0}, fail_sync_at{0};
  std::optional<uint32_t> failed_write_key;
  std::map<uint32_t, std::vector<uint8_t>> durable, pending;
  std::map<uint32_t, size_t> make_counts;
  template <typename T>
  ESPPreferenceObject make_preference(uint32_t key, bool) {
    ++make_counts[key];
    return {this, key};
  }
  bool sync() {
    ++syncs;
    bool failed = false;
    for (const auto& [key, value] : pending) {
      const auto stored = durable.find(key);
      if (stored != durable.end() && stored->second == value) continue;
      if (fail_sync || fail_sync_at == syncs || (failed_write_key && *failed_write_key == key)) {
        failed = true;
        continue;
      }
      durable[key] = value;
    }
    // ESPHome 2026.9 ESP32 preferences: successful nvs_set_blob writes remain
    // durable, failed keys are discarded too, and sync reports aggregate failure.
    pending.clear();
    return !failed;
  }
  ESPPreferences cold_boot() const {
    ESPPreferences reboot;
    reboot.durable = durable;
    return reboot;
  }
};
template <typename T>
bool ESPPreferenceObject::load(T* value) const {
  if (owner == nullptr) return false;
  // The real ESP32 backend reads queued values before consulting NVS.
  const auto queued = owner->pending.find(key);
  const auto stored = owner->durable.find(key);
  const auto* bytes = queued != owner->pending.end()   ? &queued->second
                      : stored != owner->durable.end() ? &stored->second
                                                       : nullptr;
  if (bytes == nullptr || bytes->size() != sizeof(T)) return false;
  std::memcpy(value, bytes->data(), sizeof(T));
  return true;
}
template <typename T>
bool ESPPreferenceObject::save(const T* value) const {
  if (owner == nullptr) return false;
  ++owner->save_attempts;
  if (owner->fail_save || owner->fail_save_at == owner->save_attempts) return false;
  ++owner->saves;
  const auto* data = reinterpret_cast<const uint8_t*>(value);
  owner->pending[key].assign(data, data + sizeof(T));
  return true;
}
inline ESPPreferences* global_preferences{nullptr};

struct Entity {
  virtual ~Entity() = default;
  std::string name, unit;
  bool valid{true};
  const std::string& get_name() const { return name; }
  StringRef get_unit_of_measurement_ref() const { return unit; }
  bool has_state() const {
    recorder_test::entity_read();
    return valid;
  }
};
namespace sensor {
struct Sensor : Entity {
  float state{0};
};
}  // namespace sensor
namespace number {
struct Number : Entity {
  float state{0};
};
}  // namespace number
namespace binary_sensor {
struct BinarySensor : Entity {
  bool state{false};
};
}  // namespace binary_sensor
namespace switch_ {
struct Switch : Entity {
  bool state{false};
};
}  // namespace switch_
namespace text_sensor {
struct TextSensor : Entity {
  std::string state{"normal"};
};
}  // namespace text_sensor
namespace select {
struct Select : Entity {
  std::string option{"Home Assistant"};
  StringRef current_option() const {
    recorder_test::entity_read();
    return option;
  }
};
}  // namespace select
class Application {
 public:
  std::vector<sensor::Sensor*> sensors;
  std::vector<number::Number*> numbers;
  std::vector<binary_sensor::BinarySensor*> binary_sensors;
  std::vector<switch_::Switch*> switches;
  std::vector<text_sensor::TextSensor*> text_sensors;
  std::vector<select::Select*> selects;
  const auto& get_sensors() const { return sensors; }
  const auto& get_numbers() const { return numbers; }
  const auto& get_binary_sensors() const { return binary_sensors; }
  const auto& get_switches() const { return switches; }
  const auto& get_text_sensors() const { return text_sensors; }
  const auto& get_selects() const { return selects; }
};
inline Application App;
namespace web_server_base {
class WebServerBase {
 public:
  std::vector<std::unique_ptr<AsyncWebHandler>> handlers;
  void add_handler(AsyncWebHandler* handler) { handlers.emplace_back(handler); }
};
inline WebServerBase* global_web_server_base{nullptr};
}  // namespace web_server_base
}  // namespace esphome
