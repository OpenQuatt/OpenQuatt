#include "OpenQuattCaptivePortalRouter.h"

#include <lwip/sockets.h>

#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_captive_portal_router {

static const char* const TAG = "openquatt.captive_portal_router";

float OpenQuattCaptivePortalRouter::get_setup_priority() const {
  // CaptivePortal sets up at WIFI + 1.0f; WebServer registers its routes at WIFI - 1.0f.
  return setup_priority::WIFI + 0.5f;
}

void OpenQuattCaptivePortalRouter::setup() {
  if (web_server_base::global_web_server_base == nullptr || captive_portal::global_captive_portal == nullptr) {
    ESP_LOGE(TAG, "Captive portal or shared web server is unavailable");
    this->mark_failed();
    return;
  }

  // CaptivePortal itself intentionally bypasses web-server auth. Register this
  // proxy first so the same provisioning routes win while the portal is active.
  web_server_base::global_web_server_base->add_handler_without_auth(this);
}

bool OpenQuattCaptivePortalRouter::canHandle(AsyncWebServerRequest* request) const {
  auto* portal = captive_portal::global_captive_portal;
  return portal != nullptr && portal->canHandle(request) && this->request_targets_soft_ap_(request);
}

void OpenQuattCaptivePortalRouter::handleRequest(AsyncWebServerRequest* request) {
  auto* portal = captive_portal::global_captive_portal;
  if (portal == nullptr) {
    request->send(503);
    return;
  }
  portal->handleRequest(request);
}

bool OpenQuattCaptivePortalRouter::request_targets_soft_ap_(AsyncWebServerRequest* request) const {
  auto* wifi = wifi::global_wifi_component;
  if (wifi == nullptr) {
    return false;
  }

  const network::IPAddress soft_ap_ip = wifi->wifi_soft_ap_ip();
  if (!soft_ap_ip.is_set() || !soft_ap_ip.is_ip4()) {
    return false;
  }

  httpd_req_t* raw_request = *request;
  struct sockaddr_storage local_address{};
  socklen_t address_length = sizeof(local_address);
  if (getsockname(httpd_req_to_sockfd(raw_request), reinterpret_cast<struct sockaddr*>(&local_address),
                  &address_length) != 0 ||
      local_address.ss_family != AF_INET) {
    return false;
  }

  const auto* local_ipv4 = reinterpret_cast<const struct sockaddr_in*>(&local_address);
  const esp_ip4_addr_t soft_ap_ipv4 = soft_ap_ip;
  return local_ipv4->sin_addr.s_addr == soft_ap_ipv4.addr;
}

void OpenQuattCaptivePortalRouter::dump_config() {
  ESP_LOGCONFIG(TAG, "Captive portal routing priority: %s", this->is_failed() ? "unavailable" : "enabled");
}

}  // namespace openquatt_captive_portal_router
}  // namespace esphome
