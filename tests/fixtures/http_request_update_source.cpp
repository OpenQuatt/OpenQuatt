// The Python regression inserts the generated ESPHome class, update() and URL
// resolution code here. Only platform dependencies and scheduling are stubbed.
#include <cassert>
#include <cstring>
#include <string>

#define LOG_STR(value) value
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)

using TaskHandle_t = void*;
static constexpr int pdPASS = 1;
static bool task_create_ok = true;
static unsigned task_starts = 0;
int xTaskCreate(void (*)(void*), const char*, unsigned, void*, unsigned, TaskHandle_t* handle) {
  ++task_starts;
  *handle = task_create_ok ? reinterpret_cast<void*>(1) : nullptr;
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
  std::string error;
};
namespace update {
class UpdateEntity {
 public:
  virtual ~UpdateEntity() = default;
  virtual void perform(bool) {}
  virtual void check() {}
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

std::string resolve_url(HttpRequestUpdate& component, const std::string& path) {
  auto* this_update = &component;
  struct {
    std::string firmware_url;
  } result{path};
  auto* info = &result;
  // GENERATED_RELATIVE_URL
  return result.firmware_url;
}
}  // namespace esphome::http_request

int main() {
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
  assert(component.source_url_ == pr900);
  esphome::network::connected = false;
  component.check();
  assert(component.task_source_url_.empty());
  esphome::network::connected = true;
  component.check();
  assert(component.task_source_url_ == pr900);
  const char* active = component.task_source_url_.c_str();
  component.set_source_url(std::string(pr901).c_str());
  assert(component.source_url_ == pr901);
  assert(component.task_source_url_ == pr900);
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
  assert(component.task_source_url_ == pr900);
  component.update_task_handle_ = nullptr;
#endif
  component.check();
  assert(component.task_source_url_ == pr901);
#ifdef USE_ESP32
  component.update_task_handle_ = nullptr;
  task_create_ok = false;
  component.check();
  assert(component.update_task_handle_ == nullptr);
  assert(component.error == "Failed to start manifest check");
  task_create_ok = true;
  component.check();
  assert(component.update_task_handle_ != nullptr);
#endif
}
