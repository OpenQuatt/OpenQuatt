#pragma once
#include "esphome/core/defines.h"
#if defined(USE_NETWORK) && !defined(USE_ZEPHYR)
#include <vector>
#include <string>
#include "esphome/core/helpers.h"

#include "esphome/core/progmem.h"

#if USE_ESP32
#include "esphome/core/hal.h"
#include "esphome/components/web_server_idf/web_server_idf.h"
#else
#include <ESPAsyncWebServer.h>
#endif

#if USE_ESP32
using PlatformString = std::string;
#elif USE_ARDUINO
using PlatformString = String;
#endif

namespace esphome::web_server_base {

class WebServerBase;
extern WebServerBase* global_web_server_base;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

namespace internal {

class MiddlewareHandler : public AsyncWebHandler {
 public:
  MiddlewareHandler(AsyncWebHandler* next) : next_(next) {}

  bool canHandle(AsyncWebServerRequest* request) const override { return next_->canHandle(request); }
  void handleRequest(AsyncWebServerRequest* request) override { next_->handleRequest(request); }
  void handleUpload(AsyncWebServerRequest* request, const PlatformString& filename, size_t index, uint8_t* data,
                    size_t len, bool final) override {
    next_->handleUpload(request, filename, index, data, len, final);
  }
  void handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) override {
    next_->handleBody(request, data, len, index, total);
  }
  bool isRequestHandlerTrivial() const override { return next_->isRequestHandlerTrivial(); }

 protected:
  AsyncWebHandler* next_;
};

#ifdef USE_WEBSERVER_AUTH
// Own the credentials: HTTP callbacks must not read the caller's mutable buffers.
// One bounded-lifetime mutex protects both credential updates and authentication.
struct Credentials {
  void set_username(const char* value) {
    LockGuard lock(this->mutex_);
    this->username_ = value != nullptr ? value : "";
  }
  void set_password(const char* value) {
    LockGuard lock(this->mutex_);
    this->password_ = value != nullptr ? value : "";
  }
  void set(const char* username, const char* password) {
    LockGuard lock(this->mutex_);
    this->username_ = username != nullptr ? username : "";
    this->password_ = password != nullptr ? password : "";
  }
  bool authenticate(AsyncWebServerRequest* request, bool require_configured = false) {
    LockGuard lock(this->mutex_);
    if (this->username_.empty()) return this->password_.empty() && !require_configured;
    // ESPHome treats a null password as open access. Owned strings avoid that,
    // and an incomplete configured pair must fail closed.
    return request != nullptr && !this->password_.empty() &&
           request->authenticate(this->username_.c_str(), this->password_.c_str());
  }

 private:
  Mutex mutex_;
  std::string username_;
  std::string password_;
};

class AuthMiddlewareHandler : public MiddlewareHandler {
 public:
  AuthMiddlewareHandler(AsyncWebHandler* next, Credentials* credentials)
      : MiddlewareHandler(next), credentials_(credentials) {}

  bool check_auth(AsyncWebServerRequest* request) {
    bool success = credentials_->authenticate(request);
    if (!success) {
#if USE_ESP32
      request->requestAuthentication();
#elif defined(USE_WEBSERVER_AUTH_DIGEST)
      request->requestAuthentication(nullptr, true);
#else
      request->requestAuthentication(nullptr, false);
#endif
    }
    return success;
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    if (!check_auth(request)) return;
    MiddlewareHandler::handleRequest(request);
  }
  void handleUpload(AsyncWebServerRequest* request, const PlatformString& filename, size_t index, uint8_t* data,
                    size_t len, bool final) override {
    if (!check_auth(request)) return;
    MiddlewareHandler::handleUpload(request, filename, index, data, len, final);
  }
  void handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) override {
    if (!check_auth(request)) return;
    MiddlewareHandler::handleBody(request, data, len, index, total);
  }

 protected:
  Credentials* credentials_;
};
#endif

}  // namespace internal

class WebServerBase final {
 public:
  // The AsyncWebServer is created once and intentionally never deleted: on Arduino
  // platforms ESPAsyncWebServer owns its registered handlers, so destroying it would
  // also destroy live components (e.g. the captive portal) out from under us.
  // init()/deinit() refcount users and start/stop the listener; handlers are
  // registered once at creation and survive listener restarts.
  void init() {
    this->initialized_++;
    if (this->server_ != nullptr) {
      if (this->initialized_ == 1) {
        // Restart the listener after a previous deinit()
        this->server_->begin();
      }
      return;
    }
    this->server_ = new AsyncWebServer(this->port_);
    // All content is controlled and created by user - so allowing all origins is fine here.
    // NOTE: Currently 1 header. If more are added, update in __init__.py:
    //   cg.add_define("WEB_SERVER_DEFAULT_HEADERS_COUNT", 1)
    DefaultHeaders::Instance().addHeader(ESPHOME_F("Access-Control-Allow-Origin"), ESPHOME_F("*"));
    this->server_->begin();

    for (auto* handler : this->handlers_) this->server_->addHandler(handler);
  }
  void deinit() {
    if (this->initialized_ == 0) return;  // unbalanced deinit()
    this->initialized_--;
    if (this->initialized_ == 0) {
      this->server_->end();
    }
  }
  AsyncWebServer* get_server() const { return this->server_; }

#ifdef USE_WEBSERVER_AUTH
  void set_auth_username(const char* auth_username) { credentials_.set_username(auth_username); }
  void set_auth_password(const char* auth_password) { credentials_.set_password(auth_password); }
  // Runtime callers must update the pair together, rather than exposing mixed credentials.
  void set_auth_credentials(const char* username, const char* password) { credentials_.set(username, password); }
  bool request_is_authenticated(AsyncWebServerRequest* request, bool require_configured = false) {
    return credentials_.authenticate(request, require_configured);
  }
#endif

  void add_handler(AsyncWebHandler* handler);
  /**
   * WARNING: Registers a handler that bypasses the USE_WEBSERVER_AUTH middleware.
   *
   * This should only be used for endpoints that are intentionally unauthenticated
   * (for example, captive portal or very limited-status endpoints). For normal
   * endpoints that should respect web server authentication, use add_handler().
   */
  void add_handler_without_auth(AsyncWebHandler* handler);

  void set_port(uint16_t port) { port_ = port; }
  uint16_t get_port() const { return port_; }

 protected:
  uint8_t initialized_{0};
  uint16_t port_{80};  // Keep in sync with DEFAULT_PORT in web_server/__init__.py
  AsyncWebServer* server_{nullptr};
  std::vector<AsyncWebHandler*> handlers_;
#ifdef USE_WEBSERVER_AUTH
  internal::Credentials credentials_;
#endif
};

}  // namespace esphome::web_server_base
#endif  // USE_NETWORK && !USE_ZEPHYR
