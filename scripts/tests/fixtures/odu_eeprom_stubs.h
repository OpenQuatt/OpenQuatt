#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "openquatt/includes/odu/oq_odu_generation.h"

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NVS_NOT_FOUND = 2;
constexpr int NVS_READONLY = 0, NVS_READWRITE = 1;
constexpr int ESP_PARTITION_TYPE_DATA = 1, ESP_PARTITION_SUBTYPE_DATA_NVS = 2;
using nvs_handle_t = unsigned;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
struct esp_partition_t {
  size_t size{65536U};
};
struct nvs_stats_t {
  size_t available_entries{1400U};
};
struct httpd_req_t {};
inline uint32_t test_now = 1000U;
inline uint32_t millis() { return test_now; }
inline esp_partition_t test_partition{};
inline size_t available_entries = 1400U;
inline bool allocation_ok = true, nvs_write_ok = true, nvs_commit_ok = true, commit_despite_error = false,
            nvs_read_ok = true;
inline bool profile_sync_ok = true;
inline unsigned nvs_commits = 0, revocations = 0;
inline std::map<std::string, std::vector<uint8_t>> durable, pending_storage;
inline const esp_partition_t* esp_partition_find_first(int, int, const char*) { return &test_partition; }
inline int nvs_open(const char*, int mode, nvs_handle_t* handle) {
  if (!nvs_read_ok) return ESP_FAIL;
  if (mode == NVS_READONLY && durable.empty()) return ESP_ERR_NVS_NOT_FOUND;
  *handle = 1U;
  return ESP_OK;
}
inline void nvs_close(nvs_handle_t) {}
inline int nvs_get_stats(const char*, nvs_stats_t* stats) {
  stats->available_entries = available_entries;
  return ESP_OK;
}
inline int nvs_get_blob(nvs_handle_t, const char* key, void* data, size_t* size) {
  if (!nvs_read_ok) return ESP_FAIL;
  const auto found = durable.find(key);
  if (found == durable.end()) return ESP_ERR_NVS_NOT_FOUND;
  if (data != nullptr) {
    if (*size < found->second.size()) return ESP_FAIL;
    std::memcpy(data, found->second.data(), found->second.size());
  }
  *size = found->second.size();
  return ESP_OK;
}
inline int nvs_set_blob(nvs_handle_t, const char* key, const void* data, size_t size) {
  if (!nvs_write_ok) return ESP_FAIL;
  const auto* bytes = static_cast<const uint8_t*>(data);
  pending_storage[key] = std::vector<uint8_t>(bytes, bytes + size);
  return ESP_OK;
}
inline int nvs_commit(nvs_handle_t) {
  ++nvs_commits;
  if (nvs_commit_ok || commit_despite_error)
    for (const auto& [key, bytes] : pending_storage) durable[key] = bytes;
  pending_storage.clear();
  return nvs_commit_ok ? ESP_OK : ESP_FAIL;
}
inline std::string http_output;
inline int httpd_resp_set_type(httpd_req_t*, const char*) { return ESP_OK; }
inline int httpd_resp_set_hdr(httpd_req_t*, const char*, const char*) { return ESP_OK; }
inline int httpd_resp_set_status(httpd_req_t*, const char*) { return ESP_OK; }
inline int httpd_resp_sendstr(httpd_req_t*, const char* data) {
  http_output += data;
  return ESP_OK;
}
inline int httpd_resp_sendstr_chunk(httpd_req_t*, const char* data) {
  http_output += data;
  return ESP_OK;
}
inline int httpd_resp_send_chunk(httpd_req_t*, const char*, int) { return ESP_OK; }
constexpr int HTTP_GET = 0, HTTP_POST = 1;
class AsyncWebServerRequest {
 public:
  static constexpr size_t URL_BUF_SIZE = 128U;
  void url_to(char* target) { std::strcpy(target, "/"); }
  int method() const { return HTTP_GET; }
  std::optional<std::string> get_header(const char*) const { return std::nullopt; }
  std::string arg(const char*) const { return {}; }
  void requestAuthentication() {}
  void send(int, const char* = nullptr, const char* = nullptr) {}
  operator httpd_req_t*() { return nullptr; }
};
class AsyncWebHandler {
 public:
  virtual ~AsyncWebHandler() = default;
  virtual bool canHandle(AsyncWebServerRequest*) const { return false; }
  virtual void handleRequest(AsyncWebServerRequest*) {}
};
namespace esphome {
namespace setup_priority {
constexpr float WIFI = 250.0f;
}
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual float get_setup_priority() const { return 0.0f; }
};
namespace factory_reset {
class FactoryResetComponent {
 public:
  bool failed{false};
  void mark_failed() { failed = true; }
};
}  // namespace factory_reset
namespace openquatt_common {
template <typename T>
class PsramBuffer {
 public:
  bool allocate_external(size_t count) {
    if (!allocation_ok) return false;
    values_.resize(count);
    return true;
  }
  void release() { values_.clear(); }
  T& operator[](size_t index) { return values_.at(index); }
  const T& operator[](size_t index) const { return values_.at(index); }
  T* data() { return values_.data(); }

 private:
  std::vector<T> values_;
};
}  // namespace openquatt_common
namespace modbus {
enum class ExceptionCode : uint8_t { FAILED = 1U };
class ModbusClientDevice;
struct Request {
  ModbusClientDevice* device;
  std::vector<uint8_t> pdu;
};
inline std::vector<Request> requests;
class ModbusClientDevice {
 public:
  virtual ~ModbusClientDevice() = default;
  void set_parent(void*) {}
  void set_address(uint8_t address) { address_ = address; }
  void clear_tx_queue_for_device() {
    requests.erase(
        std::remove_if(requests.begin(), requests.end(), [this](auto& request) { return request.device == this; }),
        requests.end());
  }
  bool read_holding_registers(uint16_t address, uint16_t count) { return queue_(3U, address, count); }
  bool write_single_register(uint16_t address, uint16_t value) { return queue_(6U, address, value); }
  bool write_multiple_registers(uint16_t address, std::span<const uint16_t> words) {
    queue_(16U, address, words.size());
    auto& pdu = requests.back().pdu;
    pdu.push_back(words.size() * 2U);
    for (auto value : words) {
      pdu.push_back(value >> 8U);
      pdu.push_back(value & 255U);
    }
    return true;
  }
  void deliver(const Request& request, std::span<const uint8_t> response) { on_response(request.pdu, response); }
  bool lose(const Request& request) { return on_no_response(request.pdu); }
  uint8_t address_{1U};

 protected:
  virtual void on_response(std::span<const uint8_t>, std::span<const uint8_t>) {}
  virtual void on_error(std::span<const uint8_t>, ExceptionCode) {}
  virtual bool on_no_response(std::span<const uint8_t>) { return false; }
  virtual void on_not_sent(std::span<const uint8_t>) {}

 private:
  bool queue_(uint8_t fc, uint16_t address, uint16_t count) {
    requests.push_back({this,
                        {fc, static_cast<uint8_t>(address >> 8U), static_cast<uint8_t>(address),
                         static_cast<uint8_t>(count >> 8U), static_cast<uint8_t>(count)}});
    return true;
  }
};
namespace helpers {
inline std::optional<uint16_t> client_pdu_start_address(std::span<const uint8_t> request) {
  if (request.size() < 3U) return std::nullopt;
  return uint16_t(request[1]) << 8U | request[2];
}
inline std::span<const uint8_t> server_pdu_payload(std::span<const uint8_t> response) { return response.subspan(2U); }
}  // namespace helpers
}  // namespace modbus
namespace modbus_controller {
class ModbusController {
 public:
  void* hub() { return this; }
  uint8_t device_address() const { return 1U; }
};
}  // namespace modbus_controller
namespace openquatt_odu_eeprom_dump {
class OpenQuattOduEepromDump {
 public:
  bool try_begin_external_operation() {
    if (busy) return false;
    busy = true;
    return true;
  }
  void end_external_operation() { busy = false; }
  bool busy{false};
};
}  // namespace openquatt_odu_eeprom_dump
namespace openquatt_odu_settings {
class OpenQuattOduSettings {
 public:
  bool revoke_auto_reapply() {
    ++revocations;
    return profile_sync_ok;
  }
};
}  // namespace openquatt_odu_settings
namespace openquatt_odu_defrost {
class OpenQuattOduDefrost {
 public:
  bool revoke_auto_reapply() {
    ++revocations;
    return profile_sync_ok;
  }
  oq_odu::Variant variant() const { return oq_odu::Variant::V2_NEW_MODEL; }
};
}  // namespace openquatt_odu_defrost
namespace openquatt_web_auth {
class OpenQuattWebAuth {
 public:
  bool request_is_authenticated_admin(AsyncWebServerRequest*) const { return true; }
  std::string get_csrf_token() const { return "private-csrf-token"; }
};
}  // namespace openquatt_web_auth
namespace web_server_base {
struct Server {
  bool recovery{false};
  uint32_t epoch{0U};
  bool is_recovery_active() const { return recovery; }
  uint32_t recovery_epoch() const { return epoch; }
  void add_handler(AsyncWebHandler* handler) { delete handler; }
};
inline Server test_server;
inline Server* global_web_server_base = &test_server;
}  // namespace web_server_base
}  // namespace esphome
