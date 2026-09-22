#pragma once

#include <string>
#include <mutex>

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace openquatt_web_auth {

class OpenQuattWebAuth : public Component {
  friend class OpenQuattWebAuthRequestHandler;

 public:
  void set_bootstrap_username(const std::string& bootstrap_username) { this->bootstrap_username_ = bootstrap_username; }
  void set_bootstrap_password(const std::string& bootstrap_password) { this->bootstrap_password_ = bootstrap_password; }
  void set_default_auth_enabled(bool default_auth_enabled) { this->default_auth_enabled_ = default_auth_enabled; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;

  bool set_runtime_credentials(const std::string& username, const std::string& password);
  bool set_open_access(const char* source = "runtime-disabled");
  void begin_recovery_guard(const std::string& password);
  void end_recovery_guard();
  std::string get_active_username() const {
    std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
    return this->active_username_;
  }
  std::string get_credential_source() const {
    std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
    return this->credential_source_;
  }
  bool is_auth_enabled() const {
    std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
    return !this->active_username_.empty();
  }
  bool verify_current_password(const std::string& password) const {
    std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
    return password == this->active_password_;
  }
  std::string get_csrf_token() const {
    std::lock_guard<std::recursive_mutex> lock(this->state_mutex_);
    return this->csrf_token_;
  }
  bool request_is_authenticated(AsyncWebServerRequest* request) const {
    return web_server_base::global_web_server_base != nullptr &&
           web_server_base::global_web_server_base->request_is_authenticated(request);
  }
  bool request_is_authenticated_admin(AsyncWebServerRequest* request) const {
    return web_server_base::global_web_server_base != nullptr &&
           web_server_base::global_web_server_base->request_is_authenticated(request, true);
  }
  bool is_api_security_transport_active() const;
  bool is_api_security_key_present() const;
  bool is_api_provisioning_pending() const;
  bool is_api_provisioning_closed() const;

 protected:
  // HTTP handlers hold one state transaction across policy checks and mutation.
  // Public methods/getters also lock for main-loop callers, hence recursive.
  // Lock order: component state -> base credentials. Middleware releases the
  // base lock before calling a handler, so it never takes these in reverse.
  mutable std::recursive_mutex state_mutex_;
  static constexpr uint32_t STORAGE_MAGIC = 0x4F514157;
  static constexpr uint16_t STORAGE_VERSION = 1;
  static constexpr size_t USERNAME_MAX_LEN = 32;
  static constexpr size_t PASSWORD_MAX_LEN = 64;

  struct AuthStorage {
    uint32_t magic;
    uint16_t version;
    char username[USERNAME_MAX_LEN + 1];
    char password[PASSWORD_MAX_LEN + 1];
  };

  static_assert(sizeof(AuthStorage) == 104U, "Web authentication NVS budget changed");

  bool load_storage_(AuthStorage* storage);
  bool save_storage_(const AuthStorage& storage);
  bool apply_storage_(const AuthStorage& storage, const char* source);
  bool build_storage_(const std::string& username, const std::string& password, AuthStorage* storage);
  bool is_valid_storage_(const AuthStorage& storage) const;
  void publish_state_();
  void register_http_handlers_();
  void rotate_csrf_token_();

  std::string bootstrap_username_;
  std::string bootstrap_password_;
  std::string active_username_;
  std::string active_password_;
  std::string credential_source_;
  std::string csrf_token_;
  bool default_auth_enabled_{true};
  ESPPreferenceObject pref_;
  bool handlers_registered_{false};
  bool recovery_guard_active_{false};
};

}  // namespace openquatt_web_auth
}  // namespace esphome
