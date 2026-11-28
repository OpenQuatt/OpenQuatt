#include "openquatt_ble.h"

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "OpenQuattHttpStatus.h"
#include "PsramBuffer.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"

namespace esphome::openquatt_ble {
namespace {

using openquatt_common::HttpStatus;
using openquatt_common::send_http_status;

constexpr char STATUS_PATH[] = "/openquatt/ble/status";
constexpr char SCAN_PATH[] = "/openquatt/ble/scan";
constexpr char SAVE_PATH[] = "/openquatt/ble/save";
constexpr size_t MAX_POST_BYTES = 512;
constexpr size_t MAX_STATUS_BYTES = 6144;

const char* json_bool(bool value) { return value ? "true" : "false"; }

const char* protocol_name(Protocol protocol) {
  switch (protocol) {
    case Protocol::BTHOME:
      return "bthome_v2";
    case Protocol::ATC1441:
      return "atc1441";
    case Protocol::PVVX:
      return "pvvx_custom";
    default:
      return "";
  }
}

Protocol parse_protocol(std::string_view value) {
  if (value == "bthome_v2") return Protocol::BTHOME;
  if (value == "atc1441") return Protocol::ATC1441;
  if (value == "pvvx_custom") return Protocol::PVVX;
  return static_cast<Protocol>(0);
}

int hex_digit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  return -1;
}

uint64_t parse_mac(std::string_view text) {
  if (text.size() != 17) return 0;
  uint64_t value = 0;
  for (size_t i = 0; i < 6; ++i) {
    const int high = hex_digit(text[3 * i]);
    const int low = hex_digit(text[3 * i + 1]);
    if (high < 0 || low < 0 || (i < 5 && text[3 * i + 2] != ':')) return 0;
    value = (value << 8) | static_cast<unsigned>((high << 4) | low);
  }
  return value;
}

bool parse_revision(std::string_view text, uint32_t& revision) {
  if (text.empty() || text.size() > 10) return false;
  uint64_t value = 0;
  for (const char ch : text) {
    if (ch < '0' || ch > '9') return false;
    value = value * 10 + static_cast<unsigned>(ch - '0');
  }
  if (value > UINT32_MAX) return false;
  revision = static_cast<uint32_t>(value);
  return true;
}

bool parse_flag(std::string_view text, bool& value) {
  if (text != "0" && text != "1") return false;
  value = text == "1";
  return true;
}

class StatusJson {
 public:
  explicit StatusJson(char (&buffer)[MAX_STATUS_BYTES]) : buffer_(buffer) {}

  bool format(const char* pattern, ...) __attribute__((format(printf, 2, 3))) {
    const size_t available = MAX_STATUS_BYTES - used_;
    va_list args;
    va_start(args, pattern);
    const int count = std::vsnprintf(buffer_ + used_, available, pattern, args);
    va_end(args);
    if (count < 0 || static_cast<size_t>(count) >= available) return false;
    used_ += static_cast<size_t>(count);
    return true;
  }

  bool string(std::string_view value) {
    if (!format("\"")) return false;
    for (const char ch : value) {
      const auto byte = static_cast<unsigned char>(ch);
      if (ch == '"' || ch == '\\') {
        if (!format("\\%c", ch)) return false;
      } else if (byte < 0x20) {
        if (!format("\\u%04x", static_cast<unsigned>(byte))) return false;
      } else if (!format("%c", ch)) {
        return false;
      }
    }
    return format("\"");
  }

  bool mac(uint64_t value) {
    if (value == 0) return format("null");
    char address[18];
    std::snprintf(address, sizeof(address), "%02X:%02X:%02X:%02X:%02X:%02X",
                  static_cast<unsigned>((value >> 40) & 0xff), static_cast<unsigned>((value >> 32) & 0xff),
                  static_cast<unsigned>((value >> 24) & 0xff), static_cast<unsigned>((value >> 16) & 0xff),
                  static_cast<unsigned>((value >> 8) & 0xff), static_cast<unsigned>(value & 0xff));
    return string(address);
  }

  bool number(float value) {
    return std::isfinite(value) ? format("%.7g", static_cast<double>(value)) : format("null");
  }

  bool age(uint32_t age_ms) {
    return age_ms == UINT32_MAX ? format("null") : format("%u", static_cast<unsigned>(age_ms / 1000));
  }

  size_t size() const { return used_; }

 private:
  char* buffer_;
  size_t used_{0};
};

bool write_sensor(StatusJson& json, uint64_t mac, Protocol protocol, const Reading& reading,
                  const PairedSensor* sensor) {
  return json.format("{\"mac\":") && json.mac(mac) && json.format(",\"protocol\":") &&
         json.string(protocol_name(protocol)) &&
         (!reading.advertised_name[0] ||
          (json.format(",\"advertised_name\":") && json.string(reading.advertised_name))) &&
         (!sensor || (json.format(",\"name\":") && json.string(sensor->name) &&
                      json.format(",\"use_dew_point\":%s", json_bool(sensor->use_dew_point)))) &&
         json.format(",\"temperature_c\":") && json.number(reading.temperature) && json.format(",\"humidity_pct\":") &&
         json.number(reading.humidity) && json.format(",\"dew_point_c\":") && json.number(reading.dew_point) &&
         json.format(",\"age_s\":") && json.age(reading.temperature_age_ms) && json.format(",\"pair_age_s\":") &&
         json.age(reading.pair_age_ms) && json.format(",\"rssi_dbm\":") &&
         (reading.rssi_dbm == INT16_MIN ? json.format("null")
                                        : json.format("%d", static_cast<int>(reading.rssi_dbm))) &&
         json.format("}");
}

bool write_status(StatusJson& json, const BleSnapshot& state, std::string_view csrf_token) {
  size_t required_count = 0;
  size_t valid_count = 0;
  uint64_t max_mac = 0;
  float max_dew = NAN;
  for (size_t i = 0; i < state.config.count; ++i) {
    if (!state.config.paired[i].use_dew_point) continue;
    ++required_count;
    const float dew = state.paired[i].dew_point;
    if (!std::isfinite(dew)) continue;
    ++valid_count;
    if (max_mac == 0 || dew > max_dew) {
      max_dew = dew;
      max_mac = state.config.paired[i].mac;
    }
  }
  const bool dew_valid = std::isfinite(state.dew_point);
  if (!json.format("{\"ok\":true,\"enabled\":%s,\"ready\":%s,\"scanning\":%s,\"pending\":%s,\"storage_error\":%s,"
                   "\"revision\":%u,\"max_sensors\":%zu,\"max_name_bytes\":%zu,\"room_mac\":",
                   json_bool(state.config.enabled), json_bool(state.ready), json_bool(state.searching),
                   json_bool(state.pending), json_bool(state.storage_error),
                   static_cast<unsigned>(state.config.revision), sizeof(state.config.paired) / sizeof(PairedSensor),
                   sizeof(PairedSensor::name) - 1) ||
      !json.mac(state.config.room_mac) ||
      !json.format(",\"dew_required\":%s,\"dew_valid\":%s,\"required_count\":%zu,\"valid_count\":%zu,\"dew_point_c\":",
                   json_bool(state.dew_required), json_bool(dew_valid), required_count, valid_count) ||
      !json.number(state.dew_point) || !json.format(",\"dew_max_mac\":") || !json.mac(dew_valid ? max_mac : 0) ||
      !json.format(",\"csrf_token\":") || !json.string(csrf_token) || !json.format(",\"sensors\":["))
    return false;

  for (size_t i = 0; i < state.config.count; ++i) {
    const auto& sensor = state.config.paired[i];
    if ((i && !json.format(",")) || !write_sensor(json, sensor.mac, sensor.protocol, state.paired[i], &sensor))
      return false;
  }
  if (!json.format("],\"discovered\":[")) return false;
  for (size_t i = 0; i < state.discovered_count; ++i) {
    const auto& reading = state.discovered[i];
    if ((i && !json.format(",")) || !write_sensor(json, reading.mac, reading.protocol, reading, nullptr)) return false;
  }
  return json.format("]}");
}

size_t find_paired(const BleConfig& config, uint64_t mac) {
  size_t i = 0;
  for (; i < config.count; ++i)
    if (config.paired[i].mac == mac) break;
  return i;
}

// Update the request's private snapshot. The component validates it again
// against the authoritative state before committing in the main loop.
bool apply_form(AsyncWebServerRequest& request, BleConfig& config) {
  const auto action = request.arg("action");
  if (action == "enable") return parse_flag(request.arg("enabled"), config.enabled);

  if (!request.hasArg("mac")) return false;
  const auto address = request.arg("mac");
  if (action == "room" && address.empty()) {
    config.room_mac = 0;
    return true;
  }
  const uint64_t mac = parse_mac(address);
  if (mac == 0) return false;
  const size_t i = find_paired(config, mac);
  if (action == "room") {
    if (i == config.count) return false;
    config.room_mac = mac;
    return true;
  }
  if (action == "remove") {
    if (i == config.count) return false;
    for (size_t j = i + 1; j < config.count; ++j) config.paired[j - 1] = config.paired[j];
    config.paired[--config.count] = {};
    if (config.room_mac == mac) config.room_mac = 0;
    return true;
  }
  if (action != "upsert" || i >= sizeof(config.paired) / sizeof(PairedSensor)) return false;
  const auto name = request.arg("name");
  const auto protocol = parse_protocol(request.arg("protocol"));
  bool use_dew_point = false;
  if (!valid_sensor_name(name) || protocol == static_cast<Protocol>(0) ||
      !parse_flag(request.arg("use_dew_point"), use_dew_point))
    return false;
  auto& sensor = config.paired[i];
  sensor = {};
  sensor.mac = mac;
  sensor.protocol = protocol;
  std::memcpy(sensor.name, name.data(), name.size());
  sensor.use_dew_point = use_dew_point;
  if (i == config.count) ++config.count;
  return true;
}

class BleRequestHandler final : public AsyncWebHandler {
 public:
  explicit BleRequestHandler(OpenQuattBle& parent) : parent_(parent) { scratch_.allocate(); }

  bool canHandle(AsyncWebServerRequest* request) const override {
    const httpd_req_t* raw = *request;
    if (!raw || !raw->uri) return false;
    const std::string_view path(raw->uri);
    return (request->method() == HTTP_GET && path == STATUS_PATH) ||
           (request->method() == HTTP_POST && (path == SAVE_PATH || path == SCAN_PATH));
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    auto* auth = parent_.web_auth();
    if (!auth || !auth->request_is_authenticated(request)) {
      request->requestAuthentication();
      return;
    }
    httpd_req_t* raw = *request;
    const bool writing = request->method() == HTTP_POST;
    if (writing) {
      if (!raw || raw->content_len == 0 || raw->content_len > MAX_POST_BYTES) {
        send_invalid(*request);
        return;
      }
      const auto* base = web_server_base::global_web_server_base;
      if (!base || base->is_recovery_active() || !auth->request_allows_write(request)) {
        send_http_status(raw, HttpStatus::FORBIDDEN, R"({"ok":false,"error":"forbidden"})");
        return;
      }
    }
    if (!scratch_.data()) {
      send_unavailable(raw);
      return;
    }
    if (in_use_.test_and_set(std::memory_order_acquire)) {
      send_http_status(raw, HttpStatus::TOO_MANY_REQUESTS, R"({"ok":false,"error":"configuration_busy"})");
      return;
    }
    const ReleaseScratch release{in_use_};
    auto& scratch = scratch_[0];
    if (!parent_.snapshot(scratch.state)) {
      send_unavailable(raw);
      return;
    }
    if (!writing) {
      StatusJson json(scratch.response);
      if (!write_status(json, scratch.state, auth->get_csrf_token())) {
        send_unavailable(raw);
        return;
      }
      if (httpd_resp_set_type(raw, "application/json; charset=utf-8") != ESP_OK ||
          httpd_resp_set_hdr(raw, "Cache-Control", "no-store") != ESP_OK ||
          httpd_resp_send(raw, scratch.response, json.size()) != ESP_OK)
        shutdown(httpd_req_to_sockfd(raw), SHUT_RDWR);
      return;
    }

    uint32_t revision;
    if (!parse_revision(request->arg("revision"), revision)) {
      send_invalid(*request);
      return;
    }
    if (revision != scratch.state.config.revision) {
      send_result(*request, Submit::CONFLICT, revision);
      return;
    }
    if (std::strcmp(raw->uri, SCAN_PATH) == 0) {
      bool active;
      if (!parse_flag(request->arg("active"), active)) {
        send_invalid(*request);
        return;
      }
      send_result(*request, parent_.submit_scan(active, revision), revision);
      return;
    }
    if (revision == UINT32_MAX || !apply_form(*request, scratch.state.config)) {
      send_invalid(*request);
      return;
    }
    send_result(*request, parent_.submit_config(scratch.state.config, revision), revision + 1);
  }

 private:
  struct Scratch {
    BleSnapshot state;
    char response[MAX_STATUS_BYTES];
  };

  struct ReleaseScratch {
    std::atomic_flag& flag;
    ~ReleaseScratch() { flag.clear(std::memory_order_release); }
  };

  static void send_invalid(AsyncWebServerRequest& request) {
    request.send(400, "application/json", R"({"ok":false,"error":"invalid_configuration"})");
  }

  static void send_unavailable(httpd_req_t* request) {
    send_http_status(request, HttpStatus::SERVICE_UNAVAILABLE, R"({"ok":false,"error":"ble_unavailable"})");
  }

  static void send_result(AsyncWebServerRequest& request, Submit result, uint32_t revision) {
    switch (result) {
      case Submit::ACCEPTED: {
        char json[64];
        std::snprintf(json, sizeof(json), R"({"ok":true,"pending":true,"revision":%u})",
                      static_cast<unsigned>(revision));
        send_http_status(request, HttpStatus::ACCEPTED, json);
        break;
      }
      case Submit::CONFLICT:
        request.send(409, "application/json", R"({"ok":false,"error":"stale_configuration"})");
        break;
      case Submit::BUSY:
        request.send(409, "application/json", R"({"ok":false,"error":"configuration_busy"})");
        break;
      case Submit::INVALID:
        send_invalid(request);
        break;
      case Submit::UNAVAILABLE:
        send_unavailable(request);
        break;
    }
  }

  OpenQuattBle& parent_;
  openquatt_common::PsramObjectArray<Scratch, 1> scratch_;
  std::atomic_flag in_use_ = ATOMIC_FLAG_INIT;
};

}  // namespace

void OpenQuattBle::register_http_handlers() {
  auto* base = web_server_base::global_web_server_base;
  if (base) base->add_handler(new BleRequestHandler(*this));
}

}  // namespace esphome::openquatt_ble
