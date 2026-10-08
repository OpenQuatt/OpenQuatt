#include "OpenQuattCaptivePortalRouter.h"
#include <cstdio>
#include <cstring>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
namespace esphome::openquatt_captive_portal_router {
static const char* const TAG = "openquatt.captive_portal_router";
OpenQuattCaptivePortalRouter* global_captive_portal_router = nullptr;
OpenQuattCaptivePortalRouter::OpenQuattCaptivePortalRouter() { global_captive_portal_router = this; }
bool portal_routes_active() {
  return global_captive_portal_router != nullptr && global_captive_portal_router->is_portal_active();
}
void set_portal_routes_active(bool active) {
  if (global_captive_portal_router != nullptr) global_captive_portal_router->set_portal_active(active);
}
static bool same_origin(AsyncWebServerRequest* request) {
  const auto host = request->get_header("Host");
  for (const char* name : {"Origin", "Referer"}) {
    const auto header = request->get_header(name);
    if (!header.has_value()) continue;
    if (!host.has_value() || host->empty()) return false;
    const auto& value = *header;
    const size_t scheme = value.find("://");
    if (scheme == std::string::npos ||
        (value.compare(0, scheme, "http") != 0 && value.compare(0, scheme, "https") != 0))
      return false;
    const size_t start = scheme + 3, end = value.find_first_of("/?#", start);
    if (value.substr(start, end == std::string::npos ? end : end - start) != *host) return false;
  }
  return true;
}
float OpenQuattCaptivePortalRouter::get_setup_priority() const {
  // Register before CaptivePortal setup (WIFI + 1), including early listener starts.
  return setup_priority::WIFI + 2.0f;
}
void OpenQuattCaptivePortalRouter::setup() {
  if (web_server_base::global_web_server_base == nullptr || captive_portal::global_captive_portal == nullptr) {
    ESP_LOGE(TAG, "Captive portal or shared web server is unavailable");
    this->mark_failed();
    return;
  }
  web_server_base::global_web_server_base->add_handler_without_auth(this);
}
bool OpenQuattCaptivePortalRouter::canHandle(AsyncWebServerRequest* request) const {
  if (request->method() != HTTP_GET || !this->is_portal_active()) return false;
  char url_buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  const auto url = request->url_to(url_buffer);
  return url == "/" || url == "/wifisave" || url == "/wifi/provisioning/status";
}
OpenQuattCaptivePortalRouter::JobStatus OpenQuattCaptivePortalRouter::job_status_() {
  std::lock_guard<std::mutex> lock(this->lock_);
  if (this->queued_ || this->processing_) return {this->job_generation_, "QUEUED"};
  if (this->wifi_generation_ == 0 || wifi::global_wifi_component == nullptr)
    return {this->job_generation_, this->job_state_};
  const auto status = wifi::global_wifi_component->get_provisioning_status();
  if (status.generation != this->wifi_generation_) return {this->job_generation_, "CANCELLED"};
  using State = wifi::WiFiComponent::WiFiProvisioningState;
  const char* state = "CONNECT_FAILED";
  switch (status.state) {
    case State::IDLE:
      state = "IDLE";
      break;
    case State::CONNECTING:
      state = "CONNECTING";
      break;
    case State::SAVED:
      state = "SAVED";
      break;
    case State::STORAGE_FAILED:
      state = "STORAGE_FAILED";
      break;
    case State::CANCELLED:
      state = "CANCELLED";
      break;
    case State::CONNECT_FAILED:
      break;
  }
  return {this->job_generation_, state};
}
void OpenQuattCaptivePortalRouter::handleRequest(AsyncWebServerRequest* request) {
  auto* portal = captive_portal::global_captive_portal;
  if (portal == nullptr || !this->is_portal_active() || request->method() != HTTP_GET) {
    request->send(503);
    return;
  }
  char url_buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  const auto url = request->url_to(url_buffer);
  if (url == "/") {
    portal->handleRequest(request);
    return;
  }
  if (url == "/wifi/provisioning/status") {
    const auto status = this->job_status_();
    char response[128];
    std::snprintf(response, sizeof(response), "{\"generation\":%u,\"state\":\"%s\",\"saved\":%s}",
                  static_cast<unsigned>(status.generation), status.state,
                  std::strcmp(status.state, "SAVED") == 0 ? "true" : "false");
    request->send(200, "application/json", response);
    return;
  }
  if (url != "/wifisave") {
    request->send(404);
    return;
  }
  if (!same_origin(request)) {
    request->send(403);
    return;
  }
#ifdef USE_ESP32
  // Upstream arg() truncates a decoded %00 through its C-string constructor.
  // Reject it before looking up decoded args; it must never become another pair.
  const httpd_req_t* native = *request;
  if (std::strstr(native->uri, "%00") != nullptr) {
    request->send(400, "text/plain", "Invalid Wi-Fi credentials.");
    return;
  }
#endif
  const auto ssid = request->arg("ssid"), password = request->arg("psk");
  if (ssid.empty() || ssid.size() > 32 || password.size() > 64 || ssid.find('\0') != std::string::npos ||
      password.find('\0') != std::string::npos) {
    request->send(400, "text/plain", "Invalid Wi-Fi credentials.");
    return;
  }
  uint32_t generation = 0;
  bool busy = false;
  {
    std::lock_guard<std::mutex> lock(this->lock_);
    busy = this->queued_ || this->processing_;
    if (!busy && this->wifi_generation_ != 0 && wifi::global_wifi_component != nullptr) {
      const auto status = wifi::global_wifi_component->get_provisioning_status();
      busy = status.generation == this->wifi_generation_ &&
             status.state == wifi::WiFiComponent::WiFiProvisioningState::CONNECTING;
    }
    if (!busy) {
      generation = ++this->job_generation_;
      if (generation == 0) generation = ++this->job_generation_;
      std::memcpy(this->ssid_, ssid.c_str(), ssid.size() + 1);
      std::memcpy(this->password_, password.c_str(), password.size() + 1);
      this->queued_ = true;
      this->wifi_generation_ = 0;
    }
  }
  if (busy) {
    request->send(409, "text/plain", "Wi-Fi provisioning is already in progress.");
    return;
  }
  char response[176];
  std::snprintf(
      response, sizeof(response),
      "Connecting. Request %u accepted; settings are saved only after a successful connection and storage check.",
      static_cast<unsigned>(generation));
  request->send(202, "text/plain", response);
}
void OpenQuattCaptivePortalRouter::loop() {
  const bool active =
      captive_portal::global_captive_portal != nullptr && captive_portal::global_captive_portal->is_active();
  this->portal_active_.store(active, std::memory_order_release);
  char ssid[33]{}, password[65]{};
  bool execute = false;
  uint32_t cancel_generation = 0;
  {
    std::lock_guard<std::mutex> lock(this->lock_);
    if (this->wifi_generation_ != 0 && wifi::global_wifi_component != nullptr) {
      const auto status = wifi::global_wifi_component->get_provisioning_status();
      if (status.generation == this->wifi_generation_ &&
          status.state == wifi::WiFiComponent::WiFiProvisioningState::CONNECTING &&
          millis() - this->wifi_started_ >= 90000)
        cancel_generation = this->wifi_generation_;
    }
    if (this->queued_) {
      std::memcpy(ssid, this->ssid_, sizeof(ssid));
      std::memcpy(password, this->password_, sizeof(password));
      std::memset(this->ssid_, 0, sizeof(this->ssid_));
      std::memset(this->password_, 0, sizeof(this->password_));
      this->queued_ = false;
      this->processing_ = true;
      execute = true;
    }
  }
  if (cancel_generation != 0) wifi::global_wifi_component->cancel_wifi_provisioning(cancel_generation);
  if (!execute) return;
  uint32_t wifi_generation = 0;
  if (active && wifi::global_wifi_component != nullptr && !wifi::global_wifi_component->is_disabled())
    wifi_generation = wifi::global_wifi_component->begin_wifi_provisioning(ssid, password);
  std::memset(password, 0, sizeof(password));
  {
    std::lock_guard<std::mutex> lock(this->lock_);
    this->wifi_generation_ = wifi_generation;
    this->wifi_started_ = millis();
    this->job_state_ = "CANCELLED";
    this->processing_ = false;
  }
}
void OpenQuattCaptivePortalRouter::dump_config() {
  ESP_LOGCONFIG(TAG, "Captive portal routing priority: %s", this->is_failed() ? "unavailable" : "enabled");
}
}  // namespace esphome::openquatt_captive_portal_router
