#include "OpenQuattIncidentManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "esp_random.h"
#include "esphome/components/web_server/web_server.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/log.h"
#include "includes/incidents/oq_hp_incident_sources.h"

namespace esphome {
namespace openquatt_incident_manager {

namespace {

static const char* const TAG = "openquatt.incidents";
static const uint32_t MANUAL_RESET_STORAGE_A_KEY = fnv1_hash("openquatt_incident_manual_reset_store_a");
static const uint32_t MANUAL_RESET_STORAGE_B_KEY = fnv1_hash("openquatt_incident_manual_reset_store_b");
static const uint32_t MANUAL_RESET_MARKER_KEY = fnv1_hash("openquatt_incident_manual_reset_marker");

bool url_path_matches(const char* url, const char* path) {
  if (url == nullptr || path == nullptr) return false;
  const size_t path_length = std::strlen(path);
  return std::strncmp(url, path, path_length) == 0 && (url[path_length] == '\0' || url[path_length] == '?');
}

uint32_t parse_positive_request_id(const std::string& value) {
  if (value.empty()) return 0U;
  uint32_t parsed = 0U;
  for (const char character : value) {
    if (character < '0' || character > '9') return 0U;
    const uint32_t digit = static_cast<uint32_t>(character - '0');
    if (parsed > (std::numeric_limits<uint32_t>::max() - digit) / 10U) {
      return 0U;
    }
    parsed = parsed * 10U + digit;
  }
  return parsed;
}

const char* deferred_action_name(OpenQuattIncidentManager::DeferredActionKind kind) {
  switch (kind) {
    case OpenQuattIncidentManager::DeferredActionKind::START_FAILURE_RETRY:
      return "start_failure_retry";
    case OpenQuattIncidentManager::DeferredActionKind::CONFIRM_ODU_POWER_CYCLE:
      return "confirm_odu_power_cycle";
    case OpenQuattIncidentManager::DeferredActionKind::NONE:
      break;
  }
  return "none";
}

bool header_matches_host(const std::string& header_value, const std::string& host) {
  if (host.empty() || header_value.empty()) return false;
  size_t authority_start = 0U;
  const size_t scheme_pos = header_value.find("://");
  if (scheme_pos != std::string::npos) {
    authority_start = scheme_pos + 3U;
  }
  const size_t authority_end = header_value.find_first_of("/?#", authority_start);
  return header_value.substr(authority_start, authority_end == std::string::npos
                                                  ? std::string::npos
                                                  : authority_end - authority_start) == host;
}

const char* start_failure_reset_result_name(oq_incidents::StartFailureResetResult result) {
  switch (result) {
    case oq_incidents::StartFailureResetResult::READY:
      return "ready";
    case oq_incidents::StartFailureResetResult::CLEARED:
      return "start_failure_cleared";
    case oq_incidents::StartFailureResetResult::NO_START_FAILURE:
      return "no_start_failure";
    case oq_incidents::StartFailureResetResult::STOP_NOT_CONFIRMED:
      return "stop_not_confirmed";
    case oq_incidents::StartFailureResetResult::LINK_NOT_HEALTHY:
      return "link_not_healthy";
    case oq_incidents::StartFailureResetResult::HARD_FAULT_ACTIVE:
      return "hard_fault_active";
    case oq_incidents::StartFailureResetResult::FAULT_RECOVERY_PENDING:
      return "fault_recovery_pending";
    case oq_incidents::StartFailureResetResult::HP_NOT_CONFIGURED:
      return "hp_not_configured";
  }
  return "unknown";
}

uint8_t start_confirmation_reason(uint8_t expected_mode) {
  switch (thermal_command_for_expected_mode(expected_mode)) {
    case HpThermalCommand::COOLING:
      return openquatt_decision_log::REASON_COOLING_REQUEST;
    case HpThermalCommand::HEATING:
      return openquatt_decision_log::REASON_HEATING_REQUEST;
    case HpThermalCommand::UNKNOWN:
      return openquatt_decision_log::REASON_UNKNOWN;
  }
  return openquatt_decision_log::REASON_UNKNOWN;
}

uint8_t cleared_command_reason(uint8_t expected_mode) {
  switch (thermal_command_for_expected_mode(expected_mode)) {
    case HpThermalCommand::COOLING:
      return openquatt_decision_log::REASON_COOLING_REQUEST_CLEARED;
    case HpThermalCommand::HEATING:
      return openquatt_decision_log::REASON_HEATING_REQUEST_CLEARED;
    case HpThermalCommand::UNKNOWN:
      return openquatt_decision_log::REASON_KEEP_CURRENT;
  }
  return openquatt_decision_log::REASON_KEEP_CURRENT;
}

bool write_raw(httpd_req_t* req, const char* value) {
  return value != nullptr && httpd_resp_send_chunk(req, value, HTTPD_RESP_USE_STRLEN) == ESP_OK;
}

bool write_uint(httpd_req_t* req, uint64_t value) {
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
  return write_raw(req, buffer);
}

bool write_bool(httpd_req_t* req, bool value) { return write_raw(req, value ? "true" : "false"); }

bool write_float(httpd_req_t* req, float value) {
  if (!std::isfinite(value)) return write_raw(req, "null");
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(value));
  return write_raw(req, buffer);
}

bool write_optional_bool(httpd_req_t* req, bool valid, bool value) {
  return valid ? write_bool(req, value) : write_raw(req, "null");
}

bool write_optional_uint(httpd_req_t* req, bool valid, uint64_t value) {
  return valid ? write_uint(req, value) : write_raw(req, "null");
}

bool write_optional_float(httpd_req_t* req, bool valid, float value) {
  return valid && std::isfinite(value) ? write_float(req, value) : write_raw(req, "null");
}

bool write_json_string(httpd_req_t* req, const char* value) {
  if (!write_raw(req, "\"")) return false;
  const char* cursor = value != nullptr ? value : "";
  char escaped[7];
  for (; *cursor != '\0'; ++cursor) {
    const unsigned char c = static_cast<unsigned char>(*cursor);
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
        if (c < 0x20U) {
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
          if (!write_raw(req, escaped)) return false;
        } else {
          char single[2] = {static_cast<char>(c), '\0'};
          if (!write_raw(req, single)) return false;
        }
        break;
    }
  }
  return write_raw(req, "\"");
}

const char* link_state_name(oq_incidents::LinkState state) {
  switch (state) {
    case oq_incidents::LinkState::BOOTSTRAP:
      return "bootstrap";
    case oq_incidents::LinkState::HEALTHY:
      return "healthy";
    case oq_incidents::LinkState::SUSPECT:
      return "suspect";
    case oq_incidents::LinkState::LOST:
      return "lost";
    case oq_incidents::LinkState::RECOVERING:
      return "recovering";
  }
  return "unknown";
}

const char* protection_state_name(oq_incidents::ProtectionState state) {
  switch (state) {
    case oq_incidents::ProtectionState::CLEAR:
      return "clear";
    case oq_incidents::ProtectionState::LIMITED:
      return "limited";
    case oq_incidents::ProtectionState::START_BLOCKED:
      return "start_blocked";
    case oq_incidents::ProtectionState::FAULT_ACTIVE:
      return "fault_active";
    case oq_incidents::ProtectionState::FAULT_RECOVERY:
      return "fault_recovery";
  }
  return "unknown";
}

const char* run_state_name(oq_incidents::RunState state) {
  switch (state) {
    case oq_incidents::RunState::UNKNOWN:
      return "unknown";
    case oq_incidents::RunState::STOPPED:
      return "stopped";
    case oq_incidents::RunState::START_REQUESTED:
      return "start_requested";
    case oq_incidents::RunState::WAIT_MODE:
      return "wait_mode";
    case oq_incidents::RunState::WAIT_COMPRESSOR:
      return "wait_compressor";
    case oq_incidents::RunState::RUNNING:
      return "running";
    case oq_incidents::RunState::STOPPING:
      return "stopping";
    case oq_incidents::RunState::STOP_UNCONFIRMED:
      return "stop_unconfirmed";
  }
  return "unknown";
}

const char* category_name(oq_incidents::IncidentCategory category) {
  switch (category) {
    case oq_incidents::IncidentCategory::STATUS:
      return "status";
    case oq_incidents::IncidentCategory::PROTECTION:
      return "protection";
    case oq_incidents::IncidentCategory::WARNING:
      return "warning";
    case oq_incidents::IncidentCategory::FAULT:
      return "fault";
  }
  return "unknown";
}

const char* severity_name(oq_incidents::IncidentSeverity severity) {
  switch (severity) {
    case oq_incidents::IncidentSeverity::INFO:
      return "info";
    case oq_incidents::IncidentSeverity::WARNING:
      return "warning";
    case oq_incidents::IncidentSeverity::FAULT:
      return "fault";
  }
  return "warning";
}

const char* user_action_name(oq_incidents::UserAction action) {
  switch (action) {
    case oq_incidents::UserAction::NONE:
      return "none";
    case oq_incidents::UserAction::WAIT_FOR_AUTOMATIC_RECOVERY:
      return "wait_for_automatic_recovery";
    case oq_incidents::UserAction::CHECK_INSTALLATION:
      return "check_installation";
    case oq_incidents::UserAction::CONTACT_INSTALLER:
      return "contact_installer";
  }
  return "contact_installer";
}

const char* recovery_condition_name(oq_incidents::RecoveryCondition condition) {
  switch (condition) {
    case oq_incidents::RecoveryCondition::WHEN_BIT_CLEARS:
      return "when_bit_clears";
    case oq_incidents::RecoveryCondition::STABLE_READS_AND_RECOVERY_WINDOW:
      return "stable_reads_and_recovery_window";
    case oq_incidents::RecoveryCondition::PREHEAT_COMPLETE:
      return "preheat_complete";
    case oq_incidents::RecoveryCondition::CONFIRMED_ODU_POWER_CYCLE:
      return "confirmed_odu_power_cycle";
    case oq_incidents::RecoveryCondition::STABLE_TELEMETRY:
      return "stable_telemetry";
    case oq_incidents::RecoveryCondition::EXPLICIT_RETRY_AFTER_SAFE_STOP:
      return "explicit_retry_after_safe_stop";
    case oq_incidents::RecoveryCondition::FRESH_STOP_CONFIRMATION:
      return "fresh_stop_confirmation";
    case oq_incidents::RecoveryCondition::AFTER_STABLE_READS:
      return "after_stable_reads";
    case oq_incidents::RecoveryCondition::REVIEW_REQUIRED:
      return "review_required";
  }
  return "review_required";
}

const char* incident_lifecycle_name(const oq_incidents::IncidentDefinition& definition,
                                    const oq_incidents::IncidentRuntime& runtime) {
  if (runtime.confirmed_active) return "active";
  if (runtime.latched && definition.clear_policy == oq_incidents::ClearPolicy::AFTER_CONFIRMED_ODU_POWER_CYCLE) {
    return "latched";
  }
  return "cleared";
}

const char* availability_name(const oq_incidents::DerivedOutputs& outputs) {
  if (outputs.available_for_start) return "available";
  if (outputs.protection_state == oq_incidents::ProtectionState::FAULT_ACTIVE) {
    return "unavailable";
  }
  if (outputs.link_state == oq_incidents::LinkState::RECOVERING ||
      outputs.protection_state == oq_incidents::ProtectionState::FAULT_RECOVERY) {
    return "recovering";
  }
  if (outputs.must_stop || outputs.link_state == oq_incidents::LinkState::LOST) {
    return "unavailable";
  }
  if (outputs.protection_state == oq_incidents::ProtectionState::START_BLOCKED) {
    return "blocked";
  }
  return "unknown";
}

const char* boiler_role_name(uint8_t role) {
  switch (role) {
    case 1:
      return "assist";
    case 2:
      return "fallback";
    case 3:
      return "commissioning";
    default:
      return "off";
  }
}

const char* system_action_name(uint8_t control_mode, bool fallback_requested, bool fallback_active,
                               uint8_t block_reason, bool boiler_command_active) {
  if ((fallback_active || control_mode == 4U) && boiler_command_active) {
    return "boiler_fallback";
  }
  if ((fallback_requested || control_mode == 4U) && (!boiler_command_active || block_reason != 0U)) {
    return "fallback_blocked";
  }
  if (control_mode == 3U && boiler_command_active) return "boiler_assist";
  return "none";
}

bool write_effects(httpd_req_t* req, oq_incidents::EffectMask effects) {
  struct NamedEffect {
    oq_incidents::IncidentEffect effect;
    const char* name;
  };
  static constexpr NamedEffect kEffects[] = {
      {oq_incidents::IncidentEffect::DISPLAY, "display"},
      {oq_incidents::IncidentEffect::LIMIT_CAPACITY, "limit_capacity"},
      {oq_incidents::IncidentEffect::BLOCK_START, "block_start"},
      {oq_incidents::IncidentEffect::STOP_COMPRESSOR, "stop_compressor"},
      {oq_incidents::IncidentEffect::MARK_HP_UNAVAILABLE, "mark_hp_unavailable"},
      {oq_incidents::IncidentEffect::PUMP_UNAVAILABLE, "pump_unavailable"},
      {oq_incidents::IncidentEffect::ALLOW_CM4, "allow_cm4"},
      {oq_incidents::IncidentEffect::BLOCK_BOILER, "block_boiler"},
      {oq_incidents::IncidentEffect::REQUIRE_CONFIRMED_ODU_POWER_CYCLE, "require_confirmed_odu_power_cycle"},
  };
  if (!write_raw(req, "[")) return false;
  bool first = true;
  for (const NamedEffect& entry : kEffects) {
    if (!oq_incidents::has_effect(effects, entry.effect)) continue;
    if ((!first && !write_raw(req, ",")) || !write_json_string(req, entry.name)) {
      return false;
    }
    first = false;
  }
  return write_raw(req, "]");
}

class IncidentManagerRequestHandler : public AsyncWebHandler {
 public:
  explicit IncidentManagerRequestHandler(OpenQuattIncidentManager* parent) : parent_(parent) {}

  bool passes_same_origin_(AsyncWebServerRequest* request) const {
    const auto host = request->get_header("Host");
    if (!host.has_value() || host->empty()) return false;
    const auto origin = request->get_header("Origin");
    if (origin.has_value() && !header_matches_host(origin.value(), host.value())) {
      return false;
    }
    const auto referer = request->get_header("Referer");
    return !referer.has_value() || header_matches_host(referer.value(), host.value());
  }

  bool passes_csrf_(AsyncWebServerRequest* request) const {
    const std::string token = request->arg("csrf_token");
    return !token.empty() && token == this->parent_->get_action_csrf_token();
  }

  bool canHandle(AsyncWebServerRequest* request) const override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url_buf);
    if (url_path_matches(url_buf, "/openquatt/incidents/retry-start") ||
        url_path_matches(url_buf, "/openquatt/incidents/confirm-odu-power-cycle")) {
      return request->method() == HTTP_POST;
    }
    return url_path_matches(url_buf, "/openquatt/incidents") && request->method() == HTTP_GET;
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    if (!this->parent_->request_is_authenticated(request)) {
      request->requestAuthentication();
      return;
    }
    if (!this->parent_->storage_ready()) {
      request->send(503, "application/json", R"({"error":"snapshot_unavailable"})");
      return;
    }
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url_buf);
    const bool retry_start = url_path_matches(url_buf, "/openquatt/incidents/retry-start");
    const bool confirm_odu_power_cycle = url_path_matches(url_buf, "/openquatt/incidents/confirm-odu-power-cycle");
    if (retry_start || confirm_odu_power_cycle) {
      if (!this->passes_same_origin_(request) || !this->passes_csrf_(request)) {
        request->send(403, "application/json", R"({"accepted":false,"result":"forbidden"})");
        return;
      }

      const std::string hp_arg = request->arg("hp");
      const uint8_t hp_index = hp_arg == "1" ? 1U : (hp_arg == "2" ? 2U : 0U);
      if (hp_index == 0U) {
        request->send(400, "application/json", R"({"accepted":false,"result":"invalid_hp"})");
        return;
      }
      if (!this->parent_->hp_configured(hp_index)) {
        request->send(404, "application/json", R"({"accepted":false,"result":"hp_not_configured"})");
        return;
      }
      const uint32_t request_id = parse_positive_request_id(request->arg("request_id"));
      if (request_id == 0U) {
        request->send(400, "application/json", R"({"accepted":false,"result":"invalid_request_id"})");
        return;
      }
      const OpenQuattIncidentManager::DeferredActionQueueResult queue_result =
          retry_start ? this->parent_->defer_start_failure_retry(hp_index, request_id)
                      : this->parent_->defer_odu_power_cycle_confirmation(hp_index, request_id);
      if (queue_result == OpenQuattIncidentManager::DeferredActionQueueResult::BUSY) {
        request->send(409, "application/json", R"({"accepted":false,"result":"action_in_progress"})");
        return;
      }
      if (queue_result == OpenQuattIncidentManager::DeferredActionQueueResult::INVALID) {
        request->send(400, "application/json", R"({"accepted":false,"result":"invalid_request_id"})");
        return;
      }
      char response[192];
      std::snprintf(
          response, sizeof(response), R"({"accepted":true,"duplicate":%s,"hp":%u,"action":"%s","action_id":%u})",
          queue_result == OpenQuattIncidentManager::DeferredActionQueueResult::DUPLICATE ? "true" : "false", hp_index,
          retry_start ? "start_failure_retry" : "confirm_odu_power_cycle", static_cast<unsigned>(request_id));
      request->send(202, "application/json", response);
      return;
    }

    httpd_req_t* req = *request;
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    this->parent_->write_snapshot(req);
  }

 protected:
  OpenQuattIncidentManager* parent_;
};

}  // namespace

float OpenQuattIncidentManager::get_setup_priority() const { return setup_priority::WIFI; }

void OpenQuattIncidentManager::setup() {
  const auto register_handler = [this]() {
    if (web_server_base::global_web_server_base == nullptr) {
      ESP_LOGW(TAG, "web_server_base is unavailable; incident endpoint disabled");
    } else if (this->web_auth_ == nullptr) {
      ESP_LOGE(TAG, "Runtime web auth is unavailable; incident endpoint disabled");
    } else {
      web_server_base::global_web_server_base->add_handler(new IncidentManagerRequestHandler(this));
    }
  };

  if (!this->external_state_.allocate()) {
    ESP_LOGE(TAG,
             "Could not allocate %u-byte incident state arena in PSRAM; "
             "incident control remains fail-closed",
             static_cast<unsigned>(sizeof(ExternalState)));
    register_handler();
    this->mark_failed();
    return;
  }
  this->action_mutex_ = xSemaphoreCreateMutexStatic(&this->action_mutex_storage_);
  this->snapshot_mutex_ = xSemaphoreCreateMutexStatic(&this->snapshot_mutex_storage_);
  if (this->action_mutex_ == nullptr || this->snapshot_mutex_ == nullptr) {
    ESP_LOGE(TAG,
             "Could not initialize incident state synchronization; "
             "incident control remains fail-closed");
    this->external_state_.release();
    this->action_mutex_ = nullptr;
    this->snapshot_mutex_ = nullptr;
    register_handler();
    this->mark_failed();
    return;
  }

  this->units_()[0].configured = true;
  this->units_()[1].configured = OQ_TOPOLOGY_DUO != 0;
  const uint32_t now_ms = millis();
  this->rotate_action_csrf_token_();
  for (UnitState& unit : this->units_()) {
    unit.last_link_round_ms = now_ms;
  }
  this->setup_manual_reset_persistence_(now_ms);
  for (size_t slot = 0U; slot < this->units_().size(); ++slot) {
    if (this->units_()[slot].configured) {
      this->publish_transitions_(this->units_()[slot], slot, now_ms);
    }
  }
  this->publish_snapshot_(now_ms);
  register_handler();
}

void OpenQuattIncidentManager::loop() {
  if (!this->storage_ready_()) return;
  const uint32_t now_ms = millis();
  if (elapsed_ms_(now_ms, this->last_loop_ms_) < 1000U) return;
  this->last_loop_ms_ = now_ms;

  for (size_t slot = 0U; slot < this->units_().size(); ++slot) {
    UnitState& unit = this->units_()[slot];
    if (!unit.configured) continue;

    bool has_pending_fault_word = false;
    for (bool pending : unit.fault_pending) {
      has_pending_fault_word = has_pending_fault_word || pending;
    }
    if (has_pending_fault_word &&
        elapsed_ms_(now_ms, unit.fault_pending_since_ms) >= PARTIAL_FAULT_SNAPSHOT_TIMEOUT_MS) {
      this->process_fault_snapshot_(unit, slot, now_ms, true);
    }

    if (link_round_timeout_elapsed(now_ms, unit.last_link_round_ms, LINK_ROUND_TIMEOUT_MS)) {
      unit.engine.observe_link_round(now_ms, false);
      unit.last_link_round_ms = now_ms;
    }
    unit.engine.tick(now_ms);
    this->publish_transitions_(unit, slot, now_ms);
  }
  this->publish_snapshot_(now_ms);
}

void OpenQuattIncidentManager::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt heat-pump incident manager:");
  ESP_LOGCONFIG(TAG, "  Heat pumps: %u", this->configured_hp_count());
  ESP_LOGCONFIG(TAG, "  Incident catalog: %u definitions, version 1",
                static_cast<unsigned>(oq_incidents::kHpIncidentCatalog.size()));
  ESP_LOGCONFIG(TAG, "  Endpoint: /openquatt/incidents");
  ESP_LOGCONFIG(TAG, "  Start retry: POST /openquatt/incidents/retry-start");
  ESP_LOGCONFIG(TAG,
                "  ODU confirmation: POST "
                "/openquatt/incidents/confirm-odu-power-cycle");
  ESP_LOGCONFIG(TAG, "  Internal control object: %u bytes", static_cast<unsigned>(sizeof(OpenQuattIncidentManager)));
  ESP_LOGCONFIG(TAG, "  PSRAM incident arena: %u bytes", static_cast<unsigned>(sizeof(ExternalState)));
  ESP_LOGCONFIG(TAG, "  PSRAM response scratch: %u bytes (preallocated)",
                static_cast<unsigned>(sizeof(PublishedSnapshot)));
}

void OpenQuattIncidentManager::rotate_action_csrf_token_() {
  std::snprintf(this->action_csrf_token_.data(), this->action_csrf_token_.size(), "%08x%08x%08x%08x",
                static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()),
                static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()));
}

void OpenQuattIncidentManager::record_action_result_(UnitState& unit, const char* action, const char* result, bool ok,
                                                     uint32_t now_ms, uint32_t request_id) {
  unit.last_action = action != nullptr ? action : "unknown";
  unit.last_action_result = result != nullptr ? result : "unknown";
  unit.last_action_ok = ok;
  ++unit.last_action_seq;
  if (unit.last_action_seq == 0U) ++unit.last_action_seq;
  unit.last_action_request_id = request_id;
  unit.last_action_at_ms = now_ms;

  if (request_id == 0U) return;
  if (this->action_mutex_ == nullptr || xSemaphoreTake(this->action_mutex_, portMAX_DELAY) != pdTRUE) {
    ESP_LOGE(TAG, "Could not lock incident action state");
    return;
  }
  const size_t insert_index = (unit.action_result_head + unit.action_result_count) % ACTION_RESULT_HISTORY_SIZE;
  unit.action_results[insert_index] = {
      unit.last_action, unit.last_action_result, ok, unit.last_action_seq, request_id, now_ms};
  if (unit.action_result_count < ACTION_RESULT_HISTORY_SIZE) {
    ++unit.action_result_count;
  } else {
    unit.action_result_head = (unit.action_result_head + 1U) % ACTION_RESULT_HISTORY_SIZE;
  }
  if (unit.pending_action_request_id == request_id) {
    unit.pending_action_request_id = 0U;
    unit.pending_action_kind = DeferredActionKind::NONE;
  }
  xSemaphoreGive(this->action_mutex_);
}

OpenQuattIncidentManager::DeferredActionQueueResult OpenQuattIncidentManager::queue_deferred_action_(
    UnitState& unit, DeferredActionKind kind, uint32_t request_id) {
  if (request_id == 0U || kind == DeferredActionKind::NONE) {
    return DeferredActionQueueResult::INVALID;
  }
  const char* action = deferred_action_name(kind);
  DeferredActionQueueResult result = DeferredActionQueueResult::ACCEPTED;
  if (this->action_mutex_ == nullptr || xSemaphoreTake(this->action_mutex_, portMAX_DELAY) != pdTRUE) {
    return DeferredActionQueueResult::BUSY;
  }
  for (size_t offset = 0U; offset < unit.action_result_count; ++offset) {
    const size_t index = (unit.action_result_head + offset) % ACTION_RESULT_HISTORY_SIZE;
    const ActionResultRecord& record = unit.action_results[index];
    if (record.request_id == request_id && std::strcmp(record.action, action) == 0) {
      result = DeferredActionQueueResult::DUPLICATE;
      break;
    }
  }
  if (result == DeferredActionQueueResult::ACCEPTED && unit.pending_action_request_id != 0U) {
    result = unit.pending_action_request_id == request_id && unit.pending_action_kind == kind
                 ? DeferredActionQueueResult::DUPLICATE
                 : DeferredActionQueueResult::BUSY;
  }
  if (result == DeferredActionQueueResult::ACCEPTED) {
    unit.pending_action_request_id = request_id;
    unit.pending_action_kind = kind;
  }
  xSemaphoreGive(this->action_mutex_);
  return result;
}

bool OpenQuattIncidentManager::valid_hp_index_(uint8_t hp_index) { return hp_index == 1U || hp_index == 2U; }

size_t OpenQuattIncidentManager::hp_slot_(uint8_t hp_index) { return static_cast<size_t>(hp_index - 1U); }

uint32_t OpenQuattIncidentManager::elapsed_ms_(uint32_t now_ms, uint32_t since_ms) {
  return static_cast<uint32_t>(now_ms - since_ms);
}

bool OpenQuattIncidentManager::storage_ready_() const {
  return static_cast<bool>(this->external_state_) && this->action_mutex_ != nullptr && this->snapshot_mutex_ != nullptr;
}

std::array<OpenQuattIncidentManager::UnitState, 2U>& OpenQuattIncidentManager::units_() {
  return this->external_state_[0].units;
}

const std::array<OpenQuattIncidentManager::UnitState, 2U>& OpenQuattIncidentManager::units_() const {
  return this->external_state_[0].units;
}

OpenQuattIncidentManager::PublishedSnapshot& OpenQuattIncidentManager::published_snapshot_() {
  return this->external_state_[0].snapshots[this->published_snapshot_index_];
}

const OpenQuattIncidentManager::PublishedSnapshot& OpenQuattIncidentManager::published_snapshot_() const {
  return this->external_state_[0].snapshots[this->published_snapshot_index_];
}

OpenQuattIncidentManager::PublishedSnapshot& OpenQuattIncidentManager::staging_snapshot_() {
  return this->external_state_[0].snapshots[this->staging_snapshot_index_];
}

OpenQuattIncidentManager::PublishedSnapshot& OpenQuattIncidentManager::response_snapshot_() const {
  return const_cast<PublishedSnapshot&>(this->external_state_[0].snapshots[2U]);
}

OpenQuattIncidentManager::UnitState* OpenQuattIncidentManager::unit_(uint8_t hp_index) {
  if (!this->storage_ready_() || !valid_hp_index_(hp_index)) return nullptr;
  UnitState& unit = this->units_()[hp_slot_(hp_index)];
  return unit.configured ? &unit : nullptr;
}

const OpenQuattIncidentManager::UnitState* OpenQuattIncidentManager::unit_(uint8_t hp_index) const {
  if (!this->storage_ready_() || !valid_hp_index_(hp_index)) return nullptr;
  const UnitState& unit = this->units_()[hp_slot_(hp_index)];
  return unit.configured ? &unit : nullptr;
}

void OpenQuattIncidentManager::observe_transport(uint8_t hp_index, bool online, uint32_t now_ms) {
  UnitState* unit = this->unit_(hp_index);
  if (unit == nullptr) return;
  unit->transport_seen = true;
  unit->transport_online = online;
  if (!online) {
    unit->pump_raw_observations = {};
    unit->pump_context = {};
    unit->engine.observe_link_round(now_ms, false);
    unit->last_link_round_ms = now_ms;
    this->publish_transitions_(*unit, hp_slot_(hp_index), now_ms);
    this->publish_snapshot_(now_ms);
  }
}

void OpenQuattIncidentManager::observe_working_mode(uint8_t hp_index, float working_mode, uint32_t now_ms) {
  UnitState* unit = this->unit_(hp_index);
  if (unit == nullptr || !std::isfinite(working_mode)) return;
  unit->working_mode = working_mode;
  unit->working_mode_valid = true;
  ++unit->working_mode_generation;
  (void)now_ms;
}

void OpenQuattIncidentManager::observe_compressor_frequency(uint8_t hp_index, float frequency_hz, uint32_t now_ms) {
  UnitState* unit = this->unit_(hp_index);
  if (unit == nullptr || !std::isfinite(frequency_hz)) return;
  unit->compressor_frequency_hz = frequency_hz;
  unit->compressor_frequency_valid = true;
  ++unit->compressor_frequency_generation;

  const oq_incidents::RunState run_state = unit->engine.outputs().run_state;
  const bool waiting_for_start = run_state == oq_incidents::RunState::START_REQUESTED ||
                                 run_state == oq_incidents::RunState::WAIT_MODE ||
                                 run_state == oq_incidents::RunState::WAIT_COMPRESSOR;
  const bool waiting_for_stop =
      run_state == oq_incidents::RunState::STOPPING || run_state == oq_incidents::RunState::STOP_UNCONFIRMED;
  oq_incidents::RunObservation observation;
  observation.now_ms = now_ms;
  observation.compressor_frequency_valid = true;
  observation.compressor_frequency_hz = frequency_hz;
  const bool post_command_mode =
      feedback_generation_is_newer(unit->working_mode_generation, unit->command_mode_generation);
  const bool post_command_feedback =
      post_command_feedback_complete(unit->working_mode_generation, unit->command_mode_generation,
                                     unit->compressor_frequency_generation, unit->command_frequency_generation);
  observation.fresh =
      run_observation_is_fresh(waiting_for_start, waiting_for_stop, unit->engine.outputs().stop_confirmation_pending,
                               unit->stop_feedback_armed, post_command_feedback);
  observation.mode_matches_request =
      unit->working_mode_valid && static_cast<uint8_t>(std::lround(unit->working_mode)) == unit->expected_mode;
  observation.stop_mode_confirmed =
      unit->working_mode_valid && post_command_mode && static_cast<uint8_t>(std::lround(unit->working_mode)) == 0U;
  unit->engine.observe_run(observation);
  this->publish_transitions_(*unit, hp_slot_(hp_index), now_ms);
  if (unit->engine.outputs().run_state == oq_incidents::RunState::RUNNING) {
    unit->start_feedback_armed = false;
  }
  if (unit->engine.outputs().run_state == oq_incidents::RunState::STOPPED) {
    unit->stop_feedback_armed = false;
    unit->active_command_mode = 0U;
    unit->stop_confirmation_reason = openquatt_decision_log::REASON_UNKNOWN;
  }
  this->publish_snapshot_(now_ms);
}

void OpenQuattIncidentManager::observe_fault_word(uint8_t hp_index, uint16_t register_address, uint16_t word,
                                                  uint32_t now_ms) {
  UnitState* unit = this->unit_(hp_index);
  if (unit == nullptr || register_address < oq_incidents::kFirstFaultRegister ||
      register_address > oq_incidents::kLastFaultRegister) {
    return;
  }
  const bool pump_fault_active = register_address == 2121U && (word & (1U << 13U)) != 0U;
  if (pump_fault_active) {
    unit->pump_context = {};
  }
  const size_t bank = static_cast<size_t>(register_address - oq_incidents::kFirstFaultRegister);
  bool had_pending = false;
  for (bool pending : unit->fault_pending) {
    had_pending = had_pending || pending;
  }
  if (!had_pending) unit->fault_pending_since_ms = now_ms;
  unit->fault_words[bank] = word;
  unit->fault_pending[bank] = true;
  const bool fault_snapshot_complete = unit->fault_pending[0U] && unit->fault_pending[1U] && unit->fault_pending[2U];
  this->process_fault_snapshot_(*unit, hp_slot_(hp_index), now_ms, false);
  if (pump_fault_active && !fault_snapshot_complete) {
    this->publish_snapshot_(now_ms);
  }
}

void OpenQuattIncidentManager::observe_pump_register(uint8_t hp_index, uint16_t register_address, uint16_t word,
                                                     uint32_t now_ms) {
  UnitState* unit = this->unit_(hp_index);
  const size_t slot = oq_pump_ipwm::context_raw_observation_index(register_address);
  if (unit == nullptr || slot >= unit->pump_raw_observations.size()) return;
  unit->pump_raw_observations[slot].observe(word, now_ms);
}

void OpenQuattIncidentManager::observe_pump_context(uint8_t hp_index, bool flow_valid, float flow_lph,
                                                    uint32_t now_ms) {
  UnitState* unit = this->unit_(hp_index);
  if (unit == nullptr) return;

  PumpContextState& context = unit->pump_context;
  context = {};
  uint16_t request_raw = 0U;
  context.request_valid = unit->pump_raw_observations[0U].read_if_fresh(now_ms, request_raw);
  context.request_on = context.request_valid && (request_raw & 0x1000U) != 0U;
  uint16_t relay_raw = 0U;
  context.relay_valid = unit->pump_raw_observations[1U].read_if_fresh(now_ms, relay_raw);
  context.relay_on = context.relay_valid && (relay_raw & 0x0800U) != 0U;
  uint16_t flow_switch_raw = 0U;
  context.flow_switch_valid = unit->pump_raw_observations[2U].read_if_fresh(now_ms, flow_switch_raw);
  context.flow_switch_on = context.flow_switch_valid && (flow_switch_raw & 0x2000U) != 0U;
  context.feedback_valid = unit->pump_raw_observations[3U].read_if_fresh(now_ms, context.feedback_raw);
  if (context.feedback_valid) {
    const oq_pump_ipwm::DecodedFeedback feedback = oq_pump_ipwm::decode(context.feedback_raw);
    context.status = feedback.status;
    context.power_valid = feedback.power_valid;
    context.power_w = feedback.power_w;
  }
  context.flow_valid = flow_valid && std::isfinite(flow_lph);
  context.flow_lph = context.flow_valid ? flow_lph : 0.0F;
  context.updated_at_ms = now_ms;
  this->publish_snapshot_(now_ms);
}

void OpenQuattIncidentManager::process_fault_snapshot_(UnitState& unit, size_t slot, uint32_t now_ms,
                                                       bool force_partial) {
  const bool complete = unit.fault_pending[0] && unit.fault_pending[1] && unit.fault_pending[2];
  if (!complete && !force_partial) return;

  oq_incidents::FaultWordsObservation observation;
  observation.now_ms = now_ms;
  observation.words = unit.fault_words;
  observation.fresh = unit.fault_pending;
  unit.engine.observe_fault_words(observation);
  if (complete && this->manual_reset_persistence_.initialization_pending()) {
    const uint8_t hp_mask = static_cast<uint8_t>(1U << slot);
    if ((unit.fault_words[1U] & (1U << 4U)) != 0U) {
      this->initialization_manual_reset_mask_ |= hp_mask;
    }
    if (unit.initialization_fault_snapshot_count < INITIALIZATION_FAULT_SNAPSHOT_COUNT) {
      ++unit.initialization_fault_snapshot_count;
    }
    bool all_snapshots_complete = true;
    for (const UnitState& candidate : this->units_()) {
      if (candidate.configured && candidate.initialization_fault_snapshot_count < INITIALIZATION_FAULT_SNAPSHOT_COUNT) {
        all_snapshots_complete = false;
        break;
      }
    }
    if (all_snapshots_complete) {
      this->manual_reset_persistence_.complete_initialization(this->initialization_manual_reset_mask_);
    }
  }
  unit.fault_pending.fill(false);
  unit.fault_pending_since_ms = 0U;
  if (complete) {
    ++unit.fault_snapshot_generation;
    this->observe_complete_link_round_(unit, now_ms);
  }
  this->publish_transitions_(unit, slot, now_ms);
  this->publish_snapshot_(now_ms);
}

void OpenQuattIncidentManager::observe_complete_link_round_(UnitState& unit, uint32_t now_ms) {
  const bool complete =
      unit.transport_seen && unit.transport_online &&
      feedback_generation_is_newer(unit.working_mode_generation, unit.last_link_working_generation) &&
      feedback_generation_is_newer(unit.compressor_frequency_generation, unit.last_link_frequency_generation) &&
      feedback_generation_is_newer(unit.fault_snapshot_generation, unit.last_link_fault_generation);
  if (!complete) return;

  unit.engine.observe_link_round(now_ms, true);
  unit.last_link_working_generation = unit.working_mode_generation;
  unit.last_link_frequency_generation = unit.compressor_frequency_generation;
  unit.last_link_fault_generation = unit.fault_snapshot_generation;
  unit.last_link_round_ms = now_ms;
}

bool OpenQuattIncidentManager::request_start(uint8_t hp_index, uint8_t expected_mode, uint32_t now_ms) {
  UnitState* unit = this->unit_(hp_index);
  const bool newly_armed = unit != nullptr && unit->engine.outputs().run_state == oq_incidents::RunState::STOPPED;
  if (unit == nullptr || !unit->engine.request_start(now_ms)) return false;
  unit->expected_mode = expected_mode;
  unit->active_command_mode = expected_mode;
  unit->command_mode_generation = unit->working_mode_generation;
  unit->command_frequency_generation = unit->compressor_frequency_generation;
  unit->start_feedback_armed = newly_armed;
  unit->stop_feedback_armed = false;
  this->publish_transitions_(*unit, hp_slot_(hp_index), now_ms);
  this->publish_snapshot_(now_ms);
  return true;
}

void OpenQuattIncidentManager::request_stop(uint8_t hp_index, uint32_t now_ms) {
  UnitState* unit = this->unit_(hp_index);
  if (unit == nullptr) return;
  const oq_incidents::DerivedOutputs stop_cause_outputs = unit->engine.outputs();
  if (unit->engine.request_stop(now_ms)) {
    uint8_t stop_reason = openquatt_decision_log::REASON_UNKNOWN;
    if (stop_cause_outputs.must_stop) {
      stop_reason = reason_for_incident_id_(stop_cause_outputs.primary_incident_id);
      if (stop_reason == openquatt_decision_log::REASON_UNKNOWN &&
          stop_cause_outputs.primary_incident_id != oq_incidents::kNoIncident) {
        stop_reason = incident_reason_(oq_incidents::definition_for_id(stop_cause_outputs.primary_incident_id));
      }
    }
    if (stop_reason == openquatt_decision_log::REASON_UNKNOWN) {
      stop_reason = cleared_command_reason(unit->active_command_mode);
    }
    unit->expected_mode = 0U;
    unit->stop_confirmation_reason = stop_reason;
    unit->command_mode_generation = unit->working_mode_generation;
    unit->command_frequency_generation = unit->compressor_frequency_generation;
    unit->start_feedback_armed = false;
    unit->stop_feedback_armed = true;
  }
  this->publish_transitions_(*unit, hp_slot_(hp_index), now_ms);
  this->publish_snapshot_(now_ms);
}

oq_incidents::StartFailureResetResult OpenQuattIncidentManager::retry_start_failure(uint8_t hp_index, uint32_t now_ms,
                                                                                    uint32_t request_id) {
  UnitState* unit = this->unit_(hp_index);
  const oq_incidents::StartFailureResetResult result =
      perform_start_failure_retry(unit != nullptr ? &unit->engine : nullptr, now_ms);
  if (unit == nullptr) return result;
  const bool cleared = result == oq_incidents::StartFailureResetResult::CLEARED;
  record_action_result_(*unit, "start_failure_retry", start_failure_reset_result_name(result), cleared, now_ms,
                        request_id);
  if (cleared) {
    this->publish_transitions_(*unit, hp_slot_(hp_index), now_ms);
  }
  this->publish_snapshot_(now_ms);
  return result;
}

OpenQuattIncidentManager::DeferredActionQueueResult OpenQuattIncidentManager::defer_start_failure_retry(
    uint8_t hp_index, uint32_t request_id) {
  UnitState* unit = this->unit_(hp_index);
  if (unit == nullptr) return DeferredActionQueueResult::INVALID;
  const DeferredActionQueueResult queue_result =
      this->queue_deferred_action_(*unit, DeferredActionKind::START_FAILURE_RETRY, request_id);
  if (queue_result != DeferredActionQueueResult::ACCEPTED) {
    return queue_result;
  }
  this->defer([this, hp_index, request_id]() { this->retry_start_failure(hp_index, millis(), request_id); });
  return queue_result;
}

bool OpenQuattIncidentManager::acknowledge(uint8_t hp_index, oq_incidents::IncidentId incident_id) {
  UnitState* unit = this->unit_(hp_index);
  if (unit == nullptr) return false;
  const size_t slot = hp_slot_(hp_index);
  const uint32_t now_ms = millis();

  if (incident_id <= oq_incidents::kRawIncidentSlotCount) {
    if (!unit->engine.acknowledge(incident_id)) return false;
    this->publish_transitions_(*unit, slot, now_ms);
    this->publish_snapshot_(now_ms);
    return true;
  }

  const size_t synthetic_slot = synthetic_slot_(incident_id);
  if (synthetic_slot >= SYNTHETIC_INCIDENT_COUNT) return false;
  oq_incidents::IncidentRuntime& runtime = unit->synthetic_incidents[synthetic_slot];
  if (!runtime.confirmed_active && !runtime.latched) return false;
  const oq_incidents::IncidentRuntime previous = runtime;
  runtime.acknowledged = true;
  if (!runtime.confirmed_active) runtime.latched = false;
  const oq_incidents::IncidentDefinition definition = synthetic_definition_(synthetic_slot);
  this->publish_synthetic_transition_(slot, definition, reason_for_incident_id_(definition.id), previous, runtime,
                                      now_ms);
  this->publish_snapshot_(now_ms);
  return true;
}

uint8_t OpenQuattIncidentManager::acknowledge_all_cleared() {
  if (!this->storage_ready_()) return 0U;
  uint8_t acknowledged = 0U;
  const uint32_t now_ms = millis();
  for (size_t hp_slot = 0U; hp_slot < this->units_().size(); ++hp_slot) {
    UnitState& unit = this->units_()[hp_slot];
    if (!unit.configured) continue;

    for (size_t incident_slot = 0U; incident_slot < oq_incidents::kRawIncidentSlotCount; ++incident_slot) {
      const oq_incidents::IncidentId id = static_cast<oq_incidents::IncidentId>(incident_slot + 1U);
      const oq_incidents::IncidentRuntime runtime = unit.engine.incident(id);
      const oq_incidents::IncidentDefinition definition = oq_incidents::definition_for_id(id);
      if (definition.clear_policy == oq_incidents::ClearPolicy::AFTER_CONFIRMED_ODU_POWER_CYCLE) {
        continue;
      }
      if (!runtime.confirmed_active && runtime.latched && unit.engine.acknowledge(id)) {
        ++acknowledged;
      }
    }
    this->publish_transitions_(unit, hp_slot, now_ms);

    for (size_t synthetic_slot = 0U; synthetic_slot < SYNTHETIC_INCIDENT_COUNT; ++synthetic_slot) {
      oq_incidents::IncidentRuntime& runtime = unit.synthetic_incidents[synthetic_slot];
      if (runtime.confirmed_active || !runtime.latched) continue;
      const oq_incidents::IncidentRuntime previous = runtime;
      runtime.acknowledged = true;
      runtime.latched = false;
      const oq_incidents::IncidentDefinition definition = synthetic_definition_(synthetic_slot);
      this->publish_synthetic_transition_(hp_slot, definition, reason_for_incident_id_(definition.id), previous,
                                          runtime, now_ms);
      ++acknowledged;
    }
  }
  this->publish_snapshot_(now_ms);
  return acknowledged;
}

bool OpenQuattIncidentManager::confirm_odu_power_cycle(uint8_t hp_index, uint32_t now_ms, uint32_t request_id) {
  UnitState* unit = this->unit_(hp_index);
  if (unit == nullptr) {
    return false;
  }
  if (!unit->engine.has_cleared_power_cycle_latch()) {
    record_action_result_(*unit, "confirm_odu_power_cycle", "no_cleared_manual_reset_latch", false, now_ms, request_id);
    this->publish_snapshot_(now_ms);
    return false;
  }

  uint8_t target_mask = 0U;
  if (!this->manual_reset_persistence_.confirmation_target(hp_index, true, &target_mask)) {
    ESP_LOGE(TAG,
             "HP%u ODU power-cycle confirmation blocked: persistent "
             "manual-reset state is unavailable",
             hp_index);
    record_action_result_(*unit, "confirm_odu_power_cycle", "persistence_unavailable", false, now_ms, request_id);
    this->publish_snapshot_(now_ms);
    return false;
  }
  if (!this->persist_manual_reset_mask_(target_mask)) {
    this->manual_reset_persistence_.mark_confirmation_failure(hp_index, now_ms);
    ESP_LOGE(TAG,
             "HP%u ODU power-cycle confirmation blocked: persistent latch "
             "could not be cleared",
             hp_index);
    record_action_result_(*unit, "confirm_odu_power_cycle", "persistence_write_failed", false, now_ms, request_id);
    this->publish_transitions_(*unit, hp_slot_(hp_index), now_ms);
    this->publish_snapshot_(now_ms);
    return false;
  }
  this->manual_reset_persistence_.mark_persist_success(target_mask, now_ms);

  if (!unit->engine.confirm_odu_power_cycle(now_ms)) {
    // The precondition was checked above on the same component thread. If it
    // ever changes unexpectedly, re-observe the still-active runtime latch so
    // it is durably restored before availability can be released.
    this->manual_reset_persistence_.observe_runtime_latches(this->runtime_manual_reset_mask_());
    this->reconcile_manual_reset_persistence_(now_ms);
    ESP_LOGE(TAG, "HP%u ODU power-cycle confirmation raced with incident state", hp_index);
    record_action_result_(*unit, "confirm_odu_power_cycle", "incident_state_changed", false, now_ms, request_id);
    this->publish_transitions_(*unit, hp_slot_(hp_index), now_ms);
    this->publish_snapshot_(now_ms);
    return false;
  }
  record_action_result_(*unit, "confirm_odu_power_cycle", "odu_power_cycle_confirmed", true, now_ms, request_id);
  this->publish_transitions_(*unit, hp_slot_(hp_index), now_ms);
  this->publish_snapshot_(now_ms);
  return true;
}

OpenQuattIncidentManager::DeferredActionQueueResult OpenQuattIncidentManager::defer_odu_power_cycle_confirmation(
    uint8_t hp_index, uint32_t request_id) {
  UnitState* unit = this->unit_(hp_index);
  if (unit == nullptr) return DeferredActionQueueResult::INVALID;
  const DeferredActionQueueResult queue_result =
      this->queue_deferred_action_(*unit, DeferredActionKind::CONFIRM_ODU_POWER_CYCLE, request_id);
  if (queue_result != DeferredActionQueueResult::ACCEPTED) {
    return queue_result;
  }
  this->defer([this, hp_index, request_id]() { this->confirm_odu_power_cycle(hp_index, millis(), request_id); });
  return queue_result;
}

uint8_t OpenQuattIncidentManager::availability_state_(const oq_incidents::DerivedOutputs& outputs) {
  if (outputs.link_state == oq_incidents::LinkState::LOST) {
    return openquatt_decision_log::STATE_OFFLINE;
  }
  if (outputs.protection_state == oq_incidents::ProtectionState::FAULT_ACTIVE) {
    return openquatt_decision_log::STATE_FAULTED;
  }
  if (outputs.link_state == oq_incidents::LinkState::RECOVERING ||
      outputs.protection_state == oq_incidents::ProtectionState::FAULT_RECOVERY) {
    return openquatt_decision_log::STATE_RECOVERING;
  }
  if (outputs.must_stop) {
    return openquatt_decision_log::STATE_FAULTED;
  }
  if (outputs.available_for_start) {
    return openquatt_decision_log::STATE_AVAILABLE;
  }
  if (outputs.protection_state == oq_incidents::ProtectionState::START_BLOCKED) {
    return outputs.primary_incident_id == oq_incidents::incident_id(2119U, 6U) ? openquatt_decision_log::STATE_PREHEAT
                                                                               : openquatt_decision_log::STATE_BLOCKED;
  }
  if (outputs.link_state == oq_incidents::LinkState::BOOTSTRAP ||
      outputs.link_state == oq_incidents::LinkState::SUSPECT) {
    return openquatt_decision_log::STATE_SUSPECT;
  }
  return openquatt_decision_log::STATE_BLOCKED;
}

uint8_t OpenQuattIncidentManager::incident_reason_(const oq_incidents::IncidentDefinition& definition) {
  if (definition.id == oq_incidents::incident_id(2119U, 6U)) {
    return openquatt_decision_log::REASON_HP_PREHEAT;
  }
  if (definition.category == oq_incidents::IncidentCategory::PROTECTION) {
    return openquatt_decision_log::REASON_HP_PROTECTION;
  }
  return openquatt_decision_log::REASON_HP_FAULT;
}

uint8_t OpenQuattIncidentManager::incident_severity_(const oq_incidents::IncidentDefinition& definition) {
  switch (definition.severity) {
    case oq_incidents::IncidentSeverity::INFO:
      return openquatt_decision_log::SEVERITY_NORMAL;
    case oq_incidents::IncidentSeverity::WARNING:
      return openquatt_decision_log::SEVERITY_LIMITED;
    case oq_incidents::IncidentSeverity::FAULT:
      return openquatt_decision_log::SEVERITY_FAULT;
  }
  return openquatt_decision_log::SEVERITY_ATTENTION;
}

uint8_t OpenQuattIncidentManager::event_subject_(size_t slot) {
  return slot == 0U ? openquatt_decision_log::SUBJECT_HP1 : openquatt_decision_log::SUBJECT_HP2;
}

uint8_t OpenQuattIncidentManager::incident_flags_(const oq_incidents::IncidentDefinition& definition,
                                                  const oq_incidents::IncidentRuntime& runtime) {
  uint8_t flags = oq_incidents::valid_fault_location(definition.register_address, definition.bit)
                      ? openquatt_decision_log::FLAG_RAW_VALUE_VALID
                      : openquatt_decision_log::FLAG_NONE;
  if (runtime.latched) flags |= openquatt_decision_log::FLAG_LATCHED;
  if (definition.clear_policy == oq_incidents::ClearPolicy::AFTER_STABLE_READS) {
    flags |= openquatt_decision_log::FLAG_AUTO_CLEARABLE;
  } else {
    flags |= openquatt_decision_log::FLAG_MANUAL_RESET_REQUIRED;
  }
  return flags;
}

uint16_t OpenQuattIncidentManager::duration_seconds_(uint32_t from_ms, uint32_t to_ms) {
  const uint32_t seconds = elapsed_ms_(to_ms, from_ms) / 1000U;
  return seconds > std::numeric_limits<uint16_t>::max() ? std::numeric_limits<uint16_t>::max()
                                                        : static_cast<uint16_t>(seconds);
}

size_t OpenQuattIncidentManager::synthetic_slot_(oq_incidents::IncidentId incident_id) {
  switch (incident_id) {
    case LINK_LOSS_INCIDENT_ID:
      return 0U;
    case START_FAILED_INCIDENT_ID:
      return 1U;
    case STOP_UNCONFIRMED_INCIDENT_ID:
      return 2U;
    case PERSISTENCE_FAILURE_INCIDENT_ID:
      return 3U;
    default:
      return SYNTHETIC_INCIDENT_COUNT;
  }
}

oq_incidents::IncidentDefinition OpenQuattIncidentManager::synthetic_definition_(size_t slot) {
  using oq_incidents::ClearPolicy;
  using oq_incidents::DocumentationConfidence;
  using oq_incidents::FallbackPolicy;
  using oq_incidents::IncidentCategory;
  using oq_incidents::IncidentEffect;
  using oq_incidents::IncidentSeverity;
  using oq_incidents::RecoveryCondition;
  using oq_incidents::UserAction;

  const oq_incidents::EffectMask fallback_fault_effects = static_cast<oq_incidents::EffectMask>(
      IncidentEffect::DISPLAY | IncidentEffect::BLOCK_START | IncidentEffect::STOP_COMPRESSOR |
      IncidentEffect::MARK_HP_UNAVAILABLE | IncidentEffect::ALLOW_CM4);
  switch (slot) {
    case 0U:
      return {LINK_LOSS_INCIDENT_ID,
              0U,
              0U,
              "hp_link_loss",
              "hp.link_loss",
              IncidentCategory::FAULT,
              IncidentSeverity::FAULT,
              fallback_fault_effects,
              3U,
              3U,
              ClearPolicy::AFTER_STABLE_READS,
              FallbackPolicy::AFTER_SYSTEM_GUARDS,
              DocumentationConfidence::DESCRIBED,
              UserAction::CHECK_INSTALLATION,
              RecoveryCondition::STABLE_TELEMETRY};
    case 1U:
      return {START_FAILED_INCIDENT_ID,
              0U,
              0U,
              "hp_start_failed",
              "hp.start_failed",
              IncidentCategory::FAULT,
              IncidentSeverity::FAULT,
              fallback_fault_effects,
              1U,
              1U,
              ClearPolicy::AFTER_STABLE_READS,
              FallbackPolicy::AFTER_SYSTEM_GUARDS,
              DocumentationConfidence::DESCRIBED,
              UserAction::CHECK_INSTALLATION,
              RecoveryCondition::EXPLICIT_RETRY_AFTER_SAFE_STOP};
    case 2U:
      return {STOP_UNCONFIRMED_INCIDENT_ID,
              0U,
              0U,
              "hp_stop_unconfirmed",
              "hp.stop_unconfirmed",
              IncidentCategory::FAULT,
              IncidentSeverity::FAULT,
              static_cast<oq_incidents::EffectMask>(IncidentEffect::DISPLAY | IncidentEffect::BLOCK_START |
                                                    IncidentEffect::STOP_COMPRESSOR |
                                                    IncidentEffect::MARK_HP_UNAVAILABLE | IncidentEffect::BLOCK_BOILER),
              1U,
              2U,
              ClearPolicy::AFTER_STABLE_READS,
              FallbackPolicy::NEVER,
              DocumentationConfidence::DESCRIBED,
              UserAction::CHECK_INSTALLATION,
              RecoveryCondition::FRESH_STOP_CONFIRMATION};
    case 3U:
      return {PERSISTENCE_FAILURE_INCIDENT_ID,
              0U,
              0U,
              "hp_manual_reset_persistence_failure",
              "hp.manual_reset_persistence_failure",
              IncidentCategory::FAULT,
              IncidentSeverity::FAULT,
              static_cast<oq_incidents::EffectMask>(IncidentEffect::DISPLAY | IncidentEffect::BLOCK_START |
                                                    IncidentEffect::MARK_HP_UNAVAILABLE | IncidentEffect::BLOCK_BOILER),
              1U,
              1U,
              ClearPolicy::AFTER_STABLE_READS,
              FallbackPolicy::NEVER,
              DocumentationConfidence::DESCRIBED,
              UserAction::CONTACT_INSTALLER,
              RecoveryCondition::REVIEW_REQUIRED};
    default:
      return {};
  }
}

uint8_t OpenQuattIncidentManager::reason_for_incident_id_(oq_incidents::IncidentId incident_id) {
  switch (incident_id) {
    case LINK_LOSS_INCIDENT_ID:
      return openquatt_decision_log::REASON_HP_LINK_LOSS;
    case START_FAILED_INCIDENT_ID:
      return openquatt_decision_log::REASON_HP_START_FAILED;
    case STOP_UNCONFIRMED_INCIDENT_ID:
      return openquatt_decision_log::REASON_HP_STOP_UNCONFIRMED;
    case PERSISTENCE_FAILURE_INCIDENT_ID:
      return openquatt_decision_log::REASON_HP_PERSISTENCE_FAILURE;
    default:
      return openquatt_decision_log::REASON_UNKNOWN;
  }
}

uint32_t OpenQuattIncidentManager::epoch_for_runtime_ms_(uint32_t generated_epoch_s, uint32_t generated_at_ms,
                                                         uint32_t runtime_ms) {
  if (generated_epoch_s == 0U || runtime_ms == 0U) return 0U;
  const uint32_t age_s = elapsed_ms_(generated_at_ms, runtime_ms) / 1000U;
  return generated_epoch_s > age_s ? generated_epoch_s - age_s : 0U;
}

void OpenQuattIncidentManager::publish_incident_transition_(size_t slot,
                                                            const oq_incidents::IncidentDefinition& definition,
                                                            const oq_incidents::IncidentRuntime& previous,
                                                            const oq_incidents::IncidentRuntime& current,
                                                            uint32_t now_ms) {
  if (this->decision_log_ == nullptr) return;
  const uint8_t control_mode =
      this->control_mode_code_ != nullptr ? static_cast<uint8_t>(this->control_mode_code_->value()) : 0U;
  const size_t bank = static_cast<size_t>(definition.register_address - oq_incidents::kFirstFaultRegister);
  const int16_t raw_word = static_cast<int16_t>(this->units_()[slot].fault_words[bank]);

  if (!previous.confirmed_active && current.confirmed_active) {
    this->decision_log_->emit(openquatt_decision_log::EVENT_INCIDENT_START, event_subject_(slot),
                              incident_reason_(definition), incident_severity_(definition), control_mode,
                              openquatt_decision_log::STATE_IDLE, openquatt_decision_log::STATE_ACTIVE,
                              static_cast<int16_t>(definition.id), raw_word,
                              static_cast<int16_t>(definition.trip_reads), 0U, incident_flags_(definition, current));
  } else if (previous.confirmed_active && !current.confirmed_active) {
    const bool blocking_latch =
        current.latched && definition.clear_policy == oq_incidents::ClearPolicy::AFTER_CONFIRMED_ODU_POWER_CYCLE;
    this->decision_log_->emit(
        openquatt_decision_log::EVENT_INCIDENT_CLEAR, event_subject_(slot), incident_reason_(definition),
        incident_severity_(definition), control_mode, openquatt_decision_log::STATE_ACTIVE,
        blocking_latch ? openquatt_decision_log::STATE_BLOCKED : openquatt_decision_log::STATE_IDLE,
        static_cast<int16_t>(definition.id), raw_word, static_cast<int16_t>(definition.clear_reads),
        duration_seconds_(previous.first_seen_ms, now_ms), incident_flags_(definition, current));
  } else if (!previous.acknowledged && current.acknowledged) {
    this->decision_log_->emit(
        openquatt_decision_log::EVENT_INCIDENT_ACKNOWLEDGED, event_subject_(slot), incident_reason_(definition),
        incident_severity_(definition), control_mode,
        previous.confirmed_active ? openquatt_decision_log::STATE_ACTIVE : openquatt_decision_log::STATE_IDLE,
        current.confirmed_active ? openquatt_decision_log::STATE_ACTIVE : openquatt_decision_log::STATE_IDLE,
        static_cast<int16_t>(definition.id), raw_word, 0, 0U, incident_flags_(definition, current));
  }
}

void OpenQuattIncidentManager::publish_synthetic_incident_(UnitState& unit, size_t hp_slot, size_t synthetic_slot,
                                                           bool next_active, uint32_t now_ms) {
  if (synthetic_slot >= SYNTHETIC_INCIDENT_COUNT) return;
  oq_incidents::IncidentRuntime& runtime = unit.synthetic_incidents[synthetic_slot];
  if (runtime.confirmed_active == next_active) {
    if (next_active) {
      runtime.raw_active = true;
      runtime.last_seen_ms = now_ms;
    }
    return;
  }

  const oq_incidents::IncidentRuntime previous = runtime;
  if (next_active) {
    runtime.raw_active = true;
    runtime.confirmed_active = true;
    runtime.latched = true;
    runtime.acknowledged = false;
    runtime.first_seen_ms = now_ms;
    runtime.last_seen_ms = now_ms;
    runtime.cleared_at_ms = 0U;
    runtime.occurrence_count = runtime.occurrence_count == std::numeric_limits<uint32_t>::max()
                                   ? runtime.occurrence_count
                                   : runtime.occurrence_count + 1U;
  } else {
    runtime.raw_active = false;
    runtime.confirmed_active = false;
    runtime.cleared_at_ms = now_ms;
    runtime.latched = !runtime.acknowledged;
  }

  const oq_incidents::IncidentDefinition definition = synthetic_definition_(synthetic_slot);
  this->publish_synthetic_transition_(hp_slot, definition, reason_for_incident_id_(definition.id), previous, runtime,
                                      now_ms);
}

void OpenQuattIncidentManager::publish_synthetic_transition_(
    size_t hp_slot, const oq_incidents::IncidentDefinition& definition, uint8_t reason,
    const oq_incidents::IncidentRuntime& previous, const oq_incidents::IncidentRuntime& current, uint32_t now_ms) {
  if (this->decision_log_ == nullptr) return;
  const uint8_t control_mode =
      this->control_mode_code_ != nullptr ? static_cast<uint8_t>(this->control_mode_code_->value()) : 0U;
  uint8_t flags = incident_flags_(definition, current);
  if (definition.id == STOP_UNCONFIRMED_INCIDENT_ID) {
    flags |= openquatt_decision_log::FLAG_STOP_UNCONFIRMED;
  }

  if (!previous.confirmed_active && current.confirmed_active) {
    this->decision_log_->emit(openquatt_decision_log::EVENT_INCIDENT_START, event_subject_(hp_slot), reason,
                              openquatt_decision_log::SEVERITY_FAULT, control_mode, openquatt_decision_log::STATE_IDLE,
                              openquatt_decision_log::STATE_ACTIVE, static_cast<int16_t>(definition.id), 0,
                              static_cast<int16_t>(definition.trip_reads), 0U, flags);
  } else if (previous.confirmed_active && !current.confirmed_active) {
    this->decision_log_->emit(openquatt_decision_log::EVENT_INCIDENT_CLEAR, event_subject_(hp_slot), reason,
                              openquatt_decision_log::SEVERITY_FAULT, control_mode,
                              openquatt_decision_log::STATE_ACTIVE, openquatt_decision_log::STATE_IDLE,
                              static_cast<int16_t>(definition.id), 0, static_cast<int16_t>(definition.clear_reads),
                              duration_seconds_(previous.first_seen_ms, now_ms), flags);
  } else if (!previous.acknowledged && current.acknowledged) {
    this->decision_log_->emit(
        openquatt_decision_log::EVENT_INCIDENT_ACKNOWLEDGED, event_subject_(hp_slot), reason,
        openquatt_decision_log::SEVERITY_FAULT, control_mode,
        previous.confirmed_active ? openquatt_decision_log::STATE_ACTIVE : openquatt_decision_log::STATE_IDLE,
        current.confirmed_active ? openquatt_decision_log::STATE_ACTIVE : openquatt_decision_log::STATE_IDLE,
        static_cast<int16_t>(definition.id), 0, 0, 0U, flags);
  }
}

void OpenQuattIncidentManager::publish_transitions_(UnitState& unit, size_t slot, uint32_t now_ms) {
  this->reconcile_manual_reset_persistence_(now_ms);
  const oq_incidents::DerivedOutputs current_outputs = this->outputs_for_slot_(slot);
  if (current_outputs.running_confirmed) {
    unit.run_seen_since_last_confirmed_stop = true;
  }
  if (unit.restored_manual_reset_pending && this->decision_log_ != nullptr) {
    const oq_incidents::IncidentDefinition definition = oq_incidents::definition_for(2120U, 4U);
    const oq_incidents::IncidentRuntime runtime = unit.engine.incident(definition.id);
    const uint8_t control_mode =
        this->control_mode_code_ != nullptr ? static_cast<uint8_t>(this->control_mode_code_->value()) : 0U;
    uint8_t flags = incident_flags_(definition, runtime);
    flags &= static_cast<uint8_t>(~openquatt_decision_log::FLAG_RAW_VALUE_VALID);
    flags |= openquatt_decision_log::FLAG_RESTORED_AFTER_BOOT;
    this->decision_log_->emit(openquatt_decision_log::EVENT_INCIDENT_START, event_subject_(slot),
                              incident_reason_(definition), incident_severity_(definition), control_mode,
                              openquatt_decision_log::STATE_UNKNOWN, openquatt_decision_log::STATE_BLOCKED,
                              static_cast<int16_t>(definition.id), 0, 0, 0U, flags);
    unit.restored_manual_reset_pending = false;
  }
  for (size_t incident_slot = 0U; incident_slot < oq_incidents::kRawIncidentSlotCount; ++incident_slot) {
    const oq_incidents::IncidentId id = static_cast<oq_incidents::IncidentId>(incident_slot + 1U);
    const oq_incidents::IncidentRuntime current = unit.engine.incident(id);
    this->publish_incident_transition_(slot, oq_incidents::definition_for_id(id),
                                       unit.previous_incidents[incident_slot], current, now_ms);
    unit.previous_incidents[incident_slot] = current;
  }

  const bool link_incident_active = unit.synthetic_incidents[0U].confirmed_active;
  const bool next_link_loss = link_incident_active ? current_outputs.link_state != oq_incidents::LinkState::HEALTHY
                                                   : current_outputs.link_state == oq_incidents::LinkState::LOST;
  this->publish_synthetic_incident_(unit, slot, 0U, next_link_loss, now_ms);
  this->publish_synthetic_incident_(unit, slot, 1U, current_outputs.start_timed_out, now_ms);
  this->publish_synthetic_incident_(unit, slot, 2U, current_outputs.stop_unconfirmed, now_ms);
  const uint8_t hp_mask = static_cast<uint8_t>(1U << slot);
  const bool persistence_failure =
      !this->manual_reset_persistence_.initialization_pending() &&
      (!this->manual_reset_persistence_.ready() || (this->manual_reset_persistence_.fault_mask() & hp_mask) != 0U);
  this->publish_synthetic_incident_(unit, slot, 3U, persistence_failure, now_ms);

  const bool stop_confirmation_edge = should_emit_stop_confirmation(
      unit.stop_feedback_armed, unit.previous_outputs.stop_confirmed, current_outputs.stop_confirmed);
  if (this->decision_log_ != nullptr) {
    const uint8_t control_mode =
        this->control_mode_code_ != nullptr ? static_cast<uint8_t>(this->control_mode_code_->value()) : 0U;
    if (should_emit_start_confirmation(unit.start_feedback_armed, unit.previous_outputs.running_confirmed,
                                       current_outputs.running_confirmed)) {
      this->decision_log_->emit(openquatt_decision_log::EVENT_HP_START_CONFIRMED, event_subject_(slot),
                                start_confirmation_reason(unit.active_command_mode),
                                openquatt_decision_log::SEVERITY_NORMAL, control_mode,
                                openquatt_decision_log::STATE_STARTING, openquatt_decision_log::STATE_ACTIVE);
    }
    if (should_emit_operator_stop_confirmation(stop_confirmation_edge, unit.run_seen_since_last_confirmed_stop)) {
      this->decision_log_->emit(openquatt_decision_log::EVENT_HP_STOP_CONFIRMED, event_subject_(slot),
                                unit.stop_confirmation_reason, openquatt_decision_log::SEVERITY_NORMAL, control_mode,
                                openquatt_decision_log::STATE_ACTIVE, openquatt_decision_log::STATE_STANDBY);
    }
  }
  if (stop_confirmation_edge) {
    unit.run_seen_since_last_confirmed_stop = false;
  }

  const uint8_t availability = availability_state_(current_outputs);
  if (!unit.availability_reporting_initialized) {
    if (availability != openquatt_decision_log::STATE_SUSPECT &&
        availability_baseline_ready(current_outputs.link_state,
                                    this->manual_reset_persistence_.initialization_pending())) {
      unit.last_reported_availability = availability;
      unit.availability_reporting_initialized = true;
    }
  } else if (availability != openquatt_decision_log::STATE_SUSPECT && availability != unit.last_reported_availability) {
    if (this->decision_log_ != nullptr) {
      const oq_incidents::IncidentDefinition primary =
          oq_incidents::definition_for_id(current_outputs.primary_incident_id);
      uint8_t reason = reason_for_incident_id_(current_outputs.primary_incident_id);
      if (availability == openquatt_decision_log::STATE_OFFLINE) {
        reason = openquatt_decision_log::REASON_HP_LINK_LOSS;
      } else if (availability == openquatt_decision_log::STATE_RECOVERING) {
        reason = openquatt_decision_log::REASON_HP_RECOVERY_WAIT;
      } else if (reason == openquatt_decision_log::REASON_UNKNOWN) {
        reason = primary.id != oq_incidents::kNoIncident
                     ? incident_reason_(primary)
                     : static_cast<uint8_t>(openquatt_decision_log::REASON_KEEP_CURRENT);
      }
      this->decision_log_->emit(
          openquatt_decision_log::EVENT_HP_AVAILABILITY_CHANGE, event_subject_(slot), reason,
          availability == openquatt_decision_log::STATE_FAULTED || availability == openquatt_decision_log::STATE_OFFLINE
              ? openquatt_decision_log::SEVERITY_FAULT
              : openquatt_decision_log::SEVERITY_NORMAL,
          this->control_mode_code_ != nullptr ? static_cast<uint8_t>(this->control_mode_code_->value()) : 0U,
          unit.last_reported_availability, availability, static_cast<int16_t>(current_outputs.primary_incident_id),
          static_cast<int16_t>(current_outputs.active_incident_count));
    }
    unit.last_reported_availability = availability;
  }
  unit.previous_outputs = current_outputs;
}

oq_incidents::DerivedOutputs OpenQuattIncidentManager::get_outputs(uint8_t hp_index) const {
  if (!valid_hp_index_(hp_index)) {
    return {};
  }
  if (!this->storage_ready_()) {
    return incident_storage_failure_outputs();
  }
  if (!this->units_()[hp_slot_(hp_index)].configured) return {};

  const size_t slot = hp_slot_(hp_index);
  oq_incidents::DerivedOutputs outputs = incident_storage_failure_outputs();
  if (xSemaphoreTake(this->snapshot_mutex_, portMAX_DELAY) != pdTRUE) {
    return outputs;
  }
  outputs = this->published_snapshot_().units[slot].outputs;
  xSemaphoreGive(this->snapshot_mutex_);
  return outputs;
}

bool OpenQuattIncidentManager::hp_configured(uint8_t hp_index) const { return this->unit_(hp_index) != nullptr; }

uint8_t OpenQuattIncidentManager::configured_hp_count() const { return OQ_TOPOLOGY_DUO != 0 ? 2U : 1U; }

uint8_t OpenQuattIncidentManager::available_hp_count() const {
  uint8_t count = 0U;
  for (uint8_t hp_index = 1U; hp_index <= this->configured_hp_count(); ++hp_index) {
    if (this->get_outputs(hp_index).available_for_start) ++count;
  }
  return count;
}

bool OpenQuattIncidentManager::availability_complete() const {
  if (!this->storage_ready_()) return false;
  const uint8_t configured_mask =
      this->configured_hp_count() == 2U ? oq_incidents::kManualResetAllHpMask : oq_incidents::kManualResetHp1Mask;
  if (!this->manual_reset_persistence_.ready() ||
      (this->manual_reset_persistence_.fault_mask() & configured_mask) != 0U) {
    return false;
  }
  for (uint8_t hp_index = 1U; hp_index <= this->configured_hp_count(); ++hp_index) {
    const oq_incidents::LinkState state = this->get_outputs(hp_index).link_state;
    if (state == oq_incidents::LinkState::BOOTSTRAP || state == oq_incidents::LinkState::SUSPECT ||
        state == oq_incidents::LinkState::RECOVERING) {
      return false;
    }
  }
  return true;
}

bool OpenQuattIncidentManager::all_unavailable_hps_allow_fallback() const {
  std::array<oq_incidents::DerivedOutputs, 2U> outputs{};
  const size_t configured_count = this->configured_hp_count();
  for (size_t slot = 0U; slot < configured_count; ++slot) {
    outputs[slot] = this->get_outputs(static_cast<uint8_t>(slot + 1U));
  }
  return openquatt_incident_manager::all_unavailable_hps_allow_fallback(outputs.data(), configured_count);
}

bool OpenQuattIncidentManager::all_fallback_outputs_safe() const {
  std::array<oq_incidents::DerivedOutputs, 2U> outputs{};
  const size_t configured_count = this->configured_hp_count();
  for (size_t slot = 0U; slot < configured_count; ++slot) {
    outputs[slot] = this->get_outputs(static_cast<uint8_t>(slot + 1U));
  }
  return all_hp_outputs_safe_for_fallback(outputs.data(), configured_count);
}

void OpenQuattIncidentManager::set_fallback_status(bool requested, bool active, uint8_t block_reason) {
  this->fallback_requested_ = requested;
  this->fallback_active_ = active;
  this->fallback_block_reason_ = block_reason;
  this->publish_snapshot_(millis());
}

void OpenQuattIncidentManager::set_boiler_status(uint8_t role, bool command_active, bool output_continuous) {
  if (role != this->boiler_role_) {
    this->previous_boiler_role_ = this->boiler_role_;
    this->boiler_role_ = role;
    this->boiler_output_continuous_ = output_continuous;
  } else if (!command_active) {
    this->boiler_output_continuous_ = false;
  }
  this->boiler_command_active_ = command_active;
  this->publish_snapshot_(millis());
}

uint32_t OpenQuattIncidentManager::current_epoch_s_() const {
  if (this->clock_ == nullptr) return 0U;
  const auto now = this->clock_->now();
  return now.is_valid() ? static_cast<uint32_t>(now.timestamp) : 0U;
}

void OpenQuattIncidentManager::setup_manual_reset_persistence_(uint32_t now_ms) {
  const uint8_t configured_mask =
      this->configured_hp_count() == 2U ? oq_incidents::kManualResetAllHpMask : oq_incidents::kManualResetHp1Mask;
  oq_incidents::ManualResetLatchStorage storage{};
  oq_incidents::ManualResetLatchStorage storage_b{};
  oq_incidents::ManualResetLatchMarker marker{};
  bool storage_a_loaded = false;
  bool storage_b_loaded = false;
  bool marker_loaded = false;

  if (global_preferences != nullptr) {
    this->manual_reset_pref_a_ =
        global_preferences->make_preference<oq_incidents::ManualResetLatchStorage>(MANUAL_RESET_STORAGE_A_KEY, true);
    this->manual_reset_pref_b_ =
        global_preferences->make_preference<oq_incidents::ManualResetLatchStorage>(MANUAL_RESET_STORAGE_B_KEY, true);
    this->manual_reset_marker_pref_ =
        global_preferences->make_preference<oq_incidents::ManualResetLatchMarker>(MANUAL_RESET_MARKER_KEY, true);
    storage_a_loaded = this->manual_reset_pref_a_.load(&storage);
    storage_b_loaded = this->manual_reset_pref_b_.load(&storage_b);
    marker_loaded = this->manual_reset_marker_pref_.load(&marker);
  }

  const auto load_result = this->manual_reset_persistence_.load(storage_a_loaded, storage, storage_b_loaded, storage_b,
                                                                marker_loaded, marker, configured_mask);
  if (load_result == oq_incidents::ManualResetLatchPersistencePolicy::LoadResult::INITIALIZATION_REQUIRED) {
    ESP_LOGI(TAG,
             "Persistent manual-reset latch state needs an initial "
             "fault-snapshot baseline");
  } else if (load_result == oq_incidents::ManualResetLatchPersistencePolicy::LoadResult::RECOVERY_REQUIRED) {
    ESP_LOGE(TAG,
             "Persistent manual-reset latch state is ambiguous; all "
             "configured HPs are conservatively latched while redundant "
             "storage is repaired");
  }

  const oq_incidents::IncidentId power_cycle_incident = oq_incidents::incident_id(2120U, 4U);
  const uint8_t restored_mask = this->manual_reset_persistence_.persisted_mask();
  for (size_t slot = 0U; slot < this->units_().size(); ++slot) {
    if (!this->units_()[slot].configured || (restored_mask & (1U << slot)) == 0U) {
      continue;
    }
    this->units_()[slot].engine.restore_power_cycle_latch(power_cycle_incident);
    this->units_()[slot].restored_manual_reset_pending = true;
    ESP_LOGW(TAG, "Restored persistent manual-reset latch for HP%u", static_cast<unsigned>(slot + 1U));
  }

  this->reconcile_manual_reset_persistence_(now_ms);
}

void OpenQuattIncidentManager::reconcile_manual_reset_persistence_(uint32_t now_ms) {
  const uint8_t persisted_mask = this->manual_reset_persistence_.persisted_mask();
  const oq_incidents::IncidentId power_cycle_incident = oq_incidents::incident_id(2120U, 4U);
  for (size_t slot = 0U; slot < this->units_().size(); ++slot) {
    if (!this->units_()[slot].configured || (persisted_mask & (1U << slot)) == 0U ||
        this->units_()[slot].engine.has_power_cycle_latch()) {
      continue;
    }
    this->units_()[slot].engine.restore_power_cycle_latch(power_cycle_incident);
  }

  this->manual_reset_persistence_.observe_runtime_latches(this->runtime_manual_reset_mask_());
  if (!this->manual_reset_persistence_.should_attempt_persist(now_ms, MANUAL_RESET_PERSIST_RETRY_MS)) {
    return;
  }

  const uint8_t target_mask = this->manual_reset_persistence_.persistence_target_mask();
  if (this->persist_manual_reset_mask_(target_mask)) {
    this->manual_reset_persistence_.mark_persist_success(target_mask, now_ms);
    return;
  }
  this->manual_reset_persistence_.mark_persist_failure(now_ms);
  ESP_LOGE(TAG,
           "Could not persist manual-reset latch state; affected HP "
           "availability remains fail-closed");
}

bool OpenQuattIncidentManager::persist_manual_reset_mask_(uint8_t latch_mask) {
  if (global_preferences == nullptr) return false;
  const oq_incidents::ManualResetLatchStorage storage{
      oq_incidents::kManualResetStorageMagic, oq_incidents::kManualResetStorageVersion,
      static_cast<uint8_t>(latch_mask & oq_incidents::kManualResetAllHpMask), 0U};
  const oq_incidents::ManualResetLatchMarker marker{oq_incidents::kManualResetMarkerMagic,
                                                    oq_incidents::kManualResetStorageVersion, 0U};
  // Queue both conservative data copies before the initialization marker.
  // ESP32 sync can partially write its pending list and still return false, so
  // the return value below is based on read-back of both copies.
  const bool storage_a_queued = this->manual_reset_pref_a_.save(&storage);
  const bool storage_b_queued = this->manual_reset_pref_b_.save(&storage);
  const bool marker_queued = this->manual_reset_marker_pref_.save(&marker);
  const bool sync_ok = global_preferences->sync();

  oq_incidents::ManualResetLatchStorage verify_a{};
  oq_incidents::ManualResetLatchStorage verify_b{};
  oq_incidents::ManualResetLatchMarker verify_marker{};
  const bool verify_a_loaded = this->manual_reset_pref_a_.load(&verify_a);
  const bool verify_b_loaded = this->manual_reset_pref_b_.load(&verify_b);
  const bool verify_marker_loaded = this->manual_reset_marker_pref_.load(&verify_marker);
  const bool verified = oq_incidents::redundant_manual_reset_state_matches(
      verify_a_loaded, verify_a, verify_b_loaded, verify_b, verify_marker_loaded, verify_marker, storage.latch_mask);
  if (verified && (!storage_a_queued || !storage_b_queued || !marker_queued || !sync_ok)) {
    ESP_LOGW(TAG,
             "Manual-reset preference write reported a failure but "
             "redundant read-back verified the requested state");
  }
  return verified;
}

uint8_t OpenQuattIncidentManager::runtime_manual_reset_mask_() const {
  uint8_t mask = 0U;
  if (!this->storage_ready_()) return 0U;
  for (size_t slot = 0U; slot < this->units_().size(); ++slot) {
    if (this->units_()[slot].configured && this->units_()[slot].engine.has_power_cycle_latch()) {
      mask |= static_cast<uint8_t>(1U << slot);
    }
  }
  return mask;
}

oq_incidents::DerivedOutputs OpenQuattIncidentManager::outputs_for_slot_(size_t slot) const {
  if (!this->storage_ready_() || slot >= this->units_().size() || !this->units_()[slot].configured) {
    return {};
  }
  oq_incidents::DerivedOutputs outputs = this->units_()[slot].engine.outputs();
  const uint8_t hp_mask = static_cast<uint8_t>(1U << slot);
  outputs = apply_persistence_initialization_gate(outputs, this->manual_reset_persistence_.initialization_pending());
  const bool persistence_blocks =
      !this->manual_reset_persistence_.initialization_pending() &&
      (!this->manual_reset_persistence_.ready() || (this->manual_reset_persistence_.fault_mask() & hp_mask) != 0U);
  return apply_persistence_safety_gate(outputs, persistence_blocks);
}

void OpenQuattIncidentManager::publish_snapshot_(uint32_t now_ms) {
  if (!this->storage_ready_()) return;
  this->reconcile_manual_reset_persistence_(now_ms);
  PublishedSnapshot& next = this->staging_snapshot_();
  next = {};
  next.control_mode =
      this->control_mode_code_ != nullptr ? static_cast<uint8_t>(this->control_mode_code_->value()) : 0U;
  next.boiler_role = this->boiler_role_;
  next.previous_boiler_role = this->previous_boiler_role_;
  next.fallback_block_reason = this->fallback_block_reason_;
  next.fallback_requested = this->fallback_requested_;
  next.fallback_active = this->fallback_active_;
  next.boiler_command_active = this->boiler_command_active_;
  next.boiler_output_continuous = this->boiler_output_continuous_;
  next.generated_at_ms = now_ms;
  next.generated_at_epoch_s = this->current_epoch_s_();
  for (size_t slot = 0U; slot < this->units_().size(); ++slot) {
    if (!this->units_()[slot].configured) continue;
    next.units[slot].outputs = this->outputs_for_slot_(slot);
    for (size_t incident_slot = 0U; incident_slot < oq_incidents::kRawIncidentSlotCount; ++incident_slot) {
      next.units[slot].incidents[incident_slot] =
          this->units_()[slot].engine.incident(static_cast<oq_incidents::IncidentId>(incident_slot + 1U));
    }
    next.units[slot].synthetic_incidents = this->units_()[slot].synthetic_incidents;
    next.units[slot].last_action = this->units_()[slot].last_action;
    next.units[slot].last_action_result = this->units_()[slot].last_action_result;
    next.units[slot].last_action_ok = this->units_()[slot].last_action_ok;
    next.units[slot].last_action_seq = this->units_()[slot].last_action_seq;
    next.units[slot].last_action_request_id = this->units_()[slot].last_action_request_id;
    next.units[slot].last_action_at_ms = this->units_()[slot].last_action_at_ms;
    next.units[slot].pump_context = this->units_()[slot].pump_context;
    if (xSemaphoreTake(this->action_mutex_, portMAX_DELAY) == pdTRUE) {
      next.units[slot].action_result_count = this->units_()[slot].action_result_count;
      for (size_t offset = 0U; offset < next.units[slot].action_result_count; ++offset) {
        const size_t source_index = (this->units_()[slot].action_result_head + offset) % ACTION_RESULT_HISTORY_SIZE;
        next.units[slot].action_results[offset] = this->units_()[slot].action_results[source_index];
      }
      xSemaphoreGive(this->action_mutex_);
    } else {
      next.units[slot].action_result_count = 0U;
      ESP_LOGE(TAG, "Could not lock incident action snapshot");
    }
  }
  if (xSemaphoreTake(this->snapshot_mutex_, portMAX_DELAY) != pdTRUE) {
    ESP_LOGE(TAG, "Could not publish incident snapshot");
    return;
  }
  std::swap(this->published_snapshot_index_, this->staging_snapshot_index_);
  xSemaphoreGive(this->snapshot_mutex_);
}

void OpenQuattIncidentManager::write_snapshot(httpd_req_t* req) const {
  if (!this->storage_ready_()) {
    ESP_LOGE(TAG, "Incident snapshot storage is unavailable");
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, R"({"error":"snapshot_unavailable"})", HTTPD_RESP_USE_STRLEN);
    return;
  }
  if (this->response_snapshot_in_use_.exchange(true)) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, R"({"error":"snapshot_busy"})", HTTPD_RESP_USE_STRLEN);
    return;
  }
  struct ResponseSnapshotClaim {
    explicit ResponseSnapshotClaim(std::atomic<bool>* claimed) : claimed(claimed) {}
    ~ResponseSnapshotClaim() { this->claimed->store(false); }
    std::atomic<bool>* claimed;
  } response_claim(&this->response_snapshot_in_use_);

  PublishedSnapshot& snapshot = this->response_snapshot_();
  if (xSemaphoreTake(this->snapshot_mutex_, portMAX_DELAY) != pdTRUE) {
    ESP_LOGE(TAG, "Could not lock incident response snapshot");
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, R"({"error":"snapshot_unavailable"})", HTTPD_RESP_USE_STRLEN);
    return;
  }
  snapshot = this->published_snapshot_();
  xSemaphoreGive(this->snapshot_mutex_);

  const uint32_t generated_epoch_s =
      snapshot.generated_at_epoch_s != 0U ? snapshot.generated_at_epoch_s : this->current_epoch_s_();
  bool ok = write_raw(req, R"({"schema_version":1,"catalog_version":1,"generated_at_s":)") &&
            write_uint(req, generated_epoch_s) && write_raw(req, R"(,"action_csrf_token":)") &&
            write_json_string(req, this->action_csrf_token_.data()) &&
            write_raw(req, R"(,"system":{"control_mode":)") && write_uint(req, snapshot.control_mode) &&
            write_raw(req, R"(,"action":)") &&
            write_json_string(
                req, system_action_name(snapshot.control_mode, snapshot.fallback_requested, snapshot.fallback_active,
                                        snapshot.fallback_block_reason, snapshot.boiler_command_active)) &&
            write_raw(req, R"(,"boiler_role":)") && write_json_string(req, boiler_role_name(snapshot.boiler_role)) &&
            write_raw(req, R"(,"previous_boiler_role":)") &&
            write_json_string(req, boiler_role_name(snapshot.previous_boiler_role)) &&
            write_raw(req, R"(,"boiler_command_active":)") && write_bool(req, snapshot.boiler_command_active) &&
            write_raw(req, R"(,"boiler_output_continuous":)") && write_bool(req, snapshot.boiler_output_continuous) &&
            write_raw(req, R"(,"fallback_block_reason":)") && write_uint(req, snapshot.fallback_block_reason) &&
            write_raw(req, R"(},"heat_pumps":[)");

  bool first_hp = true;
  for (size_t slot = 0U; ok && slot < this->configured_hp_count(); ++slot) {
    const oq_incidents::DerivedOutputs& outputs = snapshot.units[slot].outputs;
    ok = (first_hp || write_raw(req, ",")) && write_raw(req, R"({"index":)") && write_uint(req, slot + 1U) &&
         write_raw(req, R"(,"link_state":)") && write_json_string(req, link_state_name(outputs.link_state)) &&
         write_raw(req, R"(,"protection_state":)") &&
         write_json_string(req, protection_state_name(outputs.protection_state)) &&
         write_raw(req, R"(,"run_state":)") && write_json_string(req, run_state_name(outputs.run_state)) &&
         write_raw(req, R"(,"availability":)") && write_json_string(req, availability_name(outputs)) &&
         write_raw(req, R"(,"available_for_start":)") && write_bool(req, outputs.available_for_start) &&
         write_raw(req, R"(,"must_stop":)") && write_bool(req, outputs.must_stop) &&
         write_raw(req, R"(,"fault_active":)") && write_bool(req, outputs.fault_active) &&
         write_raw(req, R"(,"protection_active":)") && write_bool(req, outputs.protection_active) &&
         write_raw(req, R"(,"running_confirmed":)") && write_bool(req, outputs.running_confirmed) &&
         write_raw(req, R"(,"stop_confirmed":)") && write_bool(req, outputs.stop_confirmed) &&
         write_raw(req, R"(,"stop_confirmation_pending":)") && write_bool(req, outputs.stop_confirmation_pending) &&
         write_raw(req, R"(,"stop_unconfirmed":)") && write_bool(req, outputs.stop_unconfirmed) &&
         write_raw(req, R"(,"fallback_cause_present":)") && write_bool(req, outputs.fallback_cause_present) &&
         write_raw(req, R"(,"fallback_eligible":)") && write_bool(req, outputs.fallback_eligible) &&
         write_raw(req, R"(,"primary_incident_id":)") && write_uint(req, outputs.primary_incident_id) &&
         write_raw(req, R"(,"pump_context":{"request_on":)") &&
         write_optional_bool(req, snapshot.units[slot].pump_context.request_valid,
                             snapshot.units[slot].pump_context.request_on) &&
         write_raw(req, R"(,"relay_on":)") &&
         write_optional_bool(req, snapshot.units[slot].pump_context.relay_valid,
                             snapshot.units[slot].pump_context.relay_on) &&
         write_raw(req, R"(,"flow_switch_on":)") &&
         write_optional_bool(req, snapshot.units[slot].pump_context.flow_switch_valid,
                             snapshot.units[slot].pump_context.flow_switch_on) &&
         write_raw(req, R"(,"ipwm_feedback_raw":)") &&
         write_optional_uint(req, snapshot.units[slot].pump_context.feedback_valid,
                             snapshot.units[slot].pump_context.feedback_raw) &&
         write_raw(req, R"(,"ipwm_status":)") &&
         write_json_string(req, oq_pump_ipwm::status_name(snapshot.units[slot].pump_context.status)) &&
         write_raw(req, R"(,"pump_power_w":)") &&
         write_optional_float(req, snapshot.units[slot].pump_context.power_valid,
                              snapshot.units[slot].pump_context.power_w) &&
         write_raw(req, R"(,"flow_lph":)") &&
         write_optional_float(req, snapshot.units[slot].pump_context.flow_valid,
                              snapshot.units[slot].pump_context.flow_lph) &&
         write_raw(req, R"(,"updated_at_ms":)") && write_uint(req, snapshot.units[slot].pump_context.updated_at_ms) &&
         write_raw(req, R"(},"last_action_result":)");
    if (ok && snapshot.units[slot].last_action_seq == 0U) {
      ok = write_raw(req, "null");
    } else if (ok) {
      ok = write_raw(req, R"({"sequence":)") && write_uint(req, snapshot.units[slot].last_action_seq) &&
           write_raw(req, R"(,"request_id":)") && write_uint(req, snapshot.units[slot].last_action_request_id) &&
           write_raw(req, R"(,"action":)") && write_json_string(req, snapshot.units[slot].last_action) &&
           write_raw(req, R"(,"ok":)") && write_bool(req, snapshot.units[slot].last_action_ok) &&
           write_raw(req, R"(,"result":)") && write_json_string(req, snapshot.units[slot].last_action_result) &&
           write_raw(req, R"(,"at_ms":)") && write_uint(req, snapshot.units[slot].last_action_at_ms) &&
           write_raw(req, "}");
    }
    ok = ok && write_raw(req, R"(,"action_results":[)");
    for (size_t action_index = 0U; ok && action_index < snapshot.units[slot].action_result_count; ++action_index) {
      const ActionResultRecord& record = snapshot.units[slot].action_results[action_index];
      ok = (action_index == 0U || write_raw(req, ",")) && write_raw(req, R"({"sequence":)") &&
           write_uint(req, record.sequence) && write_raw(req, R"(,"request_id":)") &&
           write_uint(req, record.request_id) && write_raw(req, R"(,"action":)") &&
           write_json_string(req, record.action) && write_raw(req, R"(,"ok":)") && write_bool(req, record.ok) &&
           write_raw(req, R"(,"result":)") && write_json_string(req, record.result) && write_raw(req, R"(,"at_ms":)") &&
           write_uint(req, record.at_ms) && write_raw(req, "}");
    }
    ok = ok && write_raw(req, R"(],"incidents":[)");

    bool first_incident = true;
    const auto write_incident = [&](const oq_incidents::IncidentDefinition& definition,
                                    const oq_incidents::IncidentRuntime& runtime) -> bool {
      if (!runtime.confirmed_active && !runtime.latched) return true;
      const uint32_t first_seen_s =
          epoch_for_runtime_ms_(generated_epoch_s, snapshot.generated_at_ms, runtime.first_seen_ms);
      const uint32_t last_seen_s =
          epoch_for_runtime_ms_(generated_epoch_s, snapshot.generated_at_ms, runtime.last_seen_ms);
      const uint32_t cleared_at_s =
          epoch_for_runtime_ms_(generated_epoch_s, snapshot.generated_at_ms, runtime.cleared_at_ms);
      const bool item_ok =
          (first_incident || write_raw(req, ",")) && write_raw(req, R"({"definition":{"id":)") &&
          write_uint(req, definition.id) && write_raw(req, R"(,"key":)") && write_json_string(req, definition.key) &&
          write_raw(req, R"(,"presentation_key":)") && write_json_string(req, definition.presentation_key) &&
          write_raw(req, R"(,"category":)") && write_json_string(req, category_name(definition.category)) &&
          write_raw(req, R"(,"severity":)") && write_json_string(req, severity_name(definition.severity)) &&
          write_raw(req, R"(,"effects":)") && write_effects(req, definition.effects) &&
          write_raw(req, R"(,"effect_mask":)") && write_uint(req, definition.effects) &&
          write_raw(req, R"(,"user_action":)") && write_json_string(req, user_action_name(definition.user_action)) &&
          write_raw(req, R"(,"recovery_condition":)") &&
          write_json_string(req, recovery_condition_name(definition.recovery_condition)) &&
          write_raw(req, R"(,"register_address":)") && write_uint(req, definition.register_address) &&
          write_raw(req, R"(,"bit":)") && write_uint(req, definition.bit) &&
          write_raw(req, R"(,"source_description":)") &&
          write_json_string(req, oq_incidents::source_description_for(definition.register_address, definition.bit)) &&
          write_raw(req, R"(},"runtime":{"lifecycle":)") &&
          write_json_string(req, incident_lifecycle_name(definition, runtime)) && write_raw(req, R"(,"raw_active":)") &&
          write_bool(req, runtime.raw_active) && write_raw(req, R"(,"confirmed_active":)") &&
          write_bool(req, runtime.confirmed_active) && write_raw(req, R"(,"latched":)") &&
          write_bool(req, runtime.latched) && write_raw(req, R"(,"acknowledged":)") &&
          write_bool(req, runtime.acknowledged) && write_raw(req, R"(,"first_seen_ms":)") &&
          write_uint(req, runtime.first_seen_ms) && write_raw(req, R"(,"last_seen_ms":)") &&
          write_uint(req, runtime.last_seen_ms) && write_raw(req, R"(,"cleared_at_ms":)") &&
          write_uint(req, runtime.cleared_at_ms) && write_raw(req, R"(,"first_seen_s":)") &&
          write_uint(req, first_seen_s) && write_raw(req, R"(,"last_seen_s":)") && write_uint(req, last_seen_s) &&
          write_raw(req, R"(,"cleared_at_s":)") && write_uint(req, cleared_at_s) &&
          write_raw(req, R"(,"occurrence_count":)") && write_uint(req, runtime.occurrence_count) &&
          write_raw(req, "}}");
      if (item_ok) first_incident = false;
      return item_ok;
    };

    for (size_t incident_slot = 0U; ok && incident_slot < oq_incidents::kRawIncidentSlotCount; ++incident_slot) {
      const oq_incidents::IncidentRuntime& runtime = snapshot.units[slot].incidents[incident_slot];
      const oq_incidents::IncidentDefinition definition =
          oq_incidents::definition_for_id(static_cast<oq_incidents::IncidentId>(incident_slot + 1U));
      ok = write_incident(definition, runtime);
    }
    for (size_t synthetic_slot = 0U; ok && synthetic_slot < SYNTHETIC_INCIDENT_COUNT; ++synthetic_slot) {
      ok = write_incident(synthetic_definition_(synthetic_slot),
                          snapshot.units[slot].synthetic_incidents[synthetic_slot]);
    }
    ok = ok && write_raw(req, "]}");
    first_hp = false;
  }
  ok = ok && write_raw(req, "]}");
  if (!ok) {
    ESP_LOGW(TAG, "Incident snapshot response was truncated");
  }
  httpd_resp_send_chunk(req, nullptr, 0);
}

}  // namespace openquatt_incident_manager
}  // namespace esphome
