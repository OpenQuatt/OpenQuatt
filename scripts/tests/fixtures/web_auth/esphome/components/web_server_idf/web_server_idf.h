#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cstdarg>
#include <cstdio>
#include <functional>

using StringRef = std::string;
static constexpr int HTTP_GET = 1;
static constexpr int HTTP_POST = 2;

class AsyncResponseStream {
 public:
  std::string body;
  void printf(const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    body = buffer;
  }
};

class AsyncWebServerRequest {
 public:
  std::string username;
  std::string password;
  bool challenged{false};
  bool paired_only{false};
  static constexpr size_t URL_BUF_SIZE = 256;
  std::string url{"/auth/status"};
  int verb{HTTP_GET};
  int response_code{0};
  std::string response_body;
  std::function<void()> before_send;
  std::map<std::string, std::string> headers;
  std::map<std::string, std::string> arguments;
  std::optional<std::string> get_header(const char* name) const {
    auto it = headers.find(name);
    return it == headers.end() ? std::nullopt : std::optional<std::string>(it->second);
  }
  std::string arg(const char* name) const {
    auto it = arguments.find(name);
    return it == arguments.end() ? "" : it->second;
  }
  int method() const { return verb; }
  StringRef url_to(char*) const { return url; }
  AsyncResponseStream* beginResponseStream(const char*) { return new AsyncResponseStream; }
  void send(AsyncResponseStream* stream) {
    if (before_send) before_send();
    response_code = 200;
    response_body = stream->body;
    delete stream;
  }
  void send(int code, const char* = nullptr, const char* body = "") {
    if (before_send) before_send();
    response_code = code;
    response_body = body;
  }
  bool authenticate(const char* user, const char* pass) {
    if (paired_only) return std::string(user) == pass;
    return username == user && password == pass;
  }
  void requestAuthentication() { challenged = true; }
};

class AsyncWebHandler {
 public:
  virtual ~AsyncWebHandler() = default;
  virtual bool canHandle(AsyncWebServerRequest*) const { return true; }
  virtual void handleRequest(AsyncWebServerRequest*) {}
  virtual void handleUpload(AsyncWebServerRequest*, const std::string&, size_t, uint8_t*, size_t, bool) {}
  virtual void handleBody(AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t) {}
  virtual bool isRequestHandlerTrivial() const { return false; }
};

class AsyncWebServer {
 public:
  explicit AsyncWebServer(uint16_t) {}
  void begin() {}
  void end() {}
  void addHandler(AsyncWebHandler* handler) { handlers.push_back(handler); }
  std::vector<AsyncWebHandler*> handlers;
};

class DefaultHeaders {
 public:
  static DefaultHeaders& Instance() {
    static DefaultHeaders headers;
    return headers;
  }
  void addHeader(const char*, const char*) {}
};
