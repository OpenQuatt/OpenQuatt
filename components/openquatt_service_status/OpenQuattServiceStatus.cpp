#include "OpenQuattServiceStatus.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "esphome/core/log.h"

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

bool write_raw(httpd_req_t* req, const char* value) {
  return httpd_resp_send_chunk(req, value, static_cast<ssize_t>(std::strlen(value))) == ESP_OK;
}

bool write_json_string(httpd_req_t* req, const char* value) {
  if (!write_raw(req, "\"")) return false;
  const char* cursor = value != nullptr ? value : "";
  while (*cursor != '\0') {
    const unsigned char c = static_cast<unsigned char>(*cursor++);
    char escaped[7];
    switch (c) {
      case '"':
        if (!write_raw(req, "\\\"")) return false;
        break;
      case '\\':
        if (!write_raw(req, "\\\\")) return false;
        break;
      case '\b':
        if (!write_raw(req, "\\b")) return false;
        break;
      case '\f':
        if (!write_raw(req, "\\f")) return false;
        break;
      case '\n':
        if (!write_raw(req, "\\n")) return false;
        break;
      case '\r':
        if (!write_raw(req, "\\r")) return false;
        break;
      case '\t':
        if (!write_raw(req, "\\t")) return false;
        break;
      default:
        if (c < 0x20) {
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
          if (!write_raw(req, escaped)) return false;
        } else {
          const char out[2] = {static_cast<char>(c), '\0'};
          if (!write_raw(req, out)) return false;
        }
        break;
    }
  }
  return write_raw(req, "\"");
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

bool write_entity_key(httpd_req_t* req, bool* first, const char* key) {
  if (!*first && !write_raw(req, ",")) return false;
  *first = false;
  return write_json_string(req, key) && write_raw(req, ":");
}

bool write_text_entity(httpd_req_t* req, bool* first, const char* key, const char* value) {
  return write_entity_key(req, first, key) && write_raw(req, "{\"state\":") && write_json_string(req, value) &&
         write_raw(req, ",\"value\":") && write_json_string(req, value) && write_raw(req, "}");
}

bool write_binary_entity(httpd_req_t* req, bool* first, const char* key, bool value) {
  return write_entity_key(req, first, key) &&
         write_raw(req, value ? R"({"state":"ON","value":true})" : R"({"state":"OFF","value":false})");
}

bool write_number_literal(httpd_req_t* req, float value, int decimals) {
  if (!std::isfinite(value)) {
    return write_json_string(req, "nan");
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
  return write_raw(req, buffer);
}

bool write_number_entity(httpd_req_t* req, bool* first, const char* key, float value, const char* unit = "",
                         int decimals = 0) {
  if (!write_entity_key(req, first, key) || !write_raw(req, "{\"state\":") ||
      !write_number_literal(req, value, decimals) || !write_raw(req, ",\"value\":") ||
      !write_number_literal(req, value, decimals)) {
    return false;
  }
  if (unit != nullptr && unit[0] != '\0') {
    if (!write_raw(req, ",\"uom\":") || !write_json_string(req, unit)) return false;
  }
  return write_raw(req, "}");
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

float OpenQuattServiceStatus::get_setup_priority() const { return setup_priority::WIFI; }

void OpenQuattServiceStatus::setup() {
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

void OpenQuattServiceStatus::write_status(httpd_req_t* req) const {
  const bool commissioning_active = bool_value(this->commissioning_active_);
  const int commissioning_task_code = int_value(this->commissioning_task_code_);
  bool first = true;

  if (!write_raw(req, R"({"ok":true,"entities":{)") ||
      !write_text_entity(req, &first, "commissioningStatus", text_value(this->commissioning_status_)) ||
      !write_binary_entity(req, &first, "cm100Active", int_value(this->control_mode_code_) == 100) ||
      !write_number_entity(req, &first, "boilerPowerTestResult", float_value(this->boiler_result_w_), "W", 0) ||
      !write_number_entity(req, &first, "boilerPowerTestConfidence", float_value(this->boiler_confidence_), "%", 0) ||
      !write_text_entity(req, &first, "boilerPowerTestResultQuality",
                         text_value(this->boiler_quality_, "not available")) ||
      !write_binary_entity(req, &first, "boilerPowerTestActive",
                           commissioning_active && commissioning_task_code == TASK_BOILER_POWER_TEST) ||
      !write_text_entity(req, &first, "boilerPowerTestStatus", text_value(this->boiler_status_)) ||
      !write_text_entity(req, &first, "flowAutotuneStatus", text_value(this->flow_autotune_status_)) ||
      !write_number_entity(req, &first, "flowKpSuggested", float_value(this->flow_kp_suggested_), "", 5) ||
      !write_number_entity(req, &first, "flowKiSuggested", float_value(this->flow_ki_suggested_), "", 5) ||
      !write_binary_entity(req, &first, "airPurgeActive", bool_value(this->air_purge_active_)) ||
      !write_text_entity(req, &first, "airPurgeStatus", text_value(this->air_purge_status_)) ||
      !write_number_entity(req, &first, "airPurgeRemaining", int_value(this->air_purge_remaining_), "s", 0) ||
      !write_number_entity(req, &first, "airPurgePhase", int_value(this->air_purge_phase_), "", 0) ||
      !write_number_entity(req, &first, "airPurgeTargetIpwm", int_value(this->air_purge_target_ipwm_), "iPWM", 0) ||
      !write_binary_entity(req, &first, "manualFlowActive", bool_value(this->manual_flow_active_)) ||
      !write_text_entity(req, &first, "manualFlowStatus", text_value(this->manual_flow_status_)) ||
      !write_number_entity(req, &first, "manualFlowTargetIpwm", int_value(this->manual_flow_target_ipwm_), "iPWM", 0) ||
      !write_binary_entity(req, &first, "manualHpActive", bool_value(this->manual_hp_active_)) ||
      !write_text_entity(req, &first, "manualHpStatus", text_value(this->manual_hp_status_)) ||
      !write_text_entity(req, &first, "manualHpGuardStatus",
                         text_value(this->manual_hp_guard_status_, "Vrijgegeven")) ||
      !write_binary_entity(req, &first, "hpWaterCalibrationActive", bool_value(this->hp_water_calibration_active_)) ||
      !write_text_entity(req, &first, "hpWaterCalibrationStatus", text_value(this->hp_water_calibration_status_)) ||
      !write_number_entity(req, &first, "hpWaterCalibrationRemaining", int_value(this->hp_water_calibration_remaining_),
                           "s", 0) ||
      !write_number_entity(req, &first, "hpWaterCalibrationPhase", int_value(this->hp_water_calibration_phase_), "",
                           0) ||
      !write_number_entity(req, &first, "hpWaterCalibrationSpread", float_value(this->hp_water_calibration_spread_),
                           "°C", 2) ||
      !write_number_entity(req, &first, "hpWaterCalibrationSupplyDelta",
                           float_value(this->hp_water_calibration_supply_delta_), "°C", 2) ||
      !write_number_entity(req, &first, "hpWaterCalibrationStableProgress",
                           int_value(this->hp_water_calibration_stable_progress_), "s", 0) ||
      !write_number_entity(req, &first, "hpWaterCalibrationStableRequired",
                           int_value(this->hp_water_calibration_stable_required_), "s", 0) ||
      !write_number_entity(req, &first, "hpWaterCalibrationResultReference",
                           float_value(this->hp_water_calibration_result_reference_), "°C", 2) ||
      !write_number_entity(req, &first, "hpWaterCalibrationResultSpreadBefore",
                           float_value(this->hp_water_calibration_result_spread_before_), "°C", 2) ||
      !write_number_entity(req, &first, "hpWaterCalibrationResultExpectedSpread",
                           float_value(this->hp_water_calibration_result_expected_spread_), "°C", 2) ||
      !write_number_entity(req, &first, "hpWaterCalibrationResultHp1InRawAvg",
                           float_value(this->hp_water_calibration_result_hp1_in_raw_avg_), "°C", 2) ||
      !write_number_entity(req, &first, "hpWaterCalibrationResultHp1OutRawAvg",
                           float_value(this->hp_water_calibration_result_hp1_out_raw_avg_), "°C", 2) ||
      !write_number_entity(req, &first, "hpWaterCalibrationResultHp2InRawAvg",
                           float_value(this->hp_water_calibration_result_hp2_in_raw_avg_), "°C", 2) ||
      !write_number_entity(req, &first, "hpWaterCalibrationResultHp2OutRawAvg",
                           float_value(this->hp_water_calibration_result_hp2_out_raw_avg_), "°C", 2) ||
      !write_number_entity(req, &first, "hpWaterCalibrationResultSupplyRawAvg",
                           float_value(this->hp_water_calibration_result_supply_raw_avg_), "°C", 2) ||
      !write_number_entity(req, &first, "hpWaterCalibrationResultSupplyOffset",
                           float_value(this->hp_water_calibration_result_supply_offset_), "°C", 2) ||
      !write_text_entity(req, &first, "hpWaterCalibrationResultSupplySource",
                         text_value(this->hp_water_calibration_result_supply_source_, "")) ||
      !write_raw(req, R"(}})")) {
    httpd_resp_send_chunk(req, nullptr, 0);
    return;
  }

  httpd_resp_send_chunk(req, nullptr, 0);
}

}  // namespace openquatt_service_status
}  // namespace esphome
