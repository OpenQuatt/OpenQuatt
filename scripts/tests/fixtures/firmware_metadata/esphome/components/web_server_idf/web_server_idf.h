#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace esphome {
// Upstream StringRef is a length-carrying view with c_str(), never data().
class StringRef {
 public:
  StringRef(const char* value, size_t length) : value_(value), length_(length) {}
  size_t size() const { return length_; }
  const char* c_str() const { return value_; }
  bool operator==(const char* value) const {
    return length_ == std::strlen(value) && std::equal(value_, value_ + length_, value);
  }

 private:
  const char* value_;
  size_t length_;
};
}  // namespace esphome

static constexpr int HTTP_GET = 1;
static constexpr int HTTP_POST = 2;
class AsyncResponseStream {
 public:
  std::string body;
  std::map<std::string, std::string> headers;
  void addHeader(const char* key, const char* value) { headers[key] = value; }
};
class AsyncWebServerRequest {
 public:
  static constexpr size_t URL_BUF_SIZE = 256;
  std::string url;
  int verb{HTTP_GET};
  int response_code{0};
  bool challenged{false};
  std::string response_body;
  std::map<std::string, std::string> response_headers;
  int method() const { return verb; }
  esphome::StringRef url_to(char* buffer) const {
    const size_t query = url.find('?');
    const size_t length = std::min(query == std::string::npos ? url.size() : query, URL_BUF_SIZE - 1U);
    size_t decoded = 0;
    for (size_t index = 0; index < length; ++index) {
      if (url[index] == '%' && index + 2U < length) {
        const std::string hex = url.substr(index + 1U, 2U);
        char* end;
        const auto byte = std::strtoul(hex.c_str(), &end, 16);
        if (*end == '\0') {
          buffer[decoded++] = static_cast<char>(byte);
          index += 2U;
          continue;
        }
      }
      buffer[decoded++] = url[index] == '+' ? ' ' : url[index];
    }
    buffer[decoded] = '\0';
    return {buffer, decoded};
  }
  AsyncResponseStream* beginResponse(int code, const char*, const char* body) {
    response_code = code;
    auto* response = new AsyncResponseStream;
    response->body = body;
    return response;
  }
  void send(AsyncResponseStream* response) {
    response_body = response->body;
    response_headers = response->headers;
    delete response;
  }
  void requestAuthentication() { challenged = true; }
};
class AsyncWebHandler {
 public:
  virtual ~AsyncWebHandler() = default;
  virtual bool canHandle(AsyncWebServerRequest*) const { return true; }
  virtual void handleRequest(AsyncWebServerRequest*) {}
};
