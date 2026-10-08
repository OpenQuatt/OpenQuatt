#include "OpenQuattWebAuth.h"

#include <cstdio>
#include <cstring>
#include "esp_http_server.h"
#include <nvs.h>

#include "esphome/core/application.h"
#include "esphome/components/api/api_server.h"
#include "esphome/components/provisioning/provisioning.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace openquatt_web_auth {

OpenQuattWebAuth* global_web_auth = nullptr;
OpenQuattWebAuth::OpenQuattWebAuth() { global_web_auth = this; }
bool normal_web_access_allowed() { return global_web_auth != nullptr && global_web_auth->normal_access_allowed(); }

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
      stream->addHeader("Cache-Control", "no-store");
      stream->printf(
          R"({"enabled":%s,"setup_window_active":%s,"username":"%s","source":"%s","csrf_token":"%s","busy":%s,"pending_reboot":%s,"error":"%s","generation":%u})",
          this->parent_->is_auth_enabled() ? "true" : "false", "false", username.c_str(), source.c_str(),
          csrf_token.c_str(), this->parent_->queued_ ? "true" : "false",
          this->parent_->pending_reboot() ? "true" : "false", this->parent_->error_,
          static_cast<unsigned>(this->parent_->generation_));
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
      } else {
        lock.unlock();
        request->send(409, "application/json", R"({"ok":false,"error":"setup_window_required"})");
        return;
      }
      OpenQuattWebAuth::AuthStorage candidate{};
      if (!this->parent_->build_storage_(new_username, new_password, &candidate) ||
          !this->parent_->queue_storage_(candidate)) {
        lock.unlock();
        request->send(500, "application/json", R"({"ok":false,"error":"persist_failed"})");
        return;
      }

      auto* stream = request->beginResponseStream("application/json");
      stream->printf(R"({"accepted":true,"generation":%u})", static_cast<unsigned>(this->parent_->generation_));
      lock.unlock();
      httpd_resp_set_status(*request, "202 Accepted");
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
      OpenQuattWebAuth::AuthStorage candidate{};
      if (!this->parent_->build_storage_("", "", &candidate) || !this->parent_->queue_storage_(candidate)) {
        lock.unlock();
        request->send(500, "application/json", R"({"ok":false,"error":"persist_failed"})");
        return;
      }

      auto* stream = request->beginResponseStream("application/json");
      stream->printf(R"({"accepted":true,"generation":%u})", static_cast<unsigned>(this->parent_->generation_));
      lock.unlock();
      httpd_resp_set_status(*request, "202 Accepted");
      request->send(stream);
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
#ifdef USE_WEBSERVER_AUTH
  auto* base = web_server_base::global_web_server_base;
  if (base == nullptr) return;
  // Unknown credentials deny access even if preferences/setup fail. Both pointers
  // remain owned by this component until the CPU resets.
  std::strcpy(this->boot_username_, "recovery");
  std::snprintf(this->boot_password_, sizeof(this->boot_password_), "%08x%08x%08x%08x",
                static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()),
                static_cast<unsigned>(esp_random()), static_cast<unsigned>(esp_random()));
  base->set_auth_username(this->boot_username_);
  base->set_auth_password(this->boot_password_);
  this->rotate_csrf_token_();
  this->register_http_handlers_();
  if (global_preferences == nullptr) return;
  this->pref_ = global_preferences->make_preference<AuthStorage>(STORAGE_KEY, true);
  AuthStorage storage{};
  // ESPPreferenceObject::load collapses missing/corrupt/unavailable into false.
  // Only a genuinely absent key may initialize a fresh-install policy.
  char key[11];
  std::snprintf(key, sizeof(key), "%u", static_cast<unsigned>(STORAGE_KEY));
  size_t stored_size = 0;
  const esp_err_t probe = nvs_get_blob(global_preferences->nvs_handle, key, nullptr, &stored_size);
  if (probe != ESP_OK && probe != ESP_ERR_NVS_NOT_FOUND) {
    this->error_ = "persist_failed";
    return;
  }
  const bool loaded = probe == ESP_OK;
  if (loaded && (stored_size != sizeof(storage) || !this->pref_.load(&storage) || !this->is_valid_storage_(storage))) {
    this->error_ = "invalid_storage";
    return;  // Never turn a corrupt record into open access.
  }
  if (!loaded) {
    if (!this->build_storage_(this->default_auth_enabled_ ? this->bootstrap_username_ : "",
                              this->default_auth_enabled_ ? this->bootstrap_password_ : "", &storage) ||
        !this->save_storage_(storage)) {
      this->error_ = "persist_failed";
      return;
    }
  }
  this->active_username_ = storage.username;
  this->active_password_ = storage.password;
  this->credential_source_ = loaded ? "stored" : (this->default_auth_enabled_ ? "bootstrap" : "bootstrap-open");
  if (!this->recovery_boot_) this->apply_storage_(storage, this->credential_source_.c_str());
  this->ready_.store(true, std::memory_order_release);
#endif
}

void OpenQuattWebAuth::loop() {
  std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
  if (!this->queued_) return;
  this->queued_ = false;
  const bool saved = this->save_storage_(this->pending_storage_);
  std::memset(&this->pending_storage_, 0, sizeof(this->pending_storage_));
  this->error_ = saved ? "" : "persist_failed";
  if (saved) {
    this->persisted_at_ = millis();
    this->pending_reboot_.store(true);
    this->close_normal_access();
  }
}

bool OpenQuattWebAuth::add_restart_blocker(void* context, RestartBusyCallback callback) {
  if (this->restart_blocker_count_ >= 6 || callback == nullptr) return false;
  this->restart_blockers_[this->restart_blocker_count_++] = {context, callback};
  return true;
}
bool OpenQuattWebAuth::restart_blocked() const {
  for (size_t i = 0; i < this->restart_blocker_count_; ++i)
    if (this->restart_blockers_[i].callback(this->restart_blockers_[i].context)) return true;
  return false;
}

bool OpenQuattWebAuth::queue_storage_(const AuthStorage& storage) {
  if (!this->normal_access_allowed() || this->queued_ || this->pending_reboot()) return false;
  this->pending_storage_ = storage;
  ++this->generation_;
  this->error_ = "";
  this->queued_ = true;
  return true;
}

void OpenQuattWebAuth::dump_config() {
  std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
  ESP_LOGCONFIG(TAG, "OpenQuatt Web Auth");
  ESP_LOGCONFIG(TAG, "  Active username: %s",
                this->active_username_.empty() ? "<none>" : this->active_username_.c_str());
  ESP_LOGCONFIG(TAG, "  Credential source: %s",
                this->credential_source_.empty() ? "<unknown>" : this->credential_source_.c_str());
  ESP_LOGCONFIG(TAG, "  HTTP handlers registered: %s", YESNO(this->handlers_registered_));
}

float OpenQuattWebAuth::get_setup_priority() const { return setup_priority::WIFI + 2.0f; }

bool OpenQuattWebAuth::request_is_authenticated(AsyncWebServerRequest* request) const {
  return this->normal_access_allowed() && request != nullptr &&
         request->authenticate(this->boot_username_, this->boot_password_);
}
bool OpenQuattWebAuth::request_is_authenticated_admin(AsyncWebServerRequest* request) const {
  return this->boot_username_[0] != '\0' && this->boot_password_[0] != '\0' && this->request_is_authenticated(request);
}

bool OpenQuattWebAuth::request_is_authenticated_reset_admin(AsyncWebServerRequest* request) const {
  return this->ready() && !this->recovery_boot_ && this->boot_username_[0] != '\0' && this->boot_password_[0] != '\0' &&
         request != nullptr && request->authenticate(this->boot_username_, this->boot_password_);
}

// Main-loop recovery entry: persistence never mutates the running auth pair.
bool OpenQuattWebAuth::set_runtime_credentials(const std::string& username, const std::string& password) {
  std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
  AuthStorage storage{};
  return !this->queued_ && !this->pending_reboot() && this->build_storage_(username, password, &storage) &&
         this->save_storage_(storage);
}

bool OpenQuattWebAuth::set_open_access(const char*) { return this->set_runtime_credentials("", ""); }

bool OpenQuattWebAuth::save_storage_(const AuthStorage& storage) {
  if (!this->pref_.save(&storage)) {
    ESP_LOGE(TAG, "Failed to save credentials to preferences");
    return false;
  }
  if (!global_preferences->sync()) {
    ESP_LOGE(TAG, "Failed to sync credentials to preferences");
    return false;
  }
  AuthStorage readback{};
  if (!this->pref_.load(&readback) || std::memcmp(&readback, &storage, sizeof(storage)) != 0) {
    ESP_LOGE(TAG, "Credentials readback did not match");
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

  std::memcpy(this->boot_username_, storage.username, sizeof(this->boot_username_));
  std::memcpy(this->boot_password_, storage.password, sizeof(this->boot_password_));

  return true;
}

bool OpenQuattWebAuth::build_storage_(const std::string& username, const std::string& password, AuthStorage* storage) {
  if (storage == nullptr) {
    return false;
  }
  if (username.find('\0') != std::string::npos || password.find('\0') != std::string::npos) return false;
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
