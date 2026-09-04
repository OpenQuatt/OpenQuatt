#include "OpenQuattOduRuntimeFrequency.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_odu_runtime_frequency {

static const char* const TAG = "openquatt.odu_runtime";

namespace {

bool url_path_matches(const char* url, const char* path) {
  if (url == nullptr || path == nullptr) return false;
  const size_t path_length = std::strlen(path);
  return std::strncmp(url, path, path_length) == 0 && (url[path_length] == '\0' || url[path_length] == '?');
}

bool header_matches_host(const std::string& header_value, const std::string& host) {
  if (host.empty() || header_value.empty()) return false;
  size_t authority_start = 0U;
  const size_t scheme_pos = header_value.find("://");
  if (scheme_pos != std::string::npos) authority_start = scheme_pos + 3U;
  const size_t authority_end = header_value.find_first_of("/?#", authority_start);
  return header_value.substr(authority_start, authority_end == std::string::npos
                                                  ? std::string::npos
                                                  : authority_end - authority_start) == host;
}

bool passes_same_origin(AsyncWebServerRequest* request) {
  const auto host = request->get_header("Host");
  if (!host.has_value() || host->empty()) return false;
  const auto origin = request->get_header("Origin");
  if (origin.has_value() && !header_matches_host(origin.value(), host.value())) return false;
  const auto referer = request->get_header("Referer");
  return !referer.has_value() || header_matches_host(referer.value(), host.value());
}

class ChunkedJsonWriter {
 public:
  explicit ChunkedJsonWriter(httpd_req_t* req) : req_(req) {}

  bool write_char(char value) { return this->write_bytes_(&value, 1U); }
  bool write_literal(const char* value) { return value == nullptr || this->write_bytes_(value, std::strlen(value)); }
  bool write_uint(uint32_t value) {
    char buffer[16];
    const int written = std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(value));
    return written > 0 && this->write_bytes_(buffer, static_cast<size_t>(written));
  }
  bool write_bool(bool value) { return this->write_literal(value ? "true" : "false"); }
  bool write_string(const char* value) {
    if (!this->write_char('"')) return false;
    if (value != nullptr) {
      for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor != '\0'; ++cursor) {
        if (*cursor == '"' || *cursor == '\\') {
          if (!this->write_char('\\') || !this->write_char(static_cast<char>(*cursor))) return false;
        } else if (*cursor == '\n') {
          if (!this->write_literal("\\n")) return false;
        } else if (*cursor >= 0x20U && !this->write_char(static_cast<char>(*cursor))) {
          return false;
        }
      }
    }
    return this->write_char('"');
  }
  bool finish() {
    const bool flushed = this->flush_();
    if (!this->failed_) httpd_resp_send_chunk(this->req_, nullptr, 0);
    return flushed;
  }

 private:
  static constexpr size_t BUFFER_SIZE = 256U;
  bool write_bytes_(const char* data, size_t length) {
    while (!this->failed_ && length > 0U) {
      if (this->used_ == BUFFER_SIZE && !this->flush_()) return false;
      const size_t count = std::min(BUFFER_SIZE - this->used_, length);
      std::memcpy(this->buffer_.data() + this->used_, data, count);
      this->used_ += count;
      data += count;
      length -= count;
    }
    return !this->failed_;
  }
  bool flush_() {
    if (this->failed_) return false;
    if (this->used_ == 0U) return true;
    if (httpd_resp_send_chunk(this->req_, this->buffer_.data(), static_cast<ssize_t>(this->used_)) != ESP_OK) {
      this->failed_ = true;
      this->used_ = 0U;
      return false;
    }
    this->used_ = 0U;
    return true;
  }

  httpd_req_t* req_;
  std::array<char, BUFFER_SIZE> buffer_{};
  size_t used_{0U};
  bool failed_{false};
};

bool parse_frequency_values(const std::string& raw, oq_odu_runtime_frequency::FrequencyValues& values, size_t& count) {
  count = 0U;
  if (raw.empty()) return false;
  size_t start = 0U;
  while (start < raw.size() && count < values.size()) {
    const size_t comma = raw.find(',', start);
    const size_t end = comma == std::string::npos ? raw.size() : comma;
    if (end == start) return false;
    unsigned value = 0U;
    const char* first = raw.data() + start;
    const char* last = raw.data() + end;
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last || value > oq_odu_runtime_frequency::MAX_FREQUENCY_HZ) {
      return false;
    }
    values[count++] = static_cast<uint8_t>(value);
    if (comma == std::string::npos) return true;
    if (comma + 1U == raw.size()) return false;
    start = comma + 1U;
  }
  return start == raw.size();
}

bool write_frequency_values(ChunkedJsonWriter& writer, const oq_odu_runtime_frequency::FrequencyValues& values,
                            size_t count) {
  if (!writer.write_char('[')) return false;
  for (size_t index = 0U; index < count; ++index) {
    if ((index > 0U && !writer.write_char(',')) || !writer.write_uint(values[index])) return false;
  }
  return writer.write_char(']');
}

const char* request_error(OpenQuattOduRuntimeFrequency::RequestResult result) {
  switch (result) {
    case OpenQuattOduRuntimeFrequency::RequestResult::BUSY:
      return "busy";
    case OpenQuattOduRuntimeFrequency::RequestResult::UNAVAILABLE:
      return "unavailable";
    case OpenQuattOduRuntimeFrequency::RequestResult::NOT_LOADED:
      return "load_required";
    case OpenQuattOduRuntimeFrequency::RequestResult::NOT_ARMED:
      return "arm_required";
    case OpenQuattOduRuntimeFrequency::RequestResult::INVALID_TABLE:
      return "invalid_table";
    default:
      return "request_rejected";
  }
}

class RuntimeFrequencyRequestHandler : public AsyncWebHandler {
 public:
  RuntimeFrequencyRequestHandler(OpenQuattOduRuntimeFrequency* parent, uint8_t hp_index) : parent_(parent) {
    std::snprintf(this->status_path_.data(), this->status_path_.size(), "/openquatt/odu-runtime/hp%u/status",
                  static_cast<unsigned>(hp_index));
    std::snprintf(this->load_path_.data(), this->load_path_.size(), "/openquatt/odu-runtime/hp%u/load",
                  static_cast<unsigned>(hp_index));
    std::snprintf(this->arm_path_.data(), this->arm_path_.size(), "/openquatt/odu-runtime/hp%u/arm",
                  static_cast<unsigned>(hp_index));
    std::snprintf(this->apply_path_.data(), this->apply_path_.size(), "/openquatt/odu-runtime/hp%u/apply",
                  static_cast<unsigned>(hp_index));
  }

  bool canHandle(AsyncWebServerRequest* request) const override {
    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url);
    if (url_path_matches(url, this->status_path_.data())) return request->method() == HTTP_GET;
    return request->method() == HTTP_POST &&
           (url_path_matches(url, this->load_path_.data()) || url_path_matches(url, this->arm_path_.data()) ||
            url_path_matches(url, this->apply_path_.data()));
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    if (!this->parent_->request_is_authenticated(request)) {
      request->requestAuthentication();
      return;
    }

    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url);
    if (url_path_matches(url, this->status_path_.data())) {
      this->send_status_(request);
      return;
    }

    if (!passes_same_origin(request) || request->arg("csrf_token") != this->parent_->get_csrf_token()) {
      request->send(409, "application/json", R"({"ok":false,"error":"forbidden"})");
      return;
    }

    OpenQuattOduRuntimeFrequency::RequestResult result;
    if (url_path_matches(url, this->load_path_.data())) {
      result = this->parent_->request_load();
    } else if (url_path_matches(url, this->arm_path_.data())) {
      const std::string enabled = request->arg("enabled");
      if (enabled != "true" && enabled != "false" && enabled != "1" && enabled != "0") {
        request->send(409, "application/json", R"({"ok":false,"error":"invalid_enabled"})");
        return;
      }
      result = this->parent_->request_arm(enabled == "true" || enabled == "1");
    } else {
      oq_odu_runtime_frequency::RuntimeFrequencyTables tables;
      size_t cooling_count = 0U;
      size_t heating_count = 0U;
      if (!parse_frequency_values(request->arg("cooling"), tables.cooling, cooling_count) ||
          !parse_frequency_values(request->arg("heating"), tables.heating, heating_count) ||
          cooling_count != heating_count ||
          (cooling_count != oq_odu_runtime_frequency::BASE_LEVEL_COUNT &&
           cooling_count != oq_odu_runtime_frequency::EXTENDED_LEVEL_COUNT)) {
        request->send(409, "application/json", R"({"ok":false,"error":"invalid_table"})");
        return;
      }
      tables.level_count = static_cast<uint8_t>(cooling_count);
      result = this->parent_->request_apply(tables);
    }

    if (result != OpenQuattOduRuntimeFrequency::RequestResult::ACCEPTED) {
      char response[64];
      std::snprintf(response, sizeof(response), R"({"ok":false,"error":"%s"})", request_error(result));
      request->send(result == OpenQuattOduRuntimeFrequency::RequestResult::UNAVAILABLE ? 503 : 409, "application/json",
                    response);
      return;
    }
    this->send_status_(request);
  }

 protected:
  void send_status_(AsyncWebServerRequest* request) const {
    httpd_req_t* req = *request;
    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    this->parent_->write_status(req);
  }

  static constexpr size_t PATH_SIZE = 48U;
  OpenQuattOduRuntimeFrequency* parent_;
  std::array<char, PATH_SIZE> status_path_{};
  std::array<char, PATH_SIZE> load_path_{};
  std::array<char, PATH_SIZE> arm_path_{};
  std::array<char, PATH_SIZE> apply_path_{};
};

}  // namespace

float OpenQuattOduRuntimeFrequency::get_setup_priority() const { return setup_priority::WIFI - 2.0f; }

void OpenQuattOduRuntimeFrequency::setup() {
  const bool available = this->controller_ != nullptr && this->eeprom_dump_ != nullptr && this->web_auth_ != nullptr &&
                         web_server_base::global_web_server_base != nullptr;
  this->available_.store(available, std::memory_order_release);
  if (!available) {
    ESP_LOGE(TAG, "HP%u runtime frequency service is unavailable", this->hp_index_);
    return;
  }
  web_server_base::global_web_server_base->add_handler(new RuntimeFrequencyRequestHandler(this, this->hp_index_));
}

void OpenQuattOduRuntimeFrequency::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt ODU runtime frequency service");
  ESP_LOGCONFIG(TAG, "  HP: %u", this->hp_index_);
  ESP_LOGCONFIG(TAG, "  Available: %s", YESNO(this->available_.load(std::memory_order_acquire)));
  ESP_LOGCONFIG(TAG, "  Storage: RAM only (%u-byte table payload)",
                static_cast<unsigned>(sizeof(oq_odu_runtime_frequency::RuntimeFrequencyTables)));
}

bool OpenQuattOduRuntimeFrequency::begin_request_(uint32_t& request_token) {
  bool accepted = false;
  portENTER_CRITICAL(&this->state_mux_);
  if (!this->busy_.load(std::memory_order_relaxed)) {
    request_token = this->operation_token_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    if (request_token == 0U) request_token = this->operation_token_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
    this->busy_.store(true, std::memory_order_release);
    accepted = true;
  }
  portEXIT_CRITICAL(&this->state_mux_);
  if (!accepted) return false;

  if (!this->eeprom_dump_->try_begin_external_operation()) {
    portENTER_CRITICAL(&this->state_mux_);
    if (this->operation_token_.load(std::memory_order_acquire) == request_token) {
      this->busy_.store(false, std::memory_order_release);
    }
    portEXIT_CRITICAL(&this->state_mux_);
    return false;
  }
  this->bus_reservation_token_.store(request_token, std::memory_order_release);
  portENTER_CRITICAL(&this->state_mux_);
  accepted = this->busy_.load(std::memory_order_acquire) &&
             this->operation_token_.load(std::memory_order_acquire) == request_token;
  portEXIT_CRITICAL(&this->state_mux_);
  if (!accepted) this->release_bus_(request_token);
  return accepted;
}

OpenQuattOduRuntimeFrequency::RequestResult OpenQuattOduRuntimeFrequency::request_load() {
  if (!this->available_.load(std::memory_order_acquire)) return RequestResult::UNAVAILABLE;
  uint32_t request_token = 0U;
  if (!this->begin_request_(request_token)) return RequestResult::BUSY;
  bool accepted = false;
  portENTER_CRITICAL(&this->state_mux_);
  if (this->busy_.load(std::memory_order_acquire) &&
      this->operation_token_.load(std::memory_order_acquire) == request_token) {
    this->loaded_.store(false, std::memory_order_release);
    this->armed_.store(false, std::memory_order_release);
    this->set_status_locked_("LOAD_REQUESTED");
    this->pending_action_ = PendingAction::LOAD;
    this->pending_request_token_ = request_token;
    accepted = true;
  }
  portEXIT_CRITICAL(&this->state_mux_);
  if (!accepted) {
    this->release_bus_(request_token);
    return RequestResult::BUSY;
  }
  ESP_LOGW(TAG, "HP%u LOAD_REQUESTED", this->hp_index_);
  return RequestResult::ACCEPTED;
}

OpenQuattOduRuntimeFrequency::RequestResult OpenQuattOduRuntimeFrequency::request_arm(bool enabled) {
  if (!this->available_.load(std::memory_order_acquire)) return RequestResult::UNAVAILABLE;
  RequestResult result = RequestResult::ACCEPTED;
  const char* status = enabled ? "ARMED: runtime writes enabled" : "LOCKED: runtime writes disabled";
  portENTER_CRITICAL(&this->state_mux_);
  if (this->busy_.load(std::memory_order_acquire)) {
    result = RequestResult::BUSY;
  } else if (enabled && !this->loaded_.load(std::memory_order_acquire)) {
    result = RequestResult::NOT_LOADED;
  } else {
    this->armed_.store(enabled, std::memory_order_release);
    this->set_status_locked_(status);
  }
  portEXIT_CRITICAL(&this->state_mux_);
  if (result == RequestResult::ACCEPTED) ESP_LOGW(TAG, "HP%u %s", this->hp_index_, status);
  return result;
}

OpenQuattOduRuntimeFrequency::RequestResult OpenQuattOduRuntimeFrequency::request_apply(
    const oq_odu_runtime_frequency::RuntimeFrequencyTables& tables) {
  if (!this->available_.load(std::memory_order_acquire)) return RequestResult::UNAVAILABLE;
  uint32_t request_token = 0U;
  if (!this->begin_request_(request_token)) return RequestResult::BUSY;
  RequestResult result = RequestResult::ACCEPTED;
  portENTER_CRITICAL(&this->state_mux_);
  if (!this->busy_.load(std::memory_order_acquire) ||
      this->operation_token_.load(std::memory_order_acquire) != request_token) {
    result = RequestResult::BUSY;
  } else if (!this->loaded_.load(std::memory_order_acquire)) {
    result = RequestResult::NOT_LOADED;
  } else if (!this->armed_.load(std::memory_order_acquire)) {
    result = RequestResult::NOT_ARMED;
  } else {
    const size_t expected_levels =
        oq_odu_runtime_frequency::normalized_level_count(this->extended_layout_.load(std::memory_order_acquire));
    if (tables.level_count != expected_levels ||
        !oq_odu_runtime_frequency::validate_monotonic_table(tables.cooling, tables.level_count) ||
        !oq_odu_runtime_frequency::validate_monotonic_table(tables.heating, tables.level_count)) {
      result = RequestResult::INVALID_TABLE;
    } else {
      this->operation_tables_ = tables;
      this->set_status_locked_("GUARD_READ_REQUESTED: checking ODU state");
      this->pending_action_ = PendingAction::APPLY;
      this->pending_request_token_ = request_token;
    }
  }
  if (result != RequestResult::ACCEPTED && this->operation_token_.load(std::memory_order_acquire) == request_token) {
    this->busy_.store(false, std::memory_order_release);
    this->operation_token_.fetch_add(1U, std::memory_order_acq_rel);
  }
  portEXIT_CRITICAL(&this->state_mux_);
  if (result != RequestResult::ACCEPTED) {
    this->release_bus_(request_token);
    return result;
  }
  ESP_LOGW(TAG, "HP%u GUARD_READ_REQUESTED: checking ODU state", this->hp_index_);
  return result;
}

void OpenQuattOduRuntimeFrequency::set_extended_layout(bool extended_layout) {
  bool changed = false;
  uint32_t reserved_token = 0U;
  portENTER_CRITICAL(&this->state_mux_);
  if (this->extended_layout_.load(std::memory_order_acquire) != extended_layout) {
    this->extended_layout_.store(extended_layout, std::memory_order_release);
    this->reset_runtime_state_locked_(nullptr);
    reserved_token = this->bus_reservation_token_.exchange(0U, std::memory_order_acq_rel);
    changed = true;
  }
  portEXIT_CRITICAL(&this->state_mux_);
  if (!changed) return;
  ESP_LOGI(TAG, "HP%u READY: load ODU runtime table", this->hp_index_);
  if (reserved_token != 0U) this->eeprom_dump_->end_external_operation();
}

void OpenQuattOduRuntimeFrequency::reset_runtime_state(const char* failure_message) {
  uint32_t reserved_token = 0U;
  portENTER_CRITICAL(&this->state_mux_);
  const char* status = this->reset_runtime_state_locked_(failure_message);
  reserved_token = this->bus_reservation_token_.exchange(0U, std::memory_order_acq_rel);
  portEXIT_CRITICAL(&this->state_mux_);
  ESP_LOGW(TAG, "HP%u %s", this->hp_index_, status);
  if (reserved_token != 0U) this->eeprom_dump_->end_external_operation();
}

const char* OpenQuattOduRuntimeFrequency::reset_runtime_state_locked_(const char* failure_message) {
  const bool had_write_started = this->write_started_;
  this->operation_token_.fetch_add(1U, std::memory_order_acq_rel);
  this->pending_action_ = PendingAction::NONE;
  this->pending_request_token_ = 0U;
  this->operation_ = Operation::NONE;
  this->write_started_ = false;
  this->busy_.store(false, std::memory_order_release);
  this->loaded_.store(false, std::memory_order_release);
  this->armed_.store(false, std::memory_order_release);
  if (had_write_started) this->write_tainted_.store(true, std::memory_order_release);
  const char* status =
      had_write_started && failure_message != nullptr ? failure_message : "READY: load ODU runtime table";
  this->set_status_locked_(status);
  return status;
}

void OpenQuattOduRuntimeFrequency::release_bus_(uint32_t request_token) {
  uint32_t reserved_token = request_token;
  if (request_token == 0U) {
    reserved_token = this->bus_reservation_token_.exchange(0U, std::memory_order_acq_rel);
  } else if (!this->bus_reservation_token_.compare_exchange_strong(reserved_token, 0U, std::memory_order_acq_rel)) {
    return;
  }
  if (reserved_token == 0U) return;
  this->eeprom_dump_->end_external_operation();
}

void OpenQuattOduRuntimeFrequency::set_status_locked_(const char* status) {
  std::snprintf(this->status_, sizeof(this->status_), "%s", status != nullptr ? status : "");
}

bool OpenQuattOduRuntimeFrequency::begin_operation_(Operation operation, uint32_t operation_token) {
  if (!this->token_matches_(operation_token)) return false;
  this->operation_ = operation;
  this->write_started_ = false;
  this->operation_started_ms_ = millis();
  this->operation_timeout_ms_ = this->extended_layout_.load(std::memory_order_acquire) ? EXTENDED_OPERATION_TIMEOUT_MS
                                                                                       : BASE_OPERATION_TIMEOUT_MS;
  return true;
}

bool OpenQuattOduRuntimeFrequency::token_matches_(uint32_t operation_token) const {
  return this->busy_.load(std::memory_order_acquire) &&
         this->operation_token_.load(std::memory_order_acquire) == operation_token;
}

void OpenQuattOduRuntimeFrequency::loop() {
  if (!this->busy_.load(std::memory_order_acquire)) return;

  PendingAction pending = PendingAction::NONE;
  uint32_t pending_request_token = 0U;
  portENTER_CRITICAL(&this->state_mux_);
  pending = this->pending_action_;
  pending_request_token = this->pending_request_token_;
  this->pending_action_ = PendingAction::NONE;
  this->pending_request_token_ = 0U;
  portEXIT_CRITICAL(&this->state_mux_);

  if (pending == PendingAction::LOAD) {
    if (this->begin_operation_(Operation::LOAD, pending_request_token)) {
      this->queue_load_base_(pending_request_token);
    }
    return;
  }
  if (pending == PendingAction::APPLY) {
    if (this->begin_operation_(Operation::APPLY, pending_request_token)) {
      this->queue_guard_(pending_request_token);
    }
    return;
  }

  if (this->operation_ != Operation::NONE && millis() - this->operation_started_ms_ >= this->operation_timeout_ms_) {
    const uint32_t operation_token = this->operation_token_.load(std::memory_order_acquire);
    if (this->operation_ == Operation::LOAD) {
      this->fail_operation_("LOAD_FAILED: Modbus response timeout", operation_token);
    } else if (this->write_started_) {
      this->fail_operation_("VERIFY_FAILED: write acknowledgement timeout", operation_token);
    } else {
      this->finish_without_write_("BLOCKED: ODU guard response timeout", operation_token);
    }
  }
}

void OpenQuattOduRuntimeFrequency::finish_without_write_(const char* status, uint32_t operation_token) {
  portENTER_CRITICAL(&this->state_mux_);
  if (!this->busy_.load(std::memory_order_acquire) ||
      this->operation_token_.load(std::memory_order_acquire) != operation_token) {
    portEXIT_CRITICAL(&this->state_mux_);
    return;
  }
  this->operation_token_.fetch_add(1U, std::memory_order_acq_rel);
  this->operation_ = Operation::NONE;
  this->busy_.store(false, std::memory_order_release);
  this->set_status_locked_(status);
  portEXIT_CRITICAL(&this->state_mux_);
  ESP_LOGW(TAG, "HP%u %s", this->hp_index_, status != nullptr ? status : "");
  this->release_bus_(operation_token);
}

void OpenQuattOduRuntimeFrequency::fail_operation_(const char* status, uint32_t operation_token) {
  portENTER_CRITICAL(&this->state_mux_);
  if (!this->busy_.load(std::memory_order_acquire) ||
      this->operation_token_.load(std::memory_order_acquire) != operation_token) {
    portEXIT_CRITICAL(&this->state_mux_);
    return;
  }
  if (this->write_started_) this->write_tainted_.store(true, std::memory_order_release);
  this->operation_token_.fetch_add(1U, std::memory_order_acq_rel);
  this->operation_ = Operation::NONE;
  this->write_started_ = false;
  this->busy_.store(false, std::memory_order_release);
  this->set_status_locked_(status);
  portEXIT_CRITICAL(&this->state_mux_);
  ESP_LOGW(TAG, "HP%u %s", this->hp_index_, status != nullptr ? status : "");
  this->release_bus_(operation_token);
}

void OpenQuattOduRuntimeFrequency::queue_load_base_(uint32_t operation_token) {
  auto command = modbus_controller::ModbusCommandItem::create_read_command(
      this->controller_, modbus::EntityType::HOLDING, oq_odu_runtime_frequency::BASE_TABLE_START_ADDRESS,
      oq_odu_runtime_frequency::BASE_TABLE_REGISTER_COUNT,
      [this, operation_token](modbus::EntityType, uint16_t start_address, std::span<const uint8_t> data) {
        if (!this->token_matches_(operation_token) ||
            start_address != oq_odu_runtime_frequency::BASE_TABLE_START_ADDRESS) {
          return;
        }
        oq_odu_runtime_frequency::RuntimeFrequencyTables tables;
        size_t loaded = 0U;
        if (!oq_odu_runtime_frequency::parse_base_runtime_table(data.data(), data.size(), tables, loaded)) {
          this->fail_operation_("LOAD_FAILED: incomplete base runtime table", operation_token);
          return;
        }
        if (this->extended_layout_.load(std::memory_order_acquire)) {
          this->queue_load_extension_(tables, operation_token);
          return;
        }
        this->finish_load_(tables, operation_token);
      });
  this->controller_->queue_command(std::move(command));
}

void OpenQuattOduRuntimeFrequency::queue_load_extension_(oq_odu_runtime_frequency::RuntimeFrequencyTables tables,
                                                         uint32_t operation_token) {
  auto command = modbus_controller::ModbusCommandItem::create_read_command(
      this->controller_, modbus::EntityType::HOLDING, oq_odu_runtime_frequency::EXTENDED_TABLE_START_ADDRESS,
      oq_odu_runtime_frequency::EXTENDED_TABLE_REGISTER_COUNT,
      [this, tables, operation_token](modbus::EntityType, uint16_t start_address,
                                      std::span<const uint8_t> data) mutable {
        if (!this->token_matches_(operation_token) ||
            start_address != oq_odu_runtime_frequency::EXTENDED_TABLE_START_ADDRESS) {
          return;
        }
        size_t loaded = 0U;
        if (!oq_odu_runtime_frequency::parse_extended_runtime_table(data.data(), data.size(), tables, loaded)) {
          this->fail_operation_("LOAD_FAILED: incomplete extended runtime table", operation_token);
          return;
        }
        this->finish_load_(tables, operation_token);
      });
  this->controller_->queue_command(std::move(command));
}

void OpenQuattOduRuntimeFrequency::finish_load_(const oq_odu_runtime_frequency::RuntimeFrequencyTables& tables,
                                                uint32_t operation_token) {
  if (!this->token_matches_(operation_token)) return;
  char status[64];
  std::snprintf(status, sizeof(status), "LOADED: %u/%u runtime registers",
                static_cast<unsigned>(oq_odu_runtime_frequency::runtime_register_count(tables.level_count)),
                static_cast<unsigned>(oq_odu_runtime_frequency::runtime_register_count(tables.level_count)));
  portENTER_CRITICAL(&this->state_mux_);
  if (!this->busy_.load(std::memory_order_acquire) ||
      this->operation_token_.load(std::memory_order_acquire) != operation_token) {
    portEXIT_CRITICAL(&this->state_mux_);
    return;
  }
  this->tables_ = tables;
  this->operation_ = Operation::NONE;
  this->busy_.store(false, std::memory_order_release);
  this->loaded_.store(true, std::memory_order_release);
  this->set_status_locked_(status);
  portEXIT_CRITICAL(&this->state_mux_);
  ESP_LOGW(TAG, "HP%u %s", this->hp_index_, status);
  this->release_bus_(operation_token);
}

void OpenQuattOduRuntimeFrequency::queue_guard_(uint32_t operation_token) {
  auto command = modbus_controller::ModbusCommandItem::create_read_command(
      this->controller_, modbus::EntityType::HOLDING, GUARD_START_ADDRESS, GUARD_REGISTER_COUNT,
      [this, operation_token](modbus::EntityType, uint16_t start_address, std::span<const uint8_t> data) {
        if (!this->token_matches_(operation_token) || start_address != GUARD_START_ADDRESS) return;
        uint16_t working_mode = 0U;
        uint16_t compressor_hz = 0U;
        if (!oq_odu_runtime_frequency::read_u16_word(data.data(), data.size(), GUARD_WORKING_MODE_INDEX,
                                                     working_mode)) {
          this->finish_without_write_("BLOCKED: ODU mode unknown", operation_token);
          return;
        }
        if (!oq_odu_runtime_frequency::read_u16_word(data.data(), data.size(), GUARD_COMPRESSOR_FREQUENCY_INDEX,
                                                     compressor_hz)) {
          this->finish_without_write_("BLOCKED: compressor frequency unknown", operation_token);
          return;
        }
        if (working_mode != 0U) {
          this->finish_without_write_("BLOCKED: ODU is not in standby", operation_token);
          return;
        }
        if (compressor_hz > 0U) {
          this->finish_without_write_("BLOCKED: compressor is running", operation_token);
          return;
        }
        this->begin_write_(operation_token);
      });
  this->controller_->queue_command(std::move(command));
}

void OpenQuattOduRuntimeFrequency::begin_write_(uint32_t operation_token) {
  portENTER_CRITICAL(&this->state_mux_);
  if (!this->busy_.load(std::memory_order_acquire) ||
      this->operation_token_.load(std::memory_order_acquire) != operation_token) {
    portEXIT_CRITICAL(&this->state_mux_);
    return;
  }
  this->armed_.store(false, std::memory_order_release);
  this->write_started_ = true;
  this->write_tainted_.store(true, std::memory_order_release);
  this->set_status_locked_("WRITE_QUEUED: runtime table write requested");
  portEXIT_CRITICAL(&this->state_mux_);
  ESP_LOGW(TAG, "HP%u WRITE_QUEUED: runtime table write requested", this->hp_index_);
  this->write_started_callbacks_.call();
  this->queue_write_register_(0U, operation_token);
}

void OpenQuattOduRuntimeFrequency::queue_write_register_(size_t write_index, uint32_t operation_token) {
  if (!this->token_matches_(operation_token)) return;
  const size_t register_count = oq_odu_runtime_frequency::runtime_register_count(this->operation_tables_.level_count);
  if (write_index >= register_count) {
    portENTER_CRITICAL(&this->state_mux_);
    if (!this->busy_.load(std::memory_order_acquire) ||
        this->operation_token_.load(std::memory_order_acquire) != operation_token) {
      portEXIT_CRITICAL(&this->state_mux_);
      return;
    }
    this->set_status_locked_("WRITE_CONFIRMED: runtime writes acknowledged");
    portEXIT_CRITICAL(&this->state_mux_);
    ESP_LOGW(TAG, "HP%u WRITE_CONFIRMED: runtime writes acknowledged", this->hp_index_);
    this->queue_readback_base_(operation_token);
    return;
  }

  const auto target = oq_odu_runtime_frequency::runtime_write_register(this->operation_tables_, write_index);
  if (!target.valid) {
    this->fail_operation_("VERIFY_FAILED: invalid runtime register mapping", operation_token);
    return;
  }
  auto command = modbus_controller::ModbusCommandItem::create_write_single_command(this->controller_, target.address,
                                                                                   target.value);
  command.on_data_func = [this, write_index, operation_token](modbus::EntityType, uint16_t, std::span<const uint8_t>) {
    this->queue_write_register_(write_index + 1U, operation_token);
  };
  this->controller_->queue_command(std::move(command));
}

void OpenQuattOduRuntimeFrequency::queue_readback_base_(uint32_t operation_token) {
  auto command = modbus_controller::ModbusCommandItem::create_read_command(
      this->controller_, modbus::EntityType::HOLDING, oq_odu_runtime_frequency::BASE_TABLE_START_ADDRESS,
      oq_odu_runtime_frequency::BASE_TABLE_REGISTER_COUNT,
      [this, operation_token](modbus::EntityType, uint16_t start_address, std::span<const uint8_t> data) {
        if (!this->token_matches_(operation_token) ||
            start_address != oq_odu_runtime_frequency::BASE_TABLE_START_ADDRESS) {
          return;
        }
        oq_odu_runtime_frequency::RuntimeFrequencyTables actual;
        size_t loaded = 0U;
        if (!oq_odu_runtime_frequency::parse_base_runtime_table(data.data(), data.size(), actual, loaded)) {
          this->fail_operation_("VERIFY_FAILED: incomplete base readback", operation_token);
          return;
        }
        if (this->operation_tables_.level_count == oq_odu_runtime_frequency::EXTENDED_LEVEL_COUNT) {
          this->queue_readback_extension_(actual, operation_token);
          return;
        }
        this->finish_apply_(actual, operation_token);
      });
  this->controller_->queue_command(std::move(command));
}

void OpenQuattOduRuntimeFrequency::queue_readback_extension_(oq_odu_runtime_frequency::RuntimeFrequencyTables actual,
                                                             uint32_t operation_token) {
  auto command = modbus_controller::ModbusCommandItem::create_read_command(
      this->controller_, modbus::EntityType::HOLDING, oq_odu_runtime_frequency::EXTENDED_TABLE_START_ADDRESS,
      oq_odu_runtime_frequency::EXTENDED_TABLE_REGISTER_COUNT,
      [this, actual, operation_token](modbus::EntityType, uint16_t start_address,
                                      std::span<const uint8_t> data) mutable {
        if (!this->token_matches_(operation_token) ||
            start_address != oq_odu_runtime_frequency::EXTENDED_TABLE_START_ADDRESS) {
          return;
        }
        size_t loaded = 0U;
        if (!oq_odu_runtime_frequency::parse_extended_runtime_table(data.data(), data.size(), actual, loaded)) {
          this->fail_operation_("VERIFY_FAILED: incomplete extended readback", operation_token);
          return;
        }
        this->finish_apply_(actual, operation_token);
      });
  this->controller_->queue_command(std::move(command));
}

void OpenQuattOduRuntimeFrequency::finish_apply_(const oq_odu_runtime_frequency::RuntimeFrequencyTables& actual,
                                                 uint32_t operation_token) {
  portENTER_CRITICAL(&this->state_mux_);
  if (!this->busy_.load(std::memory_order_acquire) ||
      this->operation_token_.load(std::memory_order_acquire) != operation_token) {
    portEXIT_CRITICAL(&this->state_mux_);
    return;
  }
  if (!oq_odu_runtime_frequency::tables_match(actual, this->operation_tables_)) {
    portEXIT_CRITICAL(&this->state_mux_);
    this->fail_operation_("VERIFY_FAILED: readback mismatch", operation_token);
    return;
  }
  this->tables_ = actual;
  this->operation_ = Operation::NONE;
  this->write_started_ = false;
  this->busy_.store(false, std::memory_order_release);
  this->loaded_.store(true, std::memory_order_release);
  this->write_tainted_.store(false, std::memory_order_release);
  this->set_status_locked_("APPLIED: runtime table written and read back");
  portEXIT_CRITICAL(&this->state_mux_);
  ESP_LOGW(TAG, "HP%u APPLIED: runtime table written and read back", this->hp_index_);
  this->release_bus_(operation_token);
  this->write_applied_callbacks_.call();
}

void OpenQuattOduRuntimeFrequency::write_status(httpd_req_t* req) const {
  oq_odu_runtime_frequency::RuntimeFrequencyTables tables;
  char status[sizeof(this->status_)];
  bool busy = false;
  bool loaded = false;
  bool armed = false;
  bool write_tainted = false;
  bool extended_layout = false;
  portENTER_CRITICAL(&this->state_mux_);
  tables = this->tables_;
  std::memcpy(status, this->status_, sizeof(status));
  busy = this->busy_.load(std::memory_order_acquire);
  loaded = this->loaded_.load(std::memory_order_acquire);
  armed = this->armed_.load(std::memory_order_acquire);
  write_tainted = this->write_tainted_.load(std::memory_order_acquire);
  extended_layout = this->extended_layout_.load(std::memory_order_acquire);
  portEXIT_CRITICAL(&this->state_mux_);
  status[sizeof(status) - 1U] = '\0';

  const size_t level_count = oq_odu_runtime_frequency::normalized_level_count(extended_layout);
  ChunkedJsonWriter writer(req);
  writer.write_literal(R"({"ok":true,"available":)");
  writer.write_bool(this->available_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"hp":)");
  writer.write_uint(this->hp_index_);
  writer.write_literal(R"(,"busy":)");
  writer.write_bool(busy);
  writer.write_literal(R"(,"loaded":)");
  writer.write_bool(loaded);
  writer.write_literal(R"(,"armed":)");
  writer.write_bool(armed);
  writer.write_literal(R"(,"write_tainted":)");
  writer.write_bool(write_tainted);
  writer.write_literal(R"(,"extended_layout":)");
  writer.write_bool(level_count == oq_odu_runtime_frequency::EXTENDED_LEVEL_COUNT);
  writer.write_literal(R"(,"level_count":)");
  writer.write_uint(level_count);
  writer.write_literal(R"(,"status":)");
  writer.write_string(status);
  writer.write_literal(R"(,"csrf_token":)");
  writer.write_string(this->get_csrf_token().c_str());
  writer.write_literal(R"(,"cooling":)");
  write_frequency_values(writer, tables.cooling, loaded ? level_count : 0U);
  writer.write_literal(R"(,"heating":)");
  write_frequency_values(writer, tables.heating, loaded ? level_count : 0U);
  writer.write_literal("}");
  writer.finish();
}

}  // namespace openquatt_odu_runtime_frequency
}  // namespace esphome
