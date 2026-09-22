#pragma once

#include <mutex>
#include <string>

#include "RecoveryState.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/core/component.h"

namespace esphome::openquatt_recovery {

class OpenQuattRecovery : public Component, public AsyncWebHandler {
 public:
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* auth) { this->auth_ = auth; }
  void set_button(binary_sensor::BinarySensor* button) { this->button_ = button; }
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 0.5f; }
  bool canHandle(AsyncWebServerRequest* request) const override;
  void handleRequest(AsyncWebServerRequest* request) override;

 protected:
  enum class Action { NONE, WEB_AUTH, END, API_RESET, WIFI_RESET };
  void opened_();
  static void activate_on_httpd_(void* context);
  void prepare_reboot_handoff_();
  static std::string random_token_();
  bool authorize_(AsyncWebServerRequest* request, uint32_t now) const;

  // HTTP only queues bounded values; all persistence and lifecycle work runs
  // in loop(). Lock order: recovery -> web auth -> base credentials.
  std::mutex mutex_;
  RecoveryState state_;
  openquatt_web_auth::OpenQuattWebAuth* auth_;
  binary_sensor::BinarySensor* button_;
  Action pending_{Action::NONE};
  uint32_t accepted_at_{0};
  std::string csrf_token_;
  std::string username_;
  std::string password_;
  const char* error_{""};
  bool activating_{false};
  bool activation_complete_{false};
};

}  // namespace esphome::openquatt_recovery
