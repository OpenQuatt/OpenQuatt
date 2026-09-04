#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>

#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/openquatt_odu_eeprom_dump/OpenQuattOduEepromDump.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "includes/odu/oq_odu_bottom_plate_settings.h"

namespace esphome {
namespace openquatt_odu_settings {

class OpenQuattOduSettings : public Component {
 public:
  enum class RequestResult : uint8_t { ACCEPTED = 0, BUSY, UNAVAILABLE, IDENTITY_REQUIRED, INVALID_SETTINGS };

  void set_controller(modbus_controller::ModbusController* controller) { this->controller_ = controller; }
  void set_eeprom_dump(openquatt_odu_eeprom_dump::OpenQuattOduEepromDump* eeprom_dump) {
    this->eeprom_dump_ = eeprom_dump;
  }
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* web_auth) { this->web_auth_ = web_auth; }
  void set_hp_index(uint8_t hp_index) { this->hp_index_ = hp_index; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_odu_identity(uint16_t control_board_item, oq_odu::Variant variant);
  void notify_odu_offline();
  RequestResult request_load();
  RequestResult request_save(const oq_odu::BottomPlateSettings& settings, bool auto_reapply);

  bool request_is_authenticated(AsyncWebServerRequest* request) const {
    return this->web_auth_ != nullptr && this->web_auth_->request_is_authenticated(request);
  }
  const std::string& get_csrf_token() const { return this->web_auth_->get_csrf_token(); }
  void write_status(httpd_req_t* req) const;

 protected:
  static constexpr uint16_t GUARD_START_ADDRESS = 2099U;
  static constexpr uint16_t GUARD_REGISTER_COUNT = 5U;
  static constexpr size_t GUARD_WORKING_MODE_INDEX = 0U;
  static constexpr size_t GUARD_COMPRESSOR_FREQUENCY_INDEX = 4U;
  static constexpr uint32_t OPERATION_TIMEOUT_MS = 30000U;
  static constexpr uint32_t SAFE_RETRY_MS = 60000U;
  static constexpr uint32_t FAILURE_RETRY_MS = 300000U;
  static constexpr uint32_t PERIODIC_RECONCILE_MS = 21600000U;

  enum class PendingAction : uint8_t { NONE = 0, LOAD, SAVE, RECONCILE };
  enum class Operation : uint8_t { NONE = 0, LOAD, APPLY, RECONCILE };

  modbus_controller::ModbusController* controller_{nullptr};
  openquatt_odu_eeprom_dump::OpenQuattOduEepromDump* eeprom_dump_{nullptr};
  openquatt_web_auth::OpenQuattWebAuth* web_auth_{nullptr};
  uint8_t hp_index_{0U};
  ESPPreferenceObject profile_pref_{};

  std::atomic<bool> available_{false};
  std::atomic<bool> online_{false};
  std::atomic<bool> busy_{false};
  std::atomic<bool> loaded_{false};
  std::atomic<bool> identity_ready_{false};
  std::atomic<bool> profile_available_{false};
  std::atomic<bool> auto_reapply_{false};
  std::atomic<bool> manual_apply_pending_{false};
  std::atomic<bool> write_tainted_{false};
  std::atomic<uint32_t> operation_token_{0U};
  std::atomic<uint32_t> bus_reservation_token_{0U};

  mutable portMUX_TYPE state_mux_ = portMUX_INITIALIZER_UNLOCKED;
  oq_odu::BottomPlateSettings actual_{};
  oq_odu::BottomPlateSettings desired_{};
  oq_odu::BottomPlateProfileStorage stored_profile_{};
  oq_odu::BottomPlateProfileStorage pending_profile_{};
  oq_odu::Variant variant_{oq_odu::Variant::UNKNOWN};
  uint16_t control_board_item_{0U};
  PendingAction pending_action_{PendingAction::NONE};
  uint32_t pending_request_token_{0U};
  Operation operation_{Operation::NONE};
  bool write_started_{false};
  size_t write_index_{0U};
  uint32_t operation_started_ms_{0U};
  uint32_t reconcile_due_ms_{0U};
  char status_[96]{"READY"};

  bool begin_request_(uint32_t& request_token);
  bool begin_reconcile_();
  bool begin_operation_(Operation operation, uint32_t operation_token);
  bool token_matches_(uint32_t operation_token) const;
  bool identity_matches_profile_() const;
  bool persist_profile_(const oq_odu::BottomPlateProfileStorage& profile);
  void release_bus_(uint32_t request_token = 0U);
  void set_status_locked_(const char* status);
  void schedule_reconcile_(uint32_t delay_ms);
  void finish_operation_(const char* status, uint32_t operation_token, uint32_t next_reconcile_delay_ms = 0U);
  void fail_operation_(const char* status, uint32_t operation_token);
  void queue_settings_read_(uint32_t operation_token);
  void handle_settings_read_(const oq_odu::BottomPlateSettings& settings, uint32_t operation_token);
  void queue_guard_(uint32_t operation_token);
  void queue_next_write_(uint32_t operation_token);
  void queue_readback_(uint32_t operation_token);
};

}  // namespace openquatt_odu_settings
}  // namespace esphome
