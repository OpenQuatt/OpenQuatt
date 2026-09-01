#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>

#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/openquatt_odu_eeprom_dump/OpenQuattOduEepromDump.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "includes/experimental/oq_odu_runtime_frequency_table_logic.h"

namespace esphome {
namespace openquatt_odu_runtime_frequency {

class OpenQuattOduRuntimeFrequency : public Component {
 public:
  enum class RequestResult : uint8_t { ACCEPTED = 0, BUSY, UNAVAILABLE, NOT_LOADED, NOT_ARMED, INVALID_TABLE };

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

  RequestResult request_load();
  RequestResult request_arm(bool enabled);
  RequestResult request_apply(const oq_odu_runtime_frequency::RuntimeFrequencyTables& tables);
  void set_extended_layout(bool extended_layout);
  void reset_runtime_state(const char* failure_message = nullptr);
  bool runtime_reload_blocked_after_write() const { return this->write_tainted_.load(std::memory_order_acquire); }

  bool request_is_authenticated(AsyncWebServerRequest* request) const {
    return this->web_auth_ != nullptr && this->web_auth_->request_is_authenticated(request);
  }
  const std::string& get_csrf_token() const { return this->web_auth_->get_csrf_token(); }
  void write_status(httpd_req_t* req) const;

  void add_on_write_started_callback(std::function<void()> callback) {
    this->write_started_callbacks_.add(std::move(callback));
  }
  void add_on_write_applied_callback(std::function<void()> callback) {
    this->write_applied_callbacks_.add(std::move(callback));
  }

 protected:
  static constexpr uint16_t GUARD_START_ADDRESS = 2099U;
  static constexpr uint16_t GUARD_REGISTER_COUNT = 5U;
  static constexpr size_t GUARD_WORKING_MODE_INDEX = 0U;
  static constexpr size_t GUARD_COMPRESSOR_FREQUENCY_INDEX = 4U;
  static constexpr uint32_t BASE_OPERATION_TIMEOUT_MS = 30000U;
  static constexpr uint32_t EXTENDED_OPERATION_TIMEOUT_MS = 60000U;

  enum class PendingAction : uint8_t { NONE = 0, LOAD, APPLY };
  enum class Operation : uint8_t { NONE = 0, LOAD, APPLY };

  modbus_controller::ModbusController* controller_{nullptr};
  openquatt_odu_eeprom_dump::OpenQuattOduEepromDump* eeprom_dump_{nullptr};
  openquatt_web_auth::OpenQuattWebAuth* web_auth_{nullptr};
  uint8_t hp_index_{0U};

  std::atomic<bool> available_{false};
  std::atomic<bool> busy_{false};
  std::atomic<bool> loaded_{false};
  std::atomic<bool> armed_{false};
  std::atomic<bool> extended_layout_{false};
  std::atomic<bool> write_tainted_{false};
  std::atomic<uint32_t> operation_token_{0U};

  mutable portMUX_TYPE state_mux_ = portMUX_INITIALIZER_UNLOCKED;
  oq_odu_runtime_frequency::RuntimeFrequencyTables tables_{};
  oq_odu_runtime_frequency::RuntimeFrequencyTables operation_tables_{};
  PendingAction pending_action_{PendingAction::NONE};
  uint32_t pending_request_token_{0U};
  Operation operation_{Operation::NONE};
  std::atomic<uint32_t> bus_reservation_token_{0U};
  bool write_started_{false};
  uint32_t operation_started_ms_{0U};
  uint32_t operation_timeout_ms_{BASE_OPERATION_TIMEOUT_MS};
  char status_[96]{"READY: load ODU runtime table"};

  CallbackManager<void()> write_started_callbacks_{};
  CallbackManager<void()> write_applied_callbacks_{};

  bool begin_request_(uint32_t& request_token);
  void release_bus_(uint32_t request_token = 0U);
  void set_status_locked_(const char* status);
  const char* reset_runtime_state_locked_(const char* failure_message);
  void finish_without_write_(const char* status, uint32_t operation_token);
  void fail_operation_(const char* status, uint32_t operation_token);
  void finish_apply_(const oq_odu_runtime_frequency::RuntimeFrequencyTables& actual, uint32_t operation_token);
  bool token_matches_(uint32_t operation_token) const;
  bool begin_operation_(Operation operation, uint32_t operation_token);
  void queue_load_base_(uint32_t operation_token);
  void queue_load_extension_(oq_odu_runtime_frequency::RuntimeFrequencyTables tables, uint32_t operation_token);
  void finish_load_(const oq_odu_runtime_frequency::RuntimeFrequencyTables& tables, uint32_t operation_token);
  void queue_guard_(uint32_t operation_token);
  void begin_write_(uint32_t operation_token);
  void queue_write_register_(size_t write_index, uint32_t operation_token);
  void queue_readback_base_(uint32_t operation_token);
  void queue_readback_extension_(oq_odu_runtime_frequency::RuntimeFrequencyTables actual, uint32_t operation_token);
};

class RuntimeWriteStartedTrigger : public Trigger<> {
 public:
  explicit RuntimeWriteStartedTrigger(OpenQuattOduRuntimeFrequency* parent) {
    parent->add_on_write_started_callback([this]() { this->trigger(); });
  }
};

class RuntimeWriteAppliedTrigger : public Trigger<> {
 public:
  explicit RuntimeWriteAppliedTrigger(OpenQuattOduRuntimeFrequency* parent) {
    parent->add_on_write_applied_callback([this]() { this->trigger(); });
  }
};

}  // namespace openquatt_odu_runtime_frequency
}  // namespace esphome
