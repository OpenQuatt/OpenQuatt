#include "OpenQuattFirmwareMetadata.h"

#include <cstdio>

#include "esp_random.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_firmware_metadata {

static const char* const TAG = "openquatt.firmware_metadata";

float OpenQuattFirmwareMetadata::get_setup_priority() const { return setup_priority::WIFI + 1.5f; }

void OpenQuattFirmwareMetadata::setup() {
  std::snprintf(this->boot_id_, sizeof(this->boot_id_), "%08x%08x", static_cast<unsigned>(esp_random()),
                static_cast<unsigned>(esp_random()));
  if (web_server_base::global_web_server_base == nullptr || this->web_auth_ == nullptr) {
    ESP_LOGW(TAG, "Web server/auth unavailable; firmware metadata endpoint disabled");
    return;
  }
  web_server_base::global_web_server_base->add_handler(this);
}

void OpenQuattFirmwareMetadata::dump_config() { ESP_LOGCONFIG(TAG, "OpenQuatt firmware metadata endpoint:"); }

bool OpenQuattFirmwareMetadata::canHandle(AsyncWebServerRequest* request) const {
  if (request->method() != HTTP_GET) return false;
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  const StringRef url = request->url_to(url_buf);
  // url_to strips the query and preserves the decoded length, including NULs.
  return url == "/openquatt/firmware/metadata";
}

void OpenQuattFirmwareMetadata::handleRequest(AsyncWebServerRequest* request) {
  if (this->web_auth_ == nullptr || !this->web_auth_->request_is_authenticated(request)) {
    request->requestAuthentication();
    return;
  }
  // Fixed numeric/string payload: no JSON arena or update_info reads on HTTPD.
  char body[80];
  std::snprintf(body, sizeof(body), "{\"boot_id\":\"%s\",\"manifest_revision\":%lu}", this->boot_id_,
                static_cast<unsigned long>(this->manifest_revision_.load(std::memory_order_relaxed)));
  auto* response = request->beginResponse(200, "application/json", body);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

}  // namespace openquatt_firmware_metadata
}  // namespace esphome
