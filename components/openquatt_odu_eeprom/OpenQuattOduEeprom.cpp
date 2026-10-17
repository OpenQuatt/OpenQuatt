#include "OpenQuattOduEeprom.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include "esphome/core/log.h"

namespace esphome::openquatt_odu_eeprom {
namespace {
using oq_eeprom::State;
OpenQuattOduEeprom* instances[2]{};
OpenQuattOduEeprom* bus_owner{nullptr};

bool same_origin(AsyncWebServerRequest* request) {
  const auto host = request->get_header("Host");
  if (!host.has_value() || host->empty()) return false;
  for (const char* name : {"Origin", "Referer"}) {
    const auto header = request->get_header(name);
    if (!header.has_value()) continue;
    const size_t scheme = header->find("://");
    if (scheme == std::string::npos || (header->substr(0, scheme) != "http" && header->substr(0, scheme) != "https"))
      return false;
    const size_t start = scheme + 3U;
    const size_t end = header->find_first_of("/?#", start);
    if (header->substr(start, end == std::string::npos ? end : end - start) != *host) return false;
  }
  return true;
}
bool number(const std::string& raw, uint32_t& value) {
  if (raw.empty()) return false;
  const auto result = std::from_chars(raw.data(), raw.data() + raw.size(), value);
  return result.ec == std::errc{} && result.ptr == raw.data() + raw.size();
}
uint16_t word(std::span<const uint8_t> data, size_t index) {
  return static_cast<uint16_t>((uint16_t(data[index * 2U]) << 8U) | data[index * 2U + 1U]);
}
bool blank(const oq_eeprom::Record& record) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
  for (size_t index = 0U; index < sizeof(record); ++index)
    if (bytes[index] != 0xFFU) return false;
  return true;
}
class Handler : public AsyncWebHandler {
 public:
  Handler(OpenQuattOduEeprom* owner, uint8_t hp) : owner_(owner) {
    std::snprintf(path_, sizeof(path_), "/openquatt/odu-eeprom-write/hp%u", hp);
  }
  bool canHandle(AsyncWebServerRequest* request) const override {
    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url);
    const size_t length = std::strlen(path_);
    return std::strncmp(url, path_, length) == 0 && url[length] == '/' &&
           (request->method() == HTTP_GET || request->method() == HTTP_POST);
  }
  void handleRequest(AsyncWebServerRequest* request) override {
    if (!owner_->authenticated(request)) {
      request->requestAuthentication();
      return;
    }
    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(url);
    std::string action(url + std::strlen(path_) + 1U);
    action = action.substr(0U, action.find('?'));
    bool download = false;
    if (request->method() == HTTP_GET) {
      download = action == "backup";
      if (action != "status" && !download) {
        request->send(404);
        return;
      }
    } else {
      const auto csrf = owner_->csrf();
      if (csrf.empty() || !same_origin(request) || request->arg("csrf_token") != csrf) {
        request->send(409, "application/json", R"({"error":"forbidden"})");
        return;
      }
      using Action = OpenQuattOduEeprom::Action;
      Action command = Action::NONE;
      if (action == "begin-backup")
        command = Action::BEGIN_BACKUP;
      else if (action == "capture")
        command = Action::CAPTURE;
      else if (action == "prepare")
        command = Action::PREPARE;
      else if (action == "commit")
        command = Action::COMMIT;
      else if (action == "verify")
        command = Action::VERIFY;
      else if (action == "cancel")
        command = Action::CANCEL;
      else if (action == "restore")
        command = Action::RESTORE;
      else if (action == "check-writer")
        command = Action::CHECK_WRITER;
      uint32_t generation = 0U, groups = 0U;
      if (command == Action::NONE || !number(request->arg("generation"), generation) ||
          !number(request->arg("groups"), groups) || groups > oq_eeprom::ALL ||
          !owner_->enqueue(command, static_cast<uint8_t>(groups), generation,
                           request->arg("cold_confirmed") == "true")) {
        request->send(409, "application/json", R"({"error":"rejected"})");
        return;
      }
    }
    httpd_req_t* raw = *request;
    httpd_resp_set_type(raw, "application/json; charset=utf-8");
    httpd_resp_set_hdr(raw, "Cache-Control", "no-store");
    if (download)
      httpd_resp_set_hdr(raw, "Content-Disposition", "attachment; filename=openquatt-odu-eeprom-backup.json");
    owner_->write_status(raw, download);
  }

 private:
  OpenQuattOduEeprom* owner_;
  char path_[48]{};
};
}  // namespace

const char* OpenQuattOduEeprom::slot_key_(uint8_t slot) const {
  return hp_ == 1U ? (slot == 0U ? "hp1a" : "hp1b") : (slot == 0U ? "hp2a" : "hp2b");
}
bool OpenQuattOduEeprom::load_() {
  nvs_handle_t handle{};
  const esp_err_t opened = nvs_open("oq_eeprom", NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) {
    records_[0] = {};
    return true;
  }
  if (opened != ESP_OK) return false;
  bool readable = true;
  for (uint8_t slot = 0U; slot < 2U; ++slot) {
    size_t bytes = sizeof(records_[slot]);
    const esp_err_t result = nvs_get_blob(handle, slot_key_(slot), &records_[slot], &bytes);
    if (result == ESP_ERR_NVS_NOT_FOUND)
      std::memset(&records_[slot], 0xFF, sizeof(records_[slot]));
    else if (result != ESP_OK || bytes != sizeof(records_[slot]))
      readable = false;
  }
  nvs_close(handle);
  if (!readable) return false;
  const bool a = oq_eeprom::valid_record(records_[0]) && records_[0].identity.hp == hp_;
  const bool b = oq_eeprom::valid_record(records_[1]) && records_[1].identity.hp == hp_;
  // A corrupt newer slot may have contained an interrupted write. Never fall back
  // to an older baseline and silently release the control hold.
  if ((!a && !blank(records_[0])) || (!b && !blank(records_[1]))) return false;
  if (!a && !b) {
    if (!blank(records_[0]) || !blank(records_[1])) return false;
    records_[0] = {};
    return true;
  }
  slot_ = b && (!a || static_cast<int32_t>(records_[1].generation - records_[0].generation) > 0) ? 1 : 0;
  if (slot_ == 1) records_[0] = records_[1];
  valid_record_ = true;
  const State state = record_().state;
  oq_eeprom::hold(hp_, state != State::BASELINE && state != State::CANCELLED);
  return true;
}
bool OpenQuattOduEeprom::persist_(State state) {
  auto& record = record_();
  record.state = state;
  if (++record.generation == 0U) ++record.generation;
  oq_eeprom::finalize_record(record);
  if (!oq_eeprom::valid_record(record)) return false;
  const uint8_t next = slot_ == 0 ? 1U : 0U;
  nvs_stats_t stats{};
  constexpr size_t required = 100U + 2U + (sizeof(oq_eeprom::Record) + 31U) / 32U;
  nvs_handle_t handle{};
  bool written = partition_ != nullptr && partition_->size >= 65536U && nvs_get_stats(nullptr, &stats) == ESP_OK &&
                 stats.available_entries >= required && nvs_open("oq_eeprom", NVS_READWRITE, &handle) == ESP_OK;
  if (written) {
    written = nvs_set_blob(handle, slot_key_(next), &record, sizeof(record)) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
  }
  if (written) {
    written = nvs_open("oq_eeprom", NVS_READONLY, &handle) == ESP_OK;
    if (written) {
      size_t bytes = sizeof(records_[1]);
      written = nvs_get_blob(handle, slot_key_(next), &records_[1], &bytes) == ESP_OK && bytes == sizeof(records_[1]) &&
                oq_eeprom::valid_record(records_[1]) && std::memcmp(&records_[1], &record, sizeof(record)) == 0;
      nvs_close(handle);
    }
  }
  if (!written) {
    storage_bad_ = true;
    oq_eeprom::hold(hp_, true);
    return false;
  }
  slot_ = static_cast<int8_t>(next);
  valid_record_ = true;
  return true;
}
void OpenQuattOduEeprom::setup() {
  instances[hp_ - 1U] = this;
  oq_eeprom::configured_mask |= 1U << (hp_ - 1U);
  set_parent(controller_->hub());
  set_address(controller_->device_address());
  partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, "nvs");
  const bool memory = records_.allocate_external(2U) && image_.allocate_external(1U) && frame_.allocate_external(123U);
  const bool available = memory && partition_ != nullptr && partition_->size >= 65536U;
  if (memory && !load_()) {
    storage_bad_ = true;
    oq_eeprom::hold(hp_, true);
  }
  if (!memory) {
    // A missing PSRAM buffer must never hide a previously interrupted write.
    nvs_handle_t handle{};
    const esp_err_t opened = nvs_open("oq_eeprom", NVS_READONLY, &handle);
    bool pending = opened != ESP_OK && opened != ESP_ERR_NVS_NOT_FOUND;
    if (opened == ESP_OK) {
      for (uint8_t slot = 0U; slot < 2U; ++slot) {
        size_t bytes = 0U;
        pending |= nvs_get_blob(handle, slot_key_(slot), nullptr, &bytes) != ESP_ERR_NVS_NOT_FOUND;
      }
      nvs_close(handle);
    }
    if (pending) {
      storage_bad_ = true;
      oq_eeprom::hold(hp_, true);
    }
    records_.release();
    image_.release();
    frame_.release();
  }
  // The reset counter is deliberately scheduled after both journal loads.
  // Component::mark_failed() prevents its setup() from erasing pending evidence.
  if (oq_eeprom::holding() && factory_reset_ != nullptr) factory_reset_->mark_failed();
  oq_eeprom::checked_mask.fetch_or(1U << (hp_ - 1U), std::memory_order_release);
  status_.available = available;
  // No writes are resumed on boot. Pending jobs need a new, observed power interruption.
  publish_(storage_bad_ ? "STORAGE_FAILED" : available ? "READY" : "STORAGE_UNAVAILABLE");
  web_server_base::global_web_server_base->add_handler(new Handler(this, hp_));
}
bool OpenQuattOduEeprom::enqueue(Action action, uint8_t groups, uint32_t generation, bool cold_confirmed) {
  portENTER_CRITICAL(&mux_);
  const bool accepted = status_.available && !status_.busy && !downloading_ && !storage_bad_ &&
                        generation == status_.generation && action != Action::NONE;
  if (accepted) {
    pending_ = action;
    groups_ = groups;
    expected_generation_ = generation;
    cold_confirmed_ = cold_confirmed;
    status_.busy = true;
  }
  portEXIT_CRITICAL(&mux_);
  return accepted;
}
bool OpenQuattOduEeprom::reserve_() {
  if (bus_owner != nullptr) return false;
  if ((oq_eeprom::hold_mask.load() & ~(1U << (hp_ - 1U))) != 0U) return false;
  for (auto* instance : instances) {
    if (instance == nullptr) continue;
    if (!instance->dump_->try_begin_external_operation()) {
      for (auto* previous : instances) {
        if (previous == instance) break;
        if (previous != nullptr) previous->dump_->end_external_operation();
      }
      return false;
    }
  }
  bus_owner = this;
  reserved_ = true;
  return true;
}
void OpenQuattOduEeprom::release_() {
  if (!reserved_) return;
  oq_eeprom::permit = {};
  for (auto* instance : instances)
    if (instance != nullptr) instance->dump_->end_external_operation();
  reserved_ = false;
  bus_owner = nullptr;
}
void OpenQuattOduEeprom::publish_(const char* result) {
  Status next;
  next.available = status_.available;
  next.busy = step_ != Step::IDLE;
  next.cold_seen = cold_seen_;
  next.holding = (oq_eeprom::hold_mask.load() & (1U << (hp_ - 1U))) != 0U;
  next.result = result;
  if (valid_record_) {
    const auto& record = record_();
    next.generation = record.generation;
    next.state = static_cast<uint8_t>(record.state);
    next.groups = record.groups;
    next.supported = oq_eeprom::valid_identity(record.identity);
    next.legacy = oq_eeprom::legacy_identity(static_cast<oq_odu::Variant>(record.identity.variant));
    next.power_cycle_safe =
        power_cycle_safe_ || record.state == State::WAIT_BASELINE || record.state == State::WAIT_VERIFY;
    next.baseline = record.state != State::WAIT_BASELINE && record.state != State::CANCELLED;
    next.crc = oq_eeprom::stored_crc(record.candidate);
    if (record.state == State::PREPARED)
      for (size_t i = 0; i < 510U; ++i) {
        next.changes += record.candidate[i] != record.baseline[i] ? 1U : 0U;
        next.reverted += record.source[i] != record.candidate[i] ? 1U : 0U;
      }
  }
  portENTER_CRITICAL(&mux_);
  next.busy = next.busy || pending_ != Action::NONE;
  status_ = next;
  portEXIT_CRITICAL(&mux_);
}
void OpenQuattOduEeprom::finish_(const char* result) {
  step_ = Step::IDLE;
  waiting_ = response_ = false;
  release_();
  publish_(result);
}
void OpenQuattOduEeprom::fail_(const char* result) {
  clear_tx_queue_for_device();
  if (modification_started_ && !worker_may_run_) power_cycle_safe_ = true;
  if (modification_started_ && valid_record_ && !storage_bad_) {
    if (!persist_(State::UNCERTAIN)) result = "STORAGE_FAILED";
  }
  if (modification_started_ || storage_bad_)
    oq_eeprom::hold(hp_, true);
  else if (!valid_record_ || record_().state == State::BASELINE || record_().state == State::CANCELLED)
    oq_eeprom::hold(hp_, false);
  finish_(result);
}
bool OpenQuattOduEeprom::revoke_profiles_() {
  return settings_->revoke_auto_reapply() && defrost_->revoke_auto_reapply();
}
bool OpenQuattOduEeprom::cold_ready_() const { return cold_seen_ && cold_confirmed_; }
void OpenQuattOduEeprom::offline() { offline_pending_.store(true); }
void OpenQuattOduEeprom::request_read_(uint16_t address, uint16_t count) {
  address_ = address;
  count_ = count;
  writing_ = false;
  waiting_ = true;
  response_ = false;
  request_started_ms_ = millis();
  if (!read_holding_registers(address, count)) fail_("READ_FAILED");
}
void OpenQuattOduEeprom::request_write_(uint16_t address, uint16_t value) {
  address_ = address;
  count_ = 1U;
  writing_ = true;
  oq_eeprom::permit = {nullptr, address, 1U, value, controller_->device_address(), hp_, 6U, data_mode_};
  waiting_ = true;
  response_ = false;
  request_started_ms_ = millis();
  if (!write_single_register(address, value)) fail_("WRITE_UNCERTAIN");
}
void OpenQuattOduEeprom::request_block_() {
  count_ = std::min<uint16_t>(123U, 512U - offset_);
  address_ = oq_eeprom::IMAGE_ADDRESS + offset_;
  for (size_t i = 0; i < count_; ++i) frame_[i] = record_().candidate[offset_ + i];
  writing_ = true;
  oq_eeprom::permit = {frame_.data(), address_, count_, 0U, controller_->device_address(), hp_, 16U, false};
  waiting_ = true;
  response_ = false;
  request_started_ms_ = millis();
  if (!write_multiple_registers(address_, std::span<const uint16_t>(frame_.data(), count_))) fail_("WRITE_UNCERTAIN");
}
void OpenQuattOduEeprom::loop() {
  portENTER_CRITICAL(&mux_);
  const bool downloading = downloading_;
  Action pending = pending_;
  pending_ = Action::NONE;
  portEXIT_CRITICAL(&mux_);
  if (downloading) return;
  if (offline_pending_.exchange(false)) {
    if (valid_record_ && record_().state != State::BASELINE && record_().state != State::CANCELLED) cold_seen_ = true;
    if (step_ != Step::IDLE) {
      fail_("OFFLINE");
      return;
    }
    publish_(status_.result);
  }
  if (pending != Action::NONE) {
    action_ = pending;
    modification_started_ = false;
    data_mode_ = action_ == Action::CHECK_WRITER;
    if (web_server_base::global_web_server_base->is_recovery_active() || !oq_eeprom::safe(millis(), hp_, data_mode_) ||
        !reserve_()) {
      finish_("SAFETY_BLOCKED");
      return;
    }
    request_epoch_ = web_server_base::global_web_server_base->recovery_epoch();
    stop_epoch_ = oq_eeprom::stop_epoch;
    started_ms_ = millis();
    const State state = valid_record_ ? record_().state : State::CANCELLED;
    if (action_ == Action::CANCEL) {
      if (state != State::PREPARED && state != State::WAIT_BASELINE) {
        finish_("RECOVERY_REQUIRED");
        return;
      }
      if (!persist_(state == State::PREPARED ? State::BASELINE : State::CANCELLED)) {
        fail_("STORAGE_FAILED");
        return;
      }
      oq_eeprom::hold(hp_, false);
      finish_("CANCELLED");
      return;
    }
    if ((action_ == Action::BEGIN_BACKUP && state != State::CANCELLED) ||
        (action_ == Action::CAPTURE && (state != State::WAIT_BASELINE || !cold_ready_())) ||
        ((action_ == Action::PREPARE || action_ == Action::RESTORE) && state != State::BASELINE) ||
        (action_ == Action::COMMIT && (state != State::PREPARED || expected_generation_ != record_().generation)) ||
        ((action_ == Action::VERIFY || action_ == Action::CHECK_WRITER) &&
         (state != State::WAIT_VERIFY && state != State::UNCERTAIN && state != State::WRITING)) ||
        (action_ == Action::VERIFY && !cold_ready_())) {
      finish_("INVALID_STATE");
      return;
    }
    oq_eeprom::hold(hp_, true);
    step_ = Step::IDENTITY;
    publish_("READING");
    request_read_(oq_odu::CORE_IDENTITY_REGISTER, oq_odu::CORE_IDENTITY_REGISTER_COUNT);
    return;
  }
  if (step_ == Step::IDLE) return;
  auto* server = web_server_base::global_web_server_base;
  if (server->is_recovery_active() || server->recovery_epoch() != request_epoch_ ||
      oq_eeprom::stop_epoch != stop_epoch_ || !oq_eeprom::safe(millis(), hp_, data_mode_)) {
    fail_("SAFETY_BLOCKED");
    return;
  }
  if (millis() - started_ms_ > 60000U || (waiting_ && millis() - request_started_ms_ > 8000U)) {
    fail_("TIMEOUT");
    return;
  }
  if (waiting_) {
    if (!response_) return;
    waiting_ = response_ = false;
    oq_eeprom::permit = {};
    if (!response_ok_) {
      fail_(writing_ ? "WRITE_UNCERTAIN" : "READ_FAILED");
      return;
    }
    advance_();
    return;
  }
  if (step_ == Step::POLL && static_cast<int32_t>(millis() - next_ms_) >= 0) request_read_(4003U, 1U);
}
void OpenQuattOduEeprom::advance_() {
  auto& record = record_();
  switch (step_) {
    case Step::IDENTITY:
      if (!oq_eeprom::supported_firmware(identity_)) {
        fail_("UNSUPPORTED");
        return;
      }
      step_ = Step::SERIAL;
      if (oq_eeprom::legacy_identity(static_cast<oq_odu::Variant>(identity_.variant)))
        advance_();  // Legacy applications have no usable serial block.
      else
        request_read_(11219U, 20U);
      break;
    case Step::SERIAL:
      if (!oq_eeprom::valid_identity(identity_)) {
        fail_("UNSUPPORTED");
        return;
      }
      if (valid_record_ && record.state != State::CANCELLED && !oq_eeprom::same_identity(identity_, record.identity)) {
        fail_("IDENTITY_MISMATCH");
        return;
      }
      if (action_ == Action::CHECK_WRITER) {
        step_ = Step::CHECK_WRITER;
        request_read_(4003U, 1U);
        break;
      }
      if (action_ == Action::BEGIN_BACKUP) {
        const uint32_t generation = record.generation;
        record = {};
        record.generation = generation;
        record.identity = identity_;
        if (!persist_(State::WAIT_BASELINE) || !revoke_profiles_()) {
          fail_("STORAGE_FAILED");
          return;
        }
        cold_seen_ = false;
        finish_("POWER_CYCLE_REQUIRED");
        return;
      }
      step_ = Step::GUARD;
      request_read_(2099U, 5U);
      break;
    case Step::GUARD:
      if (action_ == Action::COMMIT) {
        step_ = Step::COMMIT_READY;
        request_read_(4003U, 1U);
        break;
      }
      offset_ = 0U;
      step_ = Step::READ;
      request_read_(oq_eeprom::IMAGE_ADDRESS, 125U);
      break;
    case Step::CHECK_WRITER:
      // Status 6 may be reported while subsequent chunks are still scheduled.
      power_cycle_safe_ = !worker_may_run_ || frame_[0] == 4U || frame_[0] == 7U;
      finish_(power_cycle_safe_ ? "POWER_CYCLE_REQUIRED" : "WRITER_ACTIVE");
      break;
    case Step::COMMIT_READY:
      // A prior completion (4) cannot establish completion of our new job.
      // Require a fresh idle baseline before any shadow write is queued.
      if (frame_[0] != 0U) {
        fail_("COMMIT_NOT_IDLE");
        return;
      }
      offset_ = 0U;
      step_ = Step::READ;
      request_read_(oq_eeprom::IMAGE_ADDRESS, 125U);
      break;
    case Step::READ:
    case Step::READ_STAGED:
    case Step::READ_FINAL:
      offset_ += count_;
      if (offset_ < 512U)
        request_read_(oq_eeprom::IMAGE_ADDRESS + offset_, std::min<uint16_t>(125U, 512U - offset_));
      else
        image_read_done_();
      break;
    case Step::WRITE:
      offset_ += count_;
      if (offset_ < 512U)
        request_block_();
      else {
        step_ = Step::READ_STAGED;
        offset_ = 0U;
        request_read_(oq_eeprom::IMAGE_ADDRESS, 125U);
      }
      break;
    case Step::COMMIT: {
      constexpr uint16_t addresses[]{3999U, 4001U, 4002U, 4000U};
      constexpr uint16_t values[]{5U, 0x0632U, 3U, 0x0102U};
      if (++commit_index_ < 4U) {
        // The ODU can schedule its worker on 4002=3, before 4000 is sent.
        if (addresses[commit_index_] == 4002U) worker_may_run_ = true;
        request_write_(addresses[commit_index_], values[commit_index_]);
      } else {
        step_ = Step::POLL;
        next_ms_ = millis() + 500U;
      }
      break;
    }
    case Step::POLL:
      if (frame_[0] == 2U || frame_[0] == 6U || frame_[0] == 7U) {
        if (frame_[0] == 7U) power_cycle_safe_ = true;
        fail_("COMMIT_FAILED");
        return;
      }
      if (frame_[0] == 4U) {
        power_cycle_safe_ = true;
        step_ = Step::READ_FINAL;
        offset_ = 0U;
        request_read_(oq_eeprom::IMAGE_ADDRESS, 125U);
      } else
        next_ms_ = millis() + 500U;
      break;
    default:
      fail_("INVALID_STATE");
      break;
  }
}
void OpenQuattOduEeprom::image_read_done_() {
  auto& record = record_();
  const auto& image = image_[0];
  if (step_ == Step::READ_STAGED) {
    if (image != record.candidate || !oq_eeprom::valid_crc(image)) {
      fail_("READBACK_FAILED");
      return;
    }
    step_ = Step::COMMIT;
    commit_index_ = 0U;
    data_mode_ = true;
    request_write_(3999U, 5U);
    return;
  }
  if (step_ == Step::READ_FINAL) {
    if (image != record.candidate || !oq_eeprom::valid_crc(image)) {
      fail_("READBACK_FAILED");
      return;
    }
    if (!persist_(State::WAIT_VERIFY)) {
      fail_("STORAGE_FAILED");
      return;
    }
    cold_seen_ = false;
    finish_("POWER_CYCLE_REQUIRED");
    return;
  }
  // Read the control byte from this transaction, not from the optional
  // defrost editor cache (which is intentionally invalidated after offline).
  if (image[3274U - oq_eeprom::IMAGE_ADDRESS] != 1U) {
    fail_("INVALID_IMAGE");
    return;
  }
  if (action_ == Action::CAPTURE) {
    if (!oq_eeprom::valid_crc(image) ||
        !oq_eeprom::supported_values(image, static_cast<oq_odu::Variant>(identity_.variant))) {
      fail_("INVALID_IMAGE");
      return;
    }
    if (!revoke_profiles_()) {
      fail_("STORAGE_FAILED");
      return;
    }
    record.original = record.baseline = image;
    if (!persist_(State::BASELINE)) {
      fail_("STORAGE_FAILED");
      return;
    }
    oq_eeprom::hold(hp_, false);
    cold_seen_ = false;
    finish_("BACKUP_READY");
    return;
  }
  if (action_ == Action::PREPARE || action_ == Action::RESTORE) {
    if (oq_eeprom::stored_crc(image) != oq_eeprom::stored_crc(record.baseline) ||
        !oq_eeprom::prepare(record, image, groups_, action_ == Action::RESTORE)) {
      fail_("INVALID_IMAGE");
      return;
    }
    if (!persist_(State::PREPARED)) {
      fail_("STORAGE_FAILED");
      return;
    }
    finish_("PREPARED");
    return;
  }
  if (action_ == Action::COMMIT) {
    if (image != record.source) {
      fail_("STALE");
      return;
    }
    if (!revoke_profiles_() || !persist_(State::WRITING)) {
      fail_("STORAGE_FAILED");
      return;
    }
    // Only a validated new transaction can establish that our worker has not
    // been dispatched. A rejected request must not weaken an uncertain job.
    worker_may_run_ = false;
    power_cycle_safe_ = false;
    modification_started_ = true;
    step_ = Step::WRITE;
    offset_ = 0U;
    publish_("WRITING");
    request_block_();
    return;
  }
  if (action_ == Action::VERIFY) {
    if (!oq_eeprom::valid_crc(image)) {
      fail_("READBACK_FAILED");
      return;
    }
    const bool candidate = image == record.candidate;
    if (!candidate && image != record.baseline) {
      fail_("RECOVERY_REQUIRED");
      return;
    }
    record.baseline = image;
    record.verified = candidate ? 1U : 0U;
    if (!persist_(State::BASELINE)) {
      fail_("STORAGE_FAILED");
      return;
    }
    oq_eeprom::hold(hp_, false);
    cold_seen_ = false;
    finish_(candidate ? "VERIFIED" : "NOT_PERSISTED");
    return;
  }
  fail_("INVALID_STATE");
}
void OpenQuattOduEeprom::on_response(std::span<const uint8_t> request, std::span<const uint8_t> response) {
  if (!waiting_ || step_ == Step::IDLE || request.empty() || response.empty() || request[0] != response[0]) return;
  const auto start = modbus::helpers::client_pdu_start_address(request);
  if (!start.has_value() || *start != address_) return;
  response_ok_ = true;
  if (writing_) {
    response_ok_ = request.size() >= 5U && response.size() == 5U &&
                   std::equal(request.begin() + 1, request.begin() + 5, response.begin() + 1);
  } else {
    const auto payload = modbus::helpers::server_pdu_payload(response);
    response_ok_ = payload.size() == count_ * 2U;
    if (response_ok_) {
      if (step_ == Step::IDENTITY) {
        identity_ = {};
        identity_.hp = hp_;
        identity_.board = word(payload, 13U);
        identity_.program = word(payload, 8U);
        identity_.variant = static_cast<uint8_t>(defrost_->variant());
      } else if (step_ == Step::SERIAL) {
        for (size_t i = 0; i < 20U; ++i) identity_.serial[i] = word(payload, i);
      } else if (step_ == Step::GUARD) {
        response_ok_ = word(payload, 0U) == 0U && word(payload, 4U) == 0U;
      } else if (step_ == Step::POLL || step_ == Step::COMMIT_READY || step_ == Step::CHECK_WRITER)
        frame_[0] = word(payload, 0U);
      else
        for (size_t i = 0; i < count_; ++i) {
          const uint16_t value = word(payload, i);
          if (value > 255U) {
            response_ok_ = false;
            break;
          }
          image_[0][offset_ + i] = static_cast<uint8_t>(value);
        }
    }
  }
  response_ = true;
}
void OpenQuattOduEeprom::on_error(std::span<const uint8_t> request, modbus::ExceptionCode error) {
  (void)request;
  (void)error;
  response_ok_ = false;
  response_ = true;
}
bool OpenQuattOduEeprom::on_no_response(std::span<const uint8_t> request) {
  (void)request;
  response_ok_ = false;
  response_ = true;
  return false;  // Never retry a write or commit after a lost acknowledgement.
}
void OpenQuattOduEeprom::on_not_sent(std::span<const uint8_t> request) {
  (void)request;
  response_ok_ = false;
  response_ = true;
}
void OpenQuattOduEeprom::write_status(httpd_req_t* request, bool download) {
  Status snapshot;
  portENTER_CRITICAL(&mux_);
  snapshot = status_;
  const bool details = snapshot.baseline && !snapshot.busy && !downloading_;
  const bool can_download = download && details;
  if (details) downloading_ = true;
  portEXIT_CRITICAL(&mux_);
  if (download && !can_download) {
    httpd_resp_set_status(request, "409 Conflict");
    httpd_resp_sendstr(request, R"({"error":"backup_unavailable"})");
    return;
  }
  char buffer[640];
  const std::string token = download ? std::string{} : csrf();
  std::snprintf(
      buffer, sizeof(buffer),
      R"({"hp":%u,"available":%s,"busy":%s,"supported":%s,"baseline":%s,"cold_seen":%s,"holding":%s,"details_ready":%s,"legacy":%s,"power_cycle_safe":%s,"state":%u,"groups":%u,"changes":%u,"reverted":%u,"crc":%u,"generation":%lu,"result":"%s","csrf_token":"%s")",
      hp_, snapshot.available ? "true" : "false", snapshot.busy ? "true" : "false",
      snapshot.supported ? "true" : "false", snapshot.baseline ? "true" : "false",
      snapshot.cold_seen ? "true" : "false", snapshot.holding ? "true" : "false", details ? "true" : "false",
      snapshot.legacy ? "true" : "false", snapshot.power_cycle_safe ? "true" : "false", snapshot.state, snapshot.groups,
      snapshot.changes, snapshot.reverted, snapshot.crc, static_cast<unsigned long>(snapshot.generation),
      snapshot.result, token.c_str());
  httpd_resp_sendstr_chunk(request, buffer);
  if (details && snapshot.state == static_cast<uint8_t>(State::PREPARED)) {
    httpd_resp_sendstr_chunk(request, R"(,"diff":[)");
    bool first = true;
    for (size_t i = 0; i < 510U; ++i) {
      const auto& r = record_();
      if (r.source[i] == r.candidate[i] && r.baseline[i] == r.candidate[i]) continue;
      std::snprintf(buffer, sizeof(buffer), R"(%s{"address":%u,"stored":%u,"runtime":%u,"target":%u})",
                    first ? "" : ",", static_cast<unsigned>(oq_eeprom::IMAGE_ADDRESS + i), r.baseline[i], r.source[i],
                    r.candidate[i]);
      first = false;
      if (httpd_resp_sendstr_chunk(request, buffer) != ESP_OK) break;
    }
    httpd_resp_sendstr_chunk(request, "]");
  }
  if (can_download) {
    const auto& identity = record_().identity;
    std::snprintf(
        buffer, sizeof(buffer),
        R"(,"format":"openquatt-odu-eeprom-backup-v1","original_crc":%u,"identity":{"board":%u,"program":%u,"variant":%u,"hp":%u,"binding":"%s","serial":[)",
        oq_eeprom::stored_crc(record_().original), identity.board, identity.program, identity.variant, identity.hp,
        oq_eeprom::serial_bound(identity) ? "serial" : "hp-slot");
    httpd_resp_sendstr_chunk(request, buffer);
    for (size_t i = 0; i < identity.serial.size(); ++i) {
      std::snprintf(buffer, sizeof(buffer), "%s%u", i == 0U ? "" : ",", identity.serial[i]);
      if (httpd_resp_sendstr_chunk(request, buffer) != ESP_OK) break;
    }
    httpd_resp_sendstr_chunk(request, R"(]},"original":[)");
    for (size_t i = 0; i < 512U; ++i) {
      std::snprintf(buffer, sizeof(buffer), "%s%u", i == 0U ? "" : ",", record_().original[i]);
      if (httpd_resp_sendstr_chunk(request, buffer) != ESP_OK) break;
    }
    httpd_resp_sendstr_chunk(request, "]");
  }
  httpd_resp_sendstr_chunk(request, "}");
  httpd_resp_send_chunk(request, nullptr, 0);
  if (details) {
    portENTER_CRITICAL(&mux_);
    downloading_ = false;
    portEXIT_CRITICAL(&mux_);
  }
}

}  // namespace esphome::openquatt_odu_eeprom
