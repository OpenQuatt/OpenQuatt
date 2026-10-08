#include "OpenQuattRecovery.h"

#include <cstdio>
#include "esp_http_server.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_rom_crc.h"
#include <cstring>
#ifdef USE_CAPTIVE_PORTAL
#include "esphome/components/captive_portal/captive_portal.h"
#include "esphome/components/openquatt_captive_portal_router/OpenQuattCaptivePortalRouter.h"
#endif

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
  // Before auth, captive portal, WiFi or Ethernet can start the HTTP listener.
  web_server_base::global_web_server_base->add_handler_without_auth(this);
  ota::get_global_ota_callback()->add_global_state_listener(this);
  const RecoveryHandoff saved = handoff;
  handoff.magic = 0;  // Consume invalid markers and markers from any other reset.
  this->recovery_boot_ = esp_reset_reason() == ESP_RST_SW && saved.magic == HANDOFF_MAGIC && saved.version == 1 &&
                         saved.checksum == esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&saved), 12);
  this->auth_->set_recovery_boot(this->recovery_boot_);
  if (this->recovery_boot_) {
    this->state_.restore(millis(), saved.generation);
    this->csrf_token_ = random_token_();
  }
}

void OpenQuattRecovery::restart_(bool recovery) {
  if (recovery) this->prepare_reboot_handoff_();
  // API teardown must never process a late set-key packet after a checked clear.
  if (api::global_api_server != nullptr)
    for (auto& client : api::global_api_server->active_clients()) client->on_fatal_error();
  // Synchronous main-loop handoff retains the existing compressor off-time
  // credit policy. No scheduler gap may occur after clearing a stored API key.
  this->restart_handler_();
}

void OpenQuattRecovery::httpd_drained_(void* context) {
  static_cast<OpenQuattRecovery*>(context)->barrier_ready_.store(true, std::memory_order_release);
}

void OpenQuattRecovery::loop() {
  std::unique_lock<std::mutex> lock(this->mutex_);
  if (!this->runtime_ready_.load(std::memory_order_acquire)) return;
  const uint32_t now = millis();
  if (this->button_->has_state()) {
    const auto events = this->state_.tick(now, this->button_->state, WIFI_CAPABLE);
    if (this->pending_ == Action::NONE && (events.opened || events.wifi_reset_requested)) {
      this->restore_recovery_ = true;
      this->pending_ = events.wifi_reset_requested ? Action::WIFI_RESET : Action::RECOVERY_BOOT;
      this->state_.begin_admin_job();
      this->accepted_at_ = now;
      this->auth_->close_normal_access();
      this->error_ = "";
    }
    if (events.expired && this->pending_ == Action::NONE) {
      this->pending_ = Action::END;
      this->state_.begin_admin_job();
      this->accepted_at_ = now;
      this->auth_->close_normal_access();
    }
  }
  if (this->pending_ == Action::NONE && this->auth_->pending_reboot()) {
    this->pending_ = Action::END;
    this->accepted_at_ = this->auth_->persisted_at();
  }
  const Action action = this->pending_;
  if (action == Action::NONE || now - this->accepted_at_ < 500) return;
  // Already accepted ODU work drains (or times out through its normal failure
  // path) before resetting. Block fresh web work while it drains.
  if ((this->ota_active_.load() && !this->web_ota_active_.load()) || this->ota_completed_.load() ||
      this->auth_->restart_blocked())
    return;
  // HTTPD runs requests/uploads and queue_work serially. A request selected
  // before closing may still execute its body/upload callbacks; drain it first.
  // This also covers web OTA before its deferred STARTED notification arrives.
  if (!this->barrier_queued_) {
    auto* server = web_server_base::global_web_server_base->get_server();
    if (server == nullptr || httpd_queue_work(server->get_server(), httpd_drained_, this) != ESP_OK) {
      this->pending_ = Action::NONE;
      this->state_.job_failed();
      this->error_ = "drain_failed";
      return;
    }
    this->barrier_queued_ = true;
    return;
  }
  if (!this->barrier_ready_.load(std::memory_order_acquire)) return;
  if (!this->barrier_seen_) {
    // Let one complete scheduler/component pass consume commands queued by the
    // last HTTP request, then inspect ODU blockers again before persistence.
    this->barrier_seen_ = true;
    return;
  }
  bool saved = true;
  if (action == Action::WEB_AUTH) {
    saved = this->auth_->set_runtime_credentials(this->username_, this->password_);
    std::memset(this->username_, 0, sizeof(this->username_));
    std::memset(this->password_, 0, sizeof(this->password_));
  } else if (action == Action::API_RESET) {
    saved = api::global_api_server != nullptr && api::global_api_server->clear_noise_psk(false);
  }
#ifdef USE_WIFI
  else if (action == Action::WIFI_RESET) {
    saved = wifi::global_wifi_component != nullptr && wifi::global_wifi_component->clear_saved_sta_checked();
  }
#endif
  if (!saved) {
    this->pending_ = Action::NONE;
    this->barrier_queued_ = false;
    this->barrier_seen_ = false;
    this->barrier_ready_.store(false);
    this->error_ = "persist_failed";
    this->storage_failed_ = true;
    this->state_.job_failed();
    // Retain the boot restriction. Explicit recovery/end is still available.
    return;
  }
  const bool recovery = action == Action::RECOVERY_BOOT ||
                        ((action == Action::API_RESET || action == Action::WIFI_RESET) && this->restore_recovery_);
  // No defer(), callbacks or scheduler pass between persistence and handoff.
  lock.unlock();
  this->restart_(recovery);
}

static bool portal_route(AsyncWebServerRequest* request) {
#ifdef USE_CAPTIVE_PORTAL
  const auto* portal = captive_portal::global_captive_portal;
  if (portal == nullptr || !openquatt_captive_portal_router::portal_routes_active() || request->method() != HTTP_GET)
    return false;
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  const auto url = request->url_to(buffer);
  return url == "/" || url == "/config.json" || url == "/wifisave" || url == "/wifi/provisioning/status" ||
         url == "/generate_204" || url == "/gen_204" || url == "/hotspot-detect.html" || url == "/connecttest.txt" ||
         url == "/ncsi.txt" || url == "/success.txt" || url == "/fwlink";
#else
  (void)request;
  return false;
#endif
}

bool OpenQuattRecovery::canHandle(AsyncWebServerRequest* request) const {
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  const auto url = request->url_to(buffer);
  if ((request->method() == HTTP_GET && (url == "/recovery" || url == "/recovery/status")) ||
      (request->method() == HTTP_POST &&
       (url == "/recovery/web-auth" || url == "/recovery/end" || url == "/api-security/reset" || url == "/wifi/reset")))
    return true;
  if (portal_route(request) && !(this->auth_->restart_requested() && url == "/wifisave")) return false;
  // Body/upload callbacks remain no-ops on this first handler, so restricted
  // requests cannot reach normal entities, SSE, API UI or web OTA handlers.
  if (this->recovery_boot_ || !this->auth_->ready() || !this->runtime_ready_.load(std::memory_order_acquire))
    return true;
  if (!this->auth_->normal_access_allowed()) return !(request->method() == HTTP_GET && url == "/auth/status");
  return false;
}

bool OpenQuattRecovery::authorize_(AsyncWebServerRequest* request, uint32_t now) const {
  const auto host = request->get_header("Host");
  const auto origin = request->get_header("Origin");
  return this->recovery_boot_ && this->state_.active(now) && !this->state_.busy() && host.has_value() &&
         !host->empty() && origin.has_value() && *origin == "http://" + *host &&
         request->arg("generation") == std::to_string(this->state_.generation()) && !this->csrf_token_.empty() &&
         request->arg("csrf_token") == this->csrf_token_;
}

void OpenQuattRecovery::handleRequest(AsyncWebServerRequest* request) {
  std::unique_lock<std::mutex> lock(this->mutex_);
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  const auto url = request->url_to(buffer);
  const uint32_t now = millis();
  if (url == "/recovery" || (this->recovery_boot_ && url == "/")) {
    auto* response = request->beginResponse(200, "text/html; charset=utf-8", RECOVERY_PAGE);
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Frame-Options", "DENY");
    response->addHeader("Access-Control-Allow-Origin", "");
    lock.unlock();
    request->send(response);
    return;
  }
  if (url == "/recovery/status") {
    const bool active = this->recovery_boot_ && this->state_.active(now);
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
                       *origin == "http://" + *host && this->auth_->request_is_authenticated_reset_admin(request) &&
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
    this->restore_recovery_ = physical;
    this->pending_ = wifi_reset ? Action::WIFI_RESET : Action::API_RESET;
    this->accepted_at_ = now;
    this->auth_->close_normal_access();
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
    std::memcpy(this->username_, username.c_str(), username.size() + 1);
    std::memcpy(this->password_, password.c_str(), password.size() + 1);
    this->pending_ = Action::WEB_AUTH;
  } else {
    this->pending_ = Action::END;
  }
  this->state_.begin_job(now, this->state_.generation());
  this->accepted_at_ = now;
  this->auth_->close_normal_access();
  this->error_ = "";
  lock.unlock();
  send_json(request, "202 Accepted", R"({"accepted":true})");
}

}  // namespace esphome::openquatt_recovery
