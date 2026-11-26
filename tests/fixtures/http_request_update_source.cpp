// The Python regression inserts the generated ESPHome class, update() and URL
// resolution code here. Only platform dependencies and scheduling are stubbed.
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
#include <string>
#include <utility>

#ifdef USE_OPENQUATT_WEATHER
#include "OpenQuattWeatherPolicy.h"
#include "WeatherHttpsSlot.h"
#endif

#define LOG_STR(value) value
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)

using LogString = char;
using TaskHandle_t = void*;
static constexpr int pdPASS = 1;
static bool task_create_ok = true;
static unsigned task_starts = 0;
int xTaskCreate(void (*)(void*), const char*, unsigned stack_bytes, void*, unsigned, TaskHandle_t* handle) {
#ifdef USE_OPENQUATT_WEATHER
  assert(stack_bytes == 6144);
#else
  assert(stack_bytes == 8192);
#endif
  ++task_starts;
  assert(handle == nullptr);
  return task_create_ok ? pdPASS : 0;
}

namespace esphome {
namespace network {
static bool connected = true;
bool is_connected() { return connected; }
}  // namespace network
namespace setup_priority {
static constexpr float AFTER_WIFI = 0;
}
class PollingComponent {
 public:
  virtual ~PollingComponent() = default;
  virtual void setup() {}
  virtual void update() {}
  virtual float get_setup_priority() const { return 0; }
  void cancel_interval(unsigned) {}
  void status_set_error(const char* value) { error = value; }
  void status_clear_error() { error.clear(); }
  bool status_has_error() const { return !error.empty(); }
  void set_timeout(unsigned id, unsigned delay, std::function<void()>&& callback) {
    assert(id == 1 && delay == 250 && !timeout);
    timeout = std::move(callback);
  }
  void cancel_timeout(unsigned id) {
    assert(id == 1);
    timeout = {};
  }
  void fire_timeout() {
    assert(timeout);
    auto callback = std::move(timeout);
    timeout = {};
    callback();
  }
  std::function<void()> timeout;
  std::string error;
};
namespace update {
struct UpdateInfo {
  std::string firmware_url;
};
enum UpdateState { UPDATE_STATE_INSTALLING, UPDATE_STATE_NO_UPDATE };
class UpdateEntity {
 public:
  virtual ~UpdateEntity() = default;
  virtual void perform(bool) {}
  virtual void check() {}
  UpdateState state_ = UPDATE_STATE_NO_UPDATE;
};
}  // namespace update
namespace ota {
enum OTAState { UNUSED };
class OTAStateListener {
 public:
  virtual ~OTAStateListener() = default;
  virtual void on_ota_state(OTAState, float, uint8_t) {}
};
}  // namespace ota
namespace http_request {
class HttpRequestComponent {};
class OtaHttpRequestComponent {};
}  // namespace http_request
}  // namespace esphome

// GENERATED_HEADER

namespace esphome::http_request {
static constexpr unsigned INITIAL_CHECK_INTERVAL_ID = 0;
void HttpRequestUpdate::setup() {}
void HttpRequestUpdate::perform(bool) {}
void HttpRequestUpdate::on_ota_state(ota::OTAState, float, uint8_t) {}
void HttpRequestUpdate::update_task(void*) {}

// GENERATED_UPDATE

void complete_manifest([[maybe_unused]] HttpRequestUpdate* this_update) {
  auto* result = this_update->task_result_;
  assert(result != nullptr);
  // GENERATED_COMPLETION
  this_update->manifest_result_current_ = result->error_str == nullptr;
  if (result->error_str == nullptr) this_update->status_clear_error();
  delete result;
}

std::string resolve_url(HttpRequestUpdate& component, const std::string& path) {
  const char* source_url = component.task_result_->source_url;
  struct {
    std::string firmware_url;
  } result{path};
  auto* info = &result;
  // GENERATED_RELATIVE_URL
  return result.firmware_url;
}
}  // namespace esphome::http_request

int main() {
  using esphome::http_request::complete_manifest;
  using esphome::http_request::HttpRequestUpdate;
  using esphome::http_request::resolve_url;
  HttpRequestUpdate component;
  const std::string pr900 = "https://github.com/OpenQuatt/OpenQuatt/releases/download/pr-900/ota.manifest.json";
  const std::string pr901 = "https://github.com/OpenQuatt/OpenQuatt/releases/download/pr-901/ota.manifest.json";
  {
    std::string temporary = pr900;
    component.set_source_url(temporary.c_str());
    temporary.assign(1024, 'x');
  }
  assert(component.get_source_url() == pr900);
  esphome::network::connected = false;
  component.check();
  assert(component.task_result_ == nullptr);
  esphome::network::connected = true;
  component.check();
  assert(component.task_result_->source_url == pr900);
  const char* active = component.task_result_->source_url;
  component.set_source_url(std::string(pr901).c_str());
  assert(component.get_source_url() == pr901);
  assert(component.task_result_->source_url == pr900);
  assert(std::string(active) == pr900);
  assert(resolve_url(component, "firmware.ota.bin") ==
         "https://github.com/OpenQuatt/OpenQuatt/releases/download/pr-900/firmware.ota.bin");
  assert(resolve_url(component, "/firmware.ota.bin") == "https://github.com/firmware.ota.bin");
  assert(resolve_url(component, "https://example.org/fw.bin") == "https://example.org/fw.bin");
#ifdef USE_ESP32
  const unsigned started = task_starts;
  // A timeout/retry cannot overwrite a request still running, even after a
  // pending source change. The real worker releases this guard via defer().
  component.check();
  assert(task_starts == started);
  assert(component.task_result_->source_url == pr900);
  complete_manifest(&component);
  assert(!component.manifest_result_matches(pr901));
  assert(!component.manifest_result_matches(pr900));
#endif
  component.check();
  assert(component.task_result_->source_url == pr901);
  assert(component.get_source_url() == pr901);
#ifdef USE_ESP32
  assert(!component.manifest_result_matches(pr901));
  complete_manifest(&component);
#endif
  assert(component.manifest_result_matches(pr901));
  component.set_source_url(pr900.c_str());
  assert(!component.manifest_result_matches(pr901));
  assert(!component.manifest_result_matches(pr900));
  component.set_source_url(pr901.c_str());
#ifdef USE_ESP32
  task_create_ok = false;
  component.check();
  assert(component.task_result_ == nullptr);
  assert(component.error == "Failed to start manifest check");
  task_create_ok = true;
  component.check();
  assert(component.task_result_ != nullptr);
  complete_manifest(&component);
#endif
#ifdef USE_OPENQUATT_WEATHER
  using esphome::openquatt_weather::https_slot;
  // A queued manifest allocates no worker or URL copy while the weather
  // request owns the slot. Repeated checks leave only one retry.
  assert(https_slot.try_acquire());
  component.set_source_url(pr900.c_str());
  const unsigned starts_before_weather = task_starts;
  component.check();
  component.check();
  assert(task_starts == starts_before_weather && component.timeout);
  assert(component.task_result_ == nullptr);
  assert(component.manifest_check_timeout_ms() > 20000);
  component.fire_timeout();
  assert(task_starts == starts_before_weather && component.timeout);
  https_slot.release();
  assert(!https_slot.try_acquire_weather());
  component.fire_timeout();
  assert(task_starts == starts_before_weather + 1 && !component.timeout);
  assert(component.task_result_->source_url == pr900 && component.manifest_check_timeout_ms() == 20000);
  assert(!https_slot.try_acquire());
  complete_manifest(&component);
  assert(https_slot.try_acquire());
  component.check();
  esphome::network::connected = false;
  https_slot.release();
  component.fire_timeout();
  assert(task_starts == starts_before_weather + 1 && !component.timeout);
  assert(https_slot.try_acquire_weather());
  https_slot.release();
  esphome::network::connected = true;
  assert(https_slot.try_acquire());
  component.check();
  component.set_manifest_check_paused([]() { return true; });
  https_slot.release();
  component.fire_timeout();
  assert(task_starts == starts_before_weather + 1 && !component.timeout);
  assert(component.manifest_check_timeout_ms() == 20000);
#endif
}
