#include "OpenQuattOduEepromDump.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_odu_eeprom_dump {

static const char* const TAG = "openquatt.odu_eeprom";

namespace {

bool url_path_matches(const char* url, const char* path) {
  if (url == nullptr || path == nullptr) {
    return false;
  }
  const size_t path_length = std::strlen(path);
  return std::strncmp(url, path, path_length) == 0 && (url[path_length] == '\0' || url[path_length] == '?');
}

class ChunkedJsonWriter {
 public:
  explicit ChunkedJsonWriter(httpd_req_t* req) : req_(req) {}

  bool write_char(char value) { return this->write_bytes_(&value, 1U); }

  bool write_literal(const char* value) { return value == nullptr || this->write_bytes_(value, std::strlen(value)); }

  bool write_string(const char* value) {
    if (!this->write_char('"')) {
      return false;
    }
    if (value != nullptr) {
      for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value); *cursor != '\0'; ++cursor) {
        switch (*cursor) {
          case '\\':
            if (!this->write_literal("\\\\")) return false;
            break;
          case '"':
            if (!this->write_literal("\\\"")) return false;
            break;
          case '\n':
            if (!this->write_literal("\\n")) return false;
            break;
          case '\r':
            if (!this->write_literal("\\r")) return false;
            break;
          case '\t':
            if (!this->write_literal("\\t")) return false;
            break;
          default:
            if (*cursor < 0x20U) {
              char escaped[7];
              const int written = std::snprintf(escaped, sizeof(escaped), "\\u%04X", *cursor);
              if (written <= 0 || !this->write_bytes_(escaped, static_cast<size_t>(written))) return false;
            } else if (!this->write_char(static_cast<char>(*cursor))) {
              return false;
            }
            break;
        }
      }
    }
    return this->write_char('"');
  }

  bool write_bool(bool value) { return this->write_literal(value ? "true" : "false"); }

  bool write_uint(uint32_t value) {
    char buffer[16];
    const int written = std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned>(value));
    return written > 0 && this->write_bytes_(buffer, static_cast<size_t>(written));
  }

  bool write_uint64(uint64_t value) {
    char buffer[24];
    const int written = std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    return written > 0 && this->write_bytes_(buffer, static_cast<size_t>(written));
  }

  bool write_hex16(uint16_t value) {
    char buffer[7];
    const int written = std::snprintf(buffer, sizeof(buffer), "0x%04X", static_cast<unsigned>(value));
    return written > 0 && this->write_string(buffer);
  }

  bool finish() {
    const bool flushed = this->flush_();
    if (!this->failed_) httpd_resp_send_chunk(this->req_, nullptr, 0);
    return flushed;
  }

 private:
  static constexpr size_t BUFFER_SIZE = 512;

  bool write_bytes_(const char* data, size_t length) {
    if (this->failed_) return false;

    size_t remaining = length;
    const char* cursor = data;
    while (remaining > 0U) {
      if (this->used_ == BUFFER_SIZE && !this->flush_()) {
        return false;
      }
      const size_t count = std::min(BUFFER_SIZE - this->used_, remaining);
      std::memcpy(this->buffer_.data() + this->used_, cursor, count);
      this->used_ += count;
      cursor += count;
      remaining -= count;
    }
    return true;
  }

  bool flush_() {
    if (this->failed_) return false;
    if (this->used_ == 0U) {
      return true;
    }
    if (httpd_resp_send_chunk(this->req_, this->buffer_.data(), static_cast<ssize_t>(this->used_)) != ESP_OK) {
      this->used_ = 0U;
      this->failed_ = true;
      return false;
    }
    this->used_ = 0U;
    return true;
  }

  httpd_req_t* req_;
  std::array<char, BUFFER_SIZE> buffer_{};
  size_t used_{0};
  bool failed_{false};
};

class OpenQuattOduEepromDumpRequestHandler : public AsyncWebHandler {
 public:
  explicit OpenQuattOduEepromDumpRequestHandler(OpenQuattOduEepromDump* parent, uint8_t hp_index)
      : parent_(parent), hp_index_(hp_index) {
    std::snprintf(this->status_path_.data(), this->status_path_.size(), "/openquatt/odu-eeprom/hp%u/status",
                  static_cast<unsigned>(hp_index));
    std::snprintf(this->start_path_.data(), this->start_path_.size(), "/openquatt/odu-eeprom/hp%u/start",
                  static_cast<unsigned>(hp_index));
    std::snprintf(this->download_path_.data(), this->download_path_.size(), "/openquatt/odu-eeprom/hp%u/download",
                  static_cast<unsigned>(hp_index));
  }

  bool canHandle(AsyncWebServerRequest* request) const override {
    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url);
    if (url_path_matches(url, this->status_path_.data()) || url_path_matches(url, this->download_path_.data())) {
      return request->method() == HTTP_GET;
    }
    return url_path_matches(url, this->start_path_.data()) && request->method() == HTTP_POST;
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    if (!this->parent_->request_is_authenticated(request)) {
      request->requestAuthentication();
      return;
    }

    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url);

    if (url_path_matches(url, this->start_path_.data())) {
      const bool include_extended = request->arg("extended") != "0";
      const OpenQuattOduEepromDump::StartResult result = this->parent_->start(include_extended);
      if (result == OpenQuattOduEepromDump::StartResult::UNAVAILABLE) {
        request->send(503, "application/json", R"({"ok":false,"error":"psram_unavailable"})");
        return;
      }
      if (result == OpenQuattOduEepromDump::StartResult::BUSY) {
        request->send(409, "application/json", R"({"ok":false,"error":"dump_busy"})");
        return;
      }
      this->send_status_(request);
      return;
    }

    if (url_path_matches(url, this->download_path_.data())) {
      if (!this->parent_->begin_download()) {
        request->send(409, "application/json", R"({"ok":false,"error":"dump_not_ready"})");
        return;
      }
      httpd_req_t* req = *request;
      char disposition[96];
      std::snprintf(disposition, sizeof(disposition), "attachment; filename=\"openquatt-odu-eeprom-hp%u.json\"",
                    static_cast<unsigned>(this->hp_index_));
      httpd_resp_set_status(req, HTTPD_200);
      httpd_resp_set_type(req, "application/json; charset=utf-8");
      httpd_resp_set_hdr(req, "Cache-Control", "no-store");
      httpd_resp_set_hdr(req, "Content-Disposition", disposition);
      this->parent_->write_download(req);
      this->parent_->end_download();
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

  static constexpr size_t PATH_SIZE = 48;
  OpenQuattOduEepromDump* parent_;
  uint8_t hp_index_;
  std::array<char, PATH_SIZE> status_path_{};
  std::array<char, PATH_SIZE> start_path_{};
  std::array<char, PATH_SIZE> download_path_{};
};

template <size_t Count>
bool write_word_array(ChunkedJsonWriter& writer, const std::array<uint16_t, Count>& values) {
  if (!writer.write_char('[')) return false;
  for (size_t index = 0; index < Count; ++index) {
    if ((index > 0U && !writer.write_char(',')) || !writer.write_uint(values[index])) return false;
  }
  return writer.write_char(']');
}

}  // namespace

float OpenQuattOduEepromDump::get_setup_priority() const { return setup_priority::WIFI - 1.0f; }

bool OpenQuattOduEepromDump::available_storage_() const {
  return static_cast<bool>(this->eeprom_) && this->eeprom_.is_external() &&
         this->eeprom_.size() == EEPROM_REGISTER_COUNT;
}

void OpenQuattOduEepromDump::setup() {
  const bool allocated = this->eeprom_.allocate_external(EEPROM_REGISTER_COUNT);
  this->available_.store(allocated && this->available_storage_(), std::memory_order_release);
  if (!this->available_.load(std::memory_order_acquire)) {
    this->eeprom_.release();
    ESP_LOGE(TAG, "HP%u EEPROM snapshot storage could not be allocated in PSRAM", this->hp_index_);
  }

  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGE(TAG, "global_web_server_base is unavailable");
    return;
  }
  web_server_base::global_web_server_base->add_handler(new OpenQuattOduEepromDumpRequestHandler(this, this->hp_index_));
}

void OpenQuattOduEepromDump::dump_config() {
  ESP_LOGCONFIG(TAG, "OpenQuatt ODU EEPROM dump");
  ESP_LOGCONFIG(TAG, "  HP: %u", this->hp_index_);
  ESP_LOGCONFIG(TAG, "  Modbus device address: %u", this->device_address_);
  ESP_LOGCONFIG(TAG, "  Controller: %s", this->controller_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Web authentication: %s", this->web_auth_ == nullptr ? "<missing>" : "configured");
  ESP_LOGCONFIG(TAG, "  Snapshot buffer: %s", this->available_storage_() ? "PSRAM" : "unavailable");
}

OpenQuattOduEepromDump::StartResult OpenQuattOduEepromDump::start(bool include_extended_metadata) {
  portENTER_CRITICAL(&this->state_mux_);
  const bool busy = this->active_.load(std::memory_order_acquire) || this->starting_.load(std::memory_order_acquire) ||
                    this->download_in_progress_.load(std::memory_order_acquire) ||
                    this->external_operation_active_.load(std::memory_order_acquire);
  const bool available =
      this->available_.load(std::memory_order_acquire) && this->controller_ != nullptr && this->web_auth_ != nullptr;
  if (!busy && available) {
    this->starting_.store(true, std::memory_order_release);
  }
  portEXIT_CRITICAL(&this->state_mux_);

  if (busy) return StartResult::BUSY;
  if (!available) return StartResult::UNAVAILABLE;

  this->include_extended_metadata_ = include_extended_metadata;
  this->reset_job_();
  this->active_.store(true, std::memory_order_release);
  this->starting_.store(false, std::memory_order_release);
  ESP_LOGI(TAG, "HP%u ODU EEPROM snapshot job %u started", this->hp_index_,
           static_cast<unsigned>(this->job_id_.load()));
  return StartResult::STARTED;
}

bool OpenQuattOduEepromDump::try_begin_external_operation() {
  portENTER_CRITICAL(&this->state_mux_);
  const bool busy = this->active_.load(std::memory_order_acquire) || this->starting_.load(std::memory_order_acquire) ||
                    this->download_in_progress_.load(std::memory_order_acquire) ||
                    this->external_operation_active_.load(std::memory_order_acquire);
  if (!busy) this->external_operation_active_.store(true, std::memory_order_release);
  portEXIT_CRITICAL(&this->state_mux_);
  return !busy;
}

void OpenQuattOduEepromDump::end_external_operation() {
  this->external_operation_active_.store(false, std::memory_order_release);
}

void OpenQuattOduEepromDump::reset_job_() {
  this->dump_ready_.store(false, std::memory_order_release);
  this->extended_supported_.store(false, std::memory_order_release);
  this->core_available_.store(false, std::memory_order_release);

  std::fill(this->eeprom_.data(), this->eeprom_.data() + this->eeprom_.size(), 0U);
  portENTER_CRITICAL(&this->state_mux_);
  this->extended_.fill(0U);
  this->model_.fill(0U);
  this->customer_model_.fill(0U);
  this->serial_.fill(0U);
  this->core_.fill(0U);
  portEXIT_CRITICAL(&this->state_mux_);

  uint32_t next_job_id = this->job_id_.load(std::memory_order_relaxed) + 1U;
  if (next_job_id == 0U) next_job_id = 1U;
  this->job_id_.store(next_job_id, std::memory_order_relaxed);
  this->progress_.store(0U, std::memory_order_relaxed);
  this->registers_read_.store(0U, std::memory_order_relaxed);
  this->warning_flags_.store(WARNING_NONE, std::memory_order_relaxed);
  this->crc_matches_stored_eeprom_.store(false, std::memory_order_relaxed);
  this->calculated_crc_.store(0U, std::memory_order_relaxed);
  this->stored_crc_.store(0U, std::memory_order_relaxed);
  this->crc_retry_count_.store(0U, std::memory_order_relaxed);
  this->step_ = Step::WAITING_BUS;
  this->waiting_for_response_.store(false, std::memory_order_relaxed);
  this->response_received_.store(false, std::memory_order_relaxed);
  this->response_valid_.store(false, std::memory_order_relaxed);
  this->eeprom_offset_ = 0U;
  this->block_retry_count_ = 0U;
  this->started_ms_ = millis();
  this->completed_ms_ = 0U;
  this->captured_at_epoch_ = 0U;
  this->queued_ms_ = 0U;
  this->queue_empty_since_ms_ = 0U;
  this->bus_wait_started_ms_ = this->started_ms_;
  this->next_request_ms_ = 0U;
  this->set_error_("");
  this->set_phase_("waiting for Modbus bus");
}

bool OpenQuattOduEepromDump::modbus_bus_idle_() const {
  if (this->controller_ == nullptr) {
    return false;
  }

  auto* hub = this->controller_->hub();
  return hub != nullptr && hub->tx_buffer_empty() && !hub->tx_blocked();
}

void OpenQuattOduEepromDump::loop() {
  if (!this->active_.load(std::memory_order_acquire) || this->controller_ == nullptr) {
    return;
  }

  const uint32_t now = millis();
  if (this->waiting_for_response_.load(std::memory_order_acquire)) {
    if (this->response_received_.load(std::memory_order_acquire)) {
      this->handle_request_result_();
      return;
    }
    if (now - this->queued_ms_ >= REQUEST_TIMEOUT_MS) {
      this->fail_job_("Modbus request timed out");
      return;
    }
    if (this->modbus_bus_idle_()) {
      if (this->queue_empty_since_ms_ == 0U) {
        this->queue_empty_since_ms_ = now == 0U ? 1U : now;
      } else if (now - this->queue_empty_since_ms_ >= EMPTY_QUEUE_GRACE_MS) {
        this->response_valid_.store(false, std::memory_order_relaxed);
        this->response_received_.store(true, std::memory_order_release);
      }
    } else {
      this->queue_empty_since_ms_ = 0U;
    }
    return;
  }

  if (static_cast<int32_t>(now - this->next_request_ms_) < 0) {
    return;
  }
  if (!this->modbus_bus_idle_()) {
    if (this->bus_wait_started_ms_ == 0U) this->bus_wait_started_ms_ = now == 0U ? 1U : now;
    if (now - this->bus_wait_started_ms_ >= BUS_ACQUIRE_TIMEOUT_MS) {
      this->fail_job_("Modbus bus remained busy");
    }
    return;
  }
  this->bus_wait_started_ms_ = 0U;

  if (this->step_ == Step::WAITING_BUS) {
    this->step_ = this->include_extended_metadata_ ? Step::EXTENDED : Step::CORE;
  }
  this->queue_current_request_();
}

uint16_t OpenQuattOduEepromDump::current_start_address_() const {
  switch (this->step_) {
    case Step::EXTENDED:
      return EXTENDED_START_ADDRESS;
    case Step::MODEL:
      return MODEL_START_ADDRESS;
    case Step::CUSTOMER_MODEL:
      return CUSTOMER_MODEL_START_ADDRESS;
    case Step::SERIAL:
      return SERIAL_START_ADDRESS;
    case Step::CORE:
      return CORE_START_ADDRESS;
    case Step::EEPROM:
      return static_cast<uint16_t>(EEPROM_START_ADDRESS + this->eeprom_offset_);
    default:
      return 0U;
  }
}

uint16_t OpenQuattOduEepromDump::current_register_count_() const {
  switch (this->step_) {
    case Step::EXTENDED:
      return EXTENDED_REGISTER_COUNT;
    case Step::MODEL:
    case Step::CUSTOMER_MODEL:
    case Step::SERIAL:
      return TEXT_REGISTER_COUNT;
    case Step::CORE:
      return CORE_REGISTER_COUNT;
    case Step::EEPROM:
      return std::min<uint16_t>(EEPROM_BLOCK_SIZE, EEPROM_REGISTER_COUNT - this->eeprom_offset_);
    default:
      return 0U;
  }
}

void OpenQuattOduEepromDump::queue_current_request_() {
  this->request_start_address_ = this->current_start_address_();
  this->request_register_count_ = this->current_register_count_();
  if (this->request_register_count_ == 0U) {
    this->fail_job_("Invalid EEPROM dump state");
    return;
  }

  switch (this->step_) {
    case Step::EXTENDED:
    case Step::MODEL:
    case Step::CUSTOMER_MODEL:
    case Step::SERIAL:
      this->set_phase_("reading extended ODU identity");
      break;
    case Step::CORE:
      this->set_phase_("reading core ODU identity");
      this->progress_.store(5U, std::memory_order_release);
      break;
    case Step::EEPROM:
      this->set_phase_(this->crc_retry_count_.load() == 0U ? "reading EEPROM shadow" : "rereading EEPROM shadow");
      break;
    default:
      break;
  }

  this->response_received_.store(false, std::memory_order_relaxed);
  this->response_valid_.store(false, std::memory_order_relaxed);
  this->waiting_for_response_.store(true, std::memory_order_release);
  this->queue_empty_since_ms_ = 0U;
  this->queued_ms_ = millis();
  const uint16_t expected_start = this->request_start_address_;
  const uint32_t request_token = this->request_token_.fetch_add(1U, std::memory_order_acq_rel) + 1U;
  auto command = modbus_controller::ModbusCommandItem::create_read_command(
      this->controller_, modbus::EntityType::HOLDING, expected_start, this->request_register_count_,
      [this, expected_start, request_token](modbus::EntityType, uint16_t start_address, std::span<const uint8_t> data) {
        if (start_address == expected_start) this->on_response_(request_token, start_address, data);
      });
  this->controller_->queue_command(std::move(command));
}

uint16_t OpenQuattOduEepromDump::read_word_(std::span<const uint8_t> data, size_t index) {
  const size_t offset = index * 2U;
  return static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8U) | data[offset + 1U]);
}

void OpenQuattOduEepromDump::on_response_(uint32_t request_token, uint16_t start_address,
                                          std::span<const uint8_t> data) {
  if (!this->active_.load(std::memory_order_acquire) ||
      request_token != this->request_token_.load(std::memory_order_acquire) ||
      !this->waiting_for_response_.load(std::memory_order_acquire) || start_address != this->request_start_address_) {
    return;
  }

  const size_t expected_bytes = static_cast<size_t>(this->request_register_count_) * 2U;
  if (data.size() < expected_bytes) {
    this->response_valid_.store(false, std::memory_order_relaxed);
    this->response_received_.store(true, std::memory_order_release);
    return;
  }

  switch (this->step_) {
    case Step::EXTENDED:
      for (size_t index = 0; index < this->extended_.size(); ++index) this->extended_[index] = read_word_(data, index);
      break;
    case Step::MODEL:
      for (size_t index = 0; index < this->model_.size(); ++index) this->model_[index] = read_word_(data, index);
      break;
    case Step::CUSTOMER_MODEL:
      for (size_t index = 0; index < this->customer_model_.size(); ++index)
        this->customer_model_[index] = read_word_(data, index);
      break;
    case Step::SERIAL:
      for (size_t index = 0; index < this->serial_.size(); ++index) this->serial_[index] = read_word_(data, index);
      break;
    case Step::CORE:
      for (size_t index = 0; index < this->core_.size(); ++index) this->core_[index] = read_word_(data, index);
      break;
    case Step::EEPROM:
      for (size_t index = 0; index < this->request_register_count_; ++index)
        this->eeprom_[this->eeprom_offset_ + index] = read_word_(data, index);
      break;
    default:
      this->response_valid_.store(false, std::memory_order_relaxed);
      this->response_received_.store(true, std::memory_order_release);
      return;
  }

  this->response_valid_.store(true, std::memory_order_relaxed);
  this->response_received_.store(true, std::memory_order_release);
}

void OpenQuattOduEepromDump::handle_request_result_() {
  this->waiting_for_response_.store(false, std::memory_order_relaxed);
  this->response_received_.store(false, std::memory_order_relaxed);
  this->queue_empty_since_ms_ = 0U;
  if (this->response_valid_.load(std::memory_order_acquire)) {
    this->advance_after_success_();
  } else {
    this->handle_request_failure_();
  }
}

void OpenQuattOduEepromDump::handle_request_failure_() {
  this->next_request_ms_ = millis() + FAILURE_COOLDOWN_MS;
  if (this->step_ == Step::EEPROM) {
    if (this->block_retry_count_ == 0U) {
      this->block_retry_count_ = 1U;
      this->set_phase_("retrying EEPROM block");
      return;
    }
    this->fail_job_("EEPROM block could not be read");
    return;
  }

  if (this->step_ == Step::EXTENDED || this->step_ == Step::MODEL || this->step_ == Step::CUSTOMER_MODEL ||
      this->step_ == Step::SERIAL) {
    this->extended_supported_.store(false, std::memory_order_release);
    this->add_warning_(WARNING_EXTENDED_UNAVAILABLE);
    this->step_ = Step::CORE;
    this->set_phase_("extended identity unavailable; continuing");
    return;
  }

  if (this->step_ == Step::CORE) {
    this->add_warning_(WARNING_CORE_UNAVAILABLE);
    this->step_ = Step::EEPROM;
    this->set_phase_("core identity unavailable; continuing");
    return;
  }

  this->fail_job_("Unexpected Modbus read failure");
}

void OpenQuattOduEepromDump::advance_after_success_() {
  this->block_retry_count_ = 0U;
  switch (this->step_) {
    case Step::EXTENDED:
      this->extended_supported_.store(true, std::memory_order_release);
      this->progress_.store(2U, std::memory_order_release);
      this->step_ = Step::MODEL;
      break;
    case Step::MODEL:
      this->step_ = Step::CUSTOMER_MODEL;
      break;
    case Step::CUSTOMER_MODEL:
      this->step_ = Step::SERIAL;
      break;
    case Step::SERIAL:
      this->progress_.store(5U, std::memory_order_release);
      this->step_ = Step::CORE;
      break;
    case Step::CORE:
      this->core_available_.store(true, std::memory_order_release);
      this->progress_.store(10U, std::memory_order_release);
      this->step_ = Step::EEPROM;
      break;
    case Step::EEPROM: {
      this->eeprom_offset_ = static_cast<uint16_t>(this->eeprom_offset_ + this->request_register_count_);
      this->registers_read_.store(this->eeprom_offset_, std::memory_order_release);
      const uint8_t progress =
          static_cast<uint8_t>(10U + (static_cast<uint32_t>(this->eeprom_offset_) * 90U / EEPROM_REGISTER_COUNT));
      this->progress_.store(std::min<uint8_t>(progress, 99U), std::memory_order_release);
      if (this->eeprom_offset_ >= EEPROM_REGISTER_COUNT) {
        this->step_ = Step::VERIFYING;
        this->finish_job_();
      }
      break;
    }
    default:
      this->fail_job_("Unexpected EEPROM dump state");
      break;
  }
}

uint16_t OpenQuattOduEepromDump::calculate_crc_() const {
  uint16_t crc = 0xFFFFU;
  for (size_t index = 0; index < EEPROM_CRC_DATA_COUNT; ++index) {
    crc ^= static_cast<uint8_t>(this->eeprom_[index] & 0x00FFU);
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      crc = (crc & 0x0001U) != 0U ? static_cast<uint16_t>((crc >> 1U) ^ 0xA001U) : static_cast<uint16_t>(crc >> 1U);
    }
  }
  return crc;
}

void OpenQuattOduEepromDump::finish_job_() {
  this->set_phase_("verifying EEPROM CRC");
  const uint16_t calculated = this->calculate_crc_();
  const uint16_t stored =
      static_cast<uint16_t>((this->eeprom_[510] & 0x00FFU) | ((this->eeprom_[511] & 0x00FFU) << 8U));
  this->calculated_crc_.store(calculated, std::memory_order_release);
  this->stored_crc_.store(stored, std::memory_order_release);

  if (calculated != stored && this->crc_retry_count_.load(std::memory_order_acquire) == 0U) {
    this->crc_retry_count_.store(1U, std::memory_order_release);
    this->eeprom_offset_ = 0U;
    this->registers_read_.store(0U, std::memory_order_release);
    this->progress_.store(10U, std::memory_order_release);
    this->step_ = Step::EEPROM;
    this->next_request_ms_ = millis() + FAILURE_COOLDOWN_MS;
    this->set_phase_("runtime CRC differs; rereading EEPROM shadow");
    return;
  }

  const bool crc_matches_stored_eeprom = calculated == stored;
  this->crc_matches_stored_eeprom_.store(crc_matches_stored_eeprom, std::memory_order_release);
  if (!crc_matches_stored_eeprom) this->add_warning_(WARNING_RUNTIME_DIFFERS);
  this->step_ = Step::COMPLETE;
  this->progress_.store(100U, std::memory_order_release);
  this->registers_read_.store(EEPROM_REGISTER_COUNT, std::memory_order_release);
  this->set_phase_(this->warning_flags_.load(std::memory_order_acquire) == WARNING_NONE ? "complete"
                                                                                        : "complete with warnings");
  this->completed_ms_ = millis();
  if (this->clock_ != nullptr) {
    const auto captured_at = this->clock_->now();
    if (captured_at.is_valid()) this->captured_at_epoch_ = static_cast<uint64_t>(captured_at.timestamp);
  }
  this->dump_ready_.store(true, std::memory_order_release);
  this->active_.store(false, std::memory_order_release);
  ESP_LOGI(TAG, "HP%u ODU EEPROM snapshot complete; CRC calculated=0x%04X stored=0x%04X matches stored EEPROM=%s",
           this->hp_index_, calculated, stored, YESNO(crc_matches_stored_eeprom));
}

void OpenQuattOduEepromDump::fail_job_(const char* error) {
  this->step_ = Step::FAILED;
  this->completed_ms_ = millis();
  this->set_error_(error);
  this->set_phase_("failed");
  this->dump_ready_.store(false, std::memory_order_release);
  this->active_.store(false, std::memory_order_release);
  ESP_LOGW(TAG, "HP%u ODU EEPROM snapshot failed: %s", this->hp_index_, error);
}

void OpenQuattOduEepromDump::set_phase_(const char* phase) {
  portENTER_CRITICAL(&this->state_mux_);
  std::snprintf(this->phase_, sizeof(this->phase_), "%s", phase == nullptr ? "" : phase);
  portEXIT_CRITICAL(&this->state_mux_);
}

void OpenQuattOduEepromDump::set_error_(const char* error) {
  portENTER_CRITICAL(&this->state_mux_);
  std::snprintf(this->error_, sizeof(this->error_), "%s", error == nullptr ? "" : error);
  portEXIT_CRITICAL(&this->state_mux_);
}

void OpenQuattOduEepromDump::add_warning_(Warning warning) {
  this->warning_flags_.fetch_or(static_cast<uint8_t>(warning), std::memory_order_acq_rel);
}

void OpenQuattOduEepromDump::decode_ascii_words_(const uint16_t* words, size_t count, char* output,
                                                 size_t output_size) {
  if (output == nullptr || output_size == 0U) return;
  size_t written = 0U;
  bool stopped = false;
  for (size_t index = 0; index < count && !stopped; ++index) {
    const uint8_t bytes[2] = {static_cast<uint8_t>((words[index] >> 8U) & 0xFFU),
                              static_cast<uint8_t>(words[index] & 0xFFU)};
    for (uint8_t value : bytes) {
      if (value == 0U || value == 0xFFU) {
        stopped = true;
        break;
      }
      if (value < 0x20U || value > 0x7EU) {
        output[0] = '\0';
        return;
      }
      if (written + 1U >= output_size) {
        stopped = true;
        break;
      }
      output[written++] = static_cast<char>(value);
    }
  }
  while (written > 0U && output[written - 1U] == ' ') --written;
  output[written] = '\0';
}

void OpenQuattOduEepromDump::write_status(httpd_req_t* req) const {
  char phase[sizeof(this->phase_)];
  char error[sizeof(this->error_)];
  std::array<uint16_t, TEXT_REGISTER_COUNT> model_words{};
  std::array<uint16_t, CORE_REGISTER_COUNT> core_words{};
  bool copy_model = false;
  bool core_available = false;
  portENTER_CRITICAL(&this->state_mux_);
  std::memcpy(phase, this->phase_, sizeof(phase));
  std::memcpy(error, this->error_, sizeof(error));
  copy_model =
      this->dump_ready_.load(std::memory_order_acquire) && this->extended_supported_.load(std::memory_order_acquire);
  core_available = this->core_available_.load(std::memory_order_acquire);
  if (copy_model) model_words = this->model_;
  if (core_available) core_words = this->core_;
  portEXIT_CRITICAL(&this->state_mux_);
  phase[sizeof(phase) - 1U] = '\0';
  error[sizeof(error) - 1U] = '\0';

  char model[48]{};
  if (copy_model) decode_ascii_words_(model_words.data(), model_words.size(), model, sizeof(model));
  const uint16_t pcb_program = core_available ? core_words[8] : 0U;
  char pcb_label[16];
  std::snprintf(pcb_label, sizeof(pcb_label), "V%03u_T%02u", static_cast<unsigned>((pcb_program >> 8U) & 0xFFU),
                static_cast<unsigned>(pcb_program & 0xFFU));

  ChunkedJsonWriter writer(req);
  writer.write_literal(R"({"ok":true,"available":)");
  writer.write_bool(this->available_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"hp":)");
  writer.write_uint(this->hp_index_);
  writer.write_literal(R"(,"modbus_device_address":)");
  writer.write_uint(this->device_address_);
  writer.write_literal(R"(,"active":)");
  writer.write_bool(this->active_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"dump_ready":)");
  writer.write_bool(this->dump_ready_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"job_id":)");
  writer.write_uint(this->job_id_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"phase":)");
  writer.write_string(phase);
  writer.write_literal(R"(,"progress_percent":)");
  writer.write_uint(this->progress_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"registers_read":)");
  writer.write_uint(this->registers_read_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"register_count":512,"warning_flags":)");
  writer.write_uint(this->warning_flags_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"error":)");
  writer.write_string(error);
  writer.write_literal(R"(,"crc":{"calculated":)");
  writer.write_hex16(this->calculated_crc_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"stored":)");
  writer.write_hex16(this->stored_crc_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"matches_stored_eeprom":)");
  writer.write_bool(this->crc_matches_stored_eeprom_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"retry_count":)");
  writer.write_uint(this->crc_retry_count_.load(std::memory_order_acquire));
  writer.write_literal(R"(},"identity":{"extended_supported":)");
  writer.write_bool(this->extended_supported_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"model":)");
  writer.write_string(model);
  writer.write_literal(R"(,"core_available":)");
  writer.write_bool(core_available);
  writer.write_literal(R"(,"pcb_program_raw":)");
  writer.write_uint(pcb_program);
  writer.write_literal(R"(,"pcb_program":)");
  writer.write_string(core_available ? pcb_label : "");
  writer.write_literal(R"(,"eeprom_program_raw":)");
  writer.write_uint(core_available ? core_words[9] : 0U);
  writer.write_literal("}}");
  writer.finish();
}

bool OpenQuattOduEepromDump::begin_download() {
  portENTER_CRITICAL(&this->state_mux_);
  const bool allowed =
      !this->active_.load(std::memory_order_acquire) && !this->starting_.load(std::memory_order_acquire) &&
      this->dump_ready_.load(std::memory_order_acquire) && !this->download_in_progress_.load(std::memory_order_acquire);
  if (allowed) this->download_in_progress_.store(true, std::memory_order_release);
  portEXIT_CRITICAL(&this->state_mux_);
  return allowed;
}

void OpenQuattOduEepromDump::end_download() { this->download_in_progress_.store(false, std::memory_order_release); }

void OpenQuattOduEepromDump::write_download(httpd_req_t* req) const {
  char model[48]{};
  char customer_model[48]{};
  char serial[48]{};
  decode_ascii_words_(this->model_.data(), this->model_.size(), model, sizeof(model));
  decode_ascii_words_(this->customer_model_.data(), this->customer_model_.size(), customer_model,
                      sizeof(customer_model));
  decode_ascii_words_(this->serial_.data(), this->serial_.size(), serial, sizeof(serial));

  const uint16_t pcb_program = this->core_[8];
  const uint16_t official_firmware = this->extended_[3];
  char pcb_label[16];
  char official_label[16];
  std::snprintf(pcb_label, sizeof(pcb_label), "V%03u_T%02u", static_cast<unsigned>((pcb_program >> 8U) & 0xFFU),
                static_cast<unsigned>(pcb_program & 0xFFU));
  std::snprintf(official_label, sizeof(official_label), "%u.%u",
                static_cast<unsigned>((official_firmware >> 8U) & 0xFFU),
                static_cast<unsigned>(official_firmware & 0xFFU));
  const uint8_t warning_flags = this->warning_flags_.load(std::memory_order_acquire);

  ChunkedJsonWriter writer(req);
  writer.write_literal(R"({"format":"openquatt-odu-eeprom-v1","schema_version":1,"captured_at_epoch":)");
  writer.write_uint64(this->captured_at_epoch_);
  writer.write_literal(R"(,"source":{"device":"OpenQuatt","hp":)");
  writer.write_uint(this->hp_index_);
  writer.write_literal(R"(,"modbus_device_address":)");
  writer.write_uint(this->device_address_);
  writer.write_literal(R"(,"snapshot":"runtime_eeprom_shadow"},"job":{"id":)");
  writer.write_uint(this->job_id_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"duration_ms":)");
  writer.write_uint(this->completed_ms_ - this->started_ms_);
  writer.write_literal(R"(,"warning_flags":)");
  writer.write_uint(warning_flags);
  writer.write_literal(R"(,"warnings":[)");
  bool first_warning = true;
  auto write_warning = [&](const char* value) {
    if (!first_warning) writer.write_char(',');
    writer.write_string(value);
    first_warning = false;
  };
  if ((warning_flags & WARNING_EXTENDED_UNAVAILABLE) != 0U) write_warning("extended_metadata_unavailable");
  if ((warning_flags & WARNING_CORE_UNAVAILABLE) != 0U) write_warning("core_metadata_unavailable");
  if ((warning_flags & WARNING_RUNTIME_DIFFERS) != 0U) write_warning("runtime_shadow_differs_from_stored_eeprom");
  writer.write_literal(R"(]},"identity":{"core_available":)");
  writer.write_bool(this->core_available_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"compressor_code":)");
  writer.write_uint(this->core_[0]);
  writer.write_literal(R"(,"odu_dip_switch":)");
  writer.write_uint(this->core_[1]);
  writer.write_literal(R"(,"failures_raw":)");
  writer.write_uint(this->core_[7]);
  writer.write_literal(R"(,"eeprom_failure":)");
  writer.write_bool((this->core_[7] & 0x0080U) != 0U);
  writer.write_literal(R"(,"pcb_program":{"raw":)");
  writer.write_uint(pcb_program);
  writer.write_literal(R"(,"hex":)");
  writer.write_hex16(pcb_program);
  writer.write_literal(R"(,"main":)");
  writer.write_uint((pcb_program >> 8U) & 0xFFU);
  writer.write_literal(R"(,"sub":)");
  writer.write_uint(pcb_program & 0xFFU);
  writer.write_literal(R"(,"label":)");
  writer.write_string(pcb_label);
  writer.write_literal(R"(},"eeprom_program":{"raw":)");
  writer.write_uint(this->core_[9]);
  writer.write_literal(R"(,"hex":)");
  writer.write_hex16(this->core_[9]);
  writer.write_literal(R"(},"control_board_item":{"raw":)");
  writer.write_uint(this->core_[13]);
  writer.write_literal(R"(,"hex":)");
  writer.write_hex16(this->core_[13]);
  writer.write_literal(R"(},"extended_supported":)");
  writer.write_bool(this->extended_supported_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"odu_address":)");
  writer.write_uint(this->extended_[0]);
  writer.write_literal(R"(,"project_code":)");
  writer.write_uint(this->extended_[1]);
  writer.write_literal(R"(,"hardware_version":)");
  writer.write_uint(this->extended_[2]);
  writer.write_literal(R"(,"official_firmware":{"raw":)");
  writer.write_uint(official_firmware);
  writer.write_literal(R"(,"label":)");
  writer.write_string(official_label);
  writer.write_literal(R"(},"beta_version":)");
  writer.write_uint(this->extended_[4]);
  writer.write_literal(R"(,"extended_eeprom_version":)");
  writer.write_uint(this->extended_[5]);
  writer.write_literal(R"(,"model":)");
  writer.write_string(model);
  writer.write_literal(R"(,"customer_model":)");
  writer.write_string(customer_model);
  writer.write_literal(R"(,"serial":)");
  writer.write_string(serial);
  writer.write_literal(R"(,"raw_blocks":{"core":{"modbus_start":2114,"values":)");
  write_word_array(writer, this->core_);
  writer.write_literal(R"(},"extended":{"modbus_start":11004,"values":)");
  write_word_array(writer, this->extended_);
  writer.write_literal(R"(},"model":{"modbus_start":11120,"values":)");
  write_word_array(writer, this->model_);
  writer.write_literal(R"(},"customer_model":{"modbus_start":11160,"values":)");
  write_word_array(writer, this->customer_model_);
  writer.write_literal(R"(},"serial":{"modbus_start":11219,"values":)");
  write_word_array(writer, this->serial_);
  writer.write_literal(
      R"(}}},"eeprom":{"complete":true,"sheet_start":3000,"modbus_start":2999,"register_count":512,"crc":{"algorithm":"CRC16/Modbus","data":"low byte of sheet 3000..3509","init":65535,"polynomial":40961,"calculated":)");
  writer.write_hex16(this->calculated_crc_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"stored":)");
  writer.write_hex16(this->stored_crc_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"matches_stored_eeprom":)");
  writer.write_bool(this->crc_matches_stored_eeprom_.load(std::memory_order_acquire));
  writer.write_literal(R"(,"retry_count":)");
  writer.write_uint(this->crc_retry_count_.load(std::memory_order_acquire));
  writer.write_literal(R"(},"fingerprints":{"fan_count":)");
  writer.write_uint(this->eeprom_[310]);
  writer.write_literal(R"(,"model_main_pcb_address":)");
  writer.write_uint(this->eeprom_[317]);
  writer.write_literal(R"(,"minimum_flow":)");
  writer.write_uint(this->eeprom_[456]);
  writer.write_literal(R"(,"flow_sensor_type":)");
  writer.write_uint(this->eeprom_[459]);
  writer.write_literal(R"(,"refrigerant":)");
  writer.write_uint(this->eeprom_[498]);
  writer.write_literal(R"(,"pump_fan_power_words":[)");
  for (size_t index = 502; index <= 507; ++index) {
    if (index > 502U) writer.write_char(',');
    writer.write_uint(this->eeprom_[index]);
  }
  writer.write_literal(R"(]},"registers":[)");
  for (size_t index = 0; index < EEPROM_REGISTER_COUNT; ++index) {
    if (index > 0U) writer.write_char(',');
    const uint16_t word = this->eeprom_[index];
    writer.write_literal(R"({"sheet_address":)");
    writer.write_uint(3000U + index);
    writer.write_literal(R"(,"modbus_address":)");
    writer.write_uint(EEPROM_START_ADDRESS + index);
    writer.write_literal(R"(,"word":)");
    writer.write_uint(word);
    writer.write_literal(R"(,"hex":)");
    writer.write_hex16(word);
    writer.write_literal(R"(,"high_byte":)");
    writer.write_uint((word >> 8U) & 0xFFU);
    writer.write_literal(R"(,"low_byte":)");
    writer.write_uint(word & 0xFFU);
    writer.write_char('}');
  }
  writer.write_literal("]}}");
  writer.finish();
}

}  // namespace openquatt_odu_eeprom_dump
}  // namespace esphome
