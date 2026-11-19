#include "OpenQuattMeasurementArchive.h"
#include "MeasurementArchiveExport.h"
#include "OpenQuattHttpStatus.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include <cstdio>

namespace esphome::openquatt_measurement_archive {
namespace {
using namespace oq_measurement_archive;
constexpr char kDownloadPath[] = "/openquatt/measurement-archive/download";

class DownloadHandler : public AsyncWebHandler {
 public:
  explicit DownloadHandler(MeasurementArchive& archive) : archive_(archive) {}
  bool canHandle(AsyncWebServerRequest* request) const override {
    const httpd_req_t* raw = *request;
    constexpr size_t length = sizeof(kDownloadPath) - 1;
    return request->method() == HTTP_GET && raw && std::strncmp(raw->uri, kDownloadPath, length) == 0 &&
           (raw->uri[length] == '\0' || raw->uri[length] == '?');
  }
  void handleRequest(AsyncWebServerRequest* request) override {
    if (!archive_.downloads_ready()) {
      request->send(503);
      return;
    }
    const auto* auth = archive_.web_auth();
    if (!auth || !auth->request_is_authenticated(request)) {
      request->requestAuthentication();
      return;
    }
    const auto date = request->arg("date");
    archive_.download(*request, date.c_str());
  }

 private:
  MeasurementArchive& archive_;
};

struct Export {
  SectorIndex measurements[kMeasurementSectors];
  SectorIndex weather[kWeatherSectors];
  uint8_t sector[kSectorBytes];
  Context context;
  bool used_contexts[kContextCapacity]{};
  char body[128 * 1024];
  size_t size = 0;

  template <typename... Args>
  bool append(const char* format, Args... args) {
    const int n = std::snprintf(body + size, sizeof(body) - size, format, args...);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(body) - size) return false;
    size += n;
    return true;
  }
  bool block(const Measurement& r, uint32_t context_id, bool first) {
    if (!context_id || context_id > kContextCapacity) return false;
    used_contexts[context_id - 1] = true;
    return append("%s[%u,%u,%d,%d,%d,%d,%d,%u,%u,%u,%u]", first ? "" : ",", static_cast<unsigned>(r.epoch),
                  static_cast<unsigned>(context_id), static_cast<int>(r.heat_tenth_wh), r.room_start_centi,
                  r.room_end_centi, r.outside_mean_centi, r.setpoint_centi, r.duration_s, r.heat_valid_s,
                  r.temperature_valid_s, r.flags);
  }
  bool forecast(const Weather& r, bool first) {
    if (!append("%s[%u,%u,%u,%u,%u,%u", first ? "" : ",", static_cast<unsigned>(r.epoch),
                static_cast<unsigned>(r.first_hour_epoch), static_cast<unsigned>(r.current_epoch),
                r.current_speed_centi, r.current_direction_centi, r.valid_mask))
      return false;
    for (size_t index = 0; index < 5; ++index)
      if (!append(",[%d,%u,%u]", r.outside_centi[index], r.speed_centi[index], r.direction_two_degrees[index]))
        return false;
    return append("]");
  }
};

void export_error(httpd_req_t* request, const char* status, const char* error) {
  if (httpd_resp_set_status(request, status) != ESP_OK ||
      httpd_resp_set_type(request, "application/json; charset=utf-8") != ESP_OK ||
      httpd_resp_set_hdr(request, "Cache-Control", "no-store") != ESP_OK ||
      httpd_resp_sendstr(request, error) != ESP_OK)
    shutdown(httpd_req_to_sockfd(request), SHUT_RDWR);
}
}  // namespace

void MeasurementArchive::setup_endpoint() {
  if (web_server_base::global_web_server_base)
    web_server_base::global_web_server_base->add_handler(new DownloadHandler(*this));
}

void MeasurementArchive::enable_downloads() {
#ifdef USE_WEBSERVER_AUTH
  if (!web_auth_ || web_auth_->get_credential_source().empty()) return;
#endif
  downloads_ready_.store(true);
}

void MeasurementArchive::download(httpd_req_t* request, const char* date) {
  uint32_t start;
  if (!utc_day(date, start)) {
    export_error(request, HTTPD_400, R"({"error":"invalid_utc_date"})");
    return;
  }
  if (orphaned_) {
    export_error(request, "410 Gone", R"({"error":"archive_unlinked"})");
    return;
  }
  openquatt_common::PsramObjectArray<Export, 1> buffer;
  if (!index_ready_ || !buffer.allocate()) {
    export_error(request, "503 Service Unavailable", R"({"error":"storage_unavailable"})");
    return;
  }
  auto& out = buffer[0];
  size_t measure_first, weather_first;
  {
    const std::unique_lock<std::mutex> lock(index_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
      export_error(request, "503 Service Unavailable", R"({"error":"archive_busy"})");
      return;
    }
    measure_first = state_[0].measurements.copy_index(out.measurements);
    weather_first = state_[0].weather.copy_index(out.weather);
  }
  const uint32_t until = start + kDaySeconds;
  bool first = true;
  const bool complete =
      out.append(
          R"({"schema":1,"date_utc":"%s","measurement_fields":["end_epoch_s","heat_context_id","heat_tenth_wh","room_start_centi_c","room_end_centi_c","outside_mean_centi_c","setpoint_centi_c","duration_s","heat_valid_s","temperature_valid_s","flags"],"measurements":[)",
          date) &&
      Measurements::visit_snapshot(*this, out.measurements, measure_first, start, until, out.sector,
                                   [&](const Measurement& row, const Header& header) {
                                     const bool ok = out.block(row, header.context, first);
                                     first = false;
                                     return ok;
                                   });
  if (!complete || !out.append(R"(],"heat_contexts":[)")) {
    export_error(request, "503 Service Unavailable", R"({"error":"archive_snapshot_unavailable"})");
    return;
  }
  first = true;
  for (size_t i = 0; i < kContextCapacity; ++i) {
    if (!out.used_contexts[i]) continue;
    HeatRoute route;
    if (!Contexts::read_heat_route(*this, i + 1, out.context, route) ||
        !out.append(
            R"(%s{"id":%zu,"route":"%s","flow_source":%u,"supply_source":%u,"flow_endpoint":%u,"supply_endpoint":%u,"supply_offset_c":%.6f})",
            first ? "" : ",", i + 1,
            !route.magic                             ? "unknown"
            : route.boundary == HeatBoundary::SYSTEM ? "system"
                                                     : "heat_pumps",
            route.magic ? route.flow_source : 0, route.magic ? route.supply_source : 0,
            route.magic ? static_cast<unsigned>(route.flow_endpoint) : 0,
            route.magic ? static_cast<unsigned>(route.supply_endpoint) : 0,
            route.magic && std::isfinite(route.supply_offset_c) ? route.supply_offset_c : 0.0)) {
      export_error(request, "503 Service Unavailable", R"({"error":"archive_context_unavailable"})");
      return;
    }
    first = false;
  }
  first = true;
  if (!out.append(
          R"(],"weather_fields":["fetched_epoch_s","first_hour_epoch_s","current_wind_epoch_s","current_wind_centi_mps","current_direction_centi_deg","hourly_direction_mask","hour_0","hour_1","hour_2","hour_3","hour_4"],"weather_hour_fields":["outside_centi_c","wind_centi_mps","direction_2deg"],"weather":[)") ||
      !WeatherHistory::visit_snapshot(*this, out.weather, weather_first, start, until, out.sector,
                                      [&](const Weather& row, const Header&) {
                                        const bool ok = out.forecast(row, first);
                                        first = false;
                                        return ok;
                                      }) ||
      !out.append("]}")) {
    export_error(request, "503 Service Unavailable", R"({"error":"archive_snapshot_unavailable"})");
    return;
  }
  if (httpd_resp_set_type(request, "application/json; charset=utf-8") != ESP_OK ||
      httpd_resp_set_hdr(request, "Content-Disposition", "attachment; filename=\"openquatt-meetarchief.json\"") !=
          ESP_OK ||
      httpd_resp_set_hdr(request, "Cache-Control", "no-store") != ESP_OK ||
      httpd_resp_send(request, out.body, out.size) != ESP_OK)
    shutdown(httpd_req_to_sockfd(request), SHUT_RDWR);
}
}  // namespace esphome::openquatt_measurement_archive
