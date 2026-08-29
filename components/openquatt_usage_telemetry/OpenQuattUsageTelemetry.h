#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/static_task.h"
#include "mqtt_client.h"
#include "OpenQuattUsageTelemetryPolicy.h"
#include "PsramBuffer.h"

namespace esphome {
namespace openquatt_mqtt_config {
class OpenQuattMqttConfig;
}
namespace openquatt_usage_telemetry {

class OpenQuattUsageTelemetry : public switch_::Switch, public Component {
 public:
  void set_broker(const std::string& broker) { this->broker_ = broker; }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_tls(bool tls) { this->tls_ = tls; }
  void set_username(const std::string& username) { this->username_ = username; }
  void set_password(const std::string& password) { this->password_ = password; }
  void set_topic(const std::string& topic) { this->topic_ = topic; }
  void set_clock(time::RealTimeClock* clock) { this->clock_ = clock; }
  void set_installation_id_sensor(text_sensor::TextSensor* sensor) { this->installation_id_sensor_ = sensor; }
  void set_setup_complete_sensor(binary_sensor::BinarySensor* sensor) { this->setup_complete_sensor_ = sensor; }
  void set_choice_configured_sensor(binary_sensor::BinarySensor* sensor) { this->choice_configured_sensor_ = sensor; }
  void set_interval_ms(uint32_t interval_ms) { this->interval_ms_ = interval_ms; }
  void set_firmware_version(const std::string& version) { this->firmware_version_ = version; }
  void set_release_channel(const std::string& channel) { this->release_channel_ = channel; }
  void set_hardware_profile(const std::string& profile) { this->hardware_profile_ = profile; }
  void set_topology(const std::string& topology) { this->topology_ = topology; }
  void set_connection(const std::string& connection) { this->connection_ = connection; }
  void set_active_connection_sensor(text_sensor::TextSensor* sensor) { this->active_connection_sensor_ = sensor; }
  void set_connection_preference_select(select::Select* source) { this->connection_preference_select_ = source; }
  void set_quatt_hybrid_generation_select(select::Select* source) { this->quatt_hybrid_generation_select_ = source; }
  void set_flow_source_select(select::Select* source) { this->flow_source_select_ = source; }
  void set_q_flow_source_select(select::Select* source) { this->q_flow_source_select_ = source; }
  void set_heating_strategy_select(select::Select* source) { this->heating_strategy_select_ = source; }
  void set_room_temperature_source_select(select::Select* source) { this->room_temperature_source_select_ = source; }
  void set_room_setpoint_source_select(select::Select* source) { this->room_setpoint_source_select_ = source; }
  void set_outside_temperature_source_select(select::Select* source) {
    this->outside_temperature_source_select_ = source;
  }
  void set_heating_enable_source_select(select::Select* source) { this->heating_enable_source_select_ = source; }
  void set_cooling_enable_source_select(select::Select* source) { this->cooling_enable_source_select_ = source; }
  void set_cooling_dew_point_source_select(select::Select* source) { this->cooling_dew_point_source_select_ = source; }
  void set_external_heat_demand_source_select(select::Select* source) {
    this->external_heat_demand_source_select_ = source;
  }
  void set_loop_time_sensor(sensor::Sensor* sensor) { this->loop_time_sensor_ = sensor; }
  void set_internal_temperature_sensor(sensor::Sensor* sensor) { this->internal_temperature_sensor_ = sensor; }
  void set_wifi_signal_sensor(sensor::Sensor* sensor) { this->wifi_signal_sensor_ = sensor; }
  void set_cic_polling_switch(switch_::Switch* feature_switch) { this->cic_polling_switch_ = feature_switch; }
  void set_cic_compatibility_switch(switch_::Switch* feature_switch) {
    this->cic_compatibility_switch_ = feature_switch;
  }
  void set_ot_thermostat_switch(switch_::Switch* feature_switch) { this->ot_thermostat_switch_ = feature_switch; }
  void set_boiler_assist_switch(switch_::Switch* feature_switch) { this->boiler_assist_switch_ = feature_switch; }
  void set_boiler_connection_select(select::Select* connection_select) {
    this->boiler_connection_select_ = connection_select;
  }
  void set_mqtt_config(openquatt_mqtt_config::OpenQuattMqttConfig* config) { this->mqtt_config_ = config; }
  void set_trend_ram_switch(switch_::Switch* feature_switch) { this->trend_ram_switch_ = feature_switch; }
  void set_trend_flash_switch(switch_::Switch* feature_switch) { this->trend_flash_switch_ = feature_switch; }
  void set_decision_log_flash_switch(switch_::Switch* feature_switch) {
    this->decision_log_flash_switch_ = feature_switch;
  }
  void set_energy_history_flash_switch(switch_::Switch* feature_switch) {
    this->energy_history_flash_switch_ = feature_switch;
  }
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  bool is_configured() const { return !this->broker_.empty() && !this->topic_.empty(); }

 protected:
  void write_state(bool state) override;

  static constexpr uint32_t STORAGE_MAGIC = 0x4F515553;
  static constexpr uint16_t STORAGE_VERSION = 2;
  static constexpr uint32_t INITIAL_PUBLISH_DELAY_MS = 90UL * 1000UL;
  static constexpr uint32_t SESSION_TIMEOUT_MS = 30000;
  static constexpr uint32_t RETRY_MIN_MS = 5UL * 60UL * 1000UL;
  static constexpr uint32_t RETRY_MAX_MS = 60UL * 60UL * 1000UL;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  // PSRAM is abundant, so keep a conservative stack until HIL watermarks
  // demonstrate that this can safely be reduced.
  static constexpr uint32_t MQTT_WORKER_TASK_STACK_SIZE = 16384;
  static constexpr bool MQTT_WORKER_STACK_IN_PSRAM = true;
#else
  // Classic ESP32 cannot safely run Wi-Fi/ROM-using tasks from a PSRAM stack.
  static constexpr uint32_t MQTT_WORKER_TASK_STACK_SIZE = 8192;
  static constexpr bool MQTT_WORKER_STACK_IN_PSRAM = false;
#endif
  static constexpr int MQTT_TASK_STACK_SIZE = 12288;
  static_assert(sizeof(StackType_t) == 1U, "ESP-IDF StaticTask stack sizes are configured in bytes");

  enum class WorkerCommand : uint32_t {
    START = 1U,
    CLEANUP = 2U,
  };

  struct StorageV1 {
    uint32_t magic;
    uint16_t version;
    uint8_t enabled;
    uint8_t installation_id_present;
    std::array<uint8_t, 16> installation_id;
  };

  struct Storage {
    uint32_t magic;
    uint16_t version;
    uint8_t enabled;
    uint8_t choice_configured;
    uint8_t installation_id_present;
    std::array<uint8_t, 3> reserved;
    std::array<uint8_t, 16> installation_id;
  };

  static_assert(sizeof(StorageV1) == 24, "Legacy usage telemetry storage layout changed unexpectedly");
  static_assert(sizeof(Storage) == 28, "Usage telemetry storage layout changed unexpectedly");

  bool load_storage_(Storage* storage);
  bool load_legacy_storage_(StorageV1* storage);
  bool save_storage_(const Storage& storage);
  bool set_consent_publish_blocked_(bool blocked);
  bool ensure_installation_id_(Storage* storage);
  bool is_setup_complete_() const;
  bool apply_storage_(const Storage& storage);
  void schedule_initial_publish_();
  void schedule_immediate_publish_();
  void schedule_regular_publish_();
  void schedule_retry_();
  void start_publish_session_();
  bool ensure_worker_task_();
  bool notify_worker_(WorkerCommand command);
  bool start_client_();
  bool cleanup_client_();
  void finish_publish_session_(bool succeeded);
  void complete_publish_session_();
  bool build_payload_();
  void clear_payload_();
  std::string read_hardware_revision_() const;
  static bool time_reached_(uint32_t now_ms, uint32_t target_ms);
  static std::string format_uuid_(const std::array<uint8_t, 16>& bytes);
  static std::string random_message_id_();
  static void worker_task_(void* arg);
  static void mqtt_event_handler_(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);

  std::string broker_;
  uint16_t port_{8883};
  bool tls_{true};
  std::string username_;
  std::string password_;
  std::string topic_;
  time::RealTimeClock* clock_{nullptr};
  text_sensor::TextSensor* installation_id_sensor_{nullptr};
  binary_sensor::BinarySensor* setup_complete_sensor_{nullptr};
  binary_sensor::BinarySensor* choice_configured_sensor_{nullptr};
  uint32_t interval_ms_{60UL * 60UL * 1000UL};
  std::string firmware_version_;
  std::string release_channel_;
  std::string hardware_profile_;
  std::string topology_;
  std::string connection_;
  text_sensor::TextSensor* active_connection_sensor_{nullptr};
  select::Select* connection_preference_select_{nullptr};
  select::Select* quatt_hybrid_generation_select_{nullptr};
  select::Select* flow_source_select_{nullptr};
  select::Select* q_flow_source_select_{nullptr};
  select::Select* heating_strategy_select_{nullptr};
  select::Select* room_temperature_source_select_{nullptr};
  select::Select* room_setpoint_source_select_{nullptr};
  select::Select* outside_temperature_source_select_{nullptr};
  select::Select* heating_enable_source_select_{nullptr};
  select::Select* cooling_enable_source_select_{nullptr};
  select::Select* cooling_dew_point_source_select_{nullptr};
  select::Select* external_heat_demand_source_select_{nullptr};
  sensor::Sensor* loop_time_sensor_{nullptr};
  sensor::Sensor* internal_temperature_sensor_{nullptr};
  sensor::Sensor* wifi_signal_sensor_{nullptr};
  switch_::Switch* cic_polling_switch_{nullptr};
  switch_::Switch* cic_compatibility_switch_{nullptr};
  switch_::Switch* ot_thermostat_switch_{nullptr};
  switch_::Switch* boiler_assist_switch_{nullptr};
  select::Select* boiler_connection_select_{nullptr};
  openquatt_mqtt_config::OpenQuattMqttConfig* mqtt_config_{nullptr};
  switch_::Switch* trend_ram_switch_{nullptr};
  switch_::Switch* trend_flash_switch_{nullptr};
  switch_::Switch* decision_log_flash_switch_{nullptr};
  switch_::Switch* energy_history_flash_switch_{nullptr};
  ESPPreferenceObject pref_;
  std::array<uint8_t, 16> installation_id_bytes_{};
  std::string installation_id_;
  openquatt_common::PsramBuffer<char> publish_topic_;
  openquatt_common::PsramBuffer<char> payload_;
  size_t payload_size_{0U};
  std::string payload_message_id_;
  esp_mqtt_client_handle_t mqtt_client_{nullptr};
  bool mqtt_client_started_{false};
  uint8_t cleanup_stop_failures_{0U};
  bool cleanup_disconnect_requested_{false};
  bool worker_task_region_valid_{false};
  StaticSemaphore_t consent_mutex_storage_{};
  SemaphoreHandle_t consent_mutex_{nullptr};
  StaticTask worker_task_state_{};
  std::atomic<bool> enabled_{false};
  std::atomic<bool> consent_publish_blocked_{true};
  std::atomic<bool> choice_configured_{false};
  std::atomic<bool> session_active_{false};
  std::atomic<bool> finishing_session_{false};
  std::atomic<bool> start_task_running_{false};
  std::atomic<bool> start_task_complete_{false};
  std::atomic<bool> cleanup_task_complete_{false};
  std::atomic<bool> cleanup_succeeded_{false};
  std::atomic<bool> mqtt_connected_seen_{false};
  std::atomic<bool> mqtt_disconnected_seen_{false};
  std::atomic<bool> publish_succeeded_{false};
  std::atomic<bool> publish_failed_{false};
  std::atomic<int> pending_message_id_{-1};
  uint32_t session_started_ms_{0};
  uint32_t next_publish_ms_{0};
  uint8_t consecutive_failures_{0};
  bool boot_publish_pending_{false};
};

}  // namespace openquatt_usage_telemetry
}  // namespace esphome
