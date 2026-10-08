#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "OpenQuattExternalTelemetryTransport.h"
#include "PsramBuffer.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome::openquatt_performance_telemetry {

class OpenQuattPerformanceTelemetry : public switch_::Switch, public Component {
 public:
  void set_transport(openquatt_common::OpenQuattExternalTelemetryTransport* value) { this->transport_ = value; }
  void set_clock(time::RealTimeClock* value) { this->clock_ = value; }
  void set_setup_complete_sensor(binary_sensor::BinarySensor* value) { this->setup_complete_sensor_ = value; }
  void set_choice_configured_sensor(binary_sensor::BinarySensor* value) { this->choice_configured_sensor_ = value; }
  void set_prompt_handled_sensor(binary_sensor::BinarySensor* value) { this->prompt_handled_sensor_ = value; }
  void set_generation_select(select::Select* value) { this->generation_select_ = value; }
  void set_outside_temp_sensor(sensor::Sensor* value) { this->outside_temp_sensor_ = value; }
  void set_flow_sensor(sensor::Sensor* value) { this->flow_sensor_ = value; }
  void set_firmware_version(const std::string& value) { this->firmware_version_ = value; }
  void set_topology(const std::string& value) { this->topology_ = value; }

  void set_hp1_working_mode(sensor::Sensor* value) { this->hp_[0].working_mode = value; }
  void set_hp1_frequency(sensor::Sensor* value) { this->hp_[0].frequency = value; }
  void set_hp1_level(sensor::Sensor* value) { this->hp_[0].level = value; }
  void set_hp1_input_power(sensor::Sensor* value) { this->hp_[0].input_power = value; }
  void set_hp1_heat_power(sensor::Sensor* value) { this->hp_[0].heat_power = value; }
  void set_hp1_pump_power(sensor::Sensor* value) { this->hp_[0].pump_power = value; }
  void set_hp1_water_in(sensor::Sensor* value) { this->hp_[0].water_in = value; }
  void set_hp1_water_out(sensor::Sensor* value) { this->hp_[0].water_out = value; }
  void set_hp1_defrost(binary_sensor::BinarySensor* value) { this->hp_[0].defrost = value; }
  void set_hp1_oil_return(binary_sensor::BinarySensor* value) { this->hp_[0].oil_return = value; }
  void set_hp1_four_way_valve(binary_sensor::BinarySensor* value) { this->hp_[0].four_way_valve = value; }
  void set_hp1_bottom_plate_heater(binary_sensor::BinarySensor* value) { this->hp_[0].bottom_plate_heater = value; }

  void set_hp2_working_mode(sensor::Sensor* value) { this->hp_[1].working_mode = value; }
  void set_hp2_frequency(sensor::Sensor* value) { this->hp_[1].frequency = value; }
  void set_hp2_level(sensor::Sensor* value) { this->hp_[1].level = value; }
  void set_hp2_input_power(sensor::Sensor* value) { this->hp_[1].input_power = value; }
  void set_hp2_heat_power(sensor::Sensor* value) { this->hp_[1].heat_power = value; }
  void set_hp2_pump_power(sensor::Sensor* value) { this->hp_[1].pump_power = value; }
  void set_hp2_water_in(sensor::Sensor* value) { this->hp_[1].water_in = value; }
  void set_hp2_water_out(sensor::Sensor* value) { this->hp_[1].water_out = value; }
  void set_hp2_defrost(binary_sensor::BinarySensor* value) { this->hp_[1].defrost = value; }
  void set_hp2_oil_return(binary_sensor::BinarySensor* value) { this->hp_[1].oil_return = value; }
  void set_hp2_four_way_valve(binary_sensor::BinarySensor* value) { this->hp_[1].four_way_valve = value; }
  void set_hp2_bottom_plate_heater(binary_sensor::BinarySensor* value) { this->hp_[1].bottom_plate_heater = value; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

 protected:
  static constexpr uint32_t STORAGE_MAGIC = 0x4F515054UL;  // OQPT
  static constexpr uint16_t STORAGE_VERSION = 2U;
  static constexpr uint8_t CHOICE_CONFIGURED = 1U;
  static constexpr uint8_t PROMPT_HANDLED = 2U;
  static constexpr size_t RECORDS_PER_BATCH = 15U;
  static constexpr size_t RECORD_STORAGE_COUNT = RECORDS_PER_BATCH * 2U;
  static constexpr size_t PAYLOAD_CAPACITY = 4096U;
  static constexpr uint32_t SAMPLE_SECONDS = 10U;
  static constexpr uint32_t MINUTE_SECONDS = 60U;
  static constexpr uint32_t START_SETTLE_SECONDS = 5U * MINUTE_SECONDS;
  static constexpr uint32_t LEVEL_SETTLE_SECONDS = MINUTE_SECONDS;
  static constexpr uint32_t DEFROST_SETTLE_SECONDS = 5U * MINUTE_SECONDS;
  static constexpr uint32_t RETRY_MIN_MS = 5UL * 60UL * 1000UL;
  static constexpr uint32_t RETRY_MAX_MS = 60UL * 60UL * 1000UL;

  struct Storage {
    uint32_t magic;
    uint16_t version;
    uint8_t enabled;
    uint8_t choice_configured;
  };

  static_assert(sizeof(Storage) == 8U, "Preserve the existing NVS record size");

  struct HpSources {
    sensor::Sensor* working_mode{nullptr};
    sensor::Sensor* frequency{nullptr};
    sensor::Sensor* level{nullptr};
    sensor::Sensor* input_power{nullptr};
    sensor::Sensor* heat_power{nullptr};
    sensor::Sensor* pump_power{nullptr};
    sensor::Sensor* water_in{nullptr};
    sensor::Sensor* water_out{nullptr};
    binary_sensor::BinarySensor* defrost{nullptr};
    binary_sensor::BinarySensor* oil_return{nullptr};
    binary_sensor::BinarySensor* four_way_valve{nullptr};
    binary_sensor::BinarySensor* bottom_plate_heater{nullptr};
  };

  struct HpRuntime {
    bool active_known{false};
    bool active{false};
    int level{-1};
    uint32_t settle_until_s{0U};
  };

 public:
  struct HpAccumulator {
    int level{-1};
    bool bottom_plate_heater{false};
    float frequency_sum{0.0f};
    float water_in_sum{0.0f};
    float water_out_sum{0.0f};
    float electrical_wh{0.0f};
    float thermal_wh{0.0f};
    float pump_wh{0.0f};
  };

  struct MinuteAccumulator {
    uint32_t start_s{0U};
    uint8_t samples{0U};
    bool invalid{false};
    uint8_t active_mask{0U};
    int generation{0};
    float outside_sum{0.0f};
    float flow_sum{0.0f};
    std::array<HpAccumulator, 2U> hp{};
  };

  struct MinuteRecord {
    uint32_t start_s{0U};
    uint8_t active_mask{0U};
    int generation{0};
    float outside_c{NAN};
    float flow_lph{NAN};
    std::array<HpAccumulator, 2U> hp{};
  };

 protected:
  bool load_storage_(Storage* storage);
  bool save_storage_(const Storage& storage);
  bool apply_storage_(const Storage& storage);
  bool setup_complete_() const;
  void write_state(bool state) override;
  void reset_collection_();
  void process_sample_(uint32_t timestamp_s);
  bool sample_minute_(uint32_t timestamp_s);
  void finish_minute_();
  void close_window_(bool allow_publish);
  bool append_record_(const MinuteRecord& record);
  bool build_pending_payload_();
  void try_publish_pending_();
  void handle_transport_result_();
  void schedule_retry_();
  void clear_pending_();
  static bool valid_sensor_(const sensor::Sensor* value);
  static bool valid_binary_(const binary_sensor::BinarySensor* value);
  static int generation_code_(const select::Select* source);
  static const char* generation_name_(int generation);
  static const char* map_id_(int generation);
  static std::string random_uuid_();

  openquatt_common::OpenQuattExternalTelemetryTransport* transport_{nullptr};
  time::RealTimeClock* clock_{nullptr};
  binary_sensor::BinarySensor* setup_complete_sensor_{nullptr};
  binary_sensor::BinarySensor* choice_configured_sensor_{nullptr};
  binary_sensor::BinarySensor* prompt_handled_sensor_{nullptr};
  select::Select* generation_select_{nullptr};
  sensor::Sensor* outside_temp_sensor_{nullptr};
  sensor::Sensor* flow_sensor_{nullptr};
  std::array<HpSources, 2U> hp_{};
  std::array<HpRuntime, 2U> hp_runtime_{};
  std::string firmware_version_;
  std::string topology_;
  ESPPreferenceObject pref_;
  openquatt_common::PsramObjectArray<MinuteRecord, RECORD_STORAGE_COUNT> records_;
  openquatt_common::PsramBuffer<char> payload_;
  MinuteAccumulator minute_{};
  uint32_t last_sample_slot_s_{0U};
  uint32_t active_window_s_{0U};
  size_t active_record_count_{0U};
  uint32_t pending_window_s_{0U};
  size_t pending_record_count_{0U};
  std::string pending_batch_id_;
  uint32_t next_retry_ms_{0U};
  uint8_t consecutive_failures_{0U};
  std::atomic<bool> enabled_{false};
  std::atomic<bool> choice_configured_{false};
  std::atomic<bool> prompt_handled_{false};
  std::atomic<bool> publish_in_flight_{false};
  bool pending_publish_allowed_{false};
  int64_t next_publish_us_{0};
};

}  // namespace esphome::openquatt_performance_telemetry
