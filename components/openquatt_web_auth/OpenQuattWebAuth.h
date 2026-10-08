#pragma once

#include <string>
#include <mutex>
#include <atomic>

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

  OpenQuattWebAuth();
  void setup() override;
  void loop() override;
  void set_recovery_boot(bool recovery) { this->recovery_boot_ = recovery; }
  bool recovery_boot() const { return this->recovery_boot_; }
  bool ready() const { return this->ready_.load(std::memory_order_acquire); }
  bool normal_access_allowed() const { return this->ready() && !this->recovery_boot_ && !this->closing_.load(); }
  bool restart_requested() const { return this->closing_.load(); }
  void close_normal_access() { this->closing_.store(true); }
  using RestartBusyCallback = bool (*)(void*);
  bool add_restart_blocker(void* context, RestartBusyCallback callback);
  bool restart_blocked() const;
  bool pending_reboot() const { return this->pending_reboot_.load(); }
  uint32_t persisted_at() const { return this->persisted_at_; }
  void dump_config() override;
  float get_setup_priority() const override;

  bool set_runtime_credentials(const std::string& username, const std::string& password);
  bool set_open_access(const char* source = "runtime-disabled");

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
  bool request_is_authenticated(AsyncWebServerRequest* request) const;
  bool request_is_authenticated_admin(AsyncWebServerRequest* request) const;
  // Only explicit destructive-reset routes may retry while normal access is closed.
  bool request_is_authenticated_reset_admin(AsyncWebServerRequest* request) const;
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

  bool queue_storage_(const AuthStorage& storage);
  bool save_storage_(const AuthStorage& storage);
  bool apply_storage_(const AuthStorage& storage, const char* source);
  bool build_storage_(const std::string& username, const std::string& password, AuthStorage* storage);
  bool is_valid_storage_(const AuthStorage& storage) const;

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
  // Snapshot buffers are installed before any listener and never changed this boot.
  char boot_username_[USERNAME_MAX_LEN + 1]{};
  char boot_password_[PASSWORD_MAX_LEN + 1]{};
  struct RestartBlocker {
    void* context;
    RestartBusyCallback callback;
  };
  RestartBlocker restart_blockers_[6]{};
  size_t restart_blocker_count_{0};
  AuthStorage pending_storage_{};
  uint32_t generation_{0};
  uint32_t persisted_at_{0};
  bool queued_{false};
  bool recovery_boot_{false};
  const char* error_{""};
  std::atomic<bool> ready_{false};
  std::atomic<bool> closing_{false};
  std::atomic<bool> pending_reboot_{false};
};

extern OpenQuattWebAuth* global_web_auth;
bool normal_web_access_allowed();

}  // namespace openquatt_web_auth
}  // namespace esphome
