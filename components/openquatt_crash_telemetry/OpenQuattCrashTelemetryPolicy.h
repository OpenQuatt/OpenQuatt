#pragma once

#include <cstdint>

namespace esphome::openquatt_crash_telemetry {

enum class CrashPublishKind : uint8_t {
  NONE = 0U,
  CRASH = 1U,
  TOMBSTONE = 2U,
};

inline constexpr CrashPublishKind select_crash_publish_kind(bool tombstone_pending, bool consent_enabled,
                                                            bool setup_complete, bool crash_pending) {
  if (tombstone_pending) {
    return CrashPublishKind::TOMBSTONE;
  }
  if (consent_enabled && setup_complete && crash_pending) {
    return CrashPublishKind::CRASH;
  }
  return CrashPublishKind::NONE;
}

inline constexpr bool should_request_tombstone(bool previous_consent_known, bool previous_consent_enabled,
                                               bool current_consent_enabled, bool installation_id_available) {
  return previous_consent_known && previous_consent_enabled && !current_consent_enabled && installation_id_available;
}

inline constexpr bool persisted_consent_blocks_crash(bool consent_known, bool consent_enabled) {
  return consent_known && !consent_enabled;
}

inline constexpr bool flash_sequence_is_newer(uint32_t candidate, uint32_t reference) {
  return static_cast<int32_t>(candidate - reference) > 0;
}

inline constexpr bool crash_data_may_be_published(CrashPublishKind kind, bool consent_enabled, bool setup_complete) {
  if (kind == CrashPublishKind::TOMBSTONE) {
    return true;
  }
  return kind == CrashPublishKind::CRASH && consent_enabled && setup_complete;
}

inline constexpr bool crash_publication_is_retained(CrashPublishKind kind) {
  return kind == CrashPublishKind::TOMBSTONE;
}

inline constexpr bool should_wait_for_time_sync(CrashPublishKind kind, bool time_synchronized, uint32_t now_ms,
                                                uint32_t deadline_ms) {
  return kind == CrashPublishKind::CRASH && !time_synchronized && static_cast<int32_t>(now_ms - deadline_ms) < 0;
}

inline constexpr bool reported_at_is_usable(bool time_synchronized, bool timestamp_is_sane, bool crash_time_valid,
                                            uint32_t reported_at, uint32_t crash_timestamp) {
  return time_synchronized && timestamp_is_sane && (!crash_time_valid || reported_at >= crash_timestamp);
}

enum class CrashSessionAction : uint8_t {
  NONE = 0U,
  FINISH_SUCCESS = 1U,
  FINISH_FAILURE = 2U,
};

// Pure main-loop decision for an active publish session. The ESPHome loopTask
// must never block on MQTT lifecycle calls; it only observes these flags and
// asks the worker to start cleanup. A simultaneous success and failure keeps
// the historical priority of success.
inline constexpr CrashSessionAction select_crash_session_action(bool session_active, bool start_task_running,
                                                                bool finishing_session, bool succeeded, bool failed,
                                                                bool timed_out) {
  if (!session_active || start_task_running || finishing_session) {
    return CrashSessionAction::NONE;
  }
  if (succeeded) {
    return CrashSessionAction::FINISH_SUCCESS;
  }
  if (failed || timed_out) {
    return CrashSessionAction::FINISH_FAILURE;
  }
  return CrashSessionAction::NONE;
}

enum class CrashCleanupDecision : uint8_t {
  DESTROY = 0U,
  FORCE_DISCONNECT = 1U,
  RETRY_STOP = 2U,
  DESTROY_ALREADY_STOPPED = 3U,
};

// Crash-specific cleanup decision for the worker task. Kept local to this
// component so the already-working usage telemetry helper stays untouched.
inline constexpr CrashCleanupDecision select_crash_cleanup_decision(bool stop_succeeded, bool connected_seen,
                                                                    bool disconnected_seen,
                                                                    uint8_t consecutive_stop_failures) {
  if (stop_succeeded) {
    return CrashCleanupDecision::DESTROY;
  }
  if (connected_seen && !disconnected_seen) {
    return CrashCleanupDecision::FORCE_DISCONNECT;
  }
  if (consecutive_stop_failures < 2U) {
    return CrashCleanupDecision::RETRY_STOP;
  }
  return CrashCleanupDecision::DESTROY_ALREADY_STOPPED;
}

inline constexpr uint32_t CRASH_INITIAL_PUBLISH_DELAY_MS = 15UL * 1000UL;
inline constexpr uint32_t TOMBSTONE_INITIAL_PUBLISH_DELAY_MS = 1U;

// A crash publication waits out the early boot activity wave; an opt-out
// tombstone takes effect immediately.
inline constexpr uint32_t initial_publish_delay_ms(CrashPublishKind kind) {
  return kind == CrashPublishKind::TOMBSTONE ? TOMBSTONE_INITIAL_PUBLISH_DELAY_MS : CRASH_INITIAL_PUBLISH_DELAY_MS;
}

}  // namespace esphome::openquatt_crash_telemetry
