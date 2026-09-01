#include "OpenQuattCaptivePortalRouter.h"

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
  if (portal == nullptr || !portal->canHandle(request)) {
    return false;
  }

  char url_buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  return request->url_to(url_buffer) == "/";
}

void OpenQuattCaptivePortalRouter::handleRequest(AsyncWebServerRequest* request) {
  auto* portal = captive_portal::global_captive_portal;
  if (portal == nullptr) {
    request->send(503);
    return;
  }
  portal->handleRequest(request);
}

void OpenQuattCaptivePortalRouter::dump_config() {
  ESP_LOGCONFIG(TAG, "Captive portal routing priority: %s", this->is_failed() ? "unavailable" : "enabled");
}

}  // namespace openquatt_captive_portal_router
}  // namespace esphome
