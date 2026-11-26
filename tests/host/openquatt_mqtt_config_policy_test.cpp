#include <cassert>
#include <cstdint>

#include "components/openquatt_mqtt_config/OpenQuattMqttConfigPolicy.h"

using esphome::openquatt_mqtt_config::mqtt_client_action;
using esphome::openquatt_mqtt_config::mqtt_event_is_current;
using esphome::openquatt_mqtt_config::mqtt_stop_decision;
using esphome::openquatt_mqtt_config::MqttClientAction;
using esphome::openquatt_mqtt_config::MqttStopDecision;
using esphome::openquatt_mqtt_config::storage_generation_may_commit;
using esphome::openquatt_mqtt_config::storage_should_retry;
using esphome::openquatt_mqtt_config::storage_timeout_action;
using esphome::openquatt_mqtt_config::StorageTimeoutAction;

int main() {
  using esphome::openquatt_mqtt_config::mqtt_sample_within_age;
  constexpr uint32_t received_ms = 1250U;
  constexpr uint32_t room_stale_ms = 600000U;
  assert(mqtt_sample_within_age(received_ms + room_stale_ms, received_ms, room_stale_ms));
  assert(!mqtt_sample_within_age(received_ms + room_stale_ms + 1U, received_ms, room_stale_ms));
  assert(mqtt_sample_within_age(received_ms + room_stale_ms + 1U, received_ms, 0U));
  assert(!mqtt_sample_within_age(received_ms, 0U, 0U));
  assert(mqtt_sample_within_age(2U, UINT32_MAX - 2U, 5U));
  assert(!mqtt_sample_within_age(3U, UINT32_MAX - 2U, 5U));

  // Disabling is always stop-first while an old client exists.
  assert(mqtt_client_action(false, true, true, true, false) == MqttClientAction::STOP);
  assert(mqtt_client_action(false, false, true, false, false) == MqttClientAction::IDLE);

  // A broker/credential generation change must stop the old client before
  // constructing the replacement, including while the network is down.
  assert(mqtt_client_action(true, true, false, true, false) == MqttClientAction::STOP);
  assert(mqtt_client_action(true, true, false, false, false) == MqttClientAction::WAIT_FOR_NETWORK);
  assert(mqtt_client_action(true, true, true, false, false) == MqttClientAction::START);
  assert(mqtt_client_action(true, true, true, true, true) == MqttClientAction::IDLE);

  // Rapid A -> B -> disabled coalesces to the latest desired state: once the
  // old client is gone, no intermediate client may be started.
  const bool latest_enabled = false;
  assert(mqtt_client_action(latest_enabled, true, true, true, false) == MqttClientAction::STOP);
  assert(mqtt_client_action(latest_enabled, true, true, false, false) == MqttClientAction::IDLE);

  // Never destroy a connected client after a failed graceful stop. A forced
  // transport disconnect must be observed before stop/destroy is retried.
  assert(mqtt_stop_decision(true, true, true, false) == MqttStopDecision::DESTROY);
  assert(mqtt_stop_decision(false, true, true, true) == MqttStopDecision::FORCE_DISCONNECT);
  // A task that has been created but has not run yet also returns ESP_FAIL
  // from stop. Without a lifecycle event there is no proof that it is safe
  // to destroy its client-owned storage.
  assert(mqtt_stop_decision(false, false, false, false) == MqttStopDecision::RETRY_STOP);
  // Once DISCONNECTED was delivered, another failed stop means the task has
  // already stopped itself; a running disconnected task would stop cleanly.
  assert(mqtt_stop_decision(false, false, true, true) == MqttStopDecision::DESTROY_ALREADY_STOPPED);

  // A callback is valid only for the current client and session generation.
  constexpr uint32_t current_generation = 9U;
  assert(mqtt_event_is_current(true, true, current_generation, current_generation));
  assert(!mqtt_event_is_current(false, true, current_generation, current_generation));
  assert(!mqtt_event_is_current(true, false, current_generation, current_generation));
  assert(!mqtt_event_is_current(true, true, current_generation - 1U, current_generation));

  // A transaction retries only while it is current, not durable and below
  // its strict attempt budget.
  assert(storage_should_retry(0U, 2U, false, false));
  assert(storage_should_retry(1U, 2U, false, false));
  assert(!storage_should_retry(2U, 2U, false, false));
  assert(!storage_should_retry(0U, 2U, true, false));
  assert(!storage_should_retry(0U, 2U, false, true));
  // Runtime apply is permitted only for a durable, current and non-cancelled
  // generation. A timeout that wins cancellation can therefore never apply.
  assert(storage_generation_may_commit(true, true, false));
  assert(!storage_generation_may_commit(false, true, false));
  assert(!storage_generation_may_commit(true, false, false));
  assert(!storage_generation_may_commit(true, true, true));
  // An in-flight commit has its own persistence-pending result, distinct from
  // a completed mutation whose client reconciliation remains pending.
  assert(storage_timeout_action(true, false) == StorageTimeoutAction::RETURN_COMPLETED);
  assert(storage_timeout_action(false, true) == StorageTimeoutAction::REPORT_PERSISTENCE_PENDING);
  assert(storage_timeout_action(false, false) == StorageTimeoutAction::CANCEL);
  return 0;
}
