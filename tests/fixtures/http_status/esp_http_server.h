#pragma once
#include <cstddef>
#include <cstring>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_INVALID_ARG = 0x102;
using httpd_handle_t = void*;
struct httpd_req_t {
  httpd_handle_t handle = this;
  const char* status = nullptr;
  const char* type = nullptr;
  const char* header_names[3]{};
  const char* header_values[3]{};
  const char* body = nullptr;
  std::size_t length = 0;
  int calls = 0;
  int fail_at = 0;
  int headers = 0;
  int sends = 0;
  int closes = 0;
};
inline esp_err_t next_call(httpd_req_t* req) { return ++req->calls == req->fail_at ? ESP_FAIL : ESP_OK; }
inline esp_err_t httpd_resp_set_status(httpd_req_t* req, const char* value) {
  const auto result = next_call(req);
  if (result == ESP_OK) req->status = value;
  return result;
}
inline esp_err_t httpd_resp_set_type(httpd_req_t* req, const char* value) {
  const auto result = next_call(req);
  if (result == ESP_OK) req->type = value;
  return result;
}
inline esp_err_t httpd_resp_set_hdr(httpd_req_t* req, const char* name, const char* value) {
  const auto result = next_call(req);
  if (result == ESP_OK) {
    req->header_names[req->headers] = name;
    req->header_values[req->headers++] = value;
  }
  return result;
}
inline esp_err_t httpd_resp_send(httpd_req_t* req, const char* value, std::size_t length) {
  ++req->sends;
  const auto result = next_call(req);
  if (result == ESP_OK) {
    req->body = value;
    req->length = length;
  }
  return result;
}
inline httpd_req_t* active_socket_request = nullptr;
inline int httpd_req_to_sockfd(httpd_req_t* request) {
  active_socket_request = request;
  return 7;
}
