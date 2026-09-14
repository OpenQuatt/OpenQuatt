#include "OpenQuattMqttConfig.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <inttypes.h>
#include <string>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

namespace esphome {
namespace openquatt_mqtt_config {

namespace {

static const char* const TAG = "openquatt.mqtt_config";
static const uint32_t STORAGE_KEY = fnv1_hash("openquatt_mqtt_config_store");

bool time_reached_(uint32_t now_ms, uint32_t target_ms) { return static_cast<int32_t>(now_ms - target_ms) >= 0; }

void log_heap_state_(const char* phase) {
  constexpr uint32_t internal_heap_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  const size_t free_internal = heap_caps_get_free_size(internal_heap_caps);
  const size_t largest_internal = heap_caps_get_largest_free_block(internal_heap_caps);
  const size_t fragmentation_percent =
      free_internal == 0U ? 0U : 100U - std::min<size_t>(100U, (largest_internal * 100U) / free_internal);
  ESP_LOGD(TAG,
           "%s: heap free=%u, min=%u, largest=%u, fragmentation=%u%%, "
           "PSRAM free=%u",
           phase, static_cast<unsigned>(free_internal),
           static_cast<unsigned>(heap_caps_get_minimum_free_size(internal_heap_caps)),
           static_cast<unsigned>(largest_internal), static_cast<unsigned>(fragmentation_percent),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

std::string json_escape_(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 4);
  for (char c : input) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
        break;
    }
  }
  return out;
}

std::string base64_encode_bytes_(const uint8_t* data, size_t len) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t octet_a = data[i];
    const uint32_t octet_b = (i + 1 < len) ? data[i + 1] : 0;
    const uint32_t octet_c = (i + 2 < len) ? data[i + 2] : 0;
    const uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
    out.push_back(alphabet[(triple >> 18) & 0x3F]);
    out.push_back(alphabet[(triple >> 12) & 0x3F]);
    out.push_back((i + 1 < len) ? alphabet[(triple >> 6) & 0x3F] : '=');
    out.push_back((i + 2 < len) ? alphabet[triple & 0x3F] : '=');
  }
  return out;
}

bool header_matches_host_(const std::string& header_value, const std::string& host) {
  if (host.empty() || header_value.empty()) {
    return false;
  }

  size_t authority_start = 0;
  const size_t scheme_pos = header_value.find("://");
  if (scheme_pos != std::string::npos) {
    authority_start = scheme_pos + 3;
  }
  const size_t authority_end = header_value.find_first_of("/?#", authority_start);
  const std::string authority = header_value.substr(
      authority_start, authority_end == std::string::npos ? std::string::npos : authority_end - authority_start);
  return authority == host;
}

const char* skip_spaces_(const char* cursor) {
  while (cursor != nullptr && *cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor)) != 0) {
    cursor++;
  }
  return cursor;
}

bool parse_numeric_scalar_(const char* payload, float* value) {
  if (payload == nullptr || value == nullptr) {
    return false;
  }

  char normalized[OpenQuattMqttConfig::PAYLOAD_MAX_LEN]{};
  const size_t len = strnlen(payload, sizeof(normalized));
  if (len == 0 || len >= sizeof(normalized)) {
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    normalized[i] = payload[i] == ',' ? '.' : payload[i];
  }

  const char* begin = skip_spaces_(normalized);
  char* end = nullptr;
  const float parsed = std::strtof(begin, &end);
  if (begin == end || end == nullptr || !std::isfinite(parsed)) {
    return false;
  }

  const char* tail = skip_spaces_(end);
  if (static_cast<unsigned char>(tail[0]) == 0xC2U && static_cast<unsigned char>(tail[1]) == 0xB0U) {
    tail += 2;
    tail = skip_spaces_(tail);
  }
  if (tail[0] == 'C' || tail[0] == 'c') {
    tail++;
    tail = skip_spaces_(tail);
  }
  if (*tail != '\0') {
    return false;
  }

  *value = parsed;
  return true;
}

bool parse_numeric_json_(const char* payload, float* value) {
  if (payload == nullptr || value == nullptr) {
    return false;
  }

  const size_t len = strlen(payload);
  cJSON* root = cJSON_ParseWithLength(payload, len);
  if (root == nullptr) {
    return false;
  }

  bool ok = false;
  const cJSON* item = cJSON_IsObject(root) ? cJSON_GetObjectItemCaseSensitive(root, "value") : root;
  if (cJSON_IsNumber(item)) {
    *value = static_cast<float>(item->valuedouble);
    ok = std::isfinite(*value);
  } else if (cJSON_IsString(item) && item->valuestring != nullptr) {
    ok = parse_numeric_scalar_(item->valuestring, value);
  }

  cJSON_Delete(root);
  return ok;
}

bool parse_numeric_payload_(const char* payload, float* value) {
  const char* trimmed = skip_spaces_(payload);
  if (trimmed == nullptr || *trimmed == '\0') {
    return false;
  }
  if (*trimmed == '{' || *trimmed == '[') {
    return parse_numeric_json_(trimmed, value);
  }
  return parse_numeric_scalar_(trimmed, value);
}

std::string lowercase_trimmed_(const char* payload) {
  std::string out;
  const char* cursor = skip_spaces_(payload);
  if (cursor == nullptr) {
    return out;
  }
  while (*cursor != '\0') {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*cursor))));
    cursor++;
  }
  while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back())) != 0) {
    out.pop_back();
  }
  return out;
}

bool parse_binary_scalar_(const char* payload, bool* value) {
  if (payload == nullptr || value == nullptr) {
    return false;
  }
  const std::string text = lowercase_trimmed_(payload);
  if (text.empty()) {
    return false;
  }
  if (text == "1" || text == "true" || text == "on" || text == "yes") {
    *value = true;
    return true;
  }
  if (text == "0" || text == "false" || text == "off" || text == "no") {
    *value = false;
    return true;
  }
  return false;
}

bool parse_binary_json_(const char* payload, bool* value) {
  if (payload == nullptr || value == nullptr) {
    return false;
  }

  const size_t len = strlen(payload);
  cJSON* root = cJSON_ParseWithLength(payload, len);
  if (root == nullptr) {
    return false;
  }

  bool ok = false;
  const cJSON* item = cJSON_IsObject(root) ? cJSON_GetObjectItemCaseSensitive(root, "value") : root;
  if (cJSON_IsBool(item)) {
    *value = cJSON_IsTrue(item);
    ok = true;
  } else if (cJSON_IsNumber(item)) {
    if (item->valuedouble == 0.0) {
      *value = false;
      ok = true;
    } else if (item->valuedouble == 1.0) {
      *value = true;
      ok = true;
    }
  } else if (cJSON_IsString(item) && item->valuestring != nullptr) {
    ok = parse_binary_scalar_(item->valuestring, value);
  }

  cJSON_Delete(root);
  return ok;
}

bool parse_binary_payload_(const char* payload, bool* value) {
  const char* trimmed = skip_spaces_(payload);
  if (trimmed == nullptr || *trimmed == '\0') {
    return false;
  }
  if (*trimmed == '{' || *trimmed == '[') {
    return parse_binary_json_(trimmed, value);
  }
  return parse_binary_scalar_(trimmed, value);
}

bool send_mutation_error_(AsyncWebServerRequest* request, OpenQuattMqttConfig::MutationResult result) {
  switch (result) {
    case OpenQuattMqttConfig::MutationResult::OK:
      return false;
    case OpenQuattMqttConfig::MutationResult::INVALID:
      request->send(409, "application/json", R"({"ok":false,"error":"invalid_configuration"})");
      break;
    case OpenQuattMqttConfig::MutationResult::BUSY:
      request->send(503, "application/json", R"({"ok":false,"error":"configuration_busy"})");
      break;
    case OpenQuattMqttConfig::MutationResult::UNAVAILABLE:
      request->send(503, "application/json", R"({"ok":false,"error":"configuration_unavailable"})");
      break;
    case OpenQuattMqttConfig::MutationResult::TIMEOUT:
      request->send(504, "application/json", R"({"ok":false,"error":"configuration_timeout"})");
      break;
    case OpenQuattMqttConfig::MutationResult::SAVE_FAILED:
      request->send(500, "application/json", R"({"ok":false,"error":"preference_save_failed"})");
      break;
    case OpenQuattMqttConfig::MutationResult::SYNC_FAILED:
      request->send(500, "application/json", R"({"ok":false,"error":"preference_sync_failed"})");
      break;
    case OpenQuattMqttConfig::MutationResult::APPLY_FAILED:
      request->send(503, "application/json", R"({"ok":false,"error":"runtime_apply_failed"})");
      break;
    case OpenQuattMqttConfig::MutationResult::RUNTIME_PENDING:
      request->send(202, "application/json", R"({"ok":true,"pending":true,"status":"runtime_pending"})");
      break;
    case OpenQuattMqttConfig::MutationResult::RECOVERY_FAILED:
      request->send(500, "application/json", R"({"ok":false,"error":"storage_recovery_failed"})");
      break;
    case OpenQuattMqttConfig::MutationResult::RECOVERY_PENDING:
      request->send(503, "application/json", R"({"ok":false,"error":"storage_recovery_pending"})");
      break;
    case OpenQuattMqttConfig::MutationResult::PERSISTENCE_PENDING:
      request->send(503, "application/json", R"({"ok":false,"error":"preference_commit_pending"})");
      break;
  }
  return true;
}

class MqttConfigHandler : public AsyncWebHandler {
 public:
  explicit MqttConfigHandler(OpenQuattMqttConfig* parent) : parent_(parent) {}

  bool canHandle(AsyncWebServerRequest* request) const override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    StringRef url = request->url_to(url_buf);
    if (url == "/mqtt/status" && request->method() == HTTP_GET) {
      return true;
    }
    if (url == "/mqtt/save" && request->method() == HTTP_POST) {
      return true;
    }
    if (url == "/mqtt/input/save" && request->method() == HTTP_POST) {
      return true;
    }
    if (url == "/mqtt/input/retained/save" && request->method() == HTTP_POST) {
      return true;
    }
    return false;
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    StringRef url = request->url_to(url_buf);
    if (url == "/mqtt/status" && request->method() == HTTP_GET) {
      const auto status = this->parent_->get_status_snapshot();
      const std::string broker = json_escape_(status.broker);
      const std::string username = json_escape_(status.username);
      const auto outside_index = static_cast<size_t>(OpenQuattMqttConfig::NumericInputKind::OUTSIDE_TEMPERATURE);
      const auto room_temp_index = static_cast<size_t>(OpenQuattMqttConfig::NumericInputKind::ROOM_TEMPERATURE);
      const auto room_setpoint_index = static_cast<size_t>(OpenQuattMqttConfig::NumericInputKind::ROOM_SETPOINT);
      const auto supply_target_index =
          static_cast<size_t>(OpenQuattMqttConfig::NumericInputKind::HEATING_SUPPLY_TARGET);
      const auto heating_enable_index = static_cast<size_t>(OpenQuattMqttConfig::BinaryInputKind::HEATING_ENABLE);
      const auto cooling_enable_index = static_cast<size_t>(OpenQuattMqttConfig::BinaryInputKind::COOLING_ENABLE);
      const std::string topic = json_escape_(status.dew_point_topic);
      const std::string outside_topic = json_escape_(status.input_topics[outside_index]);
      const std::string room_temp_topic = json_escape_(status.input_topics[room_temp_index]);
      const std::string room_setpoint_topic = json_escape_(status.input_topics[room_setpoint_index]);
      const std::string supply_target_topic = json_escape_(status.input_topics[supply_target_index]);
      const std::string heating_enable_topic = json_escape_(status.binary_input_topics[heating_enable_index]);
      const std::string cooling_enable_topic = json_escape_(status.binary_input_topics[cooling_enable_index]);
      const std::string source = json_escape_(status.config_source);
      const std::string csrf_token = json_escape_(status.csrf_token);
      auto* stream = request->beginResponseStream("application/json");
      stream->printf(
          R"({"enabled":%s,"connected":%s,"runtime_pending":%s,"broker":"%s","port":%u,"username":"%s","password_set":%s,"dew_point_topic":"%s","input_topics":{"cooling_dew_point":"%s","outside_temperature":"%s","room_temperature":"%s","room_setpoint":"%s","heating_supply_target":"%s","heating_enable":"%s","cooling_enable":"%s"},"input_enabled":{"cooling_dew_point":%s,"outside_temperature":%s,"room_temperature":%s,"room_setpoint":%s,"heating_supply_target":%s,"heating_enable":%s,"cooling_enable":%s},"input_retained":{"cooling_dew_point":%s,"outside_temperature":%s,"room_temperature":%s,"room_setpoint":%s,"heating_supply_target":%s,"heating_enable":%s,"cooling_enable":%s},"input_accept_retained":{"cooling_dew_point":%s,"outside_temperature":%s,"room_temperature":%s,"room_setpoint":%s,"heating_supply_target":%s,"heating_enable":%s,"cooling_enable":%s},"non_retained_stateful_timeout_s":%u,"source":"%s","csrf_token":"%s"})",
          status.enabled ? "true" : "false", status.connected ? "true" : "false",
          status.runtime_pending ? "true" : "false", broker.c_str(), status.port, username.c_str(),
          status.password_set ? "true" : "false", topic.c_str(), topic.c_str(), outside_topic.c_str(),
          room_temp_topic.c_str(), room_setpoint_topic.c_str(), supply_target_topic.c_str(),
          heating_enable_topic.c_str(), cooling_enable_topic.c_str(),
          status.input_enabled[static_cast<size_t>(OpenQuattMqttConfig::NumericInputKind::COOLING_DEW_POINT)] ? "true"
                                                                                                              : "false",
          status.input_enabled[outside_index] ? "true" : "false",
          status.input_enabled[room_temp_index] ? "true" : "false",
          status.input_enabled[room_setpoint_index] ? "true" : "false",
          status.input_enabled[supply_target_index] ? "true" : "false",
          status.binary_input_enabled[heating_enable_index] ? "true" : "false",
          status.binary_input_enabled[cooling_enable_index] ? "true" : "false",
          status.input_retained[static_cast<size_t>(OpenQuattMqttConfig::NumericInputKind::COOLING_DEW_POINT)]
              ? "true"
              : "false",
          status.input_retained[outside_index] ? "true" : "false",
          status.input_retained[room_temp_index] ? "true" : "false",
          status.input_retained[room_setpoint_index] ? "true" : "false",
          status.input_retained[supply_target_index] ? "true" : "false",
          status.binary_input_retained[heating_enable_index] ? "true" : "false",
          status.binary_input_retained[cooling_enable_index] ? "true" : "false",
          status.input_accept_retained[static_cast<size_t>(OpenQuattMqttConfig::NumericInputKind::COOLING_DEW_POINT)]
              ? "true"
              : "false",
          status.input_accept_retained[outside_index] ? "true" : "false",
          status.input_accept_retained[room_temp_index] ? "true" : "false",
          status.input_accept_retained[room_setpoint_index] ? "true" : "false",
          status.input_accept_retained[supply_target_index] ? "true" : "false",
          status.binary_input_accept_retained[heating_enable_index] ? "true" : "false",
          status.binary_input_accept_retained[cooling_enable_index] ? "true" : "false",
          static_cast<unsigned>(30U * 60U), source.c_str(), csrf_token.c_str());
      request->send(stream);
      return;
    }

    if (url == "/mqtt/save" && request->method() == HTTP_POST) {
      if (!this->passes_same_origin_(request) || !this->passes_csrf_(request)) {
        request->send(403, "application/json", R"({"ok":false,"error":"forbidden"})");
        return;
      }

      const std::string broker = request->arg("broker");
      const std::string port_arg = request->arg("port");
      std::string username = request->arg("username");
      std::string password = request->arg("password");
      const std::string clear_password_arg = request->arg("clear_password");
      const std::string enabled_arg = request->arg("enabled");
      bool clear_password = clear_password_arg == "true" || clear_password_arg == "1" || clear_password_arg == "on";
      const bool enabled = enabled_arg == "true" || enabled_arg == "1" || enabled_arg == "on";
      const bool remove_config = !enabled && broker.empty();

      char* end = nullptr;
      unsigned long parsed_port = 1883;
      if (!port_arg.empty()) {
        parsed_port = strtoul(port_arg.c_str(), &end, 10);
      }
      if ((enabled || !port_arg.empty()) &&
          (end == nullptr || *end != '\0' || parsed_port == 0 || parsed_port > 65535)) {
        request->send(409, "application/json", R"({"ok":false,"error":"invalid_port"})");
        return;
      }
      if (remove_config) {
        username.clear();
        password.clear();
        clear_password = true;
      }
      if (broker.size() > 64U) {
        request->send(409, "application/json", R"({"ok":false,"error":"invalid_broker"})");
        return;
      }
      if (username.size() > 64U || password.size() > 128U) {
        request->send(409, "application/json", R"({"ok":false,"error":"invalid_credentials"})");
        return;
      }
      if (enabled && broker.empty()) {
        request->send(409, "application/json", R"({"ok":false,"error":"missing_broker"})");
        return;
      }

      const auto result = this->parent_->set_runtime_config(broker, static_cast<uint16_t>(parsed_port), username,
                                                            password, clear_password, enabled);
      if (send_mutation_error_(request, result)) {
        return;
      }

      const auto status = this->parent_->get_status_snapshot();
      auto* stream = request->beginResponseStream("application/json");
      stream->printf(R"({"ok":true,"enabled":%s,"connected":%s})", status.enabled ? "true" : "false",
                     status.connected ? "true" : "false");
      request->send(stream);
      return;
    }

    if (url == "/mqtt/input/save" && request->method() == HTTP_POST) {
      if (!this->passes_same_origin_(request) || !this->passes_csrf_(request)) {
        request->send(403, "application/json", R"({"ok":false,"error":"forbidden"})");
        return;
      }

      const std::string input = request->arg("input");
      const std::string enabled_arg = request->arg("enabled");
      const bool enabled = enabled_arg == "true" || enabled_arg == "1" || enabled_arg == "on";
      const auto result = this->parent_->set_input_enabled(input, enabled);
      if (send_mutation_error_(request, result)) {
        return;
      }

      const auto status = this->parent_->get_status_snapshot();
      auto* stream = request->beginResponseStream("application/json");
      stream->printf(R"({"ok":true,"enabled":%s,"connected":%s})", enabled ? "true" : "false",
                     status.connected ? "true" : "false");
      request->send(stream);
      return;
    }

    if (url == "/mqtt/input/retained/save" && request->method() == HTTP_POST) {
      if (!this->passes_same_origin_(request) || !this->passes_csrf_(request)) {
        request->send(403, "application/json", R"({"ok":false,"error":"forbidden"})");
        return;
      }

      const std::string input = request->arg("input");
      const std::string accept_retained_arg = request->arg("accept_retained");
      const bool accept_retained =
          accept_retained_arg == "true" || accept_retained_arg == "1" || accept_retained_arg == "on";
      const auto result = this->parent_->set_input_accept_retained(input, accept_retained);
      if (send_mutation_error_(request, result)) {
        return;
      }

      const auto status = this->parent_->get_status_snapshot();
      auto* stream = request->beginResponseStream("application/json");
      stream->printf(R"({"ok":true,"accept_retained":%s,"connected":%s})", accept_retained ? "true" : "false",
                     status.connected ? "true" : "false");
      request->send(stream);
      return;
    }

    request->send(404, "application/json", R"({"ok":false,"error":"not_found"})");
  }

 protected:
  bool passes_same_origin_(AsyncWebServerRequest* request) const {
    const auto host = request->get_header("Host");
    if (!host.has_value() || host->empty()) {
      return false;
    }

    const auto origin = request->get_header("Origin");
    if (origin.has_value() && !header_matches_host_(origin.value(), host.value())) {
      return false;
    }

    const auto referer = request->get_header("Referer");
    if (referer.has_value() && !header_matches_host_(referer.value(), host.value())) {
      return false;
    }

    return true;
  }

  bool passes_csrf_(AsyncWebServerRequest* request) const {
    const std::string csrf_token = request->arg("csrf_token");
    const auto status = this->parent_->get_status_snapshot();
    return !csrf_token.empty() && csrf_token == status.csrf_token;
  }

  OpenQuattMqttConfig* parent_;
};

}  // namespace

void OpenQuattMqttConfig::setup() {
  if (this->config_lock_ == nullptr) {
    this->config_lock_ = xSemaphoreCreateMutex();
  }
  if (this->runtime_lock_ == nullptr) {
    this->runtime_lock_ = xSemaphoreCreateMutex();
  }
  if (this->persistence_lock_ == nullptr) {
    this->persistence_lock_ = xSemaphoreCreateMutexStatic(&this->persistence_lock_storage_);
  }
  if (this->storage_mutation_lock_ == nullptr) {
    this->storage_mutation_lock_ = xSemaphoreCreateMutexStatic(&this->storage_mutation_lock_storage_);
  }
  if (this->storage_mutation_complete_ == nullptr) {
    this->storage_mutation_complete_ = xSemaphoreCreateBinaryStatic(&this->storage_mutation_complete_storage_);
  }
  if (this->worker_lock_ == nullptr) {
    this->worker_lock_ = xSemaphoreCreateMutexStatic(&this->worker_lock_storage_);
  }
  if (this->config_lock_ == nullptr || this->runtime_lock_ == nullptr || this->persistence_lock_ == nullptr ||
      this->storage_mutation_lock_ == nullptr || this->storage_mutation_complete_ == nullptr ||
      this->worker_lock_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate MQTT ingress state locks");
    this->mark_failed();
    return;
  }

  this->rotate_csrf_token_();
  this->register_http_handlers_();
  this->pref_ = global_preferences->make_preference<Storage>(STORAGE_KEY, true);

  Storage storage{};
  const bool loaded = this->load_storage_(&storage);
  Storage committed_storage{};
  if (!loaded) {
    if (!this->build_storage_(this->bootstrap_broker_, this->bootstrap_port_, this->bootstrap_username_,
                              this->bootstrap_password_, this->default_enabled_, 0U, 0U, &storage) ||
        !this->build_storage_("", 1883U, "", "", false, 0U, 0U, &committed_storage)) {
      ESP_LOGE(TAG, "MQTT bootstrap configuration could not be prepared");
      return;
    }
  } else {
    committed_storage = storage;
  }
  this->initialize_storage_transaction_(storage, committed_storage, !loaded);
  if (loaded && this->apply_storage_(storage, "stored") == StorageApplyResult::FAILED) {
    ESP_LOGE(TAG, "Stored MQTT configuration could not be applied");
  }
}

OpenQuattMqttConfig::StatusSnapshot OpenQuattMqttConfig::get_status_snapshot() {
  StatusSnapshot snapshot;
  this->lock_config_();
  snapshot.enabled = this->enabled_.load();
  snapshot.connected = snapshot.enabled && this->connected_.load() && this->client_events_enabled_.load() &&
                       this->callback_session_generation_.load() == this->mqtt_session_generation_.load() &&
                       this->active_client_generation_.load() == this->client_requested_generation_.load();
  snapshot.runtime_pending = this->client_reconcile_pending_.load() ||
                             this->client_applied_generation_.load() != this->client_requested_generation_.load();
  snapshot.broker = this->broker_;
  snapshot.port = this->port_;
  snapshot.username = this->username_;
  snapshot.password_set = !this->password_.empty();
  for (size_t i = 0; i < this->numeric_inputs_.size(); i++) {
    snapshot.input_topics[i] = this->numeric_inputs_[i].topic;
    snapshot.input_enabled[i] = this->is_numeric_input_enabled_(i);
    snapshot.input_retained[i] = this->numeric_inputs_[i].last_valid_retained;
    snapshot.input_accept_retained[i] = this->is_numeric_input_accept_retained_(i);
  }
  for (size_t i = 0; i < this->binary_inputs_.size(); i++) {
    snapshot.binary_input_topics[i] = this->binary_inputs_[i].topic;
    snapshot.binary_input_enabled[i] = this->is_binary_input_enabled_(i);
    snapshot.binary_input_retained[i] = this->binary_inputs_[i].last_valid_retained;
    snapshot.binary_input_accept_retained[i] = this->is_binary_input_accept_retained_(i);
  }
  snapshot.dew_point_topic = this->numeric_input_(NumericInputKind::COOLING_DEW_POINT).topic;
  snapshot.config_source = this->config_source_;
  snapshot.csrf_token = this->csrf_token_;
  this->unlock_config_();
  return snapshot;
}

void OpenQuattMqttConfig::loop() {
  if (this->process_permanent_failure_cleanup_()) {
    return;
  }
  this->process_storage_transaction_();
  if (this->client_reconcile_pending_.load() && !this->client_worker_active_.load() &&
      (this->mqtt_client_present_.load() || network::is_connected()) &&
      time_reached_(millis(), this->next_reconcile_attempt_ms_.load())) {
    this->request_client_reconcile_();
  }
  this->maybe_release_classic_worker_();
  if (this->clear_session_scoped_inputs_pending_.exchange(false)) {
    this->clear_session_scoped_inputs_();
    this->force_publish_.store(true);
  }
  const uint8_t clear_input_mask = this->clear_input_mask_pending_.exchange(0);
  if (clear_input_mask != 0U) {
    this->clear_input_(clear_input_mask);
    this->force_publish_.store(true);
  }
  this->consume_pending_numeric_payloads_();
  this->consume_pending_binary_payloads_();
  this->process_pending_input_subscriptions_();
  this->publish_runtime_state_(this->force_publish_.exchange(false));
}

void OpenQuattMqttConfig::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt MQTT Config");
  const auto status = this->get_status_snapshot();
  ESP_LOGCONFIG(TAG, "  Enabled: %s", YESNO(status.enabled));
  ESP_LOGCONFIG(TAG, "  Broker: %s:%u", status.broker.empty() ? "<none>" : status.broker.c_str(), status.port);
  ESP_LOGCONFIG(TAG, "  Username: %s", status.username.empty() ? "<none>" : status.username.c_str());
  for (size_t i = 0; i < this->numeric_inputs_.size(); i++) {
    ESP_LOGCONFIG(TAG, "  %s topic: %s", this->numeric_inputs_[i].log_name,
                  status.input_topics[i].empty() ? "<none>" : status.input_topics[i].c_str());
  }
  for (size_t i = 0; i < this->binary_inputs_.size(); i++) {
    ESP_LOGCONFIG(TAG, "  %s topic: %s", this->binary_inputs_[i].log_name,
                  status.binary_input_topics[i].empty() ? "<none>" : status.binary_input_topics[i].c_str());
  }
  ESP_LOGCONFIG(TAG, "  Runtime source: %s", status.config_source.empty() ? "<unknown>" : status.config_source.c_str());
  ESP_LOGCONFIG(TAG, "  HTTP handlers registered: %s", YESNO(this->handlers_registered_));
}

float OpenQuattMqttConfig::get_setup_priority() const { return setup_priority::LATE; }

OpenQuattMqttConfig::MutationResult OpenQuattMqttConfig::set_runtime_config(const std::string& broker, uint16_t port,
                                                                            const std::string& username,
                                                                            const std::string& password,
                                                                            bool clear_password, bool enabled) {
  Storage storage{};
  const MutationResult begin_result = this->begin_storage_mutation_(&storage);
  if (begin_result != MutationResult::OK) {
    return begin_result;
  }
  const uint8_t input_disabled_mask = storage.input_disabled_mask & INPUT_MASK_ALL;
  const uint8_t retained_disabled_mask = storage.retained_disabled_mask & STATEFUL_INPUT_MASK;
  const std::string current_password = storage.password;
  const std::string next_password = clear_password ? "" : (password.empty() ? current_password : password);
  if (!this->build_storage_(broker, port, username, next_password, enabled, input_disabled_mask, retained_disabled_mask,
                            &storage)) {
    this->cancel_storage_mutation_();
    return MutationResult::INVALID;
  }
  return this->submit_storage_mutation_(storage);
}

OpenQuattMqttConfig::MutationResult OpenQuattMqttConfig::set_input_enabled(const std::string& key, bool enabled) {
  uint8_t input_mask = 0;
  if (!this->input_mask_for_key_(key, &input_mask)) {
    return MutationResult::INVALID;
  }

  Storage storage{};
  const MutationResult begin_result = this->begin_storage_mutation_(&storage);
  if (begin_result != MutationResult::OK) {
    return begin_result;
  }

  uint8_t input_disabled_mask = storage.input_disabled_mask & INPUT_MASK_ALL;
  if (enabled) {
    input_disabled_mask &= static_cast<uint8_t>(~input_mask);
  } else {
    input_disabled_mask |= input_mask;
  }
  storage.input_disabled_mask = input_disabled_mask & INPUT_MASK_ALL;
  return this->submit_storage_mutation_(storage);
}

OpenQuattMqttConfig::MutationResult OpenQuattMqttConfig::set_input_accept_retained(const std::string& key,
                                                                                   bool accept_retained) {
  uint8_t input_mask = 0;
  if (!this->input_mask_for_key_(key, &input_mask) || (input_mask & STATEFUL_INPUT_MASK) == 0U) {
    return MutationResult::INVALID;
  }

  Storage storage{};
  const MutationResult begin_result = this->begin_storage_mutation_(&storage);
  if (begin_result != MutationResult::OK) {
    return begin_result;
  }

  uint8_t retained_disabled_mask = storage.retained_disabled_mask & STATEFUL_INPUT_MASK;
  if (accept_retained) {
    retained_disabled_mask &= static_cast<uint8_t>(~input_mask);
  } else {
    retained_disabled_mask |= input_mask;
  }
  storage.retained_disabled_mask = retained_disabled_mask & STATEFUL_INPUT_MASK;
  return this->submit_storage_mutation_(storage);
}

bool OpenQuattMqttConfig::load_storage_(Storage* storage) {
  if (storage == nullptr) {
    return false;
  }
  if (!this->pref_.load(storage)) {
    return false;
  }
  return this->is_valid_storage_(*storage);
}

void OpenQuattMqttConfig::initialize_storage_transaction_(const Storage& storage, const Storage& committed_storage,
                                                          bool persistence_pending) {
  this->lock_persistence_();
  this->desired_storage_ = storage;
  this->committed_storage_ = committed_storage;
  this->desired_storage_generation_ = 1U;
  // A fresh bootstrap has no HTTP waiter, but it must still be applied only
  // after the exact generation is proven durable.
  this->waiting_storage_generation_ = persistence_pending ? this->desired_storage_generation_ : 0U;
  this->completed_storage_generation_ = 0U;
  this->completed_storage_result_ = MutationResult::UNAVAILABLE;
  this->desired_storage_initialized_ = true;
  this->storage_persistence_pending_ = persistence_pending;
  this->storage_mutation_waiting_ = false;
  this->storage_write_started_.store(false);
  this->storage_transaction_phase_.store(persistence_pending ? StorageTransactionPhase::QUEUED
                                                             : StorageTransactionPhase::IDLE);
  this->unlock_persistence_();
  if (persistence_pending) {
    App.wake_loop_threadsafe();
  }
}

void OpenQuattMqttConfig::process_storage_transaction_() {
  Storage storage{};
  Storage committed_storage{};
  uint32_t generation = 0U;
  bool apply_after_persist = false;
  bool apply_as_bootstrap = false;

  this->lock_persistence_();
  if (!this->desired_storage_initialized_ || !this->storage_persistence_pending_) {
    this->unlock_persistence_();
    return;
  }
  StorageTransactionPhase expected = StorageTransactionPhase::QUEUED;
  if (!this->storage_transaction_phase_.compare_exchange_strong(expected, StorageTransactionPhase::PROCESSING)) {
    if (expected != StorageTransactionPhase::CANCELLED) {
      this->unlock_persistence_();
      return;
    }
  }
  storage = this->desired_storage_;
  committed_storage = this->committed_storage_;
  generation = this->desired_storage_generation_;
  apply_after_persist = this->waiting_storage_generation_ == generation;
  apply_as_bootstrap = apply_after_persist && !this->storage_mutation_waiting_;
  this->unlock_persistence_();

  if (this->storage_transaction_phase_.load() == StorageTransactionPhase::CANCELLED) {
    this->finish_storage_transaction_(generation, MutationResult::TIMEOUT, committed_storage, false);
    return;
  }

  if (apply_after_persist && !this->preflight_storage_apply_(storage)) {
    this->finish_storage_transaction_(generation, MutationResult::APPLY_FAILED, committed_storage, false);
    return;
  }

  bool durable = false;
  bool any_save_succeeded = false;
  MutationResult failure_result = MutationResult::SAVE_FAILED;
  for (uint8_t attempt = 0U; attempt < STORAGE_MAX_ATTEMPTS; attempt++) {
    if (!storage_should_retry(attempt, STORAGE_MAX_ATTEMPTS, durable,
                              this->storage_transaction_phase_.load() == StorageTransactionPhase::CANCELLED)) {
      break;
    }
    if (attempt == 0U) {
      // Claim the write before touching the preference backend. A timeout
      // may still change WRITING to CANCELLED, but it can then only report
      // recovery-pending until this loop has proven the rollback.
      this->storage_write_started_.store(true);
      StorageTransactionPhase write_expected = StorageTransactionPhase::PROCESSING;
      if (!this->storage_transaction_phase_.compare_exchange_strong(write_expected, StorageTransactionPhase::WRITING)) {
        this->storage_write_started_.store(false);
        break;
      }
    }
    // Only the ESPHome loop touches the preference backend. Every bounded
    // retry repeats save + sync with the complete candidate payload.
    const bool save_succeeded = this->pref_.save(&storage);
    any_save_succeeded = any_save_succeeded || save_succeeded;
    if (!save_succeeded) {
      ESP_LOGE(TAG,
               "Failed to save MQTT configuration to preferences "
               "(attempt %u/%u)",
               static_cast<unsigned>(attempt + 1U), static_cast<unsigned>(STORAGE_MAX_ATTEMPTS));
      failure_result = MutationResult::SAVE_FAILED;
      continue;
    }
    if (global_preferences->sync()) {
      durable = true;
      break;
    }
    ESP_LOGE(TAG,
             "Failed to sync MQTT configuration preferences "
             "(attempt %u/%u)",
             static_cast<unsigned>(attempt + 1U), static_cast<unsigned>(STORAGE_MAX_ATTEMPTS));
    failure_result = MutationResult::SYNC_FAILED;
  }

  this->lock_persistence_();
  const bool generation_is_current = generation == this->desired_storage_generation_;
  this->unlock_persistence_();
  const bool cancelled =
      this->storage_transaction_phase_.load() == StorageTransactionPhase::CANCELLED || !generation_is_current;
  if (!storage_generation_may_commit(durable, generation_is_current, cancelled)) {
    const bool recovered = !any_save_succeeded || this->restore_committed_storage_(committed_storage);
    if (!recovered) {
      ESP_LOGE(TAG,
               "MQTT configuration rollback could not be proven; keeping "
               "runtime fail-closed");
      this->start_permanent_failure_cleanup_();
    }
    this->finish_storage_transaction_(
        generation,
        recovered ? (cancelled ? MutationResult::TIMEOUT : failure_result) : MutationResult::RECOVERY_FAILED,
        committed_storage, false);
    return;
  }

  if (!apply_after_persist) {
    this->finish_storage_transaction_(generation, MutationResult::OK, storage, true);
    return;
  }

  StorageTransactionPhase commit_expected = StorageTransactionPhase::WRITING;
  if (!this->storage_transaction_phase_.compare_exchange_strong(commit_expected, StorageTransactionPhase::COMMITTING)) {
    const bool recovered = this->restore_committed_storage_(committed_storage);
    if (!recovered) {
      this->start_permanent_failure_cleanup_();
    }
    this->finish_storage_transaction_(generation, recovered ? MutationResult::TIMEOUT : MutationResult::RECOVERY_FAILED,
                                      committed_storage, false);
    return;
  }

  const StorageApplyResult apply_result = this->apply_storage_(storage, apply_as_bootstrap ? "bootstrap" : "runtime");
  if (apply_result == StorageApplyResult::FAILED) {
    const bool recovered = this->restore_committed_storage_(committed_storage);
    if (recovered && !this->is_failed() && !this->permanent_failure_cleanup_pending_.load()) {
      this->apply_storage_(committed_storage, "rollback");
    } else if (!recovered) {
      this->start_permanent_failure_cleanup_();
    }
    this->finish_storage_transaction_(generation,
                                      recovered ? MutationResult::APPLY_FAILED : MutationResult::RECOVERY_FAILED,
                                      committed_storage, false);
    return;
  }
  this->finish_storage_transaction_(
      generation,
      apply_result == StorageApplyResult::RUNTIME_PENDING ? MutationResult::RUNTIME_PENDING : MutationResult::OK,
      storage, true);
}

bool OpenQuattMqttConfig::restore_committed_storage_(const Storage& storage) {
  for (uint8_t attempt = 0U; attempt < STORAGE_MAX_ATTEMPTS; attempt++) {
    const bool saved = this->pref_.save(&storage);
    const bool synced = saved && global_preferences->sync();
    if (synced) {
      return true;
    }
    ESP_LOGE(TAG,
             "Failed to restore previous MQTT configuration "
             "(attempt %u/%u)",
             static_cast<unsigned>(attempt + 1U), static_cast<unsigned>(STORAGE_MAX_ATTEMPTS));
  }
  return false;
}

bool OpenQuattMqttConfig::preflight_storage_apply_(const Storage& storage) {
  this->lock_config_();
  const bool client_config_changed = this->enabled_.load() != (storage.enabled != 0U) ||
                                     this->broker_ != storage.broker || this->port_ != storage.port ||
                                     this->username_ != storage.username || this->password_ != storage.password;
  this->unlock_config_();
  const bool should_start = storage.enabled != 0U && storage.broker[0] != '\0';
  const bool client_present = this->mqtt_client_present_.load();
  const uint32_t requested_generation = this->client_requested_generation_.load();
  const bool lifecycle_unresolved = this->client_reconcile_pending_.load() ||
                                    this->client_applied_generation_.load() != requested_generation ||
                                    (client_present && !this->active_client_matches_(requested_generation));
  const bool needs_reconcile = client_config_changed || lifecycle_unresolved || (should_start && !client_present);
  const bool needs_worker = needs_reconcile && (client_present || (should_start && network::is_connected()));
  return !needs_worker || this->ensure_client_worker_();
}

void OpenQuattMqttConfig::finish_storage_transaction_(uint32_t generation, MutationResult result,
                                                      const Storage& desired_storage, bool commit_storage) {
  bool notify_waiter = false;
  this->lock_persistence_();
  if (generation == this->desired_storage_generation_) {
    this->desired_storage_ = desired_storage;
    if (commit_storage) {
      this->committed_storage_ = desired_storage;
    }
    this->storage_persistence_pending_ = false;
    this->completed_storage_generation_ = generation;
    this->completed_storage_result_ = result;
    notify_waiter = this->storage_mutation_waiting_ && this->waiting_storage_generation_ == generation;
    this->storage_mutation_waiting_ = false;
    this->storage_write_started_.store(false);
    this->storage_transaction_phase_.store(StorageTransactionPhase::COMPLETED);
  }
  this->unlock_persistence_();
  if (notify_waiter) {
    xSemaphoreGive(this->storage_mutation_complete_);
  }
}

OpenQuattMqttConfig::MutationResult OpenQuattMqttConfig::begin_storage_mutation_(Storage* storage) {
  if (this->is_failed() || this->permanent_failure_cleanup_pending_.load() || storage == nullptr ||
      this->storage_mutation_lock_ == nullptr || this->storage_mutation_complete_ == nullptr ||
      this->persistence_lock_ == nullptr) {
    return MutationResult::UNAVAILABLE;
  }
  if (xSemaphoreTake(this->storage_mutation_lock_, 0) != pdTRUE) {
    return MutationResult::BUSY;
  }

  this->lock_persistence_();
  const StorageTransactionPhase phase = this->storage_transaction_phase_.load();
  if (!this->desired_storage_initialized_ || this->storage_persistence_pending_ ||
      (phase != StorageTransactionPhase::IDLE && phase != StorageTransactionPhase::COMPLETED)) {
    this->unlock_persistence_();
    xSemaphoreGive(this->storage_mutation_lock_);
    return MutationResult::BUSY;
  }
  *storage = this->desired_storage_;
  this->unlock_persistence_();
  return MutationResult::OK;
}

OpenQuattMqttConfig::MutationResult OpenQuattMqttConfig::submit_storage_mutation_(const Storage& storage) {
  while (xSemaphoreTake(this->storage_mutation_complete_, 0) == pdTRUE) {
  }

  this->lock_persistence_();
  uint32_t generation = this->desired_storage_generation_ + 1U;
  if (generation == 0U) {
    generation = 1U;
  }
  this->desired_storage_ = storage;
  this->desired_storage_generation_ = generation;
  this->waiting_storage_generation_ = generation;
  this->completed_storage_generation_ = 0U;
  this->completed_storage_result_ = MutationResult::UNAVAILABLE;
  this->storage_persistence_pending_ = true;
  this->storage_mutation_waiting_ = true;
  this->storage_transaction_phase_.store(StorageTransactionPhase::QUEUED);
  this->unlock_persistence_();

  App.wake_loop_threadsafe();
  MutationResult result = MutationResult::TIMEOUT;
  const TickType_t wait_ticks = pdMS_TO_TICKS(STORAGE_MUTATION_WAIT_MS);
  const TickType_t wait_started = xTaskGetTickCount();
  while (true) {
    const TickType_t elapsed = xTaskGetTickCount() - wait_started;
    if (elapsed >= wait_ticks || xSemaphoreTake(this->storage_mutation_complete_, wait_ticks - elapsed) != pdTRUE) {
      break;
    }
    this->lock_persistence_();
    const bool exact_generation_completed = this->completed_storage_generation_ == generation;
    if (exact_generation_completed) {
      result = this->completed_storage_result_;
    }
    this->unlock_persistence_();
    if (exact_generation_completed) {
      break;
    }
  }

  bool exact_generation_completed = false;
  if (xSemaphoreTake(this->persistence_lock_, pdMS_TO_TICKS(50U)) == pdTRUE) {
    exact_generation_completed = this->completed_storage_generation_ == generation;
    if (exact_generation_completed) {
      result = this->completed_storage_result_;
    }
    xSemaphoreGive(this->persistence_lock_);
  }

  bool commit_won_cancellation_race = false;
  if (!exact_generation_completed) {
    StorageTransactionPhase phase = this->storage_transaction_phase_.load();
    if (storage_timeout_action(false, phase == StorageTransactionPhase::COMMITTING) == StorageTimeoutAction::CANCEL) {
      while (phase == StorageTransactionPhase::QUEUED || phase == StorageTransactionPhase::PROCESSING ||
             phase == StorageTransactionPhase::WRITING) {
        if (this->storage_transaction_phase_.compare_exchange_weak(phase, StorageTransactionPhase::CANCELLED)) {
          break;
        }
      }
    }
    commit_won_cancellation_race = phase == StorageTransactionPhase::COMMITTING;
    App.wake_loop_threadsafe();

    const TickType_t grace_ticks = pdMS_TO_TICKS(STORAGE_CANCELLATION_GRACE_MS);
    const TickType_t grace_started = xTaskGetTickCount();
    while (true) {
      const TickType_t elapsed = xTaskGetTickCount() - grace_started;
      if (elapsed >= grace_ticks || xSemaphoreTake(this->storage_mutation_complete_, grace_ticks - elapsed) != pdTRUE) {
        break;
      }
      if (xSemaphoreTake(this->persistence_lock_, pdMS_TO_TICKS(50U)) == pdTRUE) {
        exact_generation_completed = this->completed_storage_generation_ == generation;
        if (exact_generation_completed) {
          result = this->completed_storage_result_;
        }
        xSemaphoreGive(this->persistence_lock_);
      }
      if (exact_generation_completed) {
        break;
      }
    }
  }

  if (!exact_generation_completed && xSemaphoreTake(this->persistence_lock_, pdMS_TO_TICKS(50U)) == pdTRUE) {
    exact_generation_completed = this->completed_storage_generation_ == generation;
    if (exact_generation_completed) {
      result = this->completed_storage_result_;
    }
    xSemaphoreGive(this->persistence_lock_);
  }
  const StorageTimeoutAction final_timeout_action = storage_timeout_action(
      exact_generation_completed,
      commit_won_cancellation_race || this->storage_transaction_phase_.load() == StorageTransactionPhase::COMMITTING);
  if (final_timeout_action == StorageTimeoutAction::REPORT_PERSISTENCE_PENDING) {
    // The commit won the cancellation race but has not completed. Fail this
    // request closed so sequential restore clients cannot mistake the 2xx
    // lifecycle-pending response for durable mutation completion.
    result = MutationResult::PERSISTENCE_PENDING;
  } else if (!exact_generation_completed && this->storage_write_started_.load()) {
    // Cancellation won, so runtime apply is prohibited. A slow NVS rollback
    // may still be finishing; expose that reboot-sensitive state explicitly.
    result = MutationResult::RECOVERY_PENDING;
  }
  xSemaphoreGive(this->storage_mutation_lock_);
  return result;
}

void OpenQuattMqttConfig::cancel_storage_mutation_() {
  if (this->storage_mutation_lock_ != nullptr) {
    xSemaphoreGive(this->storage_mutation_lock_);
  }
}

OpenQuattMqttConfig::StorageApplyResult OpenQuattMqttConfig::apply_storage_(const Storage& storage,
                                                                            const char* source) {
  const bool enabled = storage.enabled != 0U;
  bool clear_all_inputs = !enabled;
  this->lock_config_();
  const bool previous_enabled = this->enabled_.load();
  const std::string previous_broker = this->broker_;
  const uint16_t previous_port = this->port_;
  const std::string previous_username = this->username_;
  const std::string previous_password = this->password_;
  const uint8_t previous_input_disabled_mask = this->input_disabled_mask_.load() & INPUT_MASK_ALL;
  const uint8_t previous_retained_disabled_mask = this->retained_disabled_mask_.load() & STATEFUL_INPUT_MASK;
  const uint8_t next_input_disabled_mask = storage.input_disabled_mask & INPUT_MASK_ALL;
  const uint8_t next_retained_disabled_mask = storage.retained_disabled_mask & STATEFUL_INPUT_MASK;
  const bool client_config_changed = previous_enabled != enabled || previous_broker != storage.broker ||
                                     previous_port != storage.port || previous_username != storage.username ||
                                     previous_password != storage.password;
  if (client_config_changed) {
    this->close_client_event_gate_();
    this->mqtt_session_generation_.fetch_add(1U);
  }
  this->broker_ = storage.broker;
  this->port_ = storage.port;
  this->username_ = storage.username;
  this->password_ = storage.password;
  this->enabled_.store(enabled);
  this->input_disabled_mask_.store(next_input_disabled_mask);
  this->retained_disabled_mask_.store(next_retained_disabled_mask);
  this->config_source_ = source != nullptr ? source : "";
  const bool broker_empty = this->broker_.empty();
  clear_all_inputs = clear_all_inputs || broker_empty || previous_broker != this->broker_ ||
                     previous_port != this->port_ || previous_username != this->username_ ||
                     previous_password != this->password_;
  this->unlock_config_();
  if (clear_all_inputs) {
    this->clear_session_scoped_inputs_pending_.store(false);
  }
  if (enabled) {
    if (clear_all_inputs) {
      this->clear_all_inputs_();
    } else {
      this->clear_disabled_inputs_();
    }
  } else {
    this->clear_all_inputs_();
  }

  const uint8_t newly_enabled_inputs =
      previous_input_disabled_mask & static_cast<uint8_t>(~next_input_disabled_mask) & INPUT_MASK_ALL;
  const uint8_t newly_accepted_retained_inputs =
      previous_retained_disabled_mask & static_cast<uint8_t>(~next_retained_disabled_mask) & STATEFUL_INPUT_MASK;
  const uint8_t newly_rejected_retained_inputs =
      static_cast<uint8_t>(~previous_retained_disabled_mask) & next_retained_disabled_mask & STATEFUL_INPUT_MASK;
  if (newly_rejected_retained_inputs != 0U) {
    this->clear_input_mask_pending_.fetch_or(newly_rejected_retained_inputs);
  }
  if (enabled && !broker_empty && (newly_enabled_inputs != 0U || newly_accepted_retained_inputs != 0U)) {
    this->resubscribe_inputs_.store(true);
  }

  uint32_t requested_generation = this->client_requested_generation_.load();
  if (client_config_changed) {
    requested_generation = this->client_requested_generation_.fetch_add(1U) + 1U;
    this->client_reconcile_pending_.store(true);
    this->next_reconcile_attempt_ms_.store(0U);
  }

  const bool client_present = this->mqtt_client_present_.load();
  const bool should_start = enabled && !broker_empty;
  const bool lifecycle_unresolved = this->client_reconcile_pending_.load() ||
                                    this->client_applied_generation_.load() != requested_generation ||
                                    (client_present && !this->active_client_matches_(requested_generation));
  if (!client_present && !should_start) {
    this->client_applied_generation_.store(requested_generation);
    this->client_reconcile_pending_.store(false);
  } else if (client_config_changed || lifecycle_unresolved || !client_present) {
    this->client_reconcile_pending_.store(true);
    this->next_reconcile_attempt_ms_.store(0U);
    if (client_present || network::is_connected()) {
      if (!this->request_client_reconcile_()) {
        const bool permanently_failed = this->is_failed() || this->permanent_failure_cleanup_pending_.load();
        ESP_LOGE(TAG, "MQTT ingress client generation %" PRIu32 " is fail-closed and reconciliation is %s",
                 requested_generation, permanently_failed ? "unavailable" : "pending retry");
        this->force_publish_.store(true);
        return permanently_failed ? StorageApplyResult::FAILED : StorageApplyResult::RUNTIME_PENDING;
      }
    }
  }

  if (!enabled || broker_empty) {
    if (enabled) {
      ESP_LOGW(TAG,
               "MQTT is enabled but no broker is configured; runtime connection "
               "is disabled");
    }
  }
  this->force_publish_.store(true);
  return StorageApplyResult::APPLIED;
}

bool OpenQuattMqttConfig::build_storage_(const std::string& broker, uint16_t port, const std::string& username,
                                         const std::string& password, bool enabled, uint8_t input_disabled_mask,
                                         uint8_t retained_disabled_mask, Storage* storage) {
  if (storage == nullptr) {
    return false;
  }
  if (broker.size() > BROKER_MAX_LEN || username.size() > USERNAME_MAX_LEN || password.size() > PASSWORD_MAX_LEN) {
    return false;
  }
  if (port == 0U) {
    return false;
  }

  memset(storage, 0, sizeof(Storage));
  storage->magic = STORAGE_MAGIC;
  storage->version = STORAGE_VERSION;
  storage->port = port;
  storage->enabled = enabled ? 1U : 0U;
  storage->input_disabled_mask = input_disabled_mask & INPUT_MASK_ALL;
  copy_string_field_(storage->broker, BROKER_MAX_LEN, broker);
  copy_string_field_(storage->username, USERNAME_MAX_LEN, username);
  copy_string_field_(storage->password, PASSWORD_MAX_LEN, password);
  storage->retained_disabled_mask = retained_disabled_mask & STATEFUL_INPUT_MASK;
  return true;
}

bool OpenQuattMqttConfig::is_valid_storage_(const Storage& storage) const {
  if (storage.magic != STORAGE_MAGIC || storage.version != STORAGE_VERSION) {
    return false;
  }
  if (storage.port == 0U || storage.enabled > 1U) {
    return false;
  }
  if ((storage.input_disabled_mask & static_cast<uint8_t>(~INPUT_MASK_ALL)) != 0U ||
      (storage.retained_disabled_mask & static_cast<uint8_t>(~STATEFUL_INPUT_MASK)) != 0U) {
    return false;
  }
  const size_t broker_len = strnlen(storage.broker, BROKER_MAX_LEN + 1U);
  const size_t username_len = strnlen(storage.username, USERNAME_MAX_LEN + 1U);
  const size_t password_len = strnlen(storage.password, PASSWORD_MAX_LEN + 1U);
  return broker_len <= BROKER_MAX_LEN && username_len <= USERNAME_MAX_LEN && password_len <= PASSWORD_MAX_LEN;
}

bool OpenQuattMqttConfig::register_http_handlers_() {
  if (this->handlers_registered_) {
    return true;
  }
  auto* base = web_server_base::global_web_server_base;
  if (base == nullptr) {
    ESP_LOGW(TAG, "Web server base not available; MQTT config API disabled");
    return false;
  }
  base->add_handler(new MqttConfigHandler(this));
  this->handlers_registered_ = true;
  return true;
}

void OpenQuattMqttConfig::rotate_csrf_token_() {
  std::array<uint8_t, 16> token_bytes{};
  for (auto& byte : token_bytes) {
    byte = static_cast<uint8_t>(random_uint32() & 0xFF);
  }
  this->csrf_token_ = base64_encode_bytes_(token_bytes.data(), token_bytes.size());
}

OpenQuattMqttConfig::ClientConfig OpenQuattMqttConfig::get_desired_client_config_() {
  ClientConfig config;
  this->lock_config_();
  config.enabled = this->enabled_.load();
  this->copy_string_field_(config.broker.data(), BROKER_MAX_LEN, this->broker_);
  config.port = this->port_;
  this->copy_string_field_(config.username.data(), USERNAME_MAX_LEN, this->username_);
  this->copy_string_field_(config.password.data(), PASSWORD_MAX_LEN, this->password_);
  this->unlock_config_();
  return config;
}

bool OpenQuattMqttConfig::active_client_matches_(uint32_t requested_generation) {
  const bool matches = this->active_client_generation_.load() == requested_generation &&
                       this->client_events_enabled_.load() &&
                       this->callback_session_generation_.load() == this->mqtt_session_generation_.load();
  return matches;
}

bool OpenQuattMqttConfig::ensure_client_worker_() {
  if (this->worker_lock_ == nullptr || xSemaphoreTake(this->worker_lock_, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  if (this->client_worker_task_state_.is_created()) {
    const bool valid = this->client_worker_region_valid_;
    xSemaphoreGive(this->worker_lock_);
    return valid;
  }

  this->client_worker_region_valid_ = false;
  const bool created =
      this->client_worker_task_state_.create(&OpenQuattMqttConfig::client_worker_task_, "oq_mqtt_client",
                                             MQTT_WORKER_TASK_STACK_SIZE, this, 5, MQTT_WORKER_STACK_IN_PSRAM);
  if (!created) {
    xSemaphoreGive(this->worker_lock_);
    ESP_LOGE(TAG, "Failed to create %u-byte MQTT ingress worker in %s",
             static_cast<unsigned>(MQTT_WORKER_TASK_STACK_SIZE), MQTT_WORKER_STACK_IN_PSRAM ? "PSRAM" : "internal RAM");
    return false;
  }

  const TaskHandle_t handle = this->client_worker_task_state_.get_handle();
  const bool stack_is_external = esp_ptr_external_ram(pxTaskGetStackStart(handle));
  if (stack_is_external != MQTT_WORKER_STACK_IN_PSRAM) {
    ESP_LOGE(TAG,
             "MQTT ingress worker stack was allocated in the wrong memory region; "
             "releasing it and disabling the component");
    vTaskSuspend(handle);
    this->client_worker_task_state_.deallocate();
    this->client_worker_active_.store(false);
    xSemaphoreGive(this->worker_lock_);
    this->start_permanent_failure_cleanup_();
    return false;
  }
  this->client_worker_region_valid_ = true;
  ESP_LOGD(TAG, "MQTT ingress worker stack: %u bytes in %s", static_cast<unsigned>(MQTT_WORKER_TASK_STACK_SIZE),
           stack_is_external ? "PSRAM" : "internal RAM");
  xSemaphoreGive(this->worker_lock_);
  return true;
}

void OpenQuattMqttConfig::start_permanent_failure_cleanup_() {
  if (!this->permanent_failure_cleanup_pending_.exchange(true)) {
    this->close_client_event_gate_();
    this->mqtt_session_generation_.fetch_add(1U);
    const uint32_t failed_generation = this->client_requested_generation_.fetch_add(1U) + 1U;
    this->client_applied_generation_.store(failed_generation);
    this->clear_session_scoped_inputs_pending_.store(false);
    this->clear_all_inputs_();
    this->client_reconcile_pending_.store(false);
    this->next_reconcile_attempt_ms_.store(0U);
    this->force_publish_.store(true);
  }
  this->process_permanent_failure_cleanup_();
}

bool OpenQuattMqttConfig::process_permanent_failure_cleanup_() {
  if (!this->permanent_failure_cleanup_pending_.load()) {
    return false;
  }
  this->close_client_event_gate_();
  if (!time_reached_(millis(), this->next_reconcile_attempt_ms_.load())) {
    return true;
  }
  if (this->client_worker_active_.load()) {
    return true;
  }
  if (!this->stop_client_()) {
    this->next_reconcile_attempt_ms_.store(millis() + MQTT_RECONCILE_RETRY_MS);
    this->force_publish_.store(true);
    return true;
  }
  this->maybe_release_classic_worker_();
  this->permanent_failure_cleanup_pending_.store(false);
  this->mark_failed();
  return true;
}

bool OpenQuattMqttConfig::request_client_reconcile_() {
  if (!this->ensure_client_worker_()) {
    const bool permanently_unavailable = this->is_failed() || this->permanent_failure_cleanup_pending_.load();
    this->client_reconcile_pending_.store(!permanently_unavailable);
    this->client_worker_active_.store(false);
    if (!permanently_unavailable) {
      this->next_reconcile_attempt_ms_.store(millis() + MQTT_RECONCILE_RETRY_MS);
    }
    return false;
  }
  if (xSemaphoreTake(this->worker_lock_, portMAX_DELAY) != pdTRUE) {
    return false;
  }
  const TaskHandle_t handle = this->client_worker_task_state_.get_handle();
  const eTaskState state = handle == nullptr ? eDeleted : eTaskGetState(handle);
  this->client_worker_active_.store(true);
  const BaseType_t notified = handle == nullptr ? pdFAIL : xTaskNotifyGive(handle);
  if (notified == pdPASS && state == eSuspended) {
    vTaskResume(handle);
  }
  xSemaphoreGive(this->worker_lock_);
  if (notified != pdPASS) {
    this->client_reconcile_pending_.store(true);
    this->client_worker_active_.store(false);
    this->next_reconcile_attempt_ms_.store(millis() + MQTT_RECONCILE_RETRY_MS);
    ESP_LOGE(TAG, "Failed to notify MQTT ingress lifecycle worker");
    return false;
  }
  App.wake_loop_threadsafe();
  return true;
}

OpenQuattMqttConfig::ClientReconcileResult OpenQuattMqttConfig::reconcile_client_(uint32_t requested_generation) {
  while (true) {
    const ClientConfig desired = this->get_desired_client_config_();
    const bool client_present = this->mqtt_client_present_.load();
    const bool client_matches = client_present && this->active_client_matches_(requested_generation);
    const MqttClientAction action = mqtt_client_action(desired.enabled, desired.broker[0] != '\0',
                                                       network::is_connected(), client_present, client_matches);

    if (action == MqttClientAction::STOP) {
      while (!this->stop_client_()) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
      }
      continue;
    }
    if (this->client_requested_generation_.load() != requested_generation) {
      return ClientReconcileResult::APPLIED;
    }
    if (action == MqttClientAction::WAIT_FOR_NETWORK) {
      return ClientReconcileResult::WAIT_FOR_NETWORK;
    }
    if (action == MqttClientAction::START) {
      const uint32_t session_generation = this->mqtt_session_generation_.load();
      if (!this->start_client_(desired, requested_generation, session_generation)) {
        return ClientReconcileResult::RETRY;
      }
      continue;
    }
    return ClientReconcileResult::APPLIED;
  }
}

bool OpenQuattMqttConfig::start_client_(const ClientConfig& config, uint32_t requested_generation,
                                        uint32_t session_generation) {
  this->lock_runtime_();
  if (this->mqtt_client_ != nullptr || this->client_requested_generation_.load() != requested_generation) {
    this->unlock_runtime_();
    return false;
  }

  esp_mqtt_client_config_t mqtt_config{};
  // esp_mqtt_client_init() copies these strings into client-owned storage.
  std::array<char, MQTT_CLIENT_ID_MAX_LEN> client_id{};
  const int client_id_len = snprintf(client_id.data(), client_id.size(), "%s-mqtt-ingress", App.get_name().c_str());
  if (client_id_len < 0 || static_cast<size_t>(client_id_len) >= client_id.size()) {
    this->unlock_runtime_();
    ESP_LOGE(TAG, "MQTT ingress client ID is too long");
    return false;
  }
  mqtt_config.broker.address.hostname = config.broker.data();
  mqtt_config.broker.address.port = config.port;
  mqtt_config.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
  mqtt_config.credentials.client_id = client_id.data();
  mqtt_config.session.keepalive = 30;
  mqtt_config.session.disable_clean_session = false;
  mqtt_config.task.stack_size = MQTT_TASK_STACK_SIZE;
  if (config.username[0] != '\0') {
    mqtt_config.credentials.username = config.username.data();
    if (config.password[0] != '\0') {
      mqtt_config.credentials.authentication.password = config.password.data();
    }
  }

  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_config);
  if (client == nullptr) {
    this->unlock_runtime_();
    ESP_LOGE(TAG, "Failed to initialize MQTT ingress client");
    return false;
  }

  this->mqtt_client_ = client;
  this->mqtt_client_present_.store(true);
  this->active_client_generation_.store(requested_generation);
  this->mqtt_client_started_ = false;
  this->disconnect_requested_ = false;
  this->mqtt_connected_seen_.store(false);
  this->mqtt_disconnected_seen_.store(false);
  this->mqtt_transport_connected_.store(false);
  this->callback_session_generation_.store(session_generation);
  this->callback_client_.store(client);
  this->client_events_enabled_.store(true);

  esp_err_t error =
      esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, &OpenQuattMqttConfig::mqtt_event_handler_, this);
  if (error == ESP_OK) {
    error = esp_mqtt_client_start(client);
  }
  if (error != ESP_OK) {
    this->close_client_event_gate_();
    this->callback_client_.store(nullptr);
    const esp_err_t destroy_error = esp_mqtt_client_destroy(client);
    if (destroy_error != ESP_OK) {
      this->unlock_runtime_();
      ESP_LOGE(TAG,
               "Failed to start MQTT ingress client (%s) and failed to release "
               "its client resources (%s); cleanup will be retried",
               esp_err_to_name(error), esp_err_to_name(destroy_error));
      return false;
    }
    this->mqtt_client_ = nullptr;
    this->mqtt_client_present_.store(false);
    this->clear_active_client_state_();
    this->unlock_runtime_();
    ESP_LOGE(TAG, "Failed to start MQTT ingress client: %s", esp_err_to_name(error));
    return false;
  }

  this->mqtt_client_started_ = true;
  this->unlock_runtime_();
  ESP_LOGI(TAG, "MQTT ingress client starting for %s:%u", config.broker.data(), config.port);
  return true;
}

bool OpenQuattMqttConfig::stop_client_() {
  this->lock_runtime_();
  esp_mqtt_client_handle_t client = this->mqtt_client_;
  if (client == nullptr) {
    this->mqtt_client_present_.store(false);
    this->connected_.store(false);
    this->unlock_runtime_();
    return true;
  }

  this->close_client_event_gate_();
  if (this->mqtt_client_started_) {
    const esp_err_t stop_error = esp_mqtt_client_stop(client);
    if (stop_error != ESP_OK && stop_error != ESP_FAIL) {
      ESP_LOGE(TAG, "Unexpected MQTT ingress stop error: %s", esp_err_to_name(stop_error));
      this->unlock_runtime_();
      return false;
    }
    const MqttStopDecision decision =
        mqtt_stop_decision(stop_error == ESP_OK, this->mqtt_transport_connected_.load(),
                           this->mqtt_connected_seen_.load(), this->mqtt_disconnected_seen_.load());
    if (decision == MqttStopDecision::FORCE_DISCONNECT) {
      if (!this->disconnect_requested_) {
        ESP_LOGW(TAG, "MQTT ingress stop failed (%s); forcing disconnect before retry", esp_err_to_name(stop_error));
        this->disconnect_requested_ = true;
      }
      const esp_err_t disconnect_error = esp_mqtt_client_disconnect(client);
      if (disconnect_error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to force MQTT ingress disconnect: %s", esp_err_to_name(disconnect_error));
      }
      this->unlock_runtime_();
      return false;
    }
    if (decision == MqttStopDecision::RETRY_STOP) {
      this->unlock_runtime_();
      return false;
    }
    if (decision == MqttStopDecision::DESTROY_ALREADY_STOPPED) {
      ESP_LOGW(TAG,
               "MQTT ingress task stopped before lifecycle cleanup; "
               "releasing its client resources");
    }
    this->mqtt_client_started_ = false;
  }

  this->callback_client_.store(nullptr);
  const esp_err_t destroy_error = esp_mqtt_client_destroy(client);
  if (destroy_error != ESP_OK) {
    this->callback_client_.store(client);
    this->unlock_runtime_();
    ESP_LOGE(TAG, "Failed to destroy MQTT ingress client: %s", esp_err_to_name(destroy_error));
    return false;
  }

  this->mqtt_client_ = nullptr;
  this->mqtt_client_present_.store(false);
  this->mqtt_client_started_ = false;
  this->disconnect_requested_ = false;
  this->mqtt_connected_seen_.store(false);
  this->mqtt_disconnected_seen_.store(false);
  this->mqtt_transport_connected_.store(false);
  this->clear_active_client_state_();
  this->unlock_runtime_();
  log_heap_state_("MQTT ingress client destroyed");
  return true;
}

void OpenQuattMqttConfig::clear_active_client_state_() { this->active_client_generation_.store(0U); }

void OpenQuattMqttConfig::close_client_event_gate_() {
  this->client_events_enabled_.store(false);
  this->connected_.store(false);
  this->resubscribe_inputs_.store(false);
}

void OpenQuattMqttConfig::maybe_release_classic_worker_() {
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
  if (this->worker_lock_ == nullptr || xSemaphoreTake(this->worker_lock_, 0) != pdTRUE) {
    return;
  }
  const TaskHandle_t handle = this->client_worker_task_state_.get_handle();
  if (handle != nullptr) {
    const eTaskState state = eTaskGetState(handle);
    const bool work_pending = this->client_reconcile_pending_.load() ||
                              this->client_requested_generation_.load() != this->client_applied_generation_.load();
    const bool can_reconcile = this->mqtt_client_present_.load() || network::is_connected();
    const bool retry_due = time_reached_(millis(), this->next_reconcile_attempt_ms_.load());
    const bool waiting_or_suspended = state == eBlocked || state == eSuspended;
    const ClassicWorkerAction action = classic_worker_action(waiting_or_suspended, this->client_worker_active_.load(),
                                                             work_pending, can_reconcile, retry_due);
    if (action == ClassicWorkerAction::WAKE) {
      this->client_worker_active_.store(true);
      if (xTaskNotifyGive(handle) == pdPASS) {
        if (state == eSuspended) {
          vTaskResume(handle);
        }
      } else {
        this->client_worker_active_.store(false);
        this->next_reconcile_attempt_ms_.store(millis() + MQTT_RECONCILE_RETRY_MS);
        ESP_LOGE(TAG, "Failed to wake classic-ESP32 MQTT ingress worker");
      }
    } else if (action == ClassicWorkerAction::RELEASE) {
      this->client_worker_task_state_.deallocate();
      this->client_worker_region_valid_ = false;
      ESP_LOGD(TAG, "Released idle classic-ESP32 MQTT ingress worker");
    }
  }
  xSemaphoreGive(this->worker_lock_);
#endif
}

void OpenQuattMqttConfig::client_worker_task_(void* arg) {
  auto* self = static_cast<OpenQuattMqttConfig*>(arg);
  if (self == nullptr) {
    while (true) {
      vTaskSuspend(nullptr);
    }
  }

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    self->client_worker_active_.store(true);
    log_heap_state_("MQTT ingress reconcile begin");

    while (true) {
      if (self->permanent_failure_cleanup_pending_.load()) {
        self->client_reconcile_pending_.store(false);
        break;
      }
      const uint32_t requested_generation = self->client_requested_generation_.load();
      const ClientReconcileResult result = self->reconcile_client_(requested_generation);
      if (self->client_requested_generation_.load() != requested_generation) {
        continue;
      }

      if (result != ClientReconcileResult::RETRY) {
        self->client_applied_generation_.store(requested_generation);
      }
      const bool retry = result != ClientReconcileResult::APPLIED;
      self->client_reconcile_pending_.store(retry);
      self->next_reconcile_attempt_ms_.store(retry ? millis() + MQTT_RECONCILE_RETRY_MS : 0U);
      break;
    }

    ESP_LOGD(TAG, "MQTT ingress worker stack free after reconcile: %u bytes",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    log_heap_state_("MQTT ingress reconcile complete");
    self->force_publish_.store(true);
    self->client_worker_active_.store(false);
    App.wake_loop_threadsafe();
  }
}

void OpenQuattMqttConfig::set_numeric_input_topic_(NumericInputKind kind, const std::string& topic) {
  this->numeric_input_(kind).topic = topic;
}

void OpenQuattMqttConfig::set_numeric_input_stale_ms_(NumericInputKind kind, uint32_t stale_ms) {
  this->numeric_input_(kind).stale_ms = stale_ms;
}

void OpenQuattMqttConfig::set_numeric_input_sensor_(NumericInputKind kind, sensor::Sensor* sensor) {
  this->numeric_input_(kind).sensor = sensor;
}

void OpenQuattMqttConfig::set_numeric_input_age_sensor_(NumericInputKind kind, sensor::Sensor* sensor) {
  this->numeric_input_(kind).age_sensor = sensor;
}

void OpenQuattMqttConfig::set_numeric_input_valid_binary_sensor_(NumericInputKind kind,
                                                                 binary_sensor::BinarySensor* binary_sensor) {
  this->numeric_input_(kind).valid_binary_sensor = binary_sensor;
}

OpenQuattMqttConfig::NumericInput& OpenQuattMqttConfig::numeric_input_(NumericInputKind kind) {
  return this->numeric_inputs_[static_cast<size_t>(kind)];
}

const OpenQuattMqttConfig::NumericInput& OpenQuattMqttConfig::numeric_input_(NumericInputKind kind) const {
  return this->numeric_inputs_[static_cast<size_t>(kind)];
}

void OpenQuattMqttConfig::set_binary_input_topic_(BinaryInputKind kind, const std::string& topic) {
  this->binary_input_(kind).topic = topic;
}

void OpenQuattMqttConfig::set_binary_input_stale_ms_(BinaryInputKind kind, uint32_t stale_ms) {
  this->binary_input_(kind).stale_ms = stale_ms;
}

void OpenQuattMqttConfig::set_binary_input_binary_sensor_(BinaryInputKind kind,
                                                          binary_sensor::BinarySensor* binary_sensor) {
  this->binary_input_(kind).binary_sensor = binary_sensor;
}

void OpenQuattMqttConfig::set_binary_input_age_sensor_(BinaryInputKind kind, sensor::Sensor* sensor) {
  this->binary_input_(kind).age_sensor = sensor;
}

void OpenQuattMqttConfig::set_binary_input_valid_binary_sensor_(BinaryInputKind kind,
                                                                binary_sensor::BinarySensor* binary_sensor) {
  this->binary_input_(kind).valid_binary_sensor = binary_sensor;
}

OpenQuattMqttConfig::BinaryInput& OpenQuattMqttConfig::binary_input_(BinaryInputKind kind) {
  return this->binary_inputs_[static_cast<size_t>(kind)];
}

const OpenQuattMqttConfig::BinaryInput& OpenQuattMqttConfig::binary_input_(BinaryInputKind kind) const {
  return this->binary_inputs_[static_cast<size_t>(kind)];
}

uint8_t OpenQuattMqttConfig::numeric_input_mask_(NumericInputKind kind) {
  return numeric_input_bit_(static_cast<size_t>(kind));
}

uint8_t OpenQuattMqttConfig::binary_input_mask_(BinaryInputKind kind) {
  return binary_input_bit_(static_cast<size_t>(kind));
}

bool OpenQuattMqttConfig::is_numeric_input_enabled_(size_t input_index) const {
  if (input_index >= this->numeric_inputs_.size()) {
    return false;
  }
  const uint8_t mask = numeric_input_bit_(input_index);
  return (this->input_disabled_mask_.load() & mask) == 0U;
}

bool OpenQuattMqttConfig::is_binary_input_enabled_(size_t input_index) const {
  if (input_index >= this->binary_inputs_.size()) {
    return false;
  }
  const uint8_t mask = binary_input_bit_(input_index);
  return (this->input_disabled_mask_.load() & mask) == 0U;
}

bool OpenQuattMqttConfig::is_numeric_input_accept_retained_(size_t input_index) const {
  if (input_index != static_cast<size_t>(NumericInputKind::ROOM_SETPOINT)) {
    return false;
  }
  const uint8_t mask = numeric_input_bit_(input_index);
  return (this->retained_disabled_mask_.load() & mask) == 0U;
}

bool OpenQuattMqttConfig::is_binary_input_accept_retained_(size_t input_index) const {
  if (input_index >= this->binary_inputs_.size()) {
    return false;
  }
  const uint8_t mask = binary_input_bit_(input_index);
  return (this->retained_disabled_mask_.load() & mask) == 0U;
}

bool OpenQuattMqttConfig::input_mask_for_key_(const std::string& key, uint8_t* mask) const {
  if (mask == nullptr) {
    return false;
  }
  for (size_t i = 0; i < this->numeric_inputs_.size(); i++) {
    if (key == this->numeric_inputs_[i].key) {
      *mask = numeric_input_bit_(i);
      return true;
    }
  }
  for (size_t i = 0; i < this->binary_inputs_.size(); i++) {
    if (key == this->binary_inputs_[i].key) {
      *mask = binary_input_bit_(i);
      return true;
    }
  }
  return false;
}

void OpenQuattMqttConfig::clear_all_inputs_() {
  portENTER_CRITICAL(&this->pending_lock_);
  for (auto& input : this->numeric_inputs_) {
    input.pending_payload_ready = false;
    input.pending_invalid_payload_ready = false;
    input.pending_retained = false;
    input.pending_session_generation = 0;
    input.last_valid_value = NAN;
    input.last_valid_ms = 0;
    input.last_valid_retained = false;
  }
  for (auto& input : this->binary_inputs_) {
    input.pending_payload_ready = false;
    input.pending_invalid_payload_ready = false;
    input.pending_retained = false;
    input.pending_session_generation = 0;
    input.last_valid_value = false;
    input.last_valid_ms = 0;
    input.last_valid_retained = false;
  }
  portEXIT_CRITICAL(&this->pending_lock_);
}

void OpenQuattMqttConfig::clear_disabled_inputs_() {
  const uint8_t disabled_mask = this->input_disabled_mask_.load() & INPUT_MASK_ALL;
  portENTER_CRITICAL(&this->pending_lock_);
  for (size_t i = 0; i < this->numeric_inputs_.size(); i++) {
    if ((disabled_mask & numeric_input_bit_(i)) == 0U) {
      continue;
    }
    auto& input = this->numeric_inputs_[i];
    input.pending_payload_ready = false;
    input.pending_invalid_payload_ready = false;
    input.pending_retained = false;
    input.pending_session_generation = 0;
    input.last_valid_value = NAN;
    input.last_valid_ms = 0;
    input.last_valid_retained = false;
  }
  for (size_t i = 0; i < this->binary_inputs_.size(); i++) {
    const uint8_t mask = binary_input_bit_(i);
    if ((disabled_mask & mask) == 0U) {
      continue;
    }
    auto& input = this->binary_inputs_[i];
    input.pending_payload_ready = false;
    input.pending_invalid_payload_ready = false;
    input.pending_retained = false;
    input.pending_session_generation = 0;
    input.last_valid_value = false;
    input.last_valid_ms = 0;
    input.last_valid_retained = false;
  }
  portEXIT_CRITICAL(&this->pending_lock_);
}

void OpenQuattMqttConfig::clear_input_(uint8_t input_mask) {
  portENTER_CRITICAL(&this->pending_lock_);
  for (size_t i = 0; i < this->numeric_inputs_.size(); i++) {
    if ((input_mask & numeric_input_bit_(i)) == 0U) {
      continue;
    }
    auto& input = this->numeric_inputs_[i];
    input.pending_payload_ready = false;
    input.pending_invalid_payload_ready = false;
    input.pending_retained = false;
    input.pending_session_generation = 0;
    input.last_valid_value = NAN;
    input.last_valid_ms = 0;
    input.last_valid_retained = false;
  }
  for (size_t i = 0; i < this->binary_inputs_.size(); i++) {
    const uint8_t mask = binary_input_bit_(i);
    if ((input_mask & mask) == 0U) {
      continue;
    }
    auto& input = this->binary_inputs_[i];
    input.pending_payload_ready = false;
    input.pending_invalid_payload_ready = false;
    input.pending_retained = false;
    input.pending_session_generation = 0;
    input.last_valid_value = false;
    input.last_valid_ms = 0;
    input.last_valid_retained = false;
  }
  portEXIT_CRITICAL(&this->pending_lock_);
}

void OpenQuattMqttConfig::clear_session_scoped_inputs_() {
  const uint8_t session_scoped_mask = this->retained_disabled_mask_.load() & STATEFUL_INPUT_MASK;
  portENTER_CRITICAL(&this->pending_lock_);
  for (size_t i = 0; i < this->numeric_inputs_.size(); i++) {
    if ((session_scoped_mask & numeric_input_bit_(i)) == 0U) {
      continue;
    }
    auto& input = this->numeric_inputs_[i];
    input.last_valid_value = NAN;
    input.last_valid_ms = 0;
    input.last_valid_retained = false;
  }
  for (size_t i = 0; i < this->binary_inputs_.size(); i++) {
    const uint8_t mask = binary_input_bit_(i);
    if ((session_scoped_mask & mask) == 0U) {
      continue;
    }
    auto& input = this->binary_inputs_[i];
    input.last_valid_value = false;
    input.last_valid_ms = 0;
    input.last_valid_retained = false;
  }
  portEXIT_CRITICAL(&this->pending_lock_);
}

void OpenQuattMqttConfig::process_pending_input_subscriptions_() {
  if (!this->resubscribe_inputs_.exchange(false)) {
    return;
  }
  if (!this->enabled_.load() || !this->connected_.load() || !this->client_events_enabled_.load() ||
      this->callback_session_generation_.load() != this->mqtt_session_generation_.load()) {
    return;
  }

  this->lock_runtime_();
  esp_mqtt_client_handle_t client = this->mqtt_client_;
  if (client != nullptr && this->client_events_enabled_.load() && this->callback_client_.load() == client &&
      this->callback_session_generation_.load() == this->mqtt_session_generation_.load()) {
    this->subscribe_inputs_(client);
  }
  this->unlock_runtime_();
}

void OpenQuattMqttConfig::subscribe_inputs_(esp_mqtt_client_handle_t client) {
  if (client == nullptr) {
    return;
  }

  for (size_t i = 0; i < this->numeric_inputs_.size(); i++) {
    const auto& input = this->numeric_inputs_[i];
    if (!this->is_numeric_input_enabled_(i)) {
      continue;
    }
    if (input.topic.empty()) {
      continue;
    }
    const int msg_id = esp_mqtt_client_subscribe(client, input.topic.c_str(), 0);
    if (msg_id < 0) {
      ESP_LOGW(TAG, "Failed to subscribe to %s", input.topic.c_str());
    } else {
      ESP_LOGI(TAG, "Subscribed to MQTT %s topic %s", input.log_name, input.topic.c_str());
    }
  }

  for (size_t i = 0; i < this->binary_inputs_.size(); i++) {
    const auto& input = this->binary_inputs_[i];
    if (!this->is_binary_input_enabled_(i)) {
      continue;
    }
    if (input.topic.empty()) {
      continue;
    }
    const int msg_id = esp_mqtt_client_subscribe(client, input.topic.c_str(), 0);
    if (msg_id < 0) {
      ESP_LOGW(TAG, "Failed to subscribe to %s", input.topic.c_str());
    } else {
      ESP_LOGI(TAG, "Subscribed to MQTT %s topic %s", input.log_name, input.topic.c_str());
    }
  }
}

int OpenQuattMqttConfig::find_numeric_input_index_by_topic_(const char* topic, int topic_len) const {
  if (topic == nullptr || topic_len <= 0) {
    return -1;
  }

  for (size_t i = 0; i < this->numeric_inputs_.size(); i++) {
    const auto& input = this->numeric_inputs_[i];
    if (this->is_numeric_input_enabled_(i) && !input.topic.empty() &&
        input.topic.size() == static_cast<size_t>(topic_len) && memcmp(topic, input.topic.data(), topic_len) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int OpenQuattMqttConfig::find_binary_input_index_by_topic_(const char* topic, int topic_len) const {
  if (topic == nullptr || topic_len <= 0) {
    return -1;
  }

  for (size_t i = 0; i < this->binary_inputs_.size(); i++) {
    const auto& input = this->binary_inputs_[i];
    if (this->is_binary_input_enabled_(i) && !input.topic.empty() &&
        input.topic.size() == static_cast<size_t>(topic_len) && memcmp(topic, input.topic.data(), topic_len) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void OpenQuattMqttConfig::mqtt_event_handler_(void* handler_args, esp_event_base_t base, int32_t event_id,
                                              void* event_data) {
  (void)base;
  auto* self = static_cast<OpenQuattMqttConfig*>(handler_args);
  auto* event = static_cast<esp_mqtt_event_handle_t>(event_data);
  if (self == nullptr || event == nullptr) {
    return;
  }

  const bool event_client_matches = self->callback_client_.load() == event->client;
  if (event_client_matches && event_id == MQTT_EVENT_CONNECTED) {
    self->mqtt_connected_seen_.store(true);
    self->mqtt_disconnected_seen_.store(false);
    self->mqtt_transport_connected_.store(true);
  } else if (event_client_matches && event_id == MQTT_EVENT_DISCONNECTED) {
    self->mqtt_disconnected_seen_.store(true);
    self->mqtt_transport_connected_.store(false);
  }
  const uint32_t event_generation = self->callback_session_generation_.load();
  if (!mqtt_event_is_current(self->client_events_enabled_.load(), event_client_matches, event_generation,
                             self->mqtt_session_generation_.load())) {
    return;
  }

  switch (event_id) {
    case MQTT_EVENT_CONNECTED:
      self->connected_.store(true);
      self->resubscribe_inputs_.store(true);
      App.wake_loop_threadsafe();
      break;
    case MQTT_EVENT_DISCONNECTED:
      self->connected_.store(false);
      self->callback_session_generation_.store(self->mqtt_session_generation_.fetch_add(1U) + 1U);
      self->clear_session_scoped_inputs_pending_.store(true);
      App.wake_loop_threadsafe();
      break;
    case MQTT_EVENT_DATA: {
      if (event->topic != nullptr && event->data != nullptr && event->current_data_offset == 0 &&
          event->data_len == event->total_data_len) {
        const int numeric_input_index = self->find_numeric_input_index_by_topic_(event->topic, event->topic_len);
        if (numeric_input_index >= 0) {
          const auto& input = self->numeric_inputs_[static_cast<size_t>(numeric_input_index)];
          if (event->retain && !self->is_numeric_input_accept_retained_(static_cast<size_t>(numeric_input_index))) {
            ESP_LOGI(TAG, "Ignoring retained MQTT %s payload for control freshness", input.log_name);
            break;
          }
          self->queue_numeric_payload_(static_cast<size_t>(numeric_input_index), event->data, event->data_len,
                                       event->retain, event_generation);
          break;
        }
        const int binary_input_index = self->find_binary_input_index_by_topic_(event->topic, event->topic_len);
        if (binary_input_index >= 0) {
          if (event->retain && !self->is_binary_input_accept_retained_(static_cast<size_t>(binary_input_index))) {
            const auto& input = self->binary_inputs_[static_cast<size_t>(binary_input_index)];
            ESP_LOGI(TAG, "Ignoring retained MQTT %s payload because retained values are disabled", input.log_name);
            break;
          }
          self->queue_binary_payload_(static_cast<size_t>(binary_input_index), event->data, event->data_len,
                                      event->retain, event_generation);
        }
      }
      break;
    }
    case MQTT_EVENT_ERROR:
      ESP_LOGW(TAG, "MQTT ingress client error");
      break;
    default:
      break;
  }
}

void OpenQuattMqttConfig::queue_numeric_payload_(size_t input_index, const char* data, int len, bool retained,
                                                 uint32_t session_generation) {
  if (input_index >= this->numeric_inputs_.size() || data == nullptr || len < 0) {
    return;
  }

  auto& input = this->numeric_inputs_[input_index];
  if (static_cast<size_t>(len) >= PAYLOAD_MAX_LEN) {
    ESP_LOGW(TAG, "Invalidating MQTT %s after overlong payload (%d bytes)", input.log_name, len);
    portENTER_CRITICAL(&this->pending_lock_);
    input.pending_payload_ready = false;
    input.pending_invalid_payload_ready = true;
    input.pending_retained = false;
    input.pending_session_generation = session_generation;
    portEXIT_CRITICAL(&this->pending_lock_);
    App.wake_loop_threadsafe();
    return;
  }
  const size_t copy_len = std::min(static_cast<size_t>(len), PAYLOAD_MAX_LEN - 1U);
  portENTER_CRITICAL(&this->pending_lock_);
  memcpy(input.pending_payload, data, copy_len);
  input.pending_payload[copy_len] = '\0';
  input.pending_payload_ready = true;
  input.pending_invalid_payload_ready = false;
  input.pending_retained = retained;
  input.pending_session_generation = session_generation;
  portEXIT_CRITICAL(&this->pending_lock_);
  App.wake_loop_threadsafe();
}

void OpenQuattMqttConfig::queue_binary_payload_(size_t input_index, const char* data, int len, bool retained,
                                                uint32_t session_generation) {
  if (input_index >= this->binary_inputs_.size() || data == nullptr || len < 0) {
    return;
  }

  auto& input = this->binary_inputs_[input_index];
  if (static_cast<size_t>(len) >= PAYLOAD_MAX_LEN) {
    ESP_LOGW(TAG, "Invalidating MQTT %s after overlong payload (%d bytes)", input.log_name, len);
    portENTER_CRITICAL(&this->pending_lock_);
    input.pending_payload_ready = false;
    input.pending_invalid_payload_ready = true;
    input.pending_retained = false;
    input.pending_session_generation = session_generation;
    portEXIT_CRITICAL(&this->pending_lock_);
    App.wake_loop_threadsafe();
    return;
  }
  const size_t copy_len = std::min(static_cast<size_t>(len), PAYLOAD_MAX_LEN - 1U);
  portENTER_CRITICAL(&this->pending_lock_);
  memcpy(input.pending_payload, data, copy_len);
  input.pending_payload[copy_len] = '\0';
  input.pending_payload_ready = true;
  input.pending_invalid_payload_ready = false;
  input.pending_retained = retained;
  input.pending_session_generation = session_generation;
  portEXIT_CRITICAL(&this->pending_lock_);
  App.wake_loop_threadsafe();
}

void OpenQuattMqttConfig::consume_pending_numeric_payloads_() {
  for (size_t i = 0; i < this->numeric_inputs_.size(); i++) {
    char payload[PAYLOAD_MAX_LEN]{};
    bool ready = false;
    bool invalid = false;
    bool retained = false;
    uint32_t session_generation = 0;

    portENTER_CRITICAL(&this->pending_lock_);
    auto& input = this->numeric_inputs_[i];
    if (input.pending_invalid_payload_ready) {
      input.pending_invalid_payload_ready = false;
      input.pending_payload_ready = false;
      session_generation = input.pending_session_generation;
      invalid = true;
    } else if (input.pending_payload_ready) {
      memcpy(payload, input.pending_payload, sizeof(payload));
      retained = input.pending_retained;
      session_generation = input.pending_session_generation;
      input.pending_payload_ready = false;
      input.pending_retained = false;
      ready = true;
    }
    portEXIT_CRITICAL(&this->pending_lock_);

    if (session_generation != this->mqtt_session_generation_.load()) {
      continue;
    }
    if (invalid) {
      this->invalidate_numeric_input_(i);
    } else if (ready) {
      this->handle_numeric_payload_(i, payload, retained);
    }
  }
}

void OpenQuattMqttConfig::consume_pending_binary_payloads_() {
  for (size_t i = 0; i < this->binary_inputs_.size(); i++) {
    char payload[PAYLOAD_MAX_LEN]{};
    bool ready = false;
    bool invalid = false;
    bool retained = false;
    uint32_t session_generation = 0;

    portENTER_CRITICAL(&this->pending_lock_);
    auto& input = this->binary_inputs_[i];
    if (input.pending_invalid_payload_ready) {
      input.pending_invalid_payload_ready = false;
      input.pending_payload_ready = false;
      session_generation = input.pending_session_generation;
      invalid = true;
    } else if (input.pending_payload_ready) {
      memcpy(payload, input.pending_payload, sizeof(payload));
      retained = input.pending_retained;
      session_generation = input.pending_session_generation;
      input.pending_payload_ready = false;
      input.pending_retained = false;
      ready = true;
    }
    portEXIT_CRITICAL(&this->pending_lock_);

    if (session_generation != this->mqtt_session_generation_.load()) {
      continue;
    }
    if (invalid) {
      this->invalidate_binary_input_(i);
    } else if (ready) {
      this->handle_binary_payload_(i, payload, retained);
    }
  }
}

void OpenQuattMqttConfig::handle_numeric_payload_(size_t input_index, const char* payload, bool retained) {
  if (input_index >= this->numeric_inputs_.size() || payload == nullptr) {
    return;
  }
  // The callback may have matched this topic immediately before a main-loop
  // policy update disabled it. Recheck at consumption time so that payload
  // can never repopulate a value cleared by that disable.
  if (!this->is_numeric_input_enabled_(input_index)) {
    return;
  }
  if (retained && !this->is_numeric_input_accept_retained_(input_index)) {
    return;
  }

  auto& input = this->numeric_inputs_[input_index];
  float value = NAN;
  if (!parse_numeric_payload_(payload, &value) || value < input.min_value || value > input.max_value) {
    ESP_LOGW(TAG, "Invalidating MQTT %s after invalid payload: %s", input.log_name, payload);
    this->invalidate_numeric_input_(input_index);
    return;
  }

  input.last_valid_value = value;
  input.last_valid_ms = millis();
  input.last_valid_retained = retained;
  this->publish_runtime_state_(true);
}

void OpenQuattMqttConfig::handle_binary_payload_(size_t input_index, const char* payload, bool retained) {
  if (input_index >= this->binary_inputs_.size() || payload == nullptr) {
    return;
  }
  if (!this->is_binary_input_enabled_(input_index)) {
    return;
  }
  if (retained && !this->is_binary_input_accept_retained_(input_index)) {
    return;
  }

  auto& input = this->binary_inputs_[input_index];
  bool value = false;
  if (!parse_binary_payload_(payload, &value)) {
    ESP_LOGW(TAG, "Invalidating MQTT %s after invalid payload: %s", input.log_name, payload);
    this->invalidate_binary_input_(input_index);
    return;
  }

  input.last_valid_value = value;
  input.last_valid_ms = millis();
  input.last_valid_retained = retained;
  this->publish_runtime_state_(true);
}

void OpenQuattMqttConfig::invalidate_numeric_input_(size_t input_index) {
  if (input_index >= this->numeric_inputs_.size()) {
    return;
  }
  auto& input = this->numeric_inputs_[input_index];
  input.last_valid_value = NAN;
  input.last_valid_ms = 0;
  input.last_valid_retained = false;
  this->publish_runtime_state_(true);
}

void OpenQuattMqttConfig::invalidate_binary_input_(size_t input_index) {
  if (input_index >= this->binary_inputs_.size()) {
    return;
  }
  auto& input = this->binary_inputs_[input_index];
  input.last_valid_value = false;
  input.last_valid_ms = 0;
  input.last_valid_retained = false;
  this->publish_runtime_state_(true);
}

void OpenQuattMqttConfig::publish_runtime_state_(bool force) {
  const uint32_t now_ms = millis();
  if (!force && (uint32_t)(now_ms - this->last_sensor_publish_ms_) < SENSOR_PUBLISH_INTERVAL_MS) {
    return;
  }
  this->last_sensor_publish_ms_ = now_ms;

  const bool mqtt_enabled = this->enabled_.load();
  for (size_t i = 0; i < this->numeric_inputs_.size(); i++) {
    auto& input = this->numeric_inputs_[i];
    const bool input_enabled = mqtt_enabled && this->is_numeric_input_enabled_(i);
    const bool has_sample = input.last_valid_ms != 0 && std::isfinite(input.last_valid_value);
    const uint32_t age_ms = has_sample ? (uint32_t)(now_ms - input.last_valid_ms) : 0U;
    const uint32_t stale_ms = this->is_numeric_input_accept_retained_(i) ? input.stale_ms
                              : i == static_cast<size_t>(NumericInputKind::ROOM_SETPOINT)
                                  ? NON_RETAINED_STATEFUL_STALE_MS
                                  : input.stale_ms;
    const bool valid = input_enabled && has_sample && (stale_ms == 0U || age_ms <= stale_ms);

    this->publish_binary_if_changed_(input.valid_binary_sensor, valid, force);
    this->publish_float_if_changed_(input.age_sensor,
                                    input_enabled && has_sample ? static_cast<float>(age_ms / 1000U) : NAN, force);
    this->publish_float_if_changed_(input.sensor, valid ? input.last_valid_value : NAN, force);
  }

  for (size_t i = 0; i < this->binary_inputs_.size(); i++) {
    auto& input = this->binary_inputs_[i];
    const bool input_enabled = mqtt_enabled && this->is_binary_input_enabled_(i);
    const bool has_sample = input.last_valid_ms != 0;
    const uint32_t age_ms = has_sample ? (uint32_t)(now_ms - input.last_valid_ms) : 0U;
    const uint32_t stale_ms =
        this->is_binary_input_accept_retained_(i) ? input.stale_ms : NON_RETAINED_STATEFUL_STALE_MS;
    const bool valid = input_enabled && has_sample && (stale_ms == 0U || age_ms <= stale_ms);

    this->publish_binary_if_changed_(input.valid_binary_sensor, valid, force);
    this->publish_float_if_changed_(input.age_sensor,
                                    input_enabled && has_sample ? static_cast<float>(age_ms / 1000U) : NAN, force);
    this->publish_binary_if_changed_(input.binary_sensor, valid && input.last_valid_value, force);
  }
}

void OpenQuattMqttConfig::publish_float_if_changed_(sensor::Sensor* sensor, float value, bool force) {
  if (sensor == nullptr) {
    return;
  }
  const bool current_has_state = sensor->has_state();
  const bool current_nan = !current_has_state || std::isnan(sensor->state);
  const bool value_nan = std::isnan(value);
  if (force || current_nan != value_nan || (!value_nan && std::fabs(sensor->state - value) > 0.001f)) {
    sensor->publish_state(value);
  }
}

void OpenQuattMqttConfig::publish_binary_if_changed_(binary_sensor::BinarySensor* binary_sensor, bool value,
                                                     bool force) {
  if (binary_sensor == nullptr) {
    return;
  }
  if (force || !binary_sensor->has_state() || binary_sensor->state != value) {
    binary_sensor->publish_state(value);
  }
}

void OpenQuattMqttConfig::lock_config_() {
  if (this->config_lock_ != nullptr) {
    xSemaphoreTake(this->config_lock_, portMAX_DELAY);
  }
}

void OpenQuattMqttConfig::unlock_config_() {
  if (this->config_lock_ != nullptr) {
    xSemaphoreGive(this->config_lock_);
  }
}

void OpenQuattMqttConfig::lock_runtime_() {
  if (this->runtime_lock_ != nullptr) {
    xSemaphoreTake(this->runtime_lock_, portMAX_DELAY);
  }
}

void OpenQuattMqttConfig::unlock_runtime_() {
  if (this->runtime_lock_ != nullptr) {
    xSemaphoreGive(this->runtime_lock_);
  }
}

void OpenQuattMqttConfig::lock_persistence_() {
  if (this->persistence_lock_ != nullptr) {
    xSemaphoreTake(this->persistence_lock_, portMAX_DELAY);
  }
}

void OpenQuattMqttConfig::unlock_persistence_() {
  if (this->persistence_lock_ != nullptr) {
    xSemaphoreGive(this->persistence_lock_);
  }
}

void OpenQuattMqttConfig::copy_string_field_(char* destination, size_t max_len, const std::string& value) {
  if (destination == nullptr || max_len == 0U) {
    return;
  }
  const size_t copy_len = std::min(max_len, value.size());
  memcpy(destination, value.data(), copy_len);
  destination[copy_len] = '\0';
}

}  // namespace openquatt_mqtt_config
}  // namespace esphome
