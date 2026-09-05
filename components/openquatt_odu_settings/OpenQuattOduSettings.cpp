#include "OpenQuattOduSettings.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_odu_settings {

static const char* const TAG = "openquatt.odu_settings";
static const uint32_t HP1_PROFILE_KEY = fnv1_hash("openquatt_odu_settings_hp1");
static const uint32_t HP2_PROFILE_KEY = fnv1_hash("openquatt_odu_settings_hp2");

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

template <typename T>
bool parse_integer(const std::string& raw, T& value) {
  if (raw.empty()) return false;
  int parsed_value = 0;
  const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), parsed_value);
  if (parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw.size()) return false;
  value = static_cast<T>(parsed_value);
  return static_cast<int>(value) == parsed_value;
}

class ChunkedJsonWriter {
 public:
  explicit ChunkedJsonWriter(httpd_req_t* req) : req_(req) {}

  bool write_char(char value) { return this->write_bytes_(&value, 1U); }
  bool write_literal(const char* value) { return value == nullptr || this->write_bytes_(value, std::strlen(value)); }
  bool write_int(int32_t value) {
    char buffer[16];
    const int written = std::snprintf(buffer, sizeof(buffer), "%ld", static_cast<long>(value));
    return written > 0 && this->write_bytes_(buffer, static_cast<size_t>(written));
  }
  bool write_bool(bool value) { return this->write_literal(value ? "true" : "false"); }
  bool write_string(const char* value) {
    if (!this->write_char('"')) return false;
    if (value != nullptr) {
      for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor != '\0'; ++cursor) {
        if (*cursor == '"' || *cursor == '\\') {
          if (!this->write_char('\\') || !this->write_char(static_cast<char>(*cursor))) return false;
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

const char* request_error(OpenQuattOduSettings::RequestResult result) {
  switch (result) {
    case OpenQuattOduSettings::RequestResult::BUSY:
      return "busy";
    case OpenQuattOduSettings::RequestResult::UNAVAILABLE:
      return "unavailable";
    case OpenQuattOduSettings::RequestResult::IDENTITY_REQUIRED:
      return "identity_required";
    case OpenQuattOduSettings::RequestResult::INVALID_SETTINGS:
      return "invalid_settings";
    default:
      return "request_rejected";
  }
}

class OduSettingsRequestHandler : public AsyncWebHandler {
 public:
  OduSettingsRequestHandler(OpenQuattOduSettings* parent, uint8_t hp_index) : parent_(parent) {
    std::snprintf(this->status_path_.data(), this->status_path_.size(), "/openquatt/odu-settings/hp%u/status",
                  static_cast<unsigned>(hp_index));
    std::snprintf(this->load_path_.data(), this->load_path_.size(), "/openquatt/odu-settings/hp%u/load",
                  static_cast<unsigned>(hp_index));
    std::snprintf(this->save_path_.data(), this->save_path_.size(), "/openquatt/odu-settings/hp%u/save",
                  static_cast<unsigned>(hp_index));
  }

  bool canHandle(AsyncWebServerRequest* request) const override {
    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url);
    if (url_path_matches(url, this->status_path_.data())) return request->method() == HTTP_GET;
    return request->method() == HTTP_POST &&
           (url_path_matches(url, this->load_path_.data()) || url_path_matches(url, this->save_path_.data()));
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

    OpenQuattOduSettings::RequestResult result;
    if (url_path_matches(url, this->load_path_.data())) {
      result = this->parent_->request_load();
    } else {
      uint8_t mode = 0U;
      int8_t start_temperature_c = 0;
      uint8_t stop_delta_c = 0U;
      const std::string auto_reapply = request->arg("auto_reapply");
      if (!parse_integer(request->arg("mode"), mode) ||
          !parse_integer(request->arg("start_temperature_c"), start_temperature_c) ||
          !parse_integer(request->arg("stop_delta_c"), stop_delta_c) ||
          (auto_reapply != "true" && auto_reapply != "false" && auto_reapply != "1" && auto_reapply != "0")) {
        request->send(409, "application/json", R"({"ok":false,"error":"invalid_settings"})");
        return;
      }
      result = this->parent_->request_save({mode, start_temperature_c, stop_delta_c},
                                           auto_reapply == "true" || auto_reapply == "1");
    }

    if (result != OpenQuattOduSettings::RequestResult::ACCEPTED) {
      char response[64];
      std::snprintf(response, sizeof(response), R"({"ok":false,"error":"%s"})", request_error(result));
      request->send(result == OpenQuattOduSettings::RequestResult::UNAVAILABLE ? 503 : 409, "application/json",
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
  OpenQuattOduSettings* parent_;
  std::array<char, PATH_SIZE> status_path_{};
  std::array<char, PATH_SIZE> load_path_{};
  std::array<char, PATH_SIZE> save_path_{};
};

}  // namespace

float OpenQuattOduSettings::get_setup_priority() const { return setup_priority::WIFI - 2.0f; }

void OpenQuattOduSettings::setup() {
  const bool available = this->controller_ != nullptr && this->eeprom_dump_ != nullptr && this->web_auth_ != nullptr &&
                         global_preferences != nullptr && web_server_base::global_web_server_base != nullptr;
  this->available_.store(available, std::memory_order_release);
  if (!available) {
    this->set_status_locked_("UNAVAILABLE");
    ESP_LOGE(TAG, "HP%u ODU settings service is unavailable", this->hp_index_);
    return;
  }

  const uint32_t storage_key = this->hp_index_ == 2U ? HP2_PROFILE_KEY : HP1_PROFILE_KEY;
  this->profile_pref_ = global_preferences->make_preference<oq_odu::BottomPlateProfileStorage>(storage_key, true);
  oq_odu::BottomPlateProfileStorage stored{};
  if (this->profile_pref_.load(&stored) && oq_odu::valid_bottom_plate_profile(stored)) {
    this->stored_profile_ = stored;
    this->desired_ = {stored.mode, stored.start_temperature_c, stored.stop_delta_c};
    this->profile_available_.store(true, std::memory_order_release);
    this->auto_reapply_.store((stored.flags & oq_odu::BOTTOM_PLATE_PROFILE_AUTO_REAPPLY) != 0U,
                              std::memory_order_release);
  }
  web_server_base::global_web_server_base->add_handler(new OduSettingsRequestHandler(this, this->hp_index_));
}

void OpenQuattOduSettings::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt ODU settings service");
  ESP_LOGCONFIG(TAG, "  HP: %u", this->hp_index_);
  ESP_LOGCONFIG(TAG, "  Available: %s", YESNO(this->available_.load(std::memory_order_acquire)));
  ESP_LOGCONFIG(TAG, "  Stored profile: %s", YESNO(this->profile_available_.load(std::memory_order_acquire)));
  ESP_LOGCONFIG(TAG, "  Auto reapply: %s", YESNO(this->auto_reapply_.load(std::memory_order_acquire)));
}

void OpenQuattOduSettings::set_odu_identity(uint16_t control_board_item, oq_odu::Variant variant) {
  if (control_board_item == 0U || variant == oq_odu::Variant::UNKNOWN) return;
  portENTER_CRITICAL(&this->state_mux_);
  this->control_board_item_ = control_board_item;
  this->variant_ = variant;
  this->online_.store(true, std::memory_order_release);
  this->identity_ready_.store(true, std::memory_order_release);
  if (this->profile_available_.load(std::memory_order_acquire) && !this->identity_matches_profile_()) {
    this->set_status_locked_("IDENTITY_MISMATCH");
  } else {
    this->set_status_locked_("READY");
    if (this->auto_reapply_.load(std::memory_order_acquire)) this->schedule_reconcile_(5000U);
  }
  portEXIT_CRITICAL(&this->state_mux_);
}

void OpenQuattOduSettings::notify_odu_offline() {
  uint32_t reserved_token = 0U;
  portENTER_CRITICAL(&this->state_mux_);
  const bool write_started = this->write_started_;
  this->operation_token_.fetch_add(1U, std::memory_order_acq_rel);
  this->pending_action_ = PendingAction::NONE;
  this->operation_ = Operation::NONE;
  this->write_started_ = false;
  this->busy_.store(false, std::memory_order_release);
  this->loaded_.store(false, std::memory_order_release);
  this->online_.store(false, std::memory_order_release);
  this->identity_ready_.store(false, std::memory_order_release);
  this->manual_apply_pending_.store(false, std::memory_order_release);
  this->control_board_item_ = 0U;
  this->variant_ = oq_odu::Variant::UNKNOWN;
  if (write_started) this->write_tainted_.store(true, std::memory_order_release);
  this->set_status_locked_(write_started ? "VERIFY_FAILED" : "OFFLINE");
  reserved_token = this->bus_reservation_token_.exchange(0U, std::memory_order_acq_rel);
  portEXIT_CRITICAL(&this->state_mux_);
  if (reserved_token != 0U) this->eeprom_dump_->end_external_operation();
}

bool OpenQuattOduSettings::begin_request_(uint32_t& request_token) {
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
  if (this->token_matches_(request_token)) return true;
  this->release_bus_(request_token);
  return false;
}

OpenQuattOduSettings::RequestResult OpenQuattOduSettings::request_load() {
  if (!this->available_.load(std::memory_order_acquire) || !this->online_.load(std::memory_order_acquire)) {
    return RequestResult::UNAVAILABLE;
  }
  uint32_t request_token = 0U;
  if (!this->begin_request_(request_token)) return RequestResult::BUSY;
  portENTER_CRITICAL(&this->state_mux_);
  this->pending_action_ = PendingAction::LOAD;
  this->pending_request_token_ = request_token;
  this->set_status_locked_("LOAD_REQUESTED");
  portEXIT_CRITICAL(&this->state_mux_);
  return RequestResult::ACCEPTED;
}

OpenQuattOduSettings::RequestResult OpenQuattOduSettings::request_save(const oq_odu::BottomPlateSettings& settings,
                                                                       bool auto_reapply) {
  if (!oq_odu::valid_bottom_plate_settings(settings)) return RequestResult::INVALID_SETTINGS;
  if (!this->available_.load(std::memory_order_acquire) || !this->online_.load(std::memory_order_acquire)) {
    return RequestResult::UNAVAILABLE;
  }
  if (!this->identity_ready_.load(std::memory_order_acquire)) return RequestResult::IDENTITY_REQUIRED;
  uint32_t request_token = 0U;
  if (!this->begin_request_(request_token)) return RequestResult::BUSY;

  bool accepted = false;
  portENTER_CRITICAL(&this->state_mux_);
  if (this->token_matches_(request_token) && this->identity_ready_.load(std::memory_order_acquire)) {
    this->desired_ = settings;
    this->pending_profile_ =
        oq_odu::make_bottom_plate_profile(settings, this->variant_, this->control_board_item_, auto_reapply);
    this->pending_action_ = PendingAction::SAVE;
    this->pending_request_token_ = request_token;
    this->set_status_locked_("SAVE_REQUESTED");
    accepted = true;
  }
  portEXIT_CRITICAL(&this->state_mux_);
  if (!accepted) {
    this->finish_operation_("IDENTITY_REQUIRED", request_token);
    return RequestResult::IDENTITY_REQUIRED;
  }
  return RequestResult::ACCEPTED;
}

bool OpenQuattOduSettings::identity_matches_profile_() const {
  return oq_odu::bottom_plate_profile_matches_identity(this->stored_profile_, this->control_board_item_,
                                                       this->variant_);
}

bool OpenQuattOduSettings::persist_profile_(const oq_odu::BottomPlateProfileStorage& profile) {
  const bool queued = this->profile_pref_.save(&profile);
  const bool sync_ok = global_preferences->sync();
  oq_odu::BottomPlateProfileStorage verify{};
  const bool verified = this->profile_pref_.load(&verify) && oq_odu::valid_bottom_plate_profile(verify) &&
                        std::memcmp(&verify, &profile, sizeof(profile)) == 0;
  if (verified && (!queued || !sync_ok)) {
    ESP_LOGW(TAG, "HP%u profile write reported failure but read-back verified it", this->hp_index_);
  }
  return verified;
}

bool OpenQuattOduSettings::begin_operation_(Operation operation, uint32_t operation_token) {
  if (!this->token_matches_(operation_token)) return false;
  this->operation_ = operation;
  this->write_started_ = false;
  this->write_index_ = 0U;
  this->operation_started_ms_ = millis();
  return true;
}

bool OpenQuattOduSettings::token_matches_(uint32_t operation_token) const {
  return this->busy_.load(std::memory_order_acquire) &&
         this->operation_token_.load(std::memory_order_acquire) == operation_token;
}

void OpenQuattOduSettings::loop() {
  if (!this->busy_.load(std::memory_order_acquire)) {
    if ((this->auto_reapply_.load(std::memory_order_acquire) ||
         this->manual_apply_pending_.load(std::memory_order_acquire)) &&
        this->online_.load(std::memory_order_acquire) && this->identity_ready_.load(std::memory_order_acquire) &&
        this->profile_available_.load(std::memory_order_acquire) && this->reconcile_due_ms_ != 0U &&
        static_cast<int32_t>(millis() - this->reconcile_due_ms_) >= 0) {
      this->begin_reconcile_();
    }
    return;
  }

  PendingAction pending = PendingAction::NONE;
  uint32_t request_token = 0U;
  portENTER_CRITICAL(&this->state_mux_);
  pending = this->pending_action_;
  request_token = this->pending_request_token_;
  this->pending_action_ = PendingAction::NONE;
  this->pending_request_token_ = 0U;
  portEXIT_CRITICAL(&this->state_mux_);

  if (pending != PendingAction::NONE) {
    const Operation operation = pending == PendingAction::LOAD
                                    ? Operation::LOAD
                                    : (pending == PendingAction::RECONCILE ? Operation::RECONCILE : Operation::APPLY);
    if (!this->begin_operation_(operation, request_token)) return;
    if (pending == PendingAction::SAVE) {
      oq_odu::BottomPlateProfileStorage profile;
      portENTER_CRITICAL(&this->state_mux_);
      profile = this->pending_profile_;
      portEXIT_CRITICAL(&this->state_mux_);
      if (!this->persist_profile_(profile)) {
        portENTER_CRITICAL(&this->state_mux_);
        if (this->profile_available_.load(std::memory_order_acquire)) {
          this->desired_ = {this->stored_profile_.mode, this->stored_profile_.start_temperature_c,
                            this->stored_profile_.stop_delta_c};
        }
        portEXIT_CRITICAL(&this->state_mux_);
        this->finish_operation_("PERSIST_FAILED", request_token);
        return;
      }
      portENTER_CRITICAL(&this->state_mux_);
      this->stored_profile_ = profile;
      portEXIT_CRITICAL(&this->state_mux_);
      this->profile_available_.store(true, std::memory_order_release);
      this->auto_reapply_.store((profile.flags & oq_odu::BOTTOM_PLATE_PROFILE_AUTO_REAPPLY) != 0U,
                                std::memory_order_release);
      this->manual_apply_pending_.store(true, std::memory_order_release);
    }
    this->queue_settings_read_(request_token);
    return;
  }

  if (this->operation_ != Operation::NONE && millis() - this->operation_started_ms_ >= OPERATION_TIMEOUT_MS) {
    const uint32_t operation_token = this->operation_token_.load(std::memory_order_acquire);
    this->fail_operation_(this->write_started_ ? "VERIFY_FAILED" : "LOAD_FAILED", operation_token);
  }
}

bool OpenQuattOduSettings::begin_reconcile_() {
  uint32_t request_token = 0U;
  this->reconcile_due_ms_ = 0U;
  if (!this->begin_request_(request_token)) {
    this->schedule_reconcile_(BUS_RETRY_MS);
    return false;
  }
  portENTER_CRITICAL(&this->state_mux_);
  if (!this->identity_matches_profile_()) {
    portEXIT_CRITICAL(&this->state_mux_);
    this->finish_operation_("IDENTITY_MISMATCH", request_token);
    return false;
  }
  this->desired_ = {this->stored_profile_.mode, this->stored_profile_.start_temperature_c,
                    this->stored_profile_.stop_delta_c};
  this->pending_action_ = PendingAction::RECONCILE;
  this->pending_request_token_ = request_token;
  this->set_status_locked_("CHECKING");
  portEXIT_CRITICAL(&this->state_mux_);
  return true;
}

void OpenQuattOduSettings::queue_settings_read_(uint32_t operation_token) {
  auto command = modbus_controller::ModbusCommandItem::create_read_command(
      this->controller_, modbus::EntityType::HOLDING, oq_odu::BOTTOM_PLATE_START_ADDRESS,
      oq_odu::BOTTOM_PLATE_REGISTER_COUNT,
      [this, operation_token](modbus::EntityType, uint16_t start_address, std::span<const uint8_t> data) {
        if (!this->token_matches_(operation_token) || start_address != oq_odu::BOTTOM_PLATE_START_ADDRESS) return;
        oq_odu::BottomPlateSettings settings;
        if (!oq_odu::decode_bottom_plate_settings(data.data(), data.size(), settings)) {
          this->fail_operation_(this->operation_ == Operation::LOAD ? "LOAD_FAILED" : "VERIFY_FAILED", operation_token);
          return;
        }
        this->handle_settings_read_(settings, operation_token);
      });
  this->controller_->queue_command(std::move(command));
}

void OpenQuattOduSettings::handle_settings_read_(const oq_odu::BottomPlateSettings& settings,
                                                 uint32_t operation_token) {
  portENTER_CRITICAL(&this->state_mux_);
  if (!this->token_matches_(operation_token)) {
    portEXIT_CRITICAL(&this->state_mux_);
    return;
  }
  this->actual_ = settings;
  this->loaded_.store(true, std::memory_order_release);
  const Operation operation = this->operation_;
  const bool matches = oq_odu::bottom_plate_settings_match(settings, this->desired_);
  if (operation != Operation::LOAD && !matches) this->set_status_locked_("APPLYING");
  portEXIT_CRITICAL(&this->state_mux_);
  if (operation == Operation::LOAD) {
    this->finish_operation_(
        this->profile_available_.load(std::memory_order_acquire) && !this->identity_matches_profile_()
            ? "IDENTITY_MISMATCH"
            : "LOADED",
        operation_token);
  } else if (matches) {
    this->manual_apply_pending_.store(false, std::memory_order_release);
    this->write_tainted_.store(false, std::memory_order_release);
    this->finish_operation_("IN_SYNC", operation_token, PERIODIC_RECONCILE_MS);
  } else {
    // Bottom-plate settings may be applied while the compressor runs.
    // The bus reservation, write ordering and full readback still apply.
    this->queue_next_write_(operation_token);
  }
}

void OpenQuattOduSettings::queue_next_write_(uint32_t operation_token) {
  if (!this->token_matches_(operation_token)) return;
  const auto targets = oq_odu::bottom_plate_write_targets(this->desired_);
  while (this->write_index_ < targets.size()) {
    const auto target = targets[this->write_index_];
    const auto actual_targets = oq_odu::bottom_plate_write_targets(this->actual_);
    const auto actual = std::find_if(actual_targets.begin(), actual_targets.end(),
                                     [target](const auto& candidate) { return candidate.address == target.address; });
    if (actual == actual_targets.end() || target.value != actual->value) break;
    ++this->write_index_;
  }
  if (this->write_index_ >= targets.size()) {
    this->queue_readback_(operation_token);
    return;
  }

  const size_t current_index = this->write_index_;
  const auto target = targets[current_index];
  this->write_started_ = true;
  this->write_tainted_.store(true, std::memory_order_release);
  auto command = modbus_controller::ModbusCommandItem::create_write_single_command(this->controller_, target.address,
                                                                                   target.value);
  command.on_data_func = [this, current_index, operation_token](modbus::EntityType, uint16_t,
                                                                std::span<const uint8_t>) {
    if (!this->token_matches_(operation_token) || this->write_index_ != current_index) return;
    ++this->write_index_;
    this->queue_next_write_(operation_token);
  };
  this->controller_->queue_command(std::move(command));
}

void OpenQuattOduSettings::queue_readback_(uint32_t operation_token) {
  auto command = modbus_controller::ModbusCommandItem::create_read_command(
      this->controller_, modbus::EntityType::HOLDING, oq_odu::BOTTOM_PLATE_START_ADDRESS,
      oq_odu::BOTTOM_PLATE_REGISTER_COUNT,
      [this, operation_token](modbus::EntityType, uint16_t start_address, std::span<const uint8_t> data) {
        if (!this->token_matches_(operation_token) || start_address != oq_odu::BOTTOM_PLATE_START_ADDRESS) return;
        oq_odu::BottomPlateSettings actual;
        if (!oq_odu::decode_bottom_plate_settings(data.data(), data.size(), actual) ||
            !oq_odu::bottom_plate_settings_match(actual, this->desired_)) {
          this->fail_operation_("VERIFY_FAILED", operation_token);
          return;
        }
        portENTER_CRITICAL(&this->state_mux_);
        this->actual_ = actual;
        this->loaded_.store(true, std::memory_order_release);
        this->manual_apply_pending_.store(false, std::memory_order_release);
        this->write_tainted_.store(false, std::memory_order_release);
        portEXIT_CRITICAL(&this->state_mux_);
        this->finish_operation_("IN_SYNC", operation_token, PERIODIC_RECONCILE_MS);
      });
  this->controller_->queue_command(std::move(command));
}

void OpenQuattOduSettings::release_bus_(uint32_t request_token) {
  uint32_t reserved_token = request_token;
  if (request_token == 0U) {
    reserved_token = this->bus_reservation_token_.exchange(0U, std::memory_order_acq_rel);
  } else if (!this->bus_reservation_token_.compare_exchange_strong(reserved_token, 0U, std::memory_order_acq_rel)) {
    return;
  }
  if (reserved_token != 0U) this->eeprom_dump_->end_external_operation();
}

void OpenQuattOduSettings::set_status_locked_(const char* status) {
  std::snprintf(this->status_, sizeof(this->status_), "%s", status != nullptr ? status : "");
}

void OpenQuattOduSettings::schedule_reconcile_(uint32_t delay_ms) {
  this->reconcile_due_ms_ = millis() + delay_ms;
  if (this->reconcile_due_ms_ == 0U) this->reconcile_due_ms_ = 1U;
}

void OpenQuattOduSettings::finish_operation_(const char* status, uint32_t operation_token,
                                             uint32_t next_reconcile_delay_ms) {
  portENTER_CRITICAL(&this->state_mux_);
  if (!this->token_matches_(operation_token)) {
    portEXIT_CRITICAL(&this->state_mux_);
    return;
  }
  this->operation_token_.fetch_add(1U, std::memory_order_acq_rel);
  this->operation_ = Operation::NONE;
  this->write_started_ = false;
  this->busy_.store(false, std::memory_order_release);
  this->set_status_locked_(status);
  if (next_reconcile_delay_ms != 0U &&
      (this->auto_reapply_.load(std::memory_order_acquire) ||
       this->manual_apply_pending_.load(std::memory_order_acquire)) &&
      this->identity_matches_profile_()) {
    this->schedule_reconcile_(next_reconcile_delay_ms);
  }
  portEXIT_CRITICAL(&this->state_mux_);
  ESP_LOGI(TAG, "HP%u %s", this->hp_index_, status);
  this->release_bus_(operation_token);
}

void OpenQuattOduSettings::fail_operation_(const char* status, uint32_t operation_token) {
  portENTER_CRITICAL(&this->state_mux_);
  if (!this->token_matches_(operation_token)) {
    portEXIT_CRITICAL(&this->state_mux_);
    return;
  }
  if (this->write_started_) this->write_tainted_.store(true, std::memory_order_release);
  this->operation_token_.fetch_add(1U, std::memory_order_acq_rel);
  this->operation_ = Operation::NONE;
  this->write_started_ = false;
  this->busy_.store(false, std::memory_order_release);
  this->set_status_locked_(status);
  if ((this->auto_reapply_.load(std::memory_order_acquire) ||
       this->manual_apply_pending_.load(std::memory_order_acquire)) &&
      this->identity_matches_profile_()) {
    this->schedule_reconcile_(FAILURE_RETRY_MS);
  }
  portEXIT_CRITICAL(&this->state_mux_);
  ESP_LOGW(TAG, "HP%u %s", this->hp_index_, status);
  this->release_bus_(operation_token);
}

void OpenQuattOduSettings::write_status(httpd_req_t* req) const {
  oq_odu::BottomPlateSettings actual;
  oq_odu::BottomPlateSettings desired;
  oq_odu::BottomPlateSettings defaults;
  oq_odu::Variant variant;
  uint16_t control_board_item = 0U;
  char status[sizeof(this->status_)];
  bool identity_matches = false;
  portENTER_CRITICAL(&this->state_mux_);
  actual = this->actual_;
  desired = this->desired_;
  variant = this->variant_;
  control_board_item = this->control_board_item_;
  defaults = oq_odu::default_bottom_plate_settings(variant);
  identity_matches = this->identity_matches_profile_();
  std::memcpy(status, this->status_, sizeof(status));
  portEXIT_CRITICAL(&this->state_mux_);

  ChunkedJsonWriter writer(req);
  writer.write_literal("{\"available\":");
  writer.write_bool(this->available_.load(std::memory_order_acquire) && this->online_.load(std::memory_order_acquire));
  writer.write_literal(",\"hp\":");
  writer.write_int(this->hp_index_);
  writer.write_literal(",\"busy\":");
  writer.write_bool(this->busy_.load(std::memory_order_acquire));
  writer.write_literal(",\"loaded\":");
  writer.write_bool(this->loaded_.load(std::memory_order_acquire));
  writer.write_literal(",\"profile_available\":");
  writer.write_bool(this->profile_available_.load(std::memory_order_acquire));
  writer.write_literal(",\"auto_reapply\":");
  writer.write_bool(this->auto_reapply_.load(std::memory_order_acquire));
  writer.write_literal(",\"identity_ready\":");
  writer.write_bool(this->identity_ready_.load(std::memory_order_acquire));
  writer.write_literal(",\"identity_matches\":");
  writer.write_bool(identity_matches);
  writer.write_literal(",\"write_uncertain\":");
  writer.write_bool(this->write_tainted_.load(std::memory_order_acquire));
  writer.write_literal(",\"variant\":");
  writer.write_int(static_cast<uint8_t>(variant));
  writer.write_literal(",\"control_board_item\":");
  writer.write_int(control_board_item);
  writer.write_literal(",\"status\":");
  writer.write_string(status);
  writer.write_literal(",\"csrf_token\":");
  writer.write_string(this->get_csrf_token().c_str());
  const auto write_settings = [&writer](const oq_odu::BottomPlateSettings& settings) {
    writer.write_literal("{\"mode\":");
    writer.write_int(settings.mode);
    writer.write_literal(",\"start_temperature_c\":");
    writer.write_int(settings.start_temperature_c);
    writer.write_literal(",\"stop_delta_c\":");
    writer.write_int(settings.stop_delta_c);
    writer.write_char('}');
  };
  writer.write_literal(",\"actual\":");
  write_settings(actual);
  writer.write_literal(",\"desired\":");
  write_settings(desired);
  writer.write_literal(",\"defaults\":");
  write_settings(defaults);
  writer.write_char('}');
  writer.finish();
}

}  // namespace openquatt_odu_settings
}  // namespace esphome
