// Actual generated OTA admission, flash() and update.perform(); platform I/O is fake.
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#ifdef USE_OPENQUATT_WEATHER
#include "WeatherHttpsSlot.h"
#endif

#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)

uint32_t clock_ms = 0, released_after = UINT32_MAX, wait_started = 0;
unsigned get_calls = 0, writes = 0, feeds = 0, starts = 0, errors = 0;
unsigned image_closes = 0;
int image_status = 200;
bool weather_live = false;
bool image_tls_live = false;
uint32_t millis() { return clock_ms; }
void delay(uint32_t ms) {
  clock_ms += ms;
#ifdef USE_OPENQUATT_WEATHER
  if (weather_live && static_cast<uint32_t>(clock_ms - wait_started) >= released_after) {
    weather_live = false;  // Worker has destroyed TLS before releasing its lease.
    esphome::openquatt_weather::https_slot.release();
  }
#endif
}
struct Application {
  void feed_wdt() { ++feeds; }
  void safe_reboot() { assert(false && "No image may be activated by this regression"); }
} App;

namespace esphome {
namespace ota {
enum OTAState { OTA_STARTED, OTA_COMPLETED, OTA_ERROR };
constexpr uint8_t OTA_RESPONSE_OK = 0;
}  // namespace ota
namespace http_request {
[[maybe_unused]] constexpr uint8_t OTA_CONNECTION_ERROR = 131;
constexpr int HTTP_STATUS_OK = 200;
struct Container {
  int status_code;
  void end() {
    assert(image_tls_live);
#ifdef USE_OPENQUATT_WEATHER
    assert(!openquatt_weather::https_slot.try_acquire());
#endif
    image_tls_live = false;
    ++image_closes;
  }
  // Like the pinned SDK, destroying the wrapper alone does not end its client.
};
std::shared_ptr<Container> image_get() {
  ++get_calls;
  if (image_status == -1) return nullptr;
  image_tls_live = true;
  return std::make_shared<Container>(Container{image_status});
}
class OtaHttpRequestComponent {
 public:
  std::string url_, md5_computed_, md5_expected_;
  uint8_t response = 139;
  void set_url(const std::string& value) { url_ = value; }
  void set_md5(const std::string& value) { md5_expected_ = value; }
  void notify_state_(ota::OTAState state, float, uint8_t) {
    if (state == ota::OTA_STARTED) ++starts;
    if (state == ota::OTA_ERROR) ++errors;
  }
  uint8_t do_ota_();
  void flash();
};

// GENERATED_FLASH

uint8_t OtaHttpRequestComponent::do_ota_() {
  // GENERATED_ADMISSION
  auto container = image_get();
  // GENERATED_IMAGE_ERROR
  assert(!weather_live);
#ifdef USE_OPENQUATT_WEATHER
  assert(!openquatt_weather::https_slot.try_acquire());
#endif
  ++writes;
  container->end();
  return response;  // Exercises lease release on both ordinary error returns.
}
}  // namespace http_request
namespace update {
enum UpdateState { UPDATE_STATE_AVAILABLE, UPDATE_STATE_INSTALLING };
}  // namespace update
namespace http_request {
class HttpRequestUpdate {
 public:
  update::UpdateState state_ = update::UPDATE_STATE_AVAILABLE;
  OtaHttpRequestComponent* ota_parent_;
  struct {
    std::string md5 = "00000000000000000000000000000000", firmware_url = "https://example.org/firmware.bin";
  } update_info;
  std::function<void()> queued;
  void publish_state() {}
  void defer(std::function<void()>&& action) { queued = std::move(action); }
  void perform(bool force);
};

// GENERATED_PERFORM
}  // namespace http_request
}  // namespace esphome

void dispatch(bool update_path, esphome::http_request::OtaHttpRequestComponent& ota) {
  if (update_path) {
    esphome::http_request::HttpRequestUpdate update;
    update.ota_parent_ = &ota;
    update.perform(false);
    assert(update.queued && update.state_ == esphome::update::UPDATE_STATE_INSTALLING);
    update.queued();
  } else {
    ota.set_url("https://example.org/firmware.bin");
    ota.set_md5("00000000000000000000000000000000");
    ota.flash();
  }
}

int main() {
  for (bool update_path : {false, true}) {
    for (uint8_t response : {uint8_t{139}, uint8_t{131}}) {
      esphome::http_request::OtaHttpRequestComponent ota;
      ota.response = response;
      get_calls = writes = feeds = starts = errors = 0;
      image_status = 200;
      image_closes = 0;
      clock_ms = update_path ? UINT32_MAX - 250 : 0;
      wait_started = clock_ms;
#ifdef USE_OPENQUATT_WEATHER
      assert(esphome::openquatt_weather::https_slot.try_acquire_weather());
      weather_live = true;
      released_after = 500;
#endif
      dispatch(update_path, ota);
      assert(get_calls == 1 && writes == 1 && starts == 1 && errors == 1);
      assert(image_closes == 1 && !image_tls_live);
#ifdef USE_OPENQUATT_WEATHER
      assert(clock_ms - wait_started == 500 && feeds == 500);
      assert(esphome::openquatt_weather::https_slot.try_acquire());
      esphome::openquatt_weather::https_slot.release();
      // A busy manifest/worker that cannot finish must prevent all GET/backend work.
      for (uint32_t release : {UINT32_MAX, uint32_t{2000}}) {
        assert(esphome::openquatt_weather::https_slot.try_acquire());
        weather_live = true;
        released_after = release;
        wait_started = clock_ms;
        get_calls = writes = feeds = 0;
        dispatch(update_path, ota);
        assert(clock_ms - wait_started == 2000 && feeds == 2000 && get_calls == 0 && writes == 0);
        if (weather_live) {
          assert(!esphome::openquatt_weather::https_slot.try_acquire());
          weather_live = false;
          esphome::openquatt_weather::https_slot.release();
        }
      }
#else
      assert(clock_ms == wait_started && feeds == 0);
#endif
      for (int rejected_status : {404, 500, -1}) {
        image_status = rejected_status;
        get_calls = writes = image_closes = 0;
        dispatch(update_path, ota);
        assert(get_calls == 1 && writes == 0 && !image_tls_live);
        assert(image_closes == (rejected_status == -1 ? 0U : 1U));
#ifdef USE_OPENQUATT_WEATHER
        assert(esphome::openquatt_weather::https_slot.try_acquire());
        esphome::openquatt_weather::https_slot.release();
#endif
      }
    }
  }
}
