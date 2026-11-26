#pragma once

#include <cstdint>

namespace esphome {
namespace openquatt_mqtt_config {

enum class MqttClientAction : uint8_t {
  IDLE = 0U,
  STOP = 1U,
  START = 2U,
  WAIT_FOR_NETWORK = 3U,
};

enum class MqttStopDecision : uint8_t {
  DESTROY = 0U,
  FORCE_DISCONNECT = 1U,
  RETRY_STOP = 2U,
  DESTROY_ALREADY_STOPPED = 3U,
};

enum class StorageTimeoutAction : uint8_t {
  CANCEL = 0U,
  RETURN_COMPLETED = 1U,
  REPORT_PERSISTENCE_PENDING = 2U,
};

inline bool mqtt_sample_within_age(uint32_t now_ms, uint32_t received_ms, uint32_t stale_ms) {
  return received_ms != 0U && (stale_ms == 0U || now_ms - received_ms <= stale_ms);
}

inline MqttClientAction mqtt_client_action(bool desired_enabled, bool desired_broker_present, bool network_connected,
                                           bool client_present, bool client_matches_desired) {
  const bool should_run = desired_enabled && desired_broker_present;
  if (client_present && (!should_run || !client_matches_desired)) {
    return MqttClientAction::STOP;
  }
  if (!should_run || client_present) {
    return MqttClientAction::IDLE;
  }
  return network_connected ? MqttClientAction::START : MqttClientAction::WAIT_FOR_NETWORK;
}

inline MqttStopDecision mqtt_stop_decision(bool stop_succeeded, bool transport_connected, bool connected_seen,
                                           bool disconnected_seen) {
  if (stop_succeeded) {
    return MqttStopDecision::DESTROY;
  }
  if (transport_connected || (connected_seen && !disconnected_seen)) {
    return MqttStopDecision::FORCE_DISCONNECT;
  }
  if (disconnected_seen) {
    return MqttStopDecision::DESTROY_ALREADY_STOPPED;
  }
  return MqttStopDecision::RETRY_STOP;
}

inline bool mqtt_event_is_current(bool callbacks_enabled, bool event_client_matches, uint32_t event_generation,
                                  uint32_t current_generation) {
  return callbacks_enabled && event_client_matches && event_generation == current_generation;
}

inline bool storage_should_retry(uint8_t attempts_completed, uint8_t max_attempts, bool durable, bool cancelled) {
  return !durable && !cancelled && attempts_completed < max_attempts;
}

inline bool storage_generation_may_commit(bool durable, bool generation_is_current, bool cancelled) {
  return durable && generation_is_current && !cancelled;
}

inline StorageTimeoutAction storage_timeout_action(bool exact_generation_completed, bool commit_in_progress) {
  if (exact_generation_completed) {
    return StorageTimeoutAction::RETURN_COMPLETED;
  }
  return commit_in_progress ? StorageTimeoutAction::REPORT_PERSISTENCE_PENDING : StorageTimeoutAction::CANCEL;
}

}  // namespace openquatt_mqtt_config
}  // namespace esphome
