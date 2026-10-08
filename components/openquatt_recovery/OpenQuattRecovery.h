#pragma once

#include <mutex>
#include <string>
#include <functional>
#include "esphome/components/ota/ota_backend.h"

#include "RecoveryState.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/core/component.h"

namespace esphome::openquatt_recovery {

class OpenQuattRecovery : public Component, public AsyncWebHandler, public ota::OTAGlobalStateListener {
 public:
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* auth) { this->auth_ = auth; }
  void set_button(binary_sensor::BinarySensor* button) { this->button_ = button; }
  void set_restart_handler(std::function<void()> handler) { this->restart_handler_ = std::move(handler); }
  void set_web_ota(ota::OTAComponent* component) { this->web_ota_ = component; }
  void on_ota_global_state(ota::OTAState state, float, uint8_t, ota::OTAComponent* component) override {
    this->web_ota_active_.store(component != nullptr && component == this->web_ota_);
    if (state == ota::OTA_COMPLETED) this->ota_completed_.store(true);
    this->ota_active_ = state == ota::OTA_STARTED || state == ota::OTA_IN_PROGRESS;
  }
  void set_runtime_ready() { this->runtime_ready_.store(true, std::memory_order_release); }
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::WIFI + 3.0f; }
  bool canHandle(AsyncWebServerRequest* request) const override;
  void handleRequest(AsyncWebServerRequest* request) override;

 protected:
  enum class Action { NONE, RECOVERY_BOOT, WEB_AUTH, END, API_RESET, WIFI_RESET };
  void restart_(bool recovery);
  static void httpd_drained_(void* context);
  void prepare_reboot_handoff_();
  static std::string random_token_();
  bool authorize_(AsyncWebServerRequest* request, uint32_t now) const;

  // HTTP only queues bounded values; all persistence and lifecycle work runs
  // in loop(). Lock order: recovery -> web auth -> base credentials.
  std::mutex mutex_;
  RecoveryState state_;
  openquatt_web_auth::OpenQuattWebAuth* auth_{nullptr};
  binary_sensor::BinarySensor* button_{nullptr};
  Action pending_{Action::NONE};
  uint32_t accepted_at_{0};
  std::string csrf_token_;
  char username_[33]{};
  char password_[65]{};
  const char* error_{""};
  std::function<void()> restart_handler_;
  std::atomic<bool> runtime_ready_{false};
  bool recovery_boot_{false};
  bool restore_recovery_{false};
  ota::OTAComponent* web_ota_{nullptr};
  std::atomic<bool> web_ota_active_{false};
  std::atomic<bool> ota_active_{false};
  std::atomic<bool> ota_completed_{false};
  std::atomic<bool> barrier_ready_{false};
  bool barrier_queued_{false};
  bool barrier_seen_{false};
  bool storage_failed_{false};
};

}  // namespace esphome::openquatt_recovery
