#pragma once

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include "esphome/core/component.h"
#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/openquatt_odu_eeprom_dump/OpenQuattOduEepromDump.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "includes/control/oq_defrost_logic.h"
#include "includes/odu/oq_odu_defrost_diagnostics.h"

namespace esphome::openquatt_odu_defrost {

// HTTP only enqueues an action or copies this small, locked snapshot. All
// Modbus, observations and cycle ownership are confined to the main loop.
struct Snapshot {
  bool online{false}, fresh{false}, identity{false}, loaded{false}, automatic{false}, busy{false};
  bool active{false}, manual{false}, can_trigger{false};
  int mode{-1}, operation_mode{-1};
  float ambient{NAN}, coil{NAN}, evaporation{NAN}, hz{NAN};
  int elapsed_s{-1}, since_s{-1}, duration_s{-1};
  const char* state{"IDLE"};
  const char* guard{"OFFLINE"};
  oq_defrost::Diagnostics diagnostics{};
};

class OpenQuattOduDefrost : public Component, public modbus::ModbusClientDevice {
 public:
  enum class Action : uint8_t { NONE, LOAD, TRIGGER, SAVE };
  void set_controller(modbus_controller::ModbusController* value) { controller_ = value; }
  void set_eeprom_dump(openquatt_odu_eeprom_dump::OpenQuattOduEepromDump* value) { dump_ = value; }
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* value) { auth_ = value; }
  void set_hp_index(uint8_t value) { hp_ = value; }
  void set_odu_identity(oq_odu::Variant variant);
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 2.0f; }
  bool authenticated(AsyncWebServerRequest* request) const { return auth_->request_is_authenticated(request); }
  std::string csrf() const { return auth_->get_csrf_token(); }
  bool enqueue(Action action);
  bool enqueue_save(int desired, int expected);
  bool supports_mode(int mode) const;
  oq_odu::Variant variant() const;
  void write_status(httpd_req_t* req);

  // Main-loop interface used by the thermal actuator and sensor callbacks.
  oq_defrost::Cycle cycle;
  void observe(unsigned slot, uint32_t now) {
    sample_ms_[slot] = now;
    sample_seen_[slot] = true;
  }
  bool sample_fresh(unsigned slot, uint32_t now) const {
    return sample_seen_[slot] && now - sample_ms_[slot] <= oq_defrost::FRESH_MS;
  }
  void offline();
  void update(Snapshot values, uint32_t now);
  bool take_trigger();
  bool take_save(int& desired, int& expected);
  void reject(const char* reason);
  void confirm_saved();
  bool send_forced_once(uint32_t now);
  bool send_mode_once(int desired, uint32_t now);
  bool normal_write_allowed(int value, bool safety_stop);
  bool loaded() const { return parameters_.loaded; }
  bool automatic() const { return parameters_.automatic(); }
  int current_mode() const { return parameters_.loaded ? parameters_.mode() : -1; }
  bool busy() const {
    return loading_ || trigger_ready_ || save_ready_ || save_writing_ || cycle.owns() ||
           cycle.phase == oq_defrost::Phase::RESYNC;
  }
  bool holding() const {
    return cycle.owns() || trigger_ready_ || save_ready_ || save_writing_ || (loading_ && trigger_after_load_) ||
           (loading_ && save_after_load_);
  }
  bool reserved() const { return reserved_; }
  void release();
  void on_read_holding_registers(uint16_t address, std::span<const uint16_t> values,
                                 modbus::ResponseStatus status) override;
  void on_not_sent(std::span<const uint8_t>) override;
  bool on_no_response(std::span<const uint8_t>) override;
  void on_error(std::span<const uint8_t>, modbus::ExceptionCode) override;
  void on_response(std::span<const uint8_t> request, std::span<const uint8_t> response) override;

 private:
  modbus_controller::ModbusController* controller_{nullptr};
  openquatt_odu_eeprom_dump::OpenQuattOduEepromDump* dump_{nullptr};
  openquatt_web_auth::OpenQuattWebAuth* auth_{nullptr};
  uint8_t hp_{1};
  oq_odu::Variant variant_{oq_odu::Variant::UNKNOWN};
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  Snapshot snapshot_{};
  Action pending_{Action::NONE};
  bool action_pending_{false};
  bool loading_{false}, trigger_after_load_{false}, trigger_ready_{false}, reserved_{false};
  bool save_after_load_{false}, save_ready_{false}, save_writing_{false}, save_verifying_{false};
  int save_desired_{-1}, save_expected_{-1}, save_write_{-1};
  oq_defrost::Parameters parameters_{};
  oq_defrost::Diagnostics diagnostics_{};
  uint32_t sample_ms_[4]{};
  bool sample_seen_[4]{};
  uint8_t block_{0};
  oq_defrost::Phase previous_phase_{oq_defrost::Phase::IDLE};
  uint32_t loading_ms_{0};
  uint32_t save_ms_{0};
  bool read_block_();
  bool read_base_();
  uint8_t parameter_block_count_() const;
  void fail_(const char* reason);
};
}  // namespace esphome::openquatt_odu_defrost
