#pragma once

#include <cstring>

#include "esp_http_server.h"
#include "lwip/sockets.h"

namespace esphome::openquatt_common {

enum class HttpStatus {
  ACCEPTED,
  FORBIDDEN,
  GONE,
  TOO_MANY_REQUESTS,
  SERVICE_UNAVAILABLE,
  GATEWAY_TIMEOUT,
};

// Only for a fresh response, before writing a body or starting a chunked stream.
// ESPHome 2026.9/2026.10 maps these numeric codes to 500. Send directly through
// IDF without allocating a response object or copying the body. IDF consumes
// the body synchronously; the caller retains ownership.
inline esp_err_t send_http_status(httpd_req_t* request, HttpStatus status, const char* body = nullptr) {
  if (request == nullptr) return ESP_ERR_INVALID_ARG;
  const char* status_line = nullptr;
  switch (status) {
    case HttpStatus::ACCEPTED:
      status_line = "202 Accepted";
      break;
    case HttpStatus::FORBIDDEN:
      status_line = "403 Forbidden";
      break;
    case HttpStatus::GONE:
      status_line = "410 Gone";
      break;
    case HttpStatus::TOO_MANY_REQUESTS:
      status_line = "429 Too Many Requests";
      break;
    case HttpStatus::SERVICE_UNAVAILABLE:
      status_line = "503 Service Unavailable";
      break;
    case HttpStatus::GATEWAY_TIMEOUT:
      status_line = "504 Gateway Timeout";
      break;
    default:
      return ESP_ERR_INVALID_ARG;
  }

  esp_err_t result = httpd_resp_set_status(request, status_line);
  if (result == ESP_OK && body != nullptr) result = httpd_resp_set_type(request, "application/json");
  if (result == ESP_OK) result = httpd_resp_set_hdr(request, "Accept-Ranges", "none");
  // Match the sole default header registered by our WebServerBase. A contract
  // test requires this list to stay aligned when that registration changes.
  if (result == ESP_OK) result = httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  if (result == ESP_OK) result = httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  if (result == ESP_OK) result = httpd_resp_send(request, body, body == nullptr ? 0 : std::strlen(body));
  if (result != ESP_OK) {
    // Never retry a partially written response or leave a failed response on a
    // reusable connection. Do not retry or start a follow-up action here; a
    // previously submitted mutation may already be pending or committed.
    // This handler already owns the live request on HTTPD. Do not queue a
    // close by descriptor: that descriptor could be reused before it runs.
    shutdown(httpd_req_to_sockfd(request), SHUT_RDWR);
  }
  return result;
}

}  // namespace esphome::openquatt_common
