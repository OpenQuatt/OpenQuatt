#include "OpenQuattWeather.h"

#include "WeatherForecastRecordingJson.h"
#include "OpenQuattHttpStatus.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esp_timer.h"
#include "lwip/sockets.h"

namespace esphome::openquatt_weather {
namespace {

bool matches_route(AsyncWebServerRequest* request, const char* path, size_t size) {
  const httpd_req_t* raw = *request;
  const char* uri = raw ? raw->uri : nullptr;
  // Match the canonical ASCII path without the SDK's decoded URL copy.
  return request->method() == HTTP_GET && uri && std::strncmp(uri, path, size) == 0 &&
         (uri[size] == '\0' || uri[size] == '?');
}

constexpr char FORECAST_PATH[] = "/openquatt/weather/forecast";
constexpr char RECORDING_PATH[] = "/openquatt/weather/recording";
constexpr char RECORDING_STATUS_PATH[] = "/openquatt/weather/recording/status";

class WeatherForecastRequestHandler : public AsyncWebHandler {
 public:
  explicit WeatherForecastRequestHandler(OpenQuattWeather* parent) : parent_(parent) {}
  bool canHandle(AsyncWebServerRequest* request) const override {
    return matches_route(request, FORECAST_PATH, sizeof(FORECAST_PATH) - 1) ||
           matches_route(request, RECORDING_PATH, sizeof(RECORDING_PATH) - 1) ||
           matches_route(request, RECORDING_STATUS_PATH, sizeof(RECORDING_STATUS_PATH) - 1);
  }
  void handleRequest(AsyncWebServerRequest* request) override {
    auto* auth = parent_->web_auth();
    if (!auth || !auth->request_is_authenticated(request)) {
      request->requestAuthentication();
      return;
    }
    auto& snapshots = parent_->forecast_endpoint();
    if (!snapshots.available()) {
      openquatt_common::send_http_status(*request, openquatt_common::HttpStatus::SERVICE_UNAVAILABLE,
                                         R"({"ok":false,"error":"psram_unavailable"})");
      return;
    }
    if (matches_route(request, RECORDING_PATH, sizeof(RECORDING_PATH) - 1)) {
      this->write_recording_(*request);
      return;
    }
    if (matches_route(request, RECORDING_STATUS_PATH, sizeof(RECORDING_STATUS_PATH) - 1)) {
      this->write_recording_status_(*request);
      return;
    }
    if (!snapshots.begin_request([] { return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL; })) {
      openquatt_common::send_http_status(*request, openquatt_common::HttpStatus::TOO_MANY_REQUESTS,
                                         R"({"ok":false,"error":"request_busy"})");
      return;
    }
    struct EndRequest {
      ForecastSnapshotAccess& snapshots;
      ~EndRequest() { snapshots.end_request(); }
    } end{snapshots};
    httpd_req_t* req = *request;
    const bool ok =
        httpd_resp_set_status(req, HTTPD_200) == ESP_OK &&
        httpd_resp_set_type(req, "application/json; charset=utf-8") == ESP_OK &&
        httpd_resp_set_hdr(req, "Cache-Control", "no-store") == ESP_OK &&
        write_forecast_json(snapshots.request(),
                            [req](const char* bytes, size_t size) {
                              return httpd_resp_send_chunk(req, bytes, static_cast<ssize_t>(size)) == ESP_OK;
                            }) &&
        httpd_resp_send_chunk(req, nullptr, 0) == ESP_OK;
    // Never reuse a connection containing a partially transmitted JSON object.
    if (!ok) shutdown(httpd_req_to_sockfd(req), SHUT_RDWR);
  }

 private:
  void write_recording_status_(httpd_req_t* req) {
    const auto status = parent_->forecast_recording().summary(esp_timer_get_time() / 1000U);
    if (!status.available) {
      openquatt_common::send_http_status(req, openquatt_common::HttpStatus::SERVICE_UNAVAILABLE,
                                         R"({"ok":false,"error":"psram_unavailable"})");
      return;
    }
    char body[64];
    const int size = std::snprintf(body, sizeof(body), R"({"count":%u,"duration_s":%u})",
                                   static_cast<unsigned>(status.count), static_cast<unsigned>(status.duration_s));
    if (httpd_resp_set_type(req, "application/json") != ESP_OK ||
        httpd_resp_set_hdr(req, "Cache-Control", "no-store") != ESP_OK || httpd_resp_send(req, body, size) != ESP_OK)
      shutdown(httpd_req_to_sockfd(req), SHUT_RDWR);
  }

  void write_recording_(httpd_req_t* req) {
    auto& recording = parent_->forecast_recording();
    if (recording_busy_.test_and_set()) {
      openquatt_common::send_http_status(req, openquatt_common::HttpStatus::TOO_MANY_REQUESTS,
                                         R"({"ok":false,"error":"request_busy"})");
      return;
    }
    struct ReleaseExport {
      std::atomic_flag& busy;
      ~ReleaseExport() { busy.clear(); }
    } release{recording_busy_};
    ForecastRecording::Snapshot snapshot;
    const uint64_t started_us = esp_timer_get_time();
    const auto result = recording.capture(started_us / 1000U, snapshot);
    if (result != ForecastRecording::Result::READY) {
      const bool empty = result == ForecastRecording::Result::EMPTY;
      openquatt_common::send_http_status(
          req, empty ? openquatt_common::HttpStatus::GONE : openquatt_common::HttpStatus::SERVICE_UNAVAILABLE,
          empty ? R"({"ok":false,"error":"recording_empty"})" : R"({"ok":false,"error":"psram_unavailable"})");
      return;
    }
    const auto current = [&] { return recording.current(snapshot.revision); };
    const bool ok =
        current() && httpd_resp_set_status(req, HTTPD_200) == ESP_OK &&
        httpd_resp_set_type(req, "application/json; charset=utf-8") == ESP_OK &&
        httpd_resp_set_hdr(req, "Cache-Control", "no-store") == ESP_OK &&
        httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=openquatt-forecast-measurements.json") ==
            ESP_OK &&
        write_forecast_recording(snapshot, parent_->boot_id(),
                                 [&](const char* bytes, size_t length) {
                                   return static_cast<uint64_t>(esp_timer_get_time()) - started_us < 15000000U &&
                                          current() &&
                                          httpd_resp_send_chunk(req, bytes, static_cast<ssize_t>(length)) == ESP_OK;
                                 }) &&
        current() && httpd_resp_send_chunk(req, nullptr, 0) == ESP_OK;
    if (!ok) shutdown(httpd_req_to_sockfd(req), SHUT_RDWR);
  }

  std::atomic_flag recording_busy_ = ATOMIC_FLAG_INIT;
  OpenQuattWeather* parent_;
};

}  // namespace

void OpenQuattWeather::setup_forecast_endpoint_() {
  if (web_server_base::global_web_server_base)
    web_server_base::global_web_server_base->add_handler(new WeatherForecastRequestHandler(this));
}

}  // namespace esphome::openquatt_weather
