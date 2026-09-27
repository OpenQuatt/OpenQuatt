#include "OpenQuattOduDefrost.h"
#include <cstdio>
#include <cstring>
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/log.h"

namespace esphome::openquatt_odu_defrost {
namespace {
bool same_origin(AsyncWebServerRequest* request) {
  const auto host = request->get_header("Host");
  if (!host || host->empty()) return false;
  for (const char* name : {"Origin", "Referer"}) {
    const auto header = request->get_header(name);
    if (!header) continue;
    const auto start = header->find("://");
    if (start == std::string::npos) return false;
    const auto end = header->find_first_of("/?#", start + 3);
    if (header->substr(start + 3, end == std::string::npos ? end : end - start - 3) != *host) return false;
  }
  return true;
}
class Handler : public AsyncWebHandler {
 public:
  Handler(OpenQuattOduDefrost* owner, uint8_t hp) : owner_(owner) {
    snprintf(path_, sizeof(path_), "/openquatt/odu-defrost/hp%u/", hp);
  }
  bool canHandle(AsyncWebServerRequest* req) const override {
    char url[AsyncWebServerRequest::URL_BUF_SIZE];
    req->url_to(url);
    if (strncmp(url, path_, strlen(path_)) != 0) return false;
    const char* action = url + strlen(path_);
    return (req->method() == HTTP_GET && strcmp(action, "status") == 0) ||
           (req->method() == HTTP_POST &&
            (strcmp(action, "load") == 0 || strcmp(action, "trigger") == 0 || strcmp(action, "save") == 0));
  }
  static bool parse_mode(const std::string& raw, int& mode) {
    if (raw == "0")
      mode = 0;
    else if (raw == "1")
      mode = 1;
    else if (raw == "3")
      mode = 3;
    else if (raw == "4")
      mode = 4;
    else
      return false;
    return true;
  }
  static bool parse_expected(const std::string& raw, int& mode) {
    if (raw == "-1")
      mode = -1;
    else if (raw == "0")
      mode = 0;
    else if (raw == "1")
      mode = 1;
    else if (raw == "2")
      mode = 2;
    else if (raw == "3")
      mode = 3;
    else if (raw == "4")
      mode = 4;
    else
      return false;
    return true;
  }
  void handleRequest(AsyncWebServerRequest* req) override {
    if (!owner_->authenticated(req)) {
      req->requestAuthentication();
      return;
    }
    if (req->method() == HTTP_POST) {
      const auto csrf = owner_->csrf();
      if (!same_origin(req) || csrf.empty() || req->arg("csrf_token") != csrf) {
        // ESPHome's integer status adapter does not map 403 on ESP-IDF.
        httpd_resp_set_status(*req, "403 Forbidden");
        httpd_resp_set_type(*req, "application/json");
        httpd_resp_send(*req, R"({"error":"forbidden"})", HTTPD_RESP_USE_STRLEN);
        return;
      }
      char url[AsyncWebServerRequest::URL_BUF_SIZE];
      req->url_to(url);
      const char* action_name = url + strlen(path_);
      if (strcmp(action_name, "save") == 0) {
        int desired = -1, expected = -2;
        if (!parse_mode(req->arg("mode"), desired) || !parse_expected(req->arg("expected_mode"), expected) ||
            !owner_->supports_mode(desired)) {
          req->send(409, "application/json", R"({"error":"invalid_mode"})");
          return;
        }
        if (expected == -1) {
          req->send(409, "application/json", R"({"error":"stale"})");
          return;
        }
        if (!owner_->enqueue_save(desired, expected)) {
          req->send(409, "application/json", R"({"error":"busy"})");
          return;
        }
      } else {
        const auto action = strcmp(action_name, "trigger") == 0 ? OpenQuattOduDefrost::Action::TRIGGER
                                                                : OpenQuattOduDefrost::Action::LOAD;
        if (!owner_->enqueue(action)) {
          req->send(409, "application/json", R"({"error":"busy"})");
          return;
        }
      }
    }
    httpd_req_t* raw = *req;
    httpd_resp_set_type(raw, "application/json; charset=utf-8");
    httpd_resp_set_hdr(raw, "Cache-Control", "no-store");
    owner_->write_status(raw);
  }

 private:
  OpenQuattOduDefrost* owner_;
  char path_[40]{};
};
const char* boolean(bool value) { return value ? "true" : "false"; }
void number(httpd_req_t* req, float value) {
  char out[32];
  if (std::isfinite(value))
    snprintf(out, sizeof(out), "%.2f", static_cast<double>(value));
  else
    snprintf(out, sizeof(out), "null");
  httpd_resp_send_chunk(req, out, strlen(out));
}
}  // namespace

void OpenQuattOduDefrost::setup() {
  set_parent(controller_->hub());
  set_address(controller_->device_address());
  web_server_base::global_web_server_base->add_handler(new Handler(this, hp_));
}

void OpenQuattOduDefrost::set_odu_identity(oq_odu::Variant variant) {
  portENTER_CRITICAL(&mux_);
  variant_ = variant;
  parameters_ = {};
  parameters_.variant = variant;
  diagnostics_ = {};
  snapshot_.loaded = false;
  snapshot_.mode = -1;
  portEXIT_CRITICAL(&mux_);
}

oq_odu::Variant OpenQuattOduDefrost::variant() const {
  portENTER_CRITICAL(&mux_);
  const auto value = variant_;
  portEXIT_CRITICAL(&mux_);
  return value;
}

bool OpenQuattOduDefrost::supports_mode(int mode) const {
  return oq_defrost::is_supported_defrost_mode(mode, variant());
}

bool OpenQuattOduDefrost::enqueue(Action action) {
  portENTER_CRITICAL(&mux_);
  const bool accepted = !action_pending_ && !snapshot_.busy;
  if (accepted) {
    pending_ = action;
    action_pending_ = true;
  }
  portEXIT_CRITICAL(&mux_);
  return accepted;
}

bool OpenQuattOduDefrost::enqueue_save(int desired, int expected) {
  portENTER_CRITICAL(&mux_);
  const bool accepted = !action_pending_ && !snapshot_.busy;
  if (accepted) {
    pending_ = Action::SAVE;
    action_pending_ = true;
    save_desired_ = desired;
    save_expected_ = expected;
  }
  portEXIT_CRITICAL(&mux_);
  return accepted;
}

void OpenQuattOduDefrost::loop() {
  Action action;
  int save_desired = -1, save_expected = -1;
  portENTER_CRITICAL(&mux_);
  action = pending_;
  pending_ = Action::NONE;
  if (action == Action::SAVE) {
    save_desired = save_desired_;
    save_expected = save_expected_;
  }
  portEXIT_CRITICAL(&mux_);
  if (action != Action::NONE) {
    if (busy()) {
      ESP_LOGW("quatt.defrost", "HP%u request ignored: busy", hp_);
    } else if (!dump_->try_begin_external_operation()) {
      reject("BUSY");
    } else {
      reserved_ = true;
      parameters_ = {};
      parameters_.variant = variant();
      block_ = 0;
      loading_ = true;
      loading_ms_ = millis();
      trigger_after_load_ = action == Action::TRIGGER;
      save_after_load_ = action == Action::SAVE;
      if (save_after_load_) {
        save_desired_ = save_desired;
        save_expected_ = save_expected;
      }
      cycle.result = "LOADING";
      if (!read_block_()) fail_("READ_FAILED");
    }
    portENTER_CRITICAL(&mux_);
    action_pending_ = false;
    snapshot_.busy = busy();
    snapshot_.state = cycle.result;
    portEXIT_CRITICAL(&mux_);
  }
  if (loading_ && millis() - loading_ms_ >= 30000U) {
    clear_tx_queue_for_device();
    fail_("READ_FAILED");
  }
  if (trigger_ready_ && millis() - loading_ms_ >= 30000U) fail_("BUSY");
  if ((save_ready_ || save_writing_ || save_verifying_) && millis() - save_ms_ >= 30000U) {
    clear_tx_queue_for_device();
    save_ready_ = save_writing_ = save_verifying_ = false;
    fail_("WRITE_UNCERTAIN");
  }
}

void OpenQuattOduDefrost::release() {
  if (reserved_) {
    dump_->end_external_operation();
    reserved_ = false;
  }
}
void OpenQuattOduDefrost::offline() {
  clear_tx_queue_for_device();
  cycle.offline();
  parameters_.loaded = loading_ = trigger_ready_ = false;
  save_after_load_ = save_ready_ = save_writing_ = save_verifying_ = false;
  for (auto& seen : sample_seen_) seen = false;
  diagnostics_ = {};
  release();
}
void OpenQuattOduDefrost::fail_(const char* reason) {
  loading_ = trigger_ready_ = false;
  save_after_load_ = save_ready_ = save_writing_ = save_verifying_ = false;
  cycle.result = reason;
  release();
}
void OpenQuattOduDefrost::reject(const char* reason) {
  cycle.result = reason;
  release();
  ESP_LOGW("quatt.defrost", "HP%u request: %s", hp_, reason);
}
void OpenQuattOduDefrost::confirm_saved() {
  save_after_load_ = save_ready_ = save_writing_ = save_verifying_ = false;
  cycle.result = "SAVED";
  ESP_LOGI("quatt.defrost", "HP%u defrost mode already %d; no write needed", hp_, parameters_.mode());
  release();
}
bool OpenQuattOduDefrost::take_trigger() {
  // Finish all previously accepted traffic before the single forced command.
  auto* hub = controller_->hub();
  if (hub == nullptr || !hub->tx_buffer_empty() || hub->tx_blocked()) return false;
  const bool value = trigger_ready_;
  trigger_ready_ = false;
  return value;
}
bool OpenQuattOduDefrost::take_save(int& desired, int& expected) {
  auto* hub = controller_->hub();
  if (hub == nullptr || !hub->tx_buffer_empty() || hub->tx_blocked()) return false;
  if (!save_ready_) return false;
  desired = save_desired_;
  expected = save_expected_;
  save_ready_ = false;
  return true;
}
bool OpenQuattOduDefrost::send_forced_once(uint32_t now) {
  // Called only by the actuator after its current guards. ModbusClientDevice
  // deliberately does not retry this non-idempotent write on a lost response.
  clear_tx_queue_for_address();
  cycle.mode_seen = cycle.bit_seen = false;  // Only post-command polls may confirm acceptance/completion.
  cycle.request(now);
  if (!write_single_register(3999U, 4U)) {
    cycle.phase = oq_defrost::Phase::RESYNC;
    reject("WRITE_FAILED");
    return false;
  }
  ESP_LOGI("quatt.defrost", "HP%u manual request sent once (mode %d)", hp_, parameters_.mode());
  return true;
}
bool OpenQuattOduDefrost::send_mode_once(int desired, uint32_t now) {
  // Single defrost-mode write without retry; readback must prove success.
  if (!oq_defrost::is_supported_defrost_mode(desired, variant_)) {
    reject("INVALID_MODE");
    return false;
  }
  clear_tx_queue_for_address();
  save_write_ = desired;
  save_writing_ = true;
  save_verifying_ = false;
  save_ms_ = now;
  if (!write_single_register(oq_defrost::MODE_REGISTER, static_cast<uint16_t>(desired))) {
    save_writing_ = false;
    reject("WRITE_FAILED");
    return false;
  }
  ESP_LOGI("quatt.defrost", "HP%u defrost mode write sent once (%d)", hp_, desired);
  // Queue the readback immediately; the hub sends it after the write response.
  save_verifying_ = true;
  if (!read_base_()) {
    save_writing_ = save_verifying_ = false;
    reject("WRITE_UNCERTAIN");
    return false;
  }
  return true;
}
bool OpenQuattOduDefrost::normal_write_allowed(int value, bool safety_stop) {
  if (value == 0 && safety_stop) {
    if (holding()) {
      clear_tx_queue_for_device();
      loading_ = trigger_ready_ = false;
      save_after_load_ = save_ready_ = save_writing_ = save_verifying_ = false;
      cycle.safety_stop();
      release();
    }
    return true;
  }
  return cycle.fresh(millis()) && !cycle.observed_active() && !holding();
}
uint8_t OpenQuattOduDefrost::parameter_block_count_() const {
  return oq_defrost::has_mode4_defrost(variant_) ? 4U : 3U;
}
bool OpenQuattOduDefrost::read_block_() {
  constexpr uint16_t addresses[]{3270, 3307, 3336, 3414};
  constexpr uint16_t counts[]{11, 9, 6, 14};
  if (block_ >= parameter_block_count_()) return false;
  return read_holding_registers(addresses[block_], counts[block_]);
}
bool OpenQuattOduDefrost::read_base_() {
  block_ = 0;
  return read_holding_registers(3270, 11);
}
void OpenQuattOduDefrost::on_read_holding_registers(uint16_t address, std::span<const uint16_t> values,
                                                    modbus::ResponseStatus status) {
  if (save_verifying_) {
    if (status.has_value() || address != 3270 || values.size() != 11) {
      save_writing_ = save_verifying_ = false;
      fail_("READ_FAILED");
      return;
    }
    std::copy(values.begin(), values.end(), parameters_.base.data());
    parameters_.loaded = true;
    save_writing_ = save_verifying_ = false;
    if (parameters_.mode() == save_write_) {
      cycle.result = "SAVED";
      ESP_LOGI("quatt.defrost", "HP%u defrost mode saved and verified (%d)", hp_, save_write_);
    } else {
      cycle.result = "WRITE_FAILED";
      ESP_LOGW("quatt.defrost", "HP%u mode readback differs (wanted %d, got %d)", hp_, save_write_, parameters_.mode());
    }
    release();
    return;
  }
  if (!loading_) return;
  constexpr uint16_t addresses[]{3270, 3307, 3336, 3414};
  constexpr uint16_t counts[]{11, 9, 6, 14};
  if (status.has_value() || address != addresses[block_] || values.size() != counts[block_]) {
    fail_("READ_FAILED");
    return;
  }
  uint16_t* destinations[]{parameters_.base.data(), parameters_.timing.data(), parameters_.coil.data(),
                           parameters_.delta.data()};
  std::copy(values.begin(), values.end(), destinations[block_]);
  if (++block_ < parameter_block_count_()) {
    if (!read_block_()) fail_("READ_FAILED");
    return;
  }
  parameters_.loaded = true;
  loading_ = false;
  trigger_ready_ = trigger_after_load_;
  save_ready_ = save_after_load_;
  if (save_ready_) save_ms_ = millis();
  cycle.result = (trigger_ready_ || save_ready_) ? "CHECKING" : "LOADED";
  if (!trigger_ready_ && !save_ready_) release();
}
void OpenQuattOduDefrost::on_response(std::span<const uint8_t> request, std::span<const uint8_t> response) {
  modbus::ModbusClientDevice::on_response(request, response);
}
void OpenQuattOduDefrost::on_not_sent(std::span<const uint8_t>) {
  if (loading_)
    fail_("READ_FAILED");
  else if (save_writing_ || save_verifying_) {
    save_writing_ = save_verifying_ = save_ready_ = false;
    fail_("WRITE_UNCERTAIN");
  } else
    cycle.result = "WRITE_UNCERTAIN";
}
bool OpenQuattOduDefrost::on_no_response(std::span<const uint8_t> request) {
  on_not_sent(request);
  return false;
}
void OpenQuattOduDefrost::on_error(std::span<const uint8_t> request, modbus::ExceptionCode) { on_not_sent(request); }

void OpenQuattOduDefrost::update(Snapshot values, uint32_t now) {
  values.loaded = loaded();
  values.automatic = automatic();
  values.mode = automatic() ? parameters_.mode() : -1;
  values.fresh = cycle.fresh(now);
  values.operation_mode = values.fresh ? cycle.mode : -1;
  values.active = cycle.phase == oq_defrost::Phase::ACTIVE;
  values.manual = cycle.manual;
  values.busy = busy();
  values.state = cycle.result;
  values.elapsed_s = values.active ? static_cast<int>((now - cycle.started_ms) / 1000U) : -1;
  values.since_s = cycle.history_known ? static_cast<int>((now - cycle.ended_ms) / 1000U) : -1;
  values.duration_s = cycle.history_known ? static_cast<int>(cycle.duration_s) : -1;
  if (!values.fresh) values.ambient = values.coil = values.evaporation = values.hz = NAN;
  if (!sample_fresh(0, now)) values.hz = NAN;
  if (!sample_fresh(1, now)) values.ambient = NAN;
  if (!sample_fresh(2, now)) values.coil = NAN;
  if (!sample_fresh(3, now)) values.evaporation = NAN;
  if (cycle.phase != previous_phase_) {
    ESP_LOGI("quatt.defrost", "HP%u %s; mode=%d ambient=%.1f coil=%.1f evap=%.1f Hz=%.0f", hp_, cycle.result,
             values.mode, values.ambient, values.coil, values.evaporation, values.hz);
    if (cycle.phase == oq_defrost::Phase::RESYNC) {
      clear_tx_queue_for_device();  // No latent forced command may outlive its acceptance window.
      if (previous_phase_ == oq_defrost::Phase::ACTIVE && cycle.continuous && !cycle.interrupted)
        diagnostics_.ended(cycle.duration_s);
      else
        diagnostics_.end_reason = "UNKNOWN";
    }
    previous_phase_ = cycle.phase;
  }
  diagnostics_.sample(parameters_, values.fresh, values.active, values.ambient, values.coil, values.evaporation,
                      values.hz, now);
  values.diagnostics = diagnostics_;
  portENTER_CRITICAL(&mux_);
  snapshot_ = values;
  portEXIT_CRITICAL(&mux_);
}

void OpenQuattOduDefrost::write_status(httpd_req_t* req) {
  Snapshot s;
  bool pending;
  oq_odu::Variant variant;
  portENTER_CRITICAL(&mux_);
  s = snapshot_;
  pending = action_pending_;
  variant = variant_;
  portEXIT_CRITICAL(&mux_);
  char out[640];
  snprintf(
      out, sizeof(out),
      R"({"hp":%u,"online":%s,"fresh":%s,"identity_ready":%s,"variant":%u,"loaded":%s,"auto_defrost_control_ok":%s,"busy":%s,"active":%s,"manual":%s,"can_trigger":%s,"defrost_mode":%d,"operation_mode":%d,"state":"%s","guard":"%s","elapsed_s":%d,"since_last_s":%d,"last_duration_s":%d,"confidence":"limited","end_reason":"unknown","ambient_c":)",
      hp_, boolean(s.online), boolean(s.fresh), boolean(s.identity), static_cast<unsigned>(variant), boolean(s.loaded),
      boolean(s.automatic), boolean(s.busy || pending), boolean(s.active), boolean(s.manual),
      boolean(s.can_trigger && !pending), s.mode, s.operation_mode, pending ? "QUEUED" : s.state, s.guard, s.elapsed_s,
      s.since_s, s.duration_s);
  httpd_resp_send_chunk(req, out, strlen(out));
  number(req, s.ambient);
  httpd_resp_send_chunk(req, ",\"coil_c\":", HTTPD_RESP_USE_STRLEN);
  number(req, s.coil);
  httpd_resp_send_chunk(req, ",\"evaporation_c\":", HTTPD_RESP_USE_STRLEN);
  number(req, s.evaporation);
  httpd_resp_send_chunk(req, ",\"compressor_hz\":", HTTPD_RESP_USE_STRLEN);
  number(req, s.hz);
  httpd_resp_send_chunk(req, ",\"delta_k\":", HTTPD_RESP_USE_STRLEN);
  number(req, s.ambient - s.evaporation);
  httpd_resp_send_chunk(req, ",\"start_threshold_c\":", HTTPD_RESP_USE_STRLEN);
  number(req, s.diagnostics.start_c);
  httpd_resp_send_chunk(req, ",\"delta_required_k\":", HTTPD_RESP_USE_STRLEN);
  number(req, s.diagnostics.delta_k);
  httpd_resp_send_chunk(req, ",\"exit_threshold_c\":", HTTPD_RESP_USE_STRLEN);
  number(req, s.diagnostics.exit_c);
  httpd_resp_send_chunk(req, ",\"alternate_exit_c\":", HTTPD_RESP_USE_STRLEN);
  number(req, s.diagnostics.alternate_exit_c);
  snprintf(
      out, sizeof(out),
      R"(,"confirmation_s":%d,"confirmation_required_s":%d,"runtime_s":%d,"minimum_runtime_s":%d,"interval_s":%d,"max_duration_s":%d,"exit_confirmation_s":%d,"exit_required_s":%d,"inferred_end_reason":"%s")",
      s.diagnostics.confirm_s, s.diagnostics.confirm_required_s, s.diagnostics.runtime_s,
      s.diagnostics.minimum_runtime_s, s.diagnostics.interval_s, s.diagnostics.max_duration_s,
      s.diagnostics.exit_confirm_s, s.diagnostics.exit_required_s, s.diagnostics.end_reason);
  httpd_resp_send_chunk(req, out, strlen(out));
  // Auth creates a hexadecimal token; no user-controlled string enters this JSON.
  const auto token = csrf();
  httpd_resp_send_chunk(req, ",\"csrf_token\":\"", HTTPD_RESP_USE_STRLEN);
  httpd_resp_send_chunk(req, token.c_str(), token.size());
  httpd_resp_send_chunk(req, "\"}", 2);
  httpd_resp_send_chunk(req, nullptr, 0);
}
}  // namespace esphome::openquatt_odu_defrost
