#include "OpenQuattRecovery.h"

#include <cstdio>
#include "esp_http_server.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_rom_crc.h"
#include "lwip/sockets.h"

#include "recovery_page.h"
#include "esphome/core/helpers.h"
#include "esphome/core/application.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/api/api_connection.h"
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace esphome::openquatt_recovery {

struct RecoveryHandoff {
  uint32_t magic;
  uint32_t version;
  uint32_t generation;
  uint32_t checksum;
};
static RTC_NOINIT_ATTR RecoveryHandoff handoff;
static constexpr uint32_t HANDOFF_MAGIC = 0x4F515243;
#ifdef USE_WIFI
static constexpr bool WIFI_CAPABLE = true;
#else
static constexpr bool WIFI_CAPABLE = false;
#endif

static void send_json(AsyncWebServerRequest* request, const char* status, const char* body) {
  auto* response = request->beginResponse(200, "application/json", body);
  // ESPHome 2026.9's numeric status switch does not include 202 or 403.
  httpd_resp_set_status(*request, status);
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("Access-Control-Allow-Origin", "");
  request->send(response);
}

void OpenQuattRecovery::prepare_reboot_handoff_() {
  handoff = {HANDOFF_MAGIC, 1, this->state_.generation(), 0};
  handoff.checksum = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&handoff), 12);
}

std::string OpenQuattRecovery::random_token_() {
  char token[33];
  std::snprintf(token, sizeof(token), "%08x%08x%08x%08x", static_cast<unsigned>(esp_random()),
                static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()),
                static_cast<unsigned>(esp_random()));
  return token;
}

void OpenQuattRecovery::setup() {
  web_server_base::global_web_server_base->add_handler_without_auth(this);
  const RecoveryHandoff saved = handoff;
  handoff.magic = 0;  // Consume once, including invalid/non-software-reset markers.
  if (esp_reset_reason() == ESP_RST_SW && saved.magic == HANDOFF_MAGIC && saved.version == 1 &&
      saved.checksum == esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&saved), 12)) {
    this->state_.restore(millis(), saved.generation);
    // Setup precedes normal web-server registration/listening.
    this->auth_->begin_recovery_guard(random_token_());
    web_server_base::global_web_server_base->set_recovery_active(true);
  }
}

void OpenQuattRecovery::opened_() {
  this->error_ = "";
  if (this->activating_) return;
  this->activating_ = true;
  auto* server = web_server_base::global_web_server_base->get_server();
  if (httpd_queue_work(server->get_server(), activate_on_httpd_, this) != ESP_OK) {
    this->activating_ = false;
    this->state_.end();
    this->error_ = "activation_failed";
  }
}

void OpenQuattRecovery::activate_on_httpd_(void* context) {
  auto* self = static_cast<OpenQuattRecovery*>(context);
  std::lock_guard<std::mutex> lock(self->mutex_);
  auto* base = web_server_base::global_web_server_base;
  // HTTPD serializes this transition after any already-running handler. Never
  // hold a mutex across normal handler socket I/O from the control/main loop.
  if (self->state_.active(millis())) {
    base->set_recovery_active(true);
    self->auth_->begin_recovery_guard(random_token_());
    self->csrf_token_ = random_token_();
    // Enumerate AND shut down on HTTPD: do not queue bare fds that may be reused.
    int sockets[CONFIG_LWIP_MAX_SOCKETS];
    size_t count = CONFIG_LWIP_MAX_SOCKETS;
    if (httpd_get_client_list(base->get_server()->get_server(), &count, sockets) == ESP_OK) {
      for (size_t i = 0; i < count; ++i) shutdown(sockets[i], SHUT_RDWR);
    } else {
      self->error_ = "close_streams_failed";
      // Cancel a physical 10-second reset queued while HTTPD was activating.
      self->pending_ = Action::NONE;
      self->state_.job_failed();
      self->state_.end();
      self->csrf_token_.clear();
      self->auth_->end_recovery_guard();
      base->set_recovery_active(false);
    }
  }
  self->activation_complete_ = true;
}

void OpenQuattRecovery::loop() {
  std::unique_lock<std::mutex> lock(this->mutex_);
  if (this->state_.active(millis()) && this->csrf_token_.empty() && !this->activating_) this->opened_();
  if (this->activation_complete_) {
    // Publish recovery only after the main loop passed its in-flight action.
    this->activation_complete_ = false;
    this->activating_ = false;
  }
  // Do not arm from the binary sensor's default false before its first sample.
  if (this->button_->has_state()) {
    const auto events = this->state_.tick(millis(), this->button_->state, WIFI_CAPABLE);
    if (events.opened) this->opened_();
    if (events.expired) {
      this->csrf_token_.clear();
      this->auth_->end_recovery_guard();
      web_server_base::global_web_server_base->set_recovery_active(false);
    }
    if (events.wifi_reset_requested && this->state_.begin_job(millis(), this->state_.generation())) {
      this->pending_ = Action::WIFI_RESET;
      this->accepted_at_ = millis();
      this->error_ = "";
    }
  }
  const Action action = this->pending_;
  if (action == Action::API_RESET || action == Action::WIFI_RESET) {
    // Allow the accepted HTTP response to leave before changing connectivity.
    if (static_cast<uint32_t>(millis()) - this->accepted_at_ < 500) return;
    this->pending_ = Action::NONE;
    auto* api = api::global_api_server;
    bool cleared = false;
    if (action == Action::API_RESET) cleared = api->clear_noise_psk(false);
#ifdef USE_WIFI
    if (action == Action::WIFI_RESET) cleared = wifi::global_wifi_component->clear_saved_sta_checked();
#endif
    if (!cleared) {
      this->error_ = "persist_failed";
      this->state_.job_failed();
      return;
    }
    if (this->state_.active(millis())) this->prepare_reboot_handoff_();
    // safe_reboot tears down API clients by calling APIServer::loop(). Mark
    // them first so teardown cannot process a late set-key packet after clear.
    for (auto& client : api->active_clients()) client->on_fatal_error();
    lock.unlock();
    App.safe_reboot();
    return;
  }
  this->pending_ = Action::NONE;
  if (action == Action::NONE) return;
  if (action == Action::WEB_AUTH) {
    this->error_ = this->auth_->set_runtime_credentials(this->username_, this->password_) ? "" : "persist_failed";
    this->username_.clear();
    this->password_.clear();
  }
  this->state_.job_failed();  // Release the single reservation, including successful non-reboot actions.
  if (action == Action::END) {
    this->state_.end();
    this->csrf_token_.clear();
    this->auth_->end_recovery_guard();
    web_server_base::global_web_server_base->set_recovery_active(false);
  }
}

bool OpenQuattRecovery::canHandle(AsyncWebServerRequest* request) const {
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  const auto url = request->url_to(buffer);
  return (request->method() == HTTP_GET && (url == "/recovery" || url == "/recovery/status")) ||
         (request->method() == HTTP_POST && (url == "/recovery/web-auth" || url == "/recovery/end" ||
                                             url == "/api-security/reset" || url == "/wifi/reset"));
}

bool OpenQuattRecovery::authorize_(AsyncWebServerRequest* request, uint32_t now) const {
  const auto host = request->get_header("Host");
  const auto origin = request->get_header("Origin");
  return !this->activating_ && this->state_.active(now) && !this->state_.busy() && host.has_value() && !host->empty() &&
         origin.has_value() && *origin == "http://" + *host &&
         request->arg("generation") == std::to_string(this->state_.generation()) && !this->csrf_token_.empty() &&
         request->arg("csrf_token") == this->csrf_token_;
}

void OpenQuattRecovery::handleRequest(AsyncWebServerRequest* request) {
  std::unique_lock<std::mutex> lock(this->mutex_);
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  const auto url = request->url_to(buffer);
  const uint32_t now = millis();
  if (url == "/recovery") {
    auto* response = request->beginResponse(200, "text/html; charset=utf-8", RECOVERY_PAGE);
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Frame-Options", "DENY");
    response->addHeader("Access-Control-Allow-Origin", "");
    lock.unlock();
    request->send(response);
    return;
  }
  if (url == "/recovery/status") {
    const bool active = !this->activating_ && this->state_.active(now);
    auto* response = request->beginResponseStream("application/json");
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("Access-Control-Allow-Origin", "");
    response->printf(
        R"({"active":%s,"expires_in_ms":%u,"generation":%u,"csrf_token":"%s","button_held":%s,"next_threshold_ms":%u,"busy":%s,"error":"%s","capabilities":{"web_auth":true,"api_reset":true,"wifi_reset":%s}})",
        active ? "true" : "false", static_cast<unsigned>(this->state_.remaining_ms(now)),
        static_cast<unsigned>(this->state_.generation()), active ? this->csrf_token_.c_str() : "",
        this->state_.button_held() ? "true" : "false",
        static_cast<unsigned>(this->state_.next_threshold_ms(now, WIFI_CAPABLE)),
        this->state_.busy() ? "true" : "false", this->error_, WIFI_CAPABLE ? "true" : "false");
    lock.unlock();
    request->send(response);
    return;
  }
  const bool physical = this->authorize_(request, now);
  if (url == "/api-security/reset" || url == "/wifi/reset") {
    const bool wifi_reset = url == "/wifi/reset";
    const auto host = request->get_header("Host");
    const auto origin = request->get_header("Origin");
    const bool admin = !this->state_.busy() && host.has_value() && !host->empty() && origin.has_value() &&
                       *origin == "http://" + *host && this->auth_->request_is_authenticated_admin(request) &&
                       request->arg("csrf_token") == this->auth_->get_csrf_token();
    if ((!physical && !admin) || (wifi_reset && !WIFI_CAPABLE) ||
        request->arg("confirm") != (wifi_reset ? "RESET_WIFI" : "RESET_API_SECURITY")) {
      lock.unlock();
      send_json(request, "403 Forbidden", R"({"error":"confirmation_required"})");
      return;
    }
    if (physical)
      this->state_.begin_job(now, this->state_.generation());
    else
      this->state_.begin_admin_job();
    this->pending_ = wifi_reset ? Action::WIFI_RESET : Action::API_RESET;
    this->accepted_at_ = now;
    this->error_ = "";
    lock.unlock();
    send_json(request, "202 Accepted", R"({"accepted":true})");
    return;
  }
  if (!physical) {
    lock.unlock();
    send_json(request, "403 Forbidden", R"({"error":"recovery_required"})");
    return;
  }
  if (url == "/recovery/web-auth") {
    const auto username = request->arg("new_username");
    const auto password = request->arg("new_password");
    if (username.empty() || username.size() > 32 || password.empty() || password.size() > 64 ||
        username.find('\0') != std::string::npos || password.find('\0') != std::string::npos) {
      lock.unlock();
      send_json(request, "400 Bad Request", R"({"error":"invalid_credentials"})");
      return;
    }
    this->username_ = username;
    this->password_ = password;
    this->pending_ = Action::WEB_AUTH;
  } else {
    this->pending_ = Action::END;
  }
  this->state_.begin_job(now, this->state_.generation());
  this->error_ = "";
  lock.unlock();
  send_json(request, "202 Accepted", R"({"accepted":true})");
}

}  // namespace esphome::openquatt_recovery
