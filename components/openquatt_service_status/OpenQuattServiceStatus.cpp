#include "OpenQuattServiceStatus.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "OpenQuattHttpStatus.h"
#include "PsramBuffer.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "lwip/sockets.h"

namespace esphome {
namespace openquatt_service_status {
namespace {

static const char* const TAG = "openquatt.service_status";
static constexpr int TASK_BOILER_POWER_TEST = 1;

bool url_path_matches(const char* url, const char* path) {
  if (url == nullptr || path == nullptr) {
    return false;
  }
  const size_t path_len = std::strlen(path);
  return std::strncmp(url, path, path_len) == 0 && (url[path_len] == '\0' || url[path_len] == '?');
}

bool send_snapshot(httpd_req_t* req, const char* data, size_t length) {
  for (size_t offset = 0U; offset < length;) {
    const size_t count = std::min(length - offset, size_t{1024U});
    if (httpd_resp_send_chunk(req, data + offset, static_cast<ssize_t>(count)) != ESP_OK) {
      shutdown(httpd_req_to_sockfd(req), SHUT_RDWR);
      return false;
    }
    offset += count;
  }
  if (httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) {
    shutdown(httpd_req_to_sockfd(req), SHUT_RDWR);
    return false;
  }
  return true;
}

bool write_raw(SnapshotBufferWriter& writer, const char* value) { return writer.write(value); }

bool write_json_string(SnapshotBufferWriter& writer, const char* value) {
  if (!write_raw(writer, "\"")) return false;
  const char* cursor = value != nullptr ? value : "";
  while (*cursor != '\0') {
    const unsigned char c = static_cast<unsigned char>(*cursor++);
    char escaped[7];
    switch (c) {
      case '"':
        if (!write_raw(writer, "\\\"")) return false;
        break;
      case '\\':
        if (!write_raw(writer, "\\\\")) return false;
        break;
      case '\b':
        if (!write_raw(writer, "\\b")) return false;
        break;
      case '\f':
        if (!write_raw(writer, "\\f")) return false;
        break;
      case '\n':
        if (!write_raw(writer, "\\n")) return false;
        break;
      case '\r':
        if (!write_raw(writer, "\\r")) return false;
        break;
      case '\t':
        if (!write_raw(writer, "\\t")) return false;
        break;
      default:
        if (c < 0x20) {
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
          if (!write_raw(writer, escaped)) return false;
        } else {
          const char out[2] = {static_cast<char>(c), '\0'};
          if (!write_raw(writer, out)) return false;
        }
        break;
    }
  }
  return write_raw(writer, "\"");
}

const char* text_value(OpenQuattServiceStatus::StringGlobal* global, const char* fallback = "IDLE") {
  if (global == nullptr) return fallback;
  const std::string& value = global->value();
  return value.empty() ? fallback : value.c_str();
}

bool bool_value(OpenQuattServiceStatus::BoolGlobal* global) { return global != nullptr && global->value(); }

int int_value(OpenQuattServiceStatus::IntGlobal* global, int fallback = 0) {
  return global != nullptr ? global->value() : fallback;
}

float float_value(OpenQuattServiceStatus::FloatGlobal* global) { return global != nullptr ? global->value() : NAN; }

bool write_entity_key(SnapshotBufferWriter& writer, bool* first, const char* key) {
  if (!*first && !write_raw(writer, ",")) return false;
  *first = false;
  return write_json_string(writer, key) && write_raw(writer, ":");
}

bool write_text_entity(SnapshotBufferWriter& writer, bool* first, const char* key, const char* value) {
  return write_entity_key(writer, first, key) && write_raw(writer, "{\"state\":") && write_json_string(writer, value) &&
         write_raw(writer, ",\"value\":") && write_json_string(writer, value) && write_raw(writer, "}");
}

bool write_binary_entity(SnapshotBufferWriter& writer, bool* first, const char* key, bool value) {
  return write_entity_key(writer, first, key) &&
         write_raw(writer, value ? R"({"state":"ON","value":true})" : R"({"state":"OFF","value":false})");
}

bool write_number_literal(SnapshotBufferWriter& writer, float value, int decimals) {
  if (!std::isfinite(value)) {
    return write_json_string(writer, "nan");
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  if (decimals > 0) {
    char* end = buffer + std::strlen(buffer) - 1;
    while (end > buffer && *end == '0') {
      *end-- = '\0';
    }
    if (end > buffer && *end == '.') {
      *end = '\0';
    }
  }
  return write_raw(writer, buffer);
}

bool write_number_entity(SnapshotBufferWriter& writer, bool* first, const char* key, float value, const char* unit = "",
                         int decimals = 0) {
  if (!write_entity_key(writer, first, key) || !write_raw(writer, "{\"state\":") ||
      !write_number_literal(writer, value, decimals) || !write_raw(writer, ",\"value\":") ||
      !write_number_literal(writer, value, decimals)) {
    return false;
  }
  if (unit != nullptr && unit[0] != '\0') {
    if (!write_raw(writer, ",\"uom\":") || !write_json_string(writer, unit)) return false;
  }
  return write_raw(writer, "}");
}

class OpenQuattServiceStatusRequestHandler : public AsyncWebHandler {
 public:
  explicit OpenQuattServiceStatusRequestHandler(OpenQuattServiceStatus* parent) : parent_(parent) {}

  bool canHandle(AsyncWebServerRequest* request) const override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url_buf);
    return url_path_matches(url_buf, "/openquatt/service/status") && request->method() == HTTP_GET;
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    httpd_req_t* req = *request;
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    this->parent_->write_status(req);
  }

 protected:
  OpenQuattServiceStatus* parent_;
};

}  // namespace

OpenQuattServiceStatus::~OpenQuattServiceStatus() {
  if (this->snapshot_lock_ != nullptr) vSemaphoreDelete(this->snapshot_lock_);
}

float OpenQuattServiceStatus::get_setup_priority() const { return setup_priority::WIFI; }

void OpenQuattServiceStatus::setup() {
  this->snapshot_lock_ = xSemaphoreCreateMutex();
  if (this->snapshot_lock_ == nullptr) ESP_LOGW(TAG, "Snapshot lock unavailable; endpoint returns 503");
  this->refresh_snapshot_();
  this->last_snapshot_ms_ = millis();
  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGW(TAG, "web_server_base is not available; service status endpoint disabled");
    return;
  }
  web_server_base::global_web_server_base->add_handler(new OpenQuattServiceStatusRequestHandler(this));
}

void OpenQuattServiceStatus::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt service status endpoint:");
  ESP_LOGCONFIG(TAG, "  Web server: %s", this->web_server_ != nullptr ? "configured" : "missing");
}

void OpenQuattServiceStatus::loop() {
  const uint32_t now = millis();
  const uint32_t interval = this->published_size_.load(std::memory_order_relaxed) == 0U ? 1000U : 250U;
  if (now - this->last_snapshot_ms_ < interval) return;
  this->last_snapshot_ms_ = now;
  this->refresh_snapshot_();
}

void OpenQuattServiceStatus::refresh_snapshot_() {
  if (this->snapshot_lock_ == nullptr) return;
  if (!this->pending_snapshot_ && !this->pending_snapshot_.allocate_external(MAX_SNAPSHOT_BYTES)) {
    this->published_size_.store(0U, std::memory_order_release);
    return;
  }
  SnapshotBufferWriter writer(this->pending_snapshot_.data(), this->pending_snapshot_.size());
  if (!this->serialize_snapshot_(writer) || writer.size() == 0U) {
    this->published_size_.store(0U, std::memory_order_release);
    return;
  }
  // Never make the control loop wait for HTTPD, even if its reader is preempted.
  if (xSemaphoreTake(this->snapshot_lock_, 0) != pdTRUE) return;
  this->pending_snapshot_.swap(this->published_snapshot_);
  this->published_at_ms_ = millis();
  this->published_size_.store(static_cast<uint32_t>(writer.size()), std::memory_order_release);
  xSemaphoreGive(this->snapshot_lock_);
}

void OpenQuattServiceStatus::write_status(httpd_req_t* req) const {
  const auto unavailable = [req]() {
    openquatt_common::send_http_status(req, openquatt_common::HttpStatus::SERVICE_UNAVAILABLE,
                                       R"({"ok":false,"error":"serialization_unavailable"})");
  };
  const uint32_t expected_size = this->published_size_.load(std::memory_order_acquire);
  openquatt_common::PsramBuffer<char> snapshot;
  if (expected_size == 0U || !snapshot.allocate_external(MAX_SNAPSHOT_BYTES)) {
    unavailable();
    return;
  }
  if (xSemaphoreTake(this->snapshot_lock_, pdMS_TO_TICKS(5U)) != pdTRUE) {
    unavailable();
    return;
  }
  const uint32_t size = this->published_size_.load(std::memory_order_relaxed);
  const bool valid = size != 0U && size <= snapshot.size() && size <= this->published_snapshot_.size() &&
                     uint32_t(millis() - this->published_at_ms_) <= 1000U;
  if (valid) std::memcpy(snapshot.data(), this->published_snapshot_.data(), size);
  xSemaphoreGive(this->snapshot_lock_);
  // Publication may become unavailable while the request allocates its buffer.
  // Never allocate or send while holding the lock.
  if (!valid) {
    unavailable();
    return;
  }
  send_snapshot(req, snapshot.data(), size);
}

bool OpenQuattServiceStatus::serialize_snapshot_(SnapshotBufferWriter& writer) const {
  const char* duo_start_guard = "Vrijgegeven";
  switch (this->duo_defrost_start_block_mask_) {
    case 1:
      duo_start_guard = "HP1 wacht op het ontdooien van HP2";
      break;
    case 2:
      duo_start_guard = "HP2 wacht op het ontdooien van HP1";
      break;
    case 3:
      duo_start_guard = "HP1 en HP2 wachten op ontdooien";
      break;
  }
  const uint32_t warmup_progress = this->warmup_progress_.read();
  const bool commissioning_active = bool_value(this->commissioning_active_);
  const int commissioning_task_code = int_value(this->commissioning_task_code_);
  bool first = true;

  if (!write_raw(writer, R"({"ok":true,"entities":{)") ||
      !write_text_entity(writer, &first, "commissioningStatus", text_value(this->commissioning_status_)) ||
      !write_binary_entity(writer, &first, "cm100Active", int_value(this->control_mode_code_) == 100) ||
      !write_number_entity(writer, &first, "boilerPowerTestResult", float_value(this->boiler_result_w_), "W", 0) ||
      !write_number_entity(writer, &first, "boilerPowerTestConfidence", float_value(this->boiler_confidence_), "%",
                           0) ||
      !write_text_entity(writer, &first, "boilerPowerTestResultQuality",
                         text_value(this->boiler_quality_, "not available")) ||
      !write_binary_entity(writer, &first, "boilerPowerTestActive",
                           commissioning_active && commissioning_task_code == TASK_BOILER_POWER_TEST) ||
      !write_text_entity(writer, &first, "boilerPowerTestStatus", text_value(this->boiler_status_)) ||
      !write_text_entity(writer, &first, "flowAutotuneStatus", text_value(this->flow_autotune_status_)) ||
      !write_number_entity(writer, &first, "flowKpSuggested", float_value(this->flow_kp_suggested_), "", 5) ||
      !write_number_entity(writer, &first, "flowKiSuggested", float_value(this->flow_ki_suggested_), "", 5) ||
      !write_binary_entity(writer, &first, "airPurgeActive", bool_value(this->air_purge_active_)) ||
      !write_text_entity(writer, &first, "airPurgeStatus", text_value(this->air_purge_status_)) ||
      !write_number_entity(writer, &first, "airPurgeRemaining", int_value(this->air_purge_remaining_), "s", 0) ||
      !write_number_entity(writer, &first, "airPurgePhase", int_value(this->air_purge_phase_), "", 0) ||
      !write_number_entity(writer, &first, "airPurgeTargetIpwm", int_value(this->air_purge_target_ipwm_), "iPWM", 0) ||
      !write_binary_entity(writer, &first, "manualFlowActive", bool_value(this->manual_flow_active_)) ||
      !write_text_entity(writer, &first, "manualFlowStatus", text_value(this->manual_flow_status_)) ||
      !write_number_entity(writer, &first, "manualFlowTargetIpwm", int_value(this->manual_flow_target_ipwm_), "iPWM",
                           0) ||
      !write_binary_entity(writer, &first, "manualHpActive", bool_value(this->manual_hp_active_)) ||
      !write_text_entity(writer, &first, "manualHpStatus", text_value(this->manual_hp_status_)) ||
      !write_text_entity(writer, &first, "manualHpGuardStatus",
                         text_value(this->manual_hp_guard_status_, "Vrijgegeven")) ||
      !write_text_entity(writer, &first, "duoStartGuardStatus", duo_start_guard) ||
      !write_binary_entity(writer, &first, "hpWaterCalibrationActive",
                           bool_value(this->hp_water_calibration_active_)) ||
      !write_text_entity(writer, &first, "hpWaterCalibrationStatus", text_value(this->hp_water_calibration_status_)) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationRemaining",
                           int_value(this->hp_water_calibration_remaining_), "s", 0) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationPhase", int_value(this->hp_water_calibration_phase_), "",
                           0) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationSpread", float_value(this->hp_water_calibration_spread_),
                           "°C", 2) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationSupplyDelta",
                           float_value(this->hp_water_calibration_supply_delta_), "°C", 2) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationStableProgress",
                           int_value(this->hp_water_calibration_stable_progress_), "s", 0) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationStableRequired",
                           int_value(this->hp_water_calibration_stable_required_), "s", 0) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationResultReference",
                           float_value(this->hp_water_calibration_result_reference_), "°C", 2) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationResultSpreadBefore",
                           float_value(this->hp_water_calibration_result_spread_before_), "°C", 2) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationResultExpectedSpread",
                           float_value(this->hp_water_calibration_result_expected_spread_), "°C", 2) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationResultHp1InRawAvg",
                           float_value(this->hp_water_calibration_result_hp1_in_raw_avg_), "°C", 2) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationResultHp1OutRawAvg",
                           float_value(this->hp_water_calibration_result_hp1_out_raw_avg_), "°C", 2) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationResultHp2InRawAvg",
                           float_value(this->hp_water_calibration_result_hp2_in_raw_avg_), "°C", 2) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationResultHp2OutRawAvg",
                           float_value(this->hp_water_calibration_result_hp2_out_raw_avg_), "°C", 2) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationResultSupplyRawAvg",
                           float_value(this->hp_water_calibration_result_supply_raw_avg_), "°C", 2) ||
      !write_number_entity(writer, &first, "hpWaterCalibrationResultSupplyOffset",
                           float_value(this->hp_water_calibration_result_supply_offset_), "°C", 2) ||
      !write_text_entity(writer, &first, "hpWaterCalibrationResultSupplySource",
                         text_value(this->hp_water_calibration_result_supply_source_, ""))) {
    return false;
  }

  if (warmup_progress != WarmupProgressSnapshot::UNAVAILABLE &&
      (!write_number_entity(writer, &first, "warmupStatus", warmup_progress >> 24U) ||
       !write_number_entity(writer, &first, "warmupElapsed", warmup_progress & 0x00ffffffU, "s"))) {
    return false;
  }
  if (!write_raw(writer, R"(}})")) {
    return false;
  }

  return true;
}

}  // namespace openquatt_service_status
}  // namespace esphome
