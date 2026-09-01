#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>

#include "PsramBuffer.h"
#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"

namespace esphome {
namespace openquatt_odu_eeprom_dump {

using openquatt_common::PsramBuffer;

class OpenQuattOduEepromDump : public Component {
 public:
  enum class StartResult : uint8_t { STARTED = 0, BUSY, UNAVAILABLE };

  void set_controller(modbus_controller::ModbusController* controller) { this->controller_ = controller; }
  void set_clock(time::RealTimeClock* clock) { this->clock_ = clock; }
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* web_auth) { this->web_auth_ = web_auth; }
  void set_hp_index(uint8_t hp_index) { this->hp_index_ = hp_index; }
  void set_device_address(uint8_t device_address) { this->device_address_ = device_address; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  bool is_active() const {
    return this->active_.load(std::memory_order_acquire) ||
           this->external_operation_active_.load(std::memory_order_acquire);
  }
  bool request_is_authenticated(AsyncWebServerRequest* request) const {
    return this->web_auth_ != nullptr && this->web_auth_->request_is_authenticated(request);
  }
  StartResult start(bool include_extended_metadata);
  bool try_begin_external_operation();
  void end_external_operation();
  void write_status(httpd_req_t* req) const;
  bool begin_download();
  void write_download(httpd_req_t* req) const;
  void end_download();

 protected:
  static constexpr uint16_t EEPROM_START_ADDRESS = 2999;
  static constexpr uint16_t EEPROM_REGISTER_COUNT = 512;
  static constexpr uint16_t EEPROM_CRC_DATA_COUNT = 510;
  // MGMT recommends reading the EEPROM as a whole. FC03 is limited to 125 registers per request,
  // so use the largest legal contiguous chunks instead of many small diagnostic reads.
  static constexpr uint16_t EEPROM_BLOCK_SIZE = modbus::MAX_NUM_OF_REGISTERS_TO_READ;
  static constexpr uint16_t CORE_START_ADDRESS = 2114;
  static constexpr uint16_t CORE_REGISTER_COUNT = 14;
  static constexpr uint16_t EXTENDED_START_ADDRESS = 11004;
  static constexpr uint16_t EXTENDED_REGISTER_COUNT = 6;
  static constexpr uint16_t MODEL_START_ADDRESS = 11120;
  static constexpr uint16_t CUSTOMER_MODEL_START_ADDRESS = 11160;
  static constexpr uint16_t SERIAL_START_ADDRESS = 11219;
  static constexpr uint16_t TEXT_REGISTER_COUNT = 20;
  static constexpr uint32_t BUS_ACQUIRE_TIMEOUT_MS = 60000;
  static constexpr uint32_t EMPTY_QUEUE_GRACE_MS = 500;
  static constexpr uint32_t FAILURE_COOLDOWN_MS = 1000;
  static constexpr uint32_t REQUEST_TIMEOUT_MS = 30000;

  enum class Step : uint8_t {
    WAITING_BUS = 0,
    EXTENDED,
    MODEL,
    CUSTOMER_MODEL,
    SERIAL,
    CORE,
    EEPROM,
    VERIFYING,
    COMPLETE,
    FAILED,
  };

  enum Warning : uint8_t {
    WARNING_NONE = 0,
    WARNING_EXTENDED_UNAVAILABLE = 1U << 0U,
    WARNING_CORE_UNAVAILABLE = 1U << 1U,
    WARNING_RUNTIME_DIFFERS = 1U << 2U,
  };

  modbus_controller::ModbusController* controller_{nullptr};
  time::RealTimeClock* clock_{nullptr};
  openquatt_web_auth::OpenQuattWebAuth* web_auth_{nullptr};
  uint8_t hp_index_{0};
  uint8_t device_address_{0};
  PsramBuffer<uint16_t> eeprom_{};
  std::array<uint16_t, EXTENDED_REGISTER_COUNT> extended_{};
  std::array<uint16_t, TEXT_REGISTER_COUNT> model_{};
  std::array<uint16_t, TEXT_REGISTER_COUNT> customer_model_{};
  std::array<uint16_t, TEXT_REGISTER_COUNT> serial_{};
  std::array<uint16_t, CORE_REGISTER_COUNT> core_{};

  std::atomic<bool> available_{false};
  std::atomic<bool> active_{false};
  std::atomic<bool> starting_{false};
  std::atomic<bool> dump_ready_{false};
  std::atomic<bool> download_in_progress_{false};
  std::atomic<bool> external_operation_active_{false};
  std::atomic<uint8_t> progress_{0};
  std::atomic<uint16_t> registers_read_{0};
  std::atomic<uint32_t> job_id_{0};
  std::atomic<uint8_t> warning_flags_{WARNING_NONE};
  std::atomic<bool> extended_supported_{false};
  std::atomic<bool> core_available_{false};
  std::atomic<bool> crc_matches_stored_eeprom_{false};
  std::atomic<uint16_t> calculated_crc_{0};
  std::atomic<uint16_t> stored_crc_{0};
  std::atomic<uint8_t> crc_retry_count_{0};
  std::atomic<uint32_t> request_token_{0};

  mutable portMUX_TYPE state_mux_ = portMUX_INITIALIZER_UNLOCKED;
  Step step_{Step::WAITING_BUS};
  bool include_extended_metadata_{true};
  std::atomic<bool> waiting_for_response_{false};
  std::atomic<bool> response_received_{false};
  std::atomic<bool> response_valid_{false};
  uint16_t request_start_address_{0};
  uint16_t request_register_count_{0};
  uint16_t eeprom_offset_{0};
  uint8_t block_retry_count_{0};
  uint32_t started_ms_{0};
  uint32_t completed_ms_{0};
  uint64_t captured_at_epoch_{0};
  uint32_t queued_ms_{0};
  uint32_t queue_empty_since_ms_{0};
  uint32_t bus_wait_started_ms_{0};
  uint32_t next_request_ms_{0};
  char phase_[48]{"idle"};
  char error_[96]{};

  bool available_storage_() const;
  void reset_job_();
  void queue_current_request_();
  bool modbus_bus_idle_() const;
  void on_response_(uint32_t request_token, uint16_t start_address, std::span<const uint8_t> data);
  void handle_request_result_();
  void handle_request_failure_();
  void advance_after_success_();
  void finish_job_();
  void fail_job_(const char* error);
  void set_phase_(const char* phase);
  void set_error_(const char* error);
  void add_warning_(Warning warning);
  uint16_t current_start_address_() const;
  uint16_t current_register_count_() const;
  uint16_t calculate_crc_() const;
  static uint16_t read_word_(std::span<const uint8_t> data, size_t index);
  static void decode_ascii_words_(const uint16_t* words, size_t count, char* output, size_t output_size);
};

}  // namespace openquatt_odu_eeprom_dump
}  // namespace esphome
