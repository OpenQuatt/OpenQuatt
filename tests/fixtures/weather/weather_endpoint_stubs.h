#pragma once
#include "weather_runtime_stubs.h"

struct httpd_req_t {
  const char* uri = nullptr;
  int status = 0;
  std::map<std::string, std::string> headers;
  std::string body;
  size_t chunks = 0, fail_at = 0, largest_chunk = 0;
  bool finished = false, closed = false;
};
inline constexpr const char* HTTPD_200 = "200 OK";
inline constexpr int SHUT_RDWR = 2;
inline httpd_req_t* closing_request = nullptr;
inline int httpd_req_to_sockfd(httpd_req_t* req) {
  closing_request = req;
  return 42;
}
inline int shutdown(int fd, int how) {
  assert(fd == 42 && how == SHUT_RDWR && closing_request);
  closing_request->closed = true;
  return 0;
}
inline int httpd_resp_set_status(httpd_req_t* req, const char*) {
  req->status = 200;
  return ESP_OK;
}
inline int httpd_resp_set_type(httpd_req_t* req, const char* type) {
  req->headers["Content-Type"] = type;
  return ESP_OK;
}
inline int httpd_resp_set_hdr(httpd_req_t* req, const char* key, const char* value) {
  req->headers[key] = value;
  return ESP_OK;
}
inline int httpd_resp_send_chunk(httpd_req_t* req, const char* bytes, ssize_t size) {
  if (req->fail_at && ++req->chunks == req->fail_at) return ESP_FAIL;
  req->largest_chunk = std::max(req->largest_chunk, static_cast<size_t>(size));
  if (bytes)
    req->body.append(bytes, size);
  else
    req->finished = true;
  return ESP_OK;
}

namespace esphome {
inline constexpr int HTTP_GET = 1, HTTP_POST = 2;
class AsyncWebServerRequest {
 public:
  static constexpr size_t URL_BUF_SIZE = 513;
  httpd_req_t response;
  std::string url = "/openquatt/weather/forecast";
  bool authenticated = true;
  int method_value = HTTP_GET;
  mutable unsigned url_copies = 0;
  int method() const { return method_value; }
  void url_to(char* bytes) const {
    ++url_copies;
    std::strncpy(bytes, url.c_str(), URL_BUF_SIZE - 1);
    bytes[URL_BUF_SIZE - 1] = '\0';
  }
  void requestAuthentication() { response.status = 401; }
  operator httpd_req_t*() {
    response.uri = url.c_str();
    return &response;
  }
};
class AsyncWebHandler {
 public:
  virtual ~AsyncWebHandler() = default;
  virtual bool canHandle(AsyncWebServerRequest*) const = 0;
  virtual void handleRequest(AsyncWebServerRequest*) = 0;
};
namespace openquatt_web_auth {
class OpenQuattWebAuth {
 public:
  bool request_is_authenticated(AsyncWebServerRequest* request) const { return request->authenticated; }
};
}  // namespace openquatt_web_auth
namespace web_server_base {
struct WebServerBase {
  std::vector<AsyncWebHandler*> handlers;
  void add_handler(AsyncWebHandler* handler) { handlers.push_back(handler); }
  void clear() {
    for (auto* handler : handlers) delete handler;
    handlers.clear();
  }
};
inline WebServerBase endpoint_server;
inline WebServerBase* global_web_server_base = &endpoint_server;
}  // namespace web_server_base
namespace openquatt_common {
enum class HttpStatus { SERVICE_UNAVAILABLE = 503, TOO_MANY_REQUESTS = 429 };
inline void send_http_status(AsyncWebServerRequest& request, HttpStatus status, const char* body) {
  request.response.status = static_cast<int>(status);
  request.response.body = body;
}
}  // namespace openquatt_common
}  // namespace esphome
