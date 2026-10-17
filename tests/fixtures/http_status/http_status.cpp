#include "OpenQuattHttpStatus.h"

#include <cassert>
#include <cstdlib>
#include <new>

// Any accidental new/response-object allocation in the real helper fails this test.
void* operator new(std::size_t) { std::abort(); }
void* operator new[](std::size_t) { std::abort(); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

using esphome::openquatt_common::HttpStatus;
using esphome::openquatt_common::send_http_status;

int main() {
  constexpr HttpStatus statuses[] = {
      HttpStatus::ACCEPTED,          HttpStatus::FORBIDDEN,           HttpStatus::GONE,
      HttpStatus::TOO_MANY_REQUESTS, HttpStatus::SERVICE_UNAVAILABLE, HttpStatus::GATEWAY_TIMEOUT};
  constexpr const char* lines[] = {"202 Accepted",          "403 Forbidden",           "410 Gone",
                                   "429 Too Many Requests", "503 Service Unavailable", "504 Gateway Timeout"};
  constexpr char body[] = R"({"ok":false,"error":"forbidden"})";
  for (std::size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
    httpd_req_t request;
    assert(send_http_status(&request, statuses[i], body) == ESP_OK);
    assert(std::strcmp(request.status, lines[i]) == 0);
    assert(std::strcmp(request.type, "application/json") == 0);
    assert(request.headers == 3);
    assert(std::strcmp(request.header_names[0], "Accept-Ranges") == 0);
    assert(std::strcmp(request.header_values[0], "none") == 0);
    assert(std::strcmp(request.header_names[1], "Access-Control-Allow-Origin") == 0);
    assert(std::strcmp(request.header_values[1], "*") == 0);
    assert(std::strcmp(request.header_names[2], "Cache-Control") == 0);
    assert(std::strcmp(request.header_values[2], "no-store") == 0);
    assert(request.body == body && request.length == sizeof(body) - 1);
    assert(request.sends == 1 && request.closes == 0);
  }
  httpd_req_t empty;
  assert(send_http_status(&empty, HttpStatus::SERVICE_UNAVAILABLE) == ESP_OK);
  assert(empty.body == nullptr && empty.length == 0 && empty.type == nullptr && empty.sends == 1);
  assert(std::strcmp(empty.status, "503 Service Unavailable") == 0);
  httpd_req_t empty_json;
  assert(send_http_status(&empty_json, HttpStatus::FORBIDDEN, "") == ESP_OK);
  assert(empty_json.body != nullptr && empty_json.length == 0 && empty_json.type != nullptr);
  assert(send_http_status(nullptr, HttpStatus::FORBIDDEN, body) == ESP_ERR_INVALID_ARG);
  httpd_req_t invalid;
  assert(send_http_status(&invalid, static_cast<HttpStatus>(999), body) == ESP_ERR_INVALID_ARG);
  assert(invalid.calls == 0 && invalid.sends == 0);
  // Inject failure at status/type/every header/send. Never send after a header
  // error, never retry a partial send, close once, and preserve the error result.
  for (int failed_call = 1; failed_call <= 6; ++failed_call) {
    httpd_req_t request;
    request.fail_at = failed_call;
    assert(send_http_status(&request, HttpStatus::FORBIDDEN, body) == ESP_FAIL);
    assert(request.calls == failed_call && request.closes == 1);
    assert(request.sends == (failed_call == 6 ? 1 : 0));
  }
}
