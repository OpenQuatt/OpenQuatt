#include "OpenQuattOduDefrost.h"
#include <cstdio>
#include <cstring>
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/log.h"
#include "includes/odu/oq_odu_defrost_status_json.h"

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
        const auto automatic = req->arg("auto_reapply");
        if (!parse_mode(req->arg("mode"), desired) || !parse_expected(req->arg("expected_mode"), expected) ||
            (automatic != "true" && automatic != "false" && automatic != "1" && automatic != "0") ||
            !owner_->supports_mode(desired)) {
          req->send(409, "application/json", R"({"error":"invalid_mode"})");
          return;
        }
        if (expected == -1) {
          req->send(409, "application/json", R"({"error":"stale"})");
          return;
        }
        if (!owner_->enqueue_save(desired, expected, automatic == "true" || automatic == "1")) {
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
}  // namespace

void OpenQuattOduDefrost::setup() {
  if (auth_ == nullptr || !auth_->add_restart_blocker(this, [](void* context) {
        auto* self = static_cast<OpenQuattOduDefrost*>(context);
        // Main-loop caller: operation fields are confined to this owner.
        portENTER_CRITICAL(&self->mux_);
        const bool pending = self->action_pending_;
        portEXIT_CRITICAL(&self->mux_);
        return pending || self->loading_ || self->trigger_ready_ || self->save_ready_ || self->save_writing_ ||
               self->save_verifying_ || self->forced_write_pending_;
      })) {
    this->mark_failed();
    return;
  }
  set_parent(controller_->hub());
  set_address(controller_->device_address());
  if (global_preferences != nullptr) {
    profile_pref_ =
        global_preferences->make_preference<oq_defrost::Profile>(hp_ == 2U ? 0x4f514432U : 0x4f514431U, true);
    consent_pref_ =
        global_preferences->make_preference<oq_defrost::Consent>(hp_ == 2U ? 0x4f514332U : 0x4f514331U, true);
    oq_defrost::Profile stored;
    if (profile_pref_.load(&stored) && oq_defrost::valid_profile(stored)) {
      profile_ = stored;
      profile_available_ = true;
      oq_defrost::Consent consent;
      const bool consent_loaded = consent_pref_.load(&consent);
      consent_authorized_ = oq_defrost::profile_has_consent(stored, consent_loaded, consent);
      profile_state_ = oq_defrost::profile_boot_state(stored, consent_loaded, consent);
    }
  }
  web_server_base::global_web_server_base->add_handler(new Handler(this, hp_));
}

void OpenQuattOduDefrost::set_odu_identity(oq_odu::Variant variant, uint16_t control_board_item) {
  if (variant_ == variant && control_board_item_ == control_board_item) return;
  // Identity changes invalidate all queued traffic before adopting a new target.
  offline();
  portENTER_CRITICAL(&mux_);
  variant_ = variant;
  control_board_item_ = control_board_item;
  pending_ = Action::NONE;
  action_pending_ = false;
  parameters_ = {};
  parameters_.variant = variant;
  diagnostics_ = {};
  snapshot_.loaded = false;
  snapshot_.mode = -1;
  portEXIT_CRITICAL(&mux_);
  if (profile_available_) {
    profile_state_ = oq_defrost::profile_state_after_identity(profile_state_, profile_, variant_, control_board_item_);
    reconcile_due_ms_ = millis() + 5000U;
  }
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
  if (!openquatt_web_auth::normal_web_access_allowed()) return false;
  portENTER_CRITICAL(&mux_);
  const bool accepted = !action_pending_ && !snapshot_.busy;
  if (accepted) {
    pending_ = action;
    action_pending_ = true;
  }
  portEXIT_CRITICAL(&mux_);
  return accepted;
}

bool OpenQuattOduDefrost::enqueue_save(int desired, int expected, bool auto_reapply) {
  if (!openquatt_web_auth::normal_web_access_allowed()) return false;
  portENTER_CRITICAL(&mux_);
  const bool accepted = !action_pending_ && !snapshot_.busy;
  if (accepted) {
    pending_ = Action::SAVE;
    action_pending_ = true;
    save_desired_ = desired;
    save_expected_ = expected;
    pending_auto_reapply_ = auto_reapply;
  }
  portEXIT_CRITICAL(&mux_);
  return accepted;
}

void OpenQuattOduDefrost::loop() {
  Action action;
  int save_desired = -1, save_expected = -1;
  bool save_auto_reapply = false, request_pending = false;
  Snapshot snapshot;
  portENTER_CRITICAL(&mux_);
  action = pending_;
  pending_ = Action::NONE;
  request_pending = action_pending_;
  snapshot = snapshot_;
  if (action == Action::SAVE) {
    save_desired = save_desired_;
    save_expected = save_expected_;
    save_auto_reapply = pending_auto_reapply_;
  }
  portEXIT_CRITICAL(&mux_);
  if (action != Action::NONE && !openquatt_web_auth::normal_web_access_allowed()) {
    portENTER_CRITICAL(&mux_);
    action_pending_ = false;
    portEXIT_CRITICAL(&mux_);
    reject("RECOVERY_CANCELLED");
    action = Action::NONE;
  }
  if (!auth_->restart_requested() && action == Action::NONE && profile_available_ && consent_authorized_ &&
      oq_defrost::profile_reconcile_ready(profile_, profile_state_, variant_, control_board_item_, snapshot.online,
                                          snapshot.fresh, snapshot.identity, busy(), request_pending) &&
      reconcile_due_ms_ != 0U && static_cast<int32_t>(millis() - reconcile_due_ms_) >= 0) {
    reconcile_due_ms_ = millis() + 60000U;
    if (oq_defrost::profile_matches(profile_, variant_, control_board_item_)) {
      // Same guarded save route as a user request; fresh read supplies expected mode.
      portENTER_CRITICAL(&mux_);
      const bool accepted = !action_pending_ && !snapshot_.busy;
      if (accepted) action_pending_ = true;
      portEXIT_CRITICAL(&mux_);
      if (accepted) {
        action = Action::SAVE;
        reconcile_ = true;
        save_desired = profile_.mode;
        save_expected = -1;
        save_auto_reapply = true;
      }
    } else {
      profile_state_ = "IDENTITY_MISMATCH";
    }
  }
  if (action != Action::NONE) {
    if (busy()) {
      finish_profile_(false);
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
        save_auto_reapply_ = save_auto_reapply;
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
  if (forced_write_pending_ && millis() - forced_write_ms_ >= 30000U) {
    clear_tx_queue_for_device();
    forced_write_pending_ = false;
    cycle.result = "WRITE_UNCERTAIN";
  }
}

void OpenQuattOduDefrost::release() {
  if (reserved_) {
    dump_->end_external_operation();
    reserved_ = false;
  }
}
void OpenQuattOduDefrost::offline() {
  portENTER_CRITICAL(&mux_);
  pending_ = Action::NONE;
  action_pending_ = false;
  snapshot_.online = snapshot_.fresh = snapshot_.identity = snapshot_.loaded = snapshot_.busy = false;
  snapshot_.mode = -1;
  portEXIT_CRITICAL(&mux_);
  clear_tx_queue_for_device();
  cycle.offline();
  parameters_.loaded = loading_ = trigger_ready_ = false;
  save_after_load_ = save_ready_ = save_writing_ = save_verifying_ = false;
  for (auto& seen : sample_seen_) seen = false;
  diagnostics_ = {};
  if (profile_available_ && !oq_defrost::profile_inhibited(profile_state_)) profile_state_ = "PENDING";
  reconcile_ = false;
  release();
}
void OpenQuattOduDefrost::fail_(const char* reason) {
  loading_ = trigger_ready_ = false;
  save_after_load_ = save_ready_ = save_writing_ = save_verifying_ = false;
  cycle.result = reason;
  finish_profile_(false);
  release();
}
void OpenQuattOduDefrost::reject(const char* reason) {
  cycle.result = reason;
  finish_profile_(false);
  release();
  ESP_LOGW("quatt.defrost", "HP%u request: %s", hp_, reason);
}
void OpenQuattOduDefrost::confirm_saved() {
  if (!persist_profile_()) {
    fail_("PERSIST_FAILED");
    return;
  }
  save_after_load_ = save_ready_ = save_writing_ = save_verifying_ = false;
  cycle.result = "SAVED";
  finish_profile_(true);
  ESP_LOGI("quatt.defrost", "HP%u defrost mode already %d; no write needed", hp_, parameters_.mode());
  release();
}
bool OpenQuattOduDefrost::persist_profile_() {
  if (reconcile_) return oq_defrost::profile_matches(profile_, variant_, control_board_item_);
  const auto candidate = oq_defrost::make_profile(save_desired_, variant_, control_board_item_, save_auto_reapply_);
  consent_authorized_ = false;
  if (!oq_defrost::valid_profile(candidate) || global_preferences == nullptr) {
    profile_state_ = "REVOKE_FAILED";
    return false;
  }
  struct Store {
    ESPPreferenceObject& profile;
    ESPPreferenceObject& consent;
    bool save_profile(const oq_defrost::Profile& value) { return profile.save(&value); }
    bool load_profile(oq_defrost::Profile& value) { return profile.load(&value); }
    bool save_consent(const oq_defrost::Consent& value) { return consent.save(&value); }
    bool load_consent(oq_defrost::Consent& value) { return consent.load(&value); }
    bool sync() { return global_preferences->sync(); }
  } store{profile_pref_, consent_pref_};
  const auto result = oq_defrost::save_profile_transaction(store, candidate);
  if (result != oq_defrost::ProfileSaveResult::SAVED) {
    profile_state_ = result == oq_defrost::ProfileSaveResult::REVOKE_FAILED ? "REVOKE_FAILED" : "PERSIST_FAILED";
    return false;
  }
  profile_ = candidate;
  profile_available_ = true;
  consent_authorized_ = candidate.flags == 1U;
  profile_state_ = "PENDING";
  return true;
}
void OpenQuattOduDefrost::finish_profile_(bool verified) {
  if (profile_available_ && !oq_defrost::profile_inhibited(profile_state_))
    profile_state_ = verified ? "IN_SYNC" : "PENDING";
  reconcile_ = false;
  if (profile_available_ && consent_authorized_) reconcile_due_ms_ = millis() + 60000U;
}
bool OpenQuattOduDefrost::take_trigger() {
  if (trigger_ready_ && !openquatt_web_auth::normal_web_access_allowed()) {
    trigger_ready_ = false;
    reject("RECOVERY_CANCELLED");
    return false;
  }
  // Finish all previously accepted traffic before the single forced command.
  auto* hub = controller_->hub();
  if (hub == nullptr || !hub->tx_buffer_empty() || hub->tx_blocked()) return false;
  const bool value = trigger_ready_;
  trigger_ready_ = false;
  return value;
}
bool OpenQuattOduDefrost::take_save(int& desired, int& expected) {
  if (save_ready_ && !reconcile_ && !openquatt_web_auth::normal_web_access_allowed()) {
    save_ready_ = false;
    reject("RECOVERY_CANCELLED");
    return false;
  }
  auto* hub = controller_->hub();
  if (hub == nullptr || !hub->tx_buffer_empty() || hub->tx_blocked()) return false;
  if (!save_ready_) return false;
  desired = save_desired_;
  expected = save_expected_;
  save_ready_ = false;
  return true;
}
bool OpenQuattOduDefrost::send_forced_once(uint32_t now) {
  if (!openquatt_web_auth::normal_web_access_allowed()) {
    reject("RECOVERY_CANCELLED");
    return false;
  }
  // Called only by the actuator after its current guards. ModbusClientDevice
  // deliberately does not retry this non-idempotent write on a lost response.
  clear_tx_queue_for_address();
  cycle.mode_seen = cycle.bit_seen = false;  // Only post-command polls may confirm acceptance/completion.
  cycle.request(now);
  forced_write_pending_ = true;
  forced_write_ms_ = now;
  if (!write_single_register(3999U, 4U)) {
    forced_write_pending_ = false;
    cycle.phase = oq_defrost::Phase::RESYNC;
    reject("WRITE_FAILED");
    return false;
  }
  ESP_LOGI("quatt.defrost", "HP%u manual request sent once (mode %d)", hp_, parameters_.mode());
  return true;
}
bool OpenQuattOduDefrost::send_mode_once(int desired, uint32_t now) {
  if (!reconcile_ && !openquatt_web_auth::normal_web_access_allowed()) {
    reject("RECOVERY_CANCELLED");
    return false;
  }
  // Single defrost-mode write without retry; readback must prove success.
  if (!oq_defrost::is_supported_defrost_mode(desired, variant_)) {
    reject("INVALID_MODE");
    return false;
  }
  if (!persist_profile_()) {
    fail_("PERSIST_FAILED");
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
      finish_profile_(false);
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
      finish_profile_(true);
      ESP_LOGI("quatt.defrost", "HP%u defrost mode saved and verified (%d)", hp_, save_write_);
    } else {
      cycle.result = "WRITE_FAILED";
      finish_profile_(false);
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
  if (save_ready_ && reconcile_) save_expected_ = parameters_.mode();
  if (save_ready_) save_ms_ = millis();
  cycle.result = (trigger_ready_ || save_ready_) ? "CHECKING" : "LOADED";
  if (!trigger_ready_ && !save_ready_ && profile_available_ && !oq_defrost::profile_inhibited(profile_state_)) {
    profile_state_ = !oq_defrost::profile_matches(profile_, variant_, control_board_item_)
                         ? "IDENTITY_MISMATCH"
                         : (parameters_.mode() == profile_.mode ? "IN_SYNC" : "PENDING");
  }
  if (!trigger_ready_ && !save_ready_) release();
}
void OpenQuattOduDefrost::on_response(std::span<const uint8_t> request, std::span<const uint8_t> response) {
  if (request.size() == 5 && request[0] == 0x06 && request[1] == 0x0f && request[2] == 0x9f)
    forced_write_pending_ = false;
  modbus::ModbusClientDevice::on_response(request, response);
}
void OpenQuattOduDefrost::on_not_sent(std::span<const uint8_t> request) {
  if (request.size() == 5 && request[0] == 0x06 && request[1] == 0x0f && request[2] == 0x9f)
    forced_write_pending_ = false;
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
  values.profile_available = profile_available_;
  values.auto_reapply = profile_available_ && consent_authorized_;
  values.desired_mode = profile_available_ ? profile_.mode : -1;
  values.profile_state = profile_state_;
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
  const auto token = csrf();
  // ESP-IDF httpd_resp_send_chunk consumes the buffer before returning.
  // No shared scratch or allocation is needed; the locked snapshot stays immutable.
  const bool sent = oq_defrost::write_status_json(
      s, hp_, static_cast<unsigned>(variant), pending, token.c_str(),
      [req](const char* chunk, size_t size) { return httpd_resp_send_chunk(req, chunk, size) == ESP_OK; });
  if (!sent) ESP_LOGW("odu_defrost", "Status response interrupted or exceeded its bounded chunk");
  httpd_resp_send_chunk(req, nullptr, 0);
}
}  // namespace esphome::openquatt_odu_defrost
