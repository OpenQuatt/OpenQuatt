#pragma once

#include <esp_partition.h>
#include "PsramBuffer.h"
#include <nvs.h>
#include "esphome/components/factory_reset/factory_reset.h"
#include "esphome/components/openquatt_odu_settings/OpenQuattOduSettings.h"
#include "esphome/components/openquatt_odu_defrost/OpenQuattOduDefrost.h"
#include "includes/odu/oq_odu_eeprom_image.h"
#include "includes/odu/oq_odu_eeprom_maintenance.h"

namespace esphome::openquatt_odu_eeprom {

class OpenQuattOduEeprom : public Component, public modbus::ModbusClientDevice {
 public:
  enum class Action : uint8_t { NONE, BEGIN_BACKUP, CAPTURE, PREPARE, COMMIT, VERIFY, CANCEL, RESTORE, CHECK_WRITER };
  void set_controller(modbus_controller::ModbusController* value) { controller_ = value; }
  void set_eeprom_dump(openquatt_odu_eeprom_dump::OpenQuattOduEepromDump* value) { dump_ = value; }
  void set_settings(openquatt_odu_settings::OpenQuattOduSettings* value) { settings_ = value; }
  void set_defrost(openquatt_odu_defrost::OpenQuattOduDefrost* value) { defrost_ = value; }
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* value) { auth_ = value; }
  void set_factory_reset_counter(factory_reset::FactoryResetComponent* value) { factory_reset_ = value; }
  void set_hp_index(uint8_t value) {
    hp_ = value;
    oq_eeprom::expected_mask |= 1U << (hp_ - 1U);
  }
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 3.0f; }
  bool enqueue(Action action, uint8_t groups, uint32_t generation, bool cold_confirmed);
  bool authenticated(AsyncWebServerRequest* request) const { return auth_->request_is_authenticated_admin(request); }
  std::string csrf() const { return auth_->get_csrf_token(); }
  void write_status(httpd_req_t* request, bool download = false);
  void offline();

 protected:
  enum class Step : uint8_t {
    IDLE,
    IDENTITY,
    SERIAL,
    GUARD,
    COMMIT_READY,
    CHECK_WRITER,
    READ,
    WRITE,
    READ_STAGED,
    COMMIT,
    POLL,
    READ_FINAL
  };
  struct Status {
    bool available{false}, busy{false}, supported{false}, baseline{false}, cold_seen{false}, holding{false};
    bool legacy{false}, power_cycle_safe{false};
    uint8_t state{0U}, groups{0U};
    uint16_t changes{0U}, reverted{0U}, crc{0U};
    uint32_t generation{0U};
    const char* result{"EMPTY"};
  };
  modbus_controller::ModbusController* controller_{nullptr};
  openquatt_odu_eeprom_dump::OpenQuattOduEepromDump* dump_{nullptr};
  openquatt_odu_settings::OpenQuattOduSettings* settings_{nullptr};
  openquatt_odu_defrost::OpenQuattOduDefrost* defrost_{nullptr};
  openquatt_web_auth::OpenQuattWebAuth* auth_{nullptr};
  factory_reset::FactoryResetComponent* factory_reset_{nullptr};
  const esp_partition_t* partition_{nullptr};
  openquatt_common::PsramBuffer<oq_eeprom::Record> records_{};
  openquatt_common::PsramBuffer<oq_eeprom::Image> image_{};
  openquatt_common::PsramBuffer<uint16_t> frame_{};
  oq_eeprom::Identity identity_{};
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  Status status_{};
  Action pending_{Action::NONE}, action_{Action::NONE};
  uint8_t hp_{0U}, groups_{0U}, commit_index_{0U};
  uint32_t expected_generation_{0U}, request_epoch_{0U}, started_ms_{0U}, next_ms_{0U}, stop_epoch_{0U};
  uint32_t request_started_ms_{0U};
  uint16_t address_{0U}, count_{0U}, offset_{0U};
  int8_t slot_{-1};
  Step step_{Step::IDLE};
  bool waiting_{false}, response_{false}, response_ok_{false}, writing_{false}, reserved_{false};
  bool cold_seen_{false}, cold_confirmed_{false}, valid_record_{false}, storage_bad_{false};
  bool modification_started_{false}, data_mode_{false};
  bool worker_may_run_{true}, power_cycle_safe_{false};
  bool downloading_{false};
  std::atomic<bool> offline_pending_{false};
  oq_eeprom::Record& record_() { return records_[0]; }
  const char* slot_key_(uint8_t slot) const;
  bool load_();
  bool persist_(oq_eeprom::State state);
  bool reserve_();
  void release_();
  void publish_(const char* result);
  void finish_(const char* result);
  void fail_(const char* result);
  void request_read_(uint16_t address, uint16_t count);
  void request_write_(uint16_t address, uint16_t value);
  void request_block_();
  void advance_();
  void image_read_done_();
  bool revoke_profiles_();
  bool cold_ready_() const;
  void on_response(std::span<const uint8_t> request, std::span<const uint8_t> response) override;
  void on_error(std::span<const uint8_t> request, modbus::ExceptionCode error) override;
  bool on_no_response(std::span<const uint8_t> request) override;
  void on_not_sent(std::span<const uint8_t> request) override;
};

}  // namespace esphome::openquatt_odu_eeprom
