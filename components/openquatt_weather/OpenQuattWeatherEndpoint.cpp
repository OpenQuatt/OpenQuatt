#include "OpenQuattWeather.h"

#include "WeatherForecastJson.h"
#include "OpenQuattHttpStatus.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esp_timer.h"
#include "lwip/sockets.h"

namespace esphome::openquatt_weather {
namespace {

class WeatherForecastRequestHandler : public AsyncWebHandler {
 public:
  explicit WeatherForecastRequestHandler(OpenQuattWeather* parent) : parent_(parent) {}
  bool canHandle(AsyncWebServerRequest* request) const override {
    constexpr const char* path = "/openquatt/weather/forecast";
    const httpd_req_t* raw = *request;
    const char* uri = raw ? raw->uri : nullptr;
    constexpr size_t size = sizeof("/openquatt/weather/forecast") - 1;
    // Canonical ASCII route only. Match request-owned bytes without the SDK's
    // fixed 513B decoded URL copy; encoded paths and suffixes never alias it.
    return request->method() == HTTP_GET && uri && std::strncmp(uri, path, size) == 0 &&
           (uri[size] == '\0' || uri[size] == '?');
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
    if (!snapshots.begin_request(static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL)) {
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
  OpenQuattWeather* parent_;
};

}  // namespace

void OpenQuattWeather::setup_forecast_endpoint_() {
  if (web_server_base::global_web_server_base)
    web_server_base::global_web_server_base->add_handler(new WeatherForecastRequestHandler(this));
}

}  // namespace esphome::openquatt_weather
