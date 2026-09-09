#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/static_task.h"
#include "mqtt_client.h"
#include "OpenQuattMqttConfigPolicy.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace esphome {
namespace openquatt_mqtt_config {

class OpenQuattMqttConfig : public Component {
 public:
  static constexpr size_t PAYLOAD_MAX_LEN = 128;
  static constexpr size_t NUMERIC_INPUT_COUNT = 5;
  static constexpr size_t BINARY_INPUT_COUNT = 2;

  enum class NumericInputKind : uint8_t {
    COOLING_DEW_POINT = 0,
    OUTSIDE_TEMPERATURE = 1,
    ROOM_TEMPERATURE = 2,
    ROOM_SETPOINT = 3,
    HEATING_SUPPLY_TARGET = 4,
  };

  enum class BinaryInputKind : uint8_t {
    HEATING_ENABLE = 0,
    COOLING_ENABLE = 1,
  };

  void set_bootstrap_broker(const std::string& bootstrap_broker) { this->bootstrap_broker_ = bootstrap_broker; }
  void set_bootstrap_port(uint16_t bootstrap_port) { this->bootstrap_port_ = bootstrap_port; }
  void set_bootstrap_username(const std::string& bootstrap_username) { this->bootstrap_username_ = bootstrap_username; }
  void set_bootstrap_password(const std::string& bootstrap_password) { this->bootstrap_password_ = bootstrap_password; }
  void set_dew_point_topic(const std::string& dew_point_topic) {
    this->set_numeric_input_topic_(NumericInputKind::COOLING_DEW_POINT, dew_point_topic);
  }
  void set_dew_point_stale_ms(uint32_t dew_point_stale_ms) {
    this->set_numeric_input_stale_ms_(NumericInputKind::COOLING_DEW_POINT, dew_point_stale_ms);
  }
  void set_dew_point_sensor(sensor::Sensor* sensor) {
    this->set_numeric_input_sensor_(NumericInputKind::COOLING_DEW_POINT, sensor);
  }
  void set_dew_point_age_sensor(sensor::Sensor* sensor) {
    this->set_numeric_input_age_sensor_(NumericInputKind::COOLING_DEW_POINT, sensor);
  }
  void set_dew_point_valid_binary_sensor(binary_sensor::BinarySensor* binary_sensor) {
    this->set_numeric_input_valid_binary_sensor_(NumericInputKind::COOLING_DEW_POINT, binary_sensor);
  }
  void set_outside_temperature_topic(const std::string& topic) {
    this->set_numeric_input_topic_(NumericInputKind::OUTSIDE_TEMPERATURE, topic);
  }
  void set_outside_temperature_stale_ms(uint32_t stale_ms) {
    this->set_numeric_input_stale_ms_(NumericInputKind::OUTSIDE_TEMPERATURE, stale_ms);
  }
  void set_outside_temperature_sensor(sensor::Sensor* sensor) {
    this->set_numeric_input_sensor_(NumericInputKind::OUTSIDE_TEMPERATURE, sensor);
  }
  void set_outside_temperature_age_sensor(sensor::Sensor* sensor) {
    this->set_numeric_input_age_sensor_(NumericInputKind::OUTSIDE_TEMPERATURE, sensor);
  }
  void set_outside_temperature_valid_binary_sensor(binary_sensor::BinarySensor* binary_sensor) {
    this->set_numeric_input_valid_binary_sensor_(NumericInputKind::OUTSIDE_TEMPERATURE, binary_sensor);
  }
  void set_room_temperature_topic(const std::string& topic) {
    this->set_numeric_input_topic_(NumericInputKind::ROOM_TEMPERATURE, topic);
  }
  void set_room_temperature_stale_ms(uint32_t stale_ms) {
    this->set_numeric_input_stale_ms_(NumericInputKind::ROOM_TEMPERATURE, stale_ms);
  }
  void set_room_temperature_sensor(sensor::Sensor* sensor) {
    this->set_numeric_input_sensor_(NumericInputKind::ROOM_TEMPERATURE, sensor);
  }
  void set_room_temperature_age_sensor(sensor::Sensor* sensor) {
    this->set_numeric_input_age_sensor_(NumericInputKind::ROOM_TEMPERATURE, sensor);
  }
  void set_room_temperature_valid_binary_sensor(binary_sensor::BinarySensor* binary_sensor) {
    this->set_numeric_input_valid_binary_sensor_(NumericInputKind::ROOM_TEMPERATURE, binary_sensor);
  }
  void set_room_setpoint_topic(const std::string& topic) {
    this->set_numeric_input_topic_(NumericInputKind::ROOM_SETPOINT, topic);
  }
  void set_room_setpoint_stale_ms(uint32_t stale_ms) {
    this->set_numeric_input_stale_ms_(NumericInputKind::ROOM_SETPOINT, stale_ms);
  }
  void set_room_setpoint_sensor(sensor::Sensor* sensor) {
    this->set_numeric_input_sensor_(NumericInputKind::ROOM_SETPOINT, sensor);
  }
  void set_room_setpoint_age_sensor(sensor::Sensor* sensor) {
    this->set_numeric_input_age_sensor_(NumericInputKind::ROOM_SETPOINT, sensor);
  }
  void set_room_setpoint_valid_binary_sensor(binary_sensor::BinarySensor* binary_sensor) {
    this->set_numeric_input_valid_binary_sensor_(NumericInputKind::ROOM_SETPOINT, binary_sensor);
  }
  void set_heating_supply_target_topic(const std::string& topic) {
    this->set_numeric_input_topic_(NumericInputKind::HEATING_SUPPLY_TARGET, topic);
  }
  void set_heating_supply_target_stale_ms(uint32_t stale_ms) {
    this->set_numeric_input_stale_ms_(NumericInputKind::HEATING_SUPPLY_TARGET, stale_ms);
  }
  void set_heating_supply_target_sensor(sensor::Sensor* sensor) {
    this->set_numeric_input_sensor_(NumericInputKind::HEATING_SUPPLY_TARGET, sensor);
  }
  void set_heating_supply_target_age_sensor(sensor::Sensor* sensor) {
    this->set_numeric_input_age_sensor_(NumericInputKind::HEATING_SUPPLY_TARGET, sensor);
  }
  void set_heating_supply_target_valid_binary_sensor(binary_sensor::BinarySensor* binary_sensor) {
    this->set_numeric_input_valid_binary_sensor_(NumericInputKind::HEATING_SUPPLY_TARGET, binary_sensor);
  }
  void set_heating_enable_topic(const std::string& topic) {
    this->set_binary_input_topic_(BinaryInputKind::HEATING_ENABLE, topic);
  }
  void set_heating_enable_stale_ms(uint32_t stale_ms) {
    this->set_binary_input_stale_ms_(BinaryInputKind::HEATING_ENABLE, stale_ms);
  }
  void set_heating_enable_binary_sensor(binary_sensor::BinarySensor* binary_sensor) {
    this->set_binary_input_binary_sensor_(BinaryInputKind::HEATING_ENABLE, binary_sensor);
  }
  void set_heating_enable_age_sensor(sensor::Sensor* sensor) {
    this->set_binary_input_age_sensor_(BinaryInputKind::HEATING_ENABLE, sensor);
  }
  void set_heating_enable_valid_binary_sensor(binary_sensor::BinarySensor* binary_sensor) {
    this->set_binary_input_valid_binary_sensor_(BinaryInputKind::HEATING_ENABLE, binary_sensor);
  }
  void set_cooling_enable_topic(const std::string& topic) {
    this->set_binary_input_topic_(BinaryInputKind::COOLING_ENABLE, topic);
  }
  void set_cooling_enable_stale_ms(uint32_t stale_ms) {
    this->set_binary_input_stale_ms_(BinaryInputKind::COOLING_ENABLE, stale_ms);
  }
  void set_cooling_enable_binary_sensor(binary_sensor::BinarySensor* binary_sensor) {
    this->set_binary_input_binary_sensor_(BinaryInputKind::COOLING_ENABLE, binary_sensor);
  }
  void set_cooling_enable_age_sensor(sensor::Sensor* sensor) {
    this->set_binary_input_age_sensor_(BinaryInputKind::COOLING_ENABLE, sensor);
  }
  void set_cooling_enable_valid_binary_sensor(binary_sensor::BinarySensor* binary_sensor) {
    this->set_binary_input_valid_binary_sensor_(BinaryInputKind::COOLING_ENABLE, binary_sensor);
  }
  void set_default_enabled(bool default_enabled) { this->default_enabled_ = default_enabled; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  bool is_enabled() const { return this->enabled_.load(); }
  bool is_connected() const { return this->connected_.load() && this->client_events_enabled_.load(); }

  struct StatusSnapshot {
    bool enabled{false};
    bool connected{false};
    bool runtime_pending{false};
    std::string broker;
    uint16_t port{1883};
    std::string username;
    bool password_set{false};
    std::string dew_point_topic;
    std::array<std::string, NUMERIC_INPUT_COUNT> input_topics;
    std::array<std::string, BINARY_INPUT_COUNT> binary_input_topics;
    std::array<bool, NUMERIC_INPUT_COUNT> input_enabled;
    std::array<bool, BINARY_INPUT_COUNT> binary_input_enabled;
    std::array<bool, NUMERIC_INPUT_COUNT> input_retained;
    std::array<bool, BINARY_INPUT_COUNT> binary_input_retained;
    std::array<bool, NUMERIC_INPUT_COUNT> input_accept_retained;
    std::array<bool, BINARY_INPUT_COUNT> binary_input_accept_retained;
    std::string config_source;
    std::string csrf_token;
  };

  enum class MutationResult : uint8_t {
    OK = 0U,
    INVALID = 1U,
    BUSY = 2U,
    UNAVAILABLE = 3U,
    TIMEOUT = 4U,
    SAVE_FAILED = 5U,
    SYNC_FAILED = 6U,
    APPLY_FAILED = 7U,
    RUNTIME_PENDING = 8U,
    RECOVERY_FAILED = 9U,
    RECOVERY_PENDING = 10U,
    PERSISTENCE_PENDING = 11U,
  };

  StatusSnapshot get_status_snapshot();
  MutationResult set_runtime_config(const std::string& broker, uint16_t port, const std::string& username,
                                    const std::string& password, bool clear_password, bool enabled);
  MutationResult set_input_enabled(const std::string& key, bool enabled);
  MutationResult set_input_accept_retained(const std::string& key, bool accept_retained);

 protected:
  static constexpr uint32_t STORAGE_MAGIC = 0x4F514D49;
  static constexpr uint16_t STORAGE_VERSION = 1;
  static constexpr size_t BROKER_MAX_LEN = 64;
  static constexpr size_t USERNAME_MAX_LEN = 64;
  static constexpr size_t PASSWORD_MAX_LEN = 128;
  // Frozen v1 storage bit layout for the enable/retained masks. Bit positions
  // must never shift when inputs are added: bits 0..3 are the original numeric
  // inputs and bits 4..5 the binary enables from before the heating supply
  // target existed. That input owns the previously unused bit 6, so stored
  // masks from older firmware stay valid without a storage migration.
  static constexpr uint8_t HEATING_SUPPLY_TARGET_BIT = 6U;
  static constexpr uint8_t BINARY_INPUT_BIT_BASE = 4U;
  static constexpr uint8_t INPUT_MASK_ALL = 0x7FU;
  static constexpr uint8_t STATEFUL_INPUT_MASK =
      static_cast<uint8_t>((1U << static_cast<uint8_t>(NumericInputKind::ROOM_SETPOINT)) |
                           (1U << (BINARY_INPUT_BIT_BASE + static_cast<uint8_t>(BinaryInputKind::HEATING_ENABLE))) |
                           (1U << (BINARY_INPUT_BIT_BASE + static_cast<uint8_t>(BinaryInputKind::COOLING_ENABLE))));

  struct Storage {
    uint32_t magic;
    uint16_t version;
    uint16_t port;
    uint8_t enabled;
    uint8_t input_disabled_mask;
    char broker[BROKER_MAX_LEN + 1];
    char username[USERNAME_MAX_LEN + 1];
    char password[PASSWORD_MAX_LEN + 1];
    uint8_t retained_disabled_mask;
  };

  static_assert(sizeof(Storage) == 272, "Storage layout must remain compatible with version 1 preferences");
  static_assert(offsetof(Storage, retained_disabled_mask) == 269,
                "Retained policy must use the existing version 1 padding byte");

  enum class StorageTransactionPhase : uint8_t {
    IDLE = 0U,
    QUEUED = 1U,
    PROCESSING = 2U,
    WRITING = 3U,
    COMMITTING = 4U,
    CANCELLED = 5U,
    COMPLETED = 6U,
  };
  enum class StorageApplyResult : uint8_t {
    APPLIED = 0U,
    RUNTIME_PENDING = 1U,
    FAILED = 2U,
  };

  bool load_storage_(Storage* storage);
  void initialize_storage_transaction_(const Storage& storage, const Storage& committed_storage,
                                       bool persistence_pending);
  void process_storage_transaction_();
  bool restore_committed_storage_(const Storage& storage);
  bool preflight_storage_apply_(const Storage& storage);
  void finish_storage_transaction_(uint32_t generation, MutationResult result, const Storage& desired_storage,
                                   bool commit_storage);
  MutationResult begin_storage_mutation_(Storage* storage);
  MutationResult submit_storage_mutation_(const Storage& storage);
  void cancel_storage_mutation_();
  void lock_persistence_();
  void unlock_persistence_();
  StorageApplyResult apply_storage_(const Storage& storage, const char* source);
  bool build_storage_(const std::string& broker, uint16_t port, const std::string& username,
                      const std::string& password, bool enabled, uint8_t input_disabled_mask,
                      uint8_t retained_disabled_mask, Storage* storage);
  bool is_valid_storage_(const Storage& storage) const;
  bool register_http_handlers_();
  void rotate_csrf_token_();
  struct ClientConfig {
    bool enabled{false};
    std::array<char, BROKER_MAX_LEN + 1> broker{};
    uint16_t port{1883};
    std::array<char, USERNAME_MAX_LEN + 1> username{};
    std::array<char, PASSWORD_MAX_LEN + 1> password{};
  };
  ClientConfig get_desired_client_config_();
  bool active_client_matches_(uint32_t requested_generation);
  bool ensure_client_worker_();
  void start_permanent_failure_cleanup_();
  bool process_permanent_failure_cleanup_();
  bool request_client_reconcile_();
  enum class ClientReconcileResult : uint8_t {
    APPLIED = 0U,
    WAIT_FOR_NETWORK = 1U,
    RETRY = 2U,
  };
  ClientReconcileResult reconcile_client_(uint32_t requested_generation);
  bool start_client_(const ClientConfig& config, uint32_t requested_generation, uint32_t session_generation);
  bool stop_client_();
  void clear_active_client_state_();
  void close_client_event_gate_();
  void maybe_release_classic_worker_();
  static void client_worker_task_(void* arg);
  struct NumericInput {
    NumericInput(const char* key, const char* log_name, float min_value, float max_value)
        : key(key), log_name(log_name), min_value(min_value), max_value(max_value) {}
    const char* key;
    const char* log_name;
    float min_value;
    float max_value;
    std::string topic;
    uint32_t stale_ms{900000};
    sensor::Sensor* sensor{nullptr};
    sensor::Sensor* age_sensor{nullptr};
    binary_sensor::BinarySensor* valid_binary_sensor{nullptr};
    char pending_payload[PAYLOAD_MAX_LEN]{};
    bool pending_payload_ready{false};
    bool pending_invalid_payload_ready{false};
    bool pending_retained{false};
    uint32_t pending_session_generation{0};
    float last_valid_value{NAN};
    uint32_t last_valid_ms{0};
    bool last_valid_retained{false};
  };
  struct BinaryInput {
    BinaryInput(const char* key, const char* log_name) : key(key), log_name(log_name) {}
    const char* key;
    const char* log_name;
    std::string topic;
    uint32_t stale_ms{900000};
    binary_sensor::BinarySensor* binary_sensor{nullptr};
    sensor::Sensor* age_sensor{nullptr};
    binary_sensor::BinarySensor* valid_binary_sensor{nullptr};
    char pending_payload[PAYLOAD_MAX_LEN]{};
    bool pending_payload_ready{false};
    bool pending_invalid_payload_ready{false};
    bool pending_retained{false};
    uint32_t pending_session_generation{0};
    bool last_valid_value{false};
    uint32_t last_valid_ms{0};
    bool last_valid_retained{false};
  };

  void set_numeric_input_topic_(NumericInputKind kind, const std::string& topic);
  void set_numeric_input_stale_ms_(NumericInputKind kind, uint32_t stale_ms);
  void set_numeric_input_sensor_(NumericInputKind kind, sensor::Sensor* sensor);
  void set_numeric_input_age_sensor_(NumericInputKind kind, sensor::Sensor* sensor);
  void set_numeric_input_valid_binary_sensor_(NumericInputKind kind, binary_sensor::BinarySensor* binary_sensor);
  NumericInput& numeric_input_(NumericInputKind kind);
  const NumericInput& numeric_input_(NumericInputKind kind) const;
  void set_binary_input_topic_(BinaryInputKind kind, const std::string& topic);
  void set_binary_input_stale_ms_(BinaryInputKind kind, uint32_t stale_ms);
  void set_binary_input_binary_sensor_(BinaryInputKind kind, binary_sensor::BinarySensor* binary_sensor);
  void set_binary_input_age_sensor_(BinaryInputKind kind, sensor::Sensor* sensor);
  void set_binary_input_valid_binary_sensor_(BinaryInputKind kind, binary_sensor::BinarySensor* binary_sensor);
  BinaryInput& binary_input_(BinaryInputKind kind);
  const BinaryInput& binary_input_(BinaryInputKind kind) const;
  static uint8_t numeric_input_mask_(NumericInputKind kind);
  static uint8_t binary_input_mask_(BinaryInputKind kind);
  static constexpr uint8_t numeric_input_bit_(size_t index) {
    return static_cast<uint8_t>(index >= static_cast<size_t>(NumericInputKind::HEATING_SUPPLY_TARGET)
                                    ? (1U << HEATING_SUPPLY_TARGET_BIT)
                                    : (1U << index));
  }
  static constexpr uint8_t binary_input_bit_(size_t index) {
    return static_cast<uint8_t>(1U << (BINARY_INPUT_BIT_BASE + index));
  }
  bool is_numeric_input_enabled_(size_t input_index) const;
  bool is_binary_input_enabled_(size_t input_index) const;
  bool is_numeric_input_accept_retained_(size_t input_index) const;
  bool is_binary_input_accept_retained_(size_t input_index) const;
  bool input_mask_for_key_(const std::string& key, uint8_t* mask) const;
  void clear_all_inputs_();
  void clear_disabled_inputs_();
  void clear_input_(uint8_t input_mask);
  void clear_session_scoped_inputs_();
  void process_pending_input_subscriptions_();
  void subscribe_inputs_(esp_mqtt_client_handle_t client);
  int find_numeric_input_index_by_topic_(const char* topic, int topic_len) const;
  int find_binary_input_index_by_topic_(const char* topic, int topic_len) const;
  static void mqtt_event_handler_(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
  void queue_numeric_payload_(size_t input_index, const char* data, int len, bool retained,
                              uint32_t session_generation);
  void queue_binary_payload_(size_t input_index, const char* data, int len, bool retained, uint32_t session_generation);
  void consume_pending_numeric_payloads_();
  void consume_pending_binary_payloads_();
  void handle_numeric_payload_(size_t input_index, const char* payload, bool retained);
  void handle_binary_payload_(size_t input_index, const char* payload, bool retained);
  void invalidate_numeric_input_(size_t input_index);
  void invalidate_binary_input_(size_t input_index);
  void publish_runtime_state_(bool force);
  void publish_float_if_changed_(sensor::Sensor* sensor, float value, bool force);
  void publish_binary_if_changed_(binary_sensor::BinarySensor* binary_sensor, bool value, bool force);
  void lock_config_();
  void unlock_config_();
  void lock_runtime_();
  void unlock_runtime_();
  static void copy_string_field_(char* destination, size_t max_len, const std::string& value);

  static constexpr uint32_t SENSOR_PUBLISH_INTERVAL_MS = 10000;
  static constexpr uint32_t NON_RETAINED_STATEFUL_STALE_MS = 30UL * 60UL * 1000UL;
  static constexpr int MQTT_TASK_STACK_SIZE = 12288;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  static constexpr bool MQTT_WORKER_STACK_IN_PSRAM = true;
#else
  static constexpr bool MQTT_WORKER_STACK_IN_PSRAM = false;
#endif
  static constexpr uint32_t MQTT_WORKER_TASK_STACK_SIZE = 24576;
  static constexpr uint32_t MQTT_RECONCILE_RETRY_MS = 5000;
  static constexpr size_t MQTT_CLIENT_ID_MAX_LEN = 96;
  static constexpr uint8_t STORAGE_MAX_ATTEMPTS = 2U;
  static constexpr uint32_t STORAGE_MUTATION_WAIT_MS = 2000U;
  static constexpr uint32_t STORAGE_CANCELLATION_GRACE_MS = 750U;
  static_assert(sizeof(StackType_t) == 1U, "ESP-IDF StaticTask stack sizes are configured in bytes");

  esp_mqtt_client_handle_t mqtt_client_{nullptr};
  SemaphoreHandle_t config_lock_{nullptr};
  SemaphoreHandle_t runtime_lock_{nullptr};
  StaticSemaphore_t persistence_lock_storage_{};
  SemaphoreHandle_t persistence_lock_{nullptr};
  StaticSemaphore_t storage_mutation_lock_storage_{};
  SemaphoreHandle_t storage_mutation_lock_{nullptr};
  StaticSemaphore_t storage_mutation_complete_storage_{};
  SemaphoreHandle_t storage_mutation_complete_{nullptr};
  StaticSemaphore_t worker_lock_storage_{};
  SemaphoreHandle_t worker_lock_{nullptr};
  StaticTask client_worker_task_state_{};
  bool client_worker_region_valid_{false};
  bool mqtt_client_started_{false};
  bool disconnect_requested_{false};
  std::atomic<bool> connected_{false};
  std::atomic<bool> force_publish_{true};
  std::atomic<bool> resubscribe_inputs_{false};
  std::atomic<bool> clear_session_scoped_inputs_pending_{false};
  std::atomic<bool> client_reconcile_pending_{false};
  std::atomic<bool> client_worker_active_{false};
  std::atomic<bool> permanent_failure_cleanup_pending_{false};
  std::atomic<bool> mqtt_client_present_{false};
  std::atomic<bool> client_events_enabled_{false};
  std::atomic<bool> mqtt_connected_seen_{false};
  std::atomic<bool> mqtt_disconnected_seen_{false};
  std::atomic<bool> mqtt_transport_connected_{false};
  std::atomic<esp_mqtt_client_handle_t> callback_client_{nullptr};
  std::atomic<uint32_t> callback_session_generation_{0U};
  std::atomic<uint32_t> client_requested_generation_{0U};
  std::atomic<uint32_t> client_applied_generation_{0U};
  std::atomic<uint8_t> clear_input_mask_pending_{0};
  std::atomic<uint32_t> mqtt_session_generation_{0};
  std::atomic<uint8_t> input_disabled_mask_{0};
  std::atomic<uint8_t> retained_disabled_mask_{0};
  portMUX_TYPE pending_lock_ = portMUX_INITIALIZER_UNLOCKED;

  std::string bootstrap_broker_;
  uint16_t bootstrap_port_{1883};
  std::string bootstrap_username_;
  std::string bootstrap_password_;
  bool default_enabled_{false};

  std::array<NumericInput, NUMERIC_INPUT_COUNT> numeric_inputs_{{
      NumericInput("cooling_dew_point", "cooling dew point", -20.0f, 35.0f),
      NumericInput("outside_temperature", "outside temperature", -40.0f, 60.0f),
      NumericInput("room_temperature", "room temperature", 0.0f, 50.0f),
      NumericInput("room_setpoint", "room setpoint", 5.0f, 35.0f),
      NumericInput("heating_supply_target", "heating supply target", 20.0f, 70.0f),
  }};
  std::array<BinaryInput, BINARY_INPUT_COUNT> binary_inputs_{{
      BinaryInput("heating_enable", "heating enable"),
      BinaryInput("cooling_enable", "cooling enable"),
  }};

  std::string broker_;
  uint16_t port_{1883};
  std::string username_;
  std::string password_;
  std::atomic<uint32_t> active_client_generation_{0U};
  std::atomic<bool> enabled_{false};
  std::string config_source_;
  std::string csrf_token_;
  ESPPreferenceObject pref_;
  // Protected by persistence_lock_. Only process_storage_transaction_ writes
  // preferences or applies an HTTP-originated desired generation.
  Storage desired_storage_{};
  Storage committed_storage_{};
  uint32_t desired_storage_generation_{0U};
  uint32_t waiting_storage_generation_{0U};
  uint32_t completed_storage_generation_{0U};
  MutationResult completed_storage_result_{MutationResult::UNAVAILABLE};
  std::atomic<StorageTransactionPhase> storage_transaction_phase_{StorageTransactionPhase::IDLE};
  std::atomic<bool> storage_write_started_{false};
  bool desired_storage_initialized_{false};
  bool storage_persistence_pending_{false};
  bool storage_mutation_waiting_{false};
  bool handlers_registered_{false};
  uint32_t last_sensor_publish_ms_{0};
  std::atomic<uint32_t> next_reconcile_attempt_ms_{0U};
};

}  // namespace openquatt_mqtt_config
}  // namespace esphome
