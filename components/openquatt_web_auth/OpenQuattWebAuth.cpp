#include "OpenQuattWebAuth.h"

#include <cstdio>
#include <cstring>

#include "esphome/core/application.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/provisioning/provisioning.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_web_auth {

static const char* const TAG = "openquatt.web_auth";
static const uint32_t STORAGE_KEY = fnv1_hash("openquatt_web_auth_store");
static std::string json_escape_string_(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char escaped[7];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned char>(c));
          out += escaped;
        } else {
          out.push_back(c);
        }
        break;
    }
  }
  return out;
}

static bool header_matches_host_(const std::string& header_value, const std::string& host) {
  if (host.empty() || header_value.empty()) {
    return false;
  }

  size_t authority_start = 0;
  const size_t scheme_pos = header_value.find("://");
  if (scheme_pos != std::string::npos) {
    authority_start = scheme_pos + 3;
  }
  const size_t authority_end = header_value.find_first_of("/?#", authority_start);
  const std::string authority = header_value.substr(
      authority_start, authority_end == std::string::npos ? std::string::npos : authority_end - authority_start);
  return authority == host;
}

class OpenQuattWebAuthRequestHandler : public AsyncWebHandler {
 public:
  explicit OpenQuattWebAuthRequestHandler(OpenQuattWebAuth* parent) : parent_(parent) {}

  bool passes_same_origin_(AsyncWebServerRequest* request) const {
    const auto host = request->get_header("Host");
    if (!host.has_value() || host->empty()) {
      return false;
    }

    const auto origin = request->get_header("Origin");
    if (origin.has_value() && !header_matches_host_(origin.value(), host.value())) {
      return false;
    }

    const auto referer = request->get_header("Referer");
    if (referer.has_value() && !header_matches_host_(referer.value(), host.value())) {
      return false;
    }

    return true;
  }

  bool passes_csrf_(AsyncWebServerRequest* request) const {
    const std::string csrf_token = request->arg("csrf_token");
    return !csrf_token.empty() && csrf_token == this->parent_->get_csrf_token();
  }

  bool canHandle(AsyncWebServerRequest* request) const override {
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    StringRef url = request->url_to(url_buf);
    if (url == "/auth/status" && request->method() == HTTP_GET) {
      return true;
    }
    if (url == "/auth/change" && request->method() == HTTP_POST) {
      return true;
    }
    if (url == "/auth/disable" && request->method() == HTTP_POST) {
      return true;
    }
    if (url == "/api-security/status" && request->method() == HTTP_GET) {
      return true;
    }
    return false;
  }

  void handleRequest(AsyncWebServerRequest* request) override {
    // Keep policy, persistence and response snapshots coherent, but release
    // before socket writes so a slow HTTP client cannot hold up the main loop.
    std::unique_lock<std::recursive_mutex> lock(this->parent_->state_mutex_);
    char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
    StringRef url = request->url_to(url_buf);

    if (url == "/auth/status" && request->method() == HTTP_GET) {
      const std::string username = json_escape_string_(this->parent_->get_active_username());
      const std::string source = json_escape_string_(this->parent_->get_credential_source());
      const std::string csrf_token = json_escape_string_(this->parent_->get_csrf_token());
      auto* stream = request->beginResponseStream("application/json");
      stream->printf(R"({"enabled":%s,"setup_window_active":%s,"username":"%s","source":"%s","csrf_token":"%s"})",
                     this->parent_->is_auth_enabled() ? "true" : "false",
                     this->parent_->is_setup_window_active() ? "true" : "false", username.c_str(), source.c_str(),
                     csrf_token.c_str());
      lock.unlock();
      request->send(stream);
      return;
    }

    if (url == "/auth/change" && request->method() == HTTP_POST) {
      if (!this->passes_same_origin_(request) || !this->passes_csrf_(request)) {
        lock.unlock();
        request->send(409, "application/json", R"({"ok":false,"error":"forbidden"})");
        return;
      }

      const std::string current_password = request->arg("current_password");
      const std::string new_username = request->arg("new_username");
      const std::string new_password = request->arg("new_password");

      if (new_username.empty() || new_password.empty()) {
        lock.unlock();
        request->send(409, "application/json", R"({"ok":false,"error":"missing_fields"})");
        return;
      }
      if (this->parent_->is_auth_enabled()) {
        if (!this->parent_->request_is_authenticated_admin(request)) {
          lock.unlock();
          request->requestAuthentication();
          return;
        }
        if (!this->parent_->verify_current_password(current_password)) {
          lock.unlock();
          request->send(409, "application/json", R"({"ok":false,"error":"invalid_current_password"})");
          return;
        }
      } else if (!this->parent_->is_setup_window_active()) {
        lock.unlock();
        request->send(409, "application/json", R"({"ok":false,"error":"setup_window_required"})");
        return;
      }
      if (!this->parent_->set_runtime_credentials(new_username, new_password)) {
        lock.unlock();
        request->send(500, "application/json", R"({"ok":false,"error":"persist_failed"})");
        return;
      }

      const std::string username = json_escape_string_(this->parent_->get_active_username());
      const std::string source = json_escape_string_(this->parent_->get_credential_source());
      auto* stream = request->beginResponseStream("application/json");
      stream->printf(R"({"ok":true,"username":"%s","source":"%s"})", username.c_str(), source.c_str());
      lock.unlock();
      request->send(stream);
      return;
    }

    if (url == "/auth/disable" && request->method() == HTTP_POST) {
      if (!this->passes_same_origin_(request) || !this->passes_csrf_(request)) {
        lock.unlock();
        request->send(409, "application/json", R"({"ok":false,"error":"forbidden"})");
        return;
      }

      const std::string current_password = request->arg("current_password");

      if (!this->parent_->is_auth_enabled()) {
        lock.unlock();
        request->send(409, "application/json", R"({"ok":false,"error":"already_disabled"})");
        return;
      }
      if (!this->parent_->request_is_authenticated_admin(request)) {
        lock.unlock();
        request->requestAuthentication();
        return;
      }
      if (!this->parent_->verify_current_password(current_password)) {
        lock.unlock();
        request->send(409, "application/json", R"({"ok":false,"error":"invalid_current_password"})");
        return;
      }
      if (!this->parent_->set_open_access()) {
        lock.unlock();
        request->send(500, "application/json", R"({"ok":false,"error":"persist_failed"})");
        return;
      }

      lock.unlock();

      request->send(200, "application/json", R"({"ok":true,"enabled":false})");
      return;
    }

    if (url == "/api-security/status" && request->method() == HTTP_GET) {
      const bool transport_active = this->parent_->is_api_security_transport_active();
      auto* stream = request->beginResponseStream("application/json");
      stream->printf(R"({"transport_active":%s,"key_present":%s,"provisioning_pending":%s,"provisioning_closed":%s})",
                     transport_active ? "true" : "false",
                     this->parent_->is_api_security_key_present() ? "true" : "false",
                     this->parent_->is_api_provisioning_pending() ? "true" : "false",
                     this->parent_->is_api_provisioning_closed() ? "true" : "false");
      lock.unlock();
      request->send(stream);
      return;
    }

    lock.unlock();

    request->send(404);
  }

 protected:
  OpenQuattWebAuth* parent_;
};

void OpenQuattWebAuth::setup() {
  std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
#ifndef USE_WEBSERVER_AUTH
  ESP_LOGE(TAG, "web_server auth support is not enabled; keep a bootstrap web_server.auth block in YAML");
  return;
#else
  if (global_preferences == nullptr) {
    ESP_LOGE(TAG, "Preferences backend is unavailable");
    return;
  }
  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGE(TAG, "global_web_server_base is unavailable");
    return;
  }

  this->rotate_csrf_token_();
  this->register_http_handlers_();
  this->pref_ = global_preferences->make_preference<AuthStorage>(STORAGE_KEY, true);

  AuthStorage storage{};
  if (this->load_storage_(&storage)) {
    if (!this->apply_storage_(storage, "stored")) {
      ESP_LOGE(TAG, "Stored credentials could not be applied");
    }
  } else if (this->default_auth_enabled_) {
    if (!this->build_storage_(this->bootstrap_username_, this->bootstrap_password_, &storage)) {
      ESP_LOGE(TAG, "Bootstrap credentials could not be prepared");
    } else if (!this->save_storage_(storage) || !this->apply_storage_(storage, "bootstrap")) {
      ESP_LOGE(TAG, "Bootstrap credentials could not be applied");
    }
  } else {
    if (!this->build_storage_("", "", &storage)) {
      ESP_LOGE(TAG, "Open-access bootstrap state could not be prepared");
    } else if (!this->save_storage_(storage) || !this->apply_storage_(storage, "bootstrap-open")) {
      ESP_LOGE(TAG, "Open-access bootstrap state could not be applied");
    }
  }

#endif
}

void OpenQuattWebAuth::loop() {
  std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
  if (this->restore_suspended_auth_if_needed_()) {
    ESP_LOGW(TAG, "Recovery setup window expired; restored previous protected credentials");
  }
}

void OpenQuattWebAuth::dump_config() {
  std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
  ESP_LOGCONFIG(TAG, "OpenQuatt Web Auth");
  ESP_LOGCONFIG(TAG, "  Active username: %s",
                this->active_username_.empty() ? "<none>" : this->active_username_.c_str());
  ESP_LOGCONFIG(TAG, "  Credential source: %s",
                this->credential_source_.empty() ? "<unknown>" : this->credential_source_.c_str());
  ESP_LOGCONFIG(TAG, "  Setup window active: %s", YESNO(this->is_setup_window_active()));
  ESP_LOGCONFIG(TAG, "  HTTP handlers registered: %s", YESNO(this->handlers_registered_));
}

float OpenQuattWebAuth::get_setup_priority() const { return setup_priority::WIFI; }

bool OpenQuattWebAuth::set_runtime_credentials(const std::string& username, const std::string& password) {
  std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
  AuthStorage storage{};
  if (!this->build_storage_(username, password, &storage)) {
    return false;
  }
  if (!this->save_storage_(storage)) {
    return false;
  }
  this->clear_setup_window_();
  this->has_suspended_storage_ = false;
  return this->apply_storage_(storage, "runtime");
}

bool OpenQuattWebAuth::set_open_access(const char* source) {
  std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
  AuthStorage storage{};
  if (!this->build_storage_("", "", &storage)) {
    return false;
  }
  if (!this->save_storage_(storage)) {
    return false;
  }
  ESP_LOGW(TAG, "Runtime web auth disabled; interface is open on the local network");
  this->clear_setup_window_();
  this->has_suspended_storage_ = false;
  return this->apply_storage_(storage, source != nullptr ? source : "runtime-disabled");
}

bool OpenQuattWebAuth::start_recovery_window(uint32_t duration_ms) {
  std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
  if (duration_ms == 0) {
    return false;
  }

  this->setup_window_until_ms_ = static_cast<uint32_t>(millis()) + duration_ms;
  this->rotate_csrf_token_();

  if (!this->is_auth_enabled()) {
    ESP_LOGW(TAG, "Armed local setup window for open-access mode");
    return true;
  }

  if (!this->build_storage_(this->active_username_, this->active_password_, &this->suspended_storage_)) {
    return false;
  }
  this->has_suspended_storage_ = true;
  ESP_LOGW(TAG, "Armed recovery window and temporarily suspended protected web auth");
  return this->suspend_auth_runtime_("recovery-open");
}

bool OpenQuattWebAuth::load_storage_(AuthStorage* storage) {
  if (storage == nullptr) {
    return false;
  }
  if (!this->pref_.load(storage)) {
    return false;
  }
  return this->is_valid_storage_(*storage);
}

bool OpenQuattWebAuth::save_storage_(const AuthStorage& storage) {
  if (!this->pref_.save(&storage)) {
    ESP_LOGE(TAG, "Failed to save credentials to preferences");
    return false;
  }
  if (!global_preferences->sync()) {
    ESP_LOGE(TAG, "Failed to sync credentials to preferences");
    return false;
  }
  return true;
}

bool OpenQuattWebAuth::apply_storage_(const AuthStorage& storage, const char* source) {
  if (web_server_base::global_web_server_base == nullptr) {
    return false;
  }

  this->active_username_ = storage.username;
  this->active_password_ = storage.password;
  this->credential_source_ = source != nullptr ? source : "";

  web_server_base::global_web_server_base->set_auth_credentials(storage.username, storage.password);
  this->publish_state_();

  return true;
}

bool OpenQuattWebAuth::suspend_auth_runtime_(const char* source) {
  if (web_server_base::global_web_server_base == nullptr) {
    return false;
  }

  this->active_username_.clear();
  this->active_password_.clear();
  this->credential_source_ = source != nullptr ? source : "";

  web_server_base::global_web_server_base->set_auth_credentials("", "");
  this->publish_state_();
  return true;
}

bool OpenQuattWebAuth::build_storage_(const std::string& username, const std::string& password, AuthStorage* storage) {
  if (storage == nullptr) {
    return false;
  }
  const bool username_empty = username.empty();
  const bool password_empty = password.empty();
  if (username_empty != password_empty) {
    ESP_LOGE(TAG, "Username/password must either both be set or both be empty");
    return false;
  }
  if (!username_empty && username.size() > USERNAME_MAX_LEN) {
    ESP_LOGE(TAG, "Invalid username length");
    return false;
  }
  if (!password_empty && password.size() > PASSWORD_MAX_LEN) {
    ESP_LOGE(TAG, "Invalid password length");
    return false;
  }

  std::memset(storage, 0, sizeof(*storage));
  storage->magic = STORAGE_MAGIC;
  storage->version = STORAGE_VERSION;
  if (!username_empty) {
    std::strncpy(storage->username, username.c_str(), USERNAME_MAX_LEN);
    std::strncpy(storage->password, password.c_str(), PASSWORD_MAX_LEN);
  }
  return true;
}

bool OpenQuattWebAuth::is_api_security_transport_active() const {
  return api::global_api_server != nullptr && api::global_api_server->get_noise_ctx().has_psk();
}

bool OpenQuattWebAuth::is_api_security_key_present() const { return this->is_api_security_transport_active(); }

bool OpenQuattWebAuth::is_api_provisioning_pending() const {
  return provisioning::global_provisioning_manager != nullptr &&
         provisioning::global_provisioning_manager->window_pending();
}

bool OpenQuattWebAuth::is_api_provisioning_closed() const {
  return provisioning::global_provisioning_manager != nullptr && provisioning::global_provisioning_manager->closed();
}

bool OpenQuattWebAuth::is_valid_storage_(const AuthStorage& storage) const {
  if (storage.magic != STORAGE_MAGIC || storage.version != STORAGE_VERSION) {
    return false;
  }
  const bool username_empty = storage.username[0] == '\0';
  const bool password_empty = storage.password[0] == '\0';
  if (username_empty != password_empty) {
    return false;
  }
  if (username_empty && password_empty) {
    return true;
  }
  if (strnlen(storage.username, USERNAME_MAX_LEN + 1) > USERNAME_MAX_LEN) {
    return false;
  }
  if (strnlen(storage.password, PASSWORD_MAX_LEN + 1) > PASSWORD_MAX_LEN) {
    return false;
  }
  return true;
}

bool OpenQuattWebAuth::is_setup_window_active() const {
  std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
  return this->setup_window_until_ms_ != 0 && static_cast<uint32_t>(millis()) < this->setup_window_until_ms_;
}

void OpenQuattWebAuth::clear_setup_window_() { this->setup_window_until_ms_ = 0; }

void OpenQuattWebAuth::rotate_csrf_token_() {
  char token[33];
  const uint32_t part_a = esp_random();
  const uint32_t part_b = esp_random();
  const uint32_t part_c = esp_random();
  const uint32_t part_d = esp_random();
  std::snprintf(token, sizeof(token), "%08x%08x%08x%08x", static_cast<unsigned>(part_a), static_cast<unsigned>(part_b),
                static_cast<unsigned>(part_c), static_cast<unsigned>(part_d));
  this->csrf_token_ = token;
}

bool OpenQuattWebAuth::restore_suspended_auth_if_needed_() {
  if (this->setup_window_until_ms_ == 0 || this->is_setup_window_active()) {
    return false;
  }

  this->clear_setup_window_();
  if (!this->has_suspended_storage_) {
    return false;
  }

  this->has_suspended_storage_ = false;
  return this->apply_storage_(this->suspended_storage_, "stored");
}

void OpenQuattWebAuth::publish_state_() {}

void OpenQuattWebAuth::register_http_handlers_() {
  if (this->handlers_registered_) {
    return;
  }
  if (web_server_base::global_web_server_base == nullptr) {
    return;
  }
  web_server_base::global_web_server_base->add_handler(new OpenQuattWebAuthRequestHandler(this));
  this->handlers_registered_ = true;
}

}  // namespace openquatt_web_auth
}  // namespace esphome
