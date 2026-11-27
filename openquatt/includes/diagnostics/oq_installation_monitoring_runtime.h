#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "PsramBuffer.h"
#include "esphome/core/log.h"
#include "oq_installation_monitoring.h"

namespace oq_diagnostics {

inline InstallationMonitor& installation_monitor() {
  static esphome::openquatt_common::PsramObjectArray<InstallationMonitor::HistoryStorage, 1U> storage;
  static InstallationMonitor monitor([]() -> InstallationMonitor::HistoryStorage* {
    if (storage.allocate()) {
      return storage.data();
    }
    ESP_LOGE("oq_diag", "Compressor start history unavailable: PSRAM allocation failed");
    return nullptr;
  }());
  return monitor;
}

inline size_t compressor_starts_warning_limit_2h() {
  return static_cast<size_t>(lroundf(id(oq_compressor_starts_warning_limit_2h).state));
}

inline size_t compressor_starts_warning_limit_72h() {
  return static_cast<size_t>(lroundf(id(oq_compressor_starts_warning_limit_72h).state));
}

inline bool compressor_cycling_warning_2h(uint32_t now_ms, size_t warning_limit) {
  auto& monitor = installation_monitor();
  return monitor.compressor_cycling_warning_2h(now_ms, warning_limit);
}

inline bool compressor_cycling_warning_72h(uint32_t now_ms, size_t warning_limit) {
  auto& monitor = installation_monitor();
  return monitor.compressor_cycling_warning_72h(now_ms, warning_limit);
}

inline bool compressor_cycling_warning(uint32_t now_ms, size_t short_warning_limit, size_t long_warning_limit) {
  auto& monitor = installation_monitor();
  return monitor.compressor_cycling_warning(now_ms, short_warning_limit, long_warning_limit) ||
         monitor.alternating_cycling_warning(now_ms, short_warning_limit);
}

inline void publish_compressor_cycling_alert_metadata() {
  id(oq_compressor_cycling_alert_latched_sensor).publish_state(id(oq_compressor_cycling_alert_latched));
  id(oq_compressor_cycling_alert_alternating_sensor).publish_state(id(oq_compressor_cycling_alert_alternating));
  id(oq_compressor_cycling_alert_first_seen)
      .publish_state(id(oq_compressor_cycling_alert_first_seen_epoch) > 0U
                         ? id(oq_compressor_cycling_alert_first_seen_epoch)
                         : NAN);
  id(oq_compressor_cycling_alert_last_seen)
      .publish_state(
          id(oq_compressor_cycling_alert_last_seen_epoch) > 0U ? id(oq_compressor_cycling_alert_last_seen_epoch) : NAN);
  id(oq_compressor_cycling_alert_hp1_peak_2h).publish_state(id(oq_compressor_cycling_alert_hp1_peak_2h_value));
  id(oq_compressor_cycling_alert_hp1_peak_72h).publish_state(id(oq_compressor_cycling_alert_hp1_peak_72h_value));
  id(oq_compressor_cycling_alert_hp2_peak_2h).publish_state(id(oq_compressor_cycling_alert_hp2_peak_2h_value));
  id(oq_compressor_cycling_alert_hp2_peak_72h).publish_state(id(oq_compressor_cycling_alert_hp2_peak_72h_value));
}

inline void capture_compressor_cycling_alert() {
  const uint32_t now_ms = millis();
  const size_t short_warning_limit = compressor_starts_warning_limit_2h();
  const size_t long_warning_limit = compressor_starts_warning_limit_72h();
  auto& monitor = installation_monitor();
  const int hp1_short_starts = monitor.start_count(1, now_ms, InstallationMonitor::kTwoHoursMs);
  const int hp2_short_starts = monitor.start_count(2, now_ms, InstallationMonitor::kTwoHoursMs);
  const int hp1_long_starts = monitor.start_count(1, now_ms, InstallationMonitor::kSeventyTwoHoursMs);
  const int hp2_long_starts = monitor.start_count(2, now_ms, InstallationMonitor::kSeventyTwoHoursMs);
  const bool short_active = hp1_short_starts > short_warning_limit || hp2_short_starts > short_warning_limit;
  const bool long_active = hp1_long_starts > long_warning_limit || hp2_long_starts > long_warning_limit;
  const bool alternating = monitor.alternating_cycling_warning(now_ms, short_warning_limit);
  if (!short_active && !long_active && !alternating) {
    return;
  }

  const auto now = id(oq_time).now();
  const uint32_t now_epoch = now.is_valid() ? static_cast<uint32_t>(now.timestamp) : 0U;
  const bool first_occurrence = !id(oq_compressor_cycling_alert_latched);
  if (first_occurrence) {
    const bool use_short_window = short_active || alternating;
    const int hp1_value = use_short_window ? hp1_short_starts : hp1_long_starts;
    const int hp2_value = use_short_window ? hp2_short_starts : hp2_long_starts;
    const int threshold = static_cast<int>(use_short_window ? short_warning_limit : long_warning_limit);
    const bool hp1_triggered = hp1_value > threshold;
    const bool hp2_triggered = hp2_value > threshold;
    const uint8_t subject =
        alternating || hp1_triggered == hp2_triggered
            ? openquatt_decision_log::SUBJECT_BOTH
            : (hp1_triggered ? openquatt_decision_log::SUBJECT_HP1 : openquatt_decision_log::SUBJECT_HP2);
    id(oq_decision_log)
        .emit(openquatt_decision_log::EVENT_ATTENTION_PATTERN, subject,
              openquatt_decision_log::REASON_START_STOP_RATE_HIGH, openquatt_decision_log::SEVERITY_ATTENTION,
              (uint8_t)id(oq_control_mode_code), openquatt_decision_log::STATE_ACTIVE,
              openquatt_decision_log::STATE_ACTIVE, (int16_t)hp1_value, (int16_t)hp2_value, (int16_t)threshold);
  }
  id(oq_compressor_cycling_alert_latched) = true;
  if (first_occurrence) {
    id(oq_compressor_cycling_alert_first_seen_epoch) = now_epoch;
    id(oq_compressor_cycling_alert_hp1_peak_2h_value) = 0;
    id(oq_compressor_cycling_alert_hp1_peak_72h_value) = 0;
    id(oq_compressor_cycling_alert_hp2_peak_2h_value) = 0;
    id(oq_compressor_cycling_alert_hp2_peak_72h_value) = 0;
    id(oq_compressor_cycling_alert_alternating) = false;
  } else if (id(oq_compressor_cycling_alert_first_seen_epoch) == 0U && now_epoch > 0U) {
    id(oq_compressor_cycling_alert_first_seen_epoch) = now_epoch;
  }
  if (now_epoch > 0U) {
    id(oq_compressor_cycling_alert_last_seen_epoch) = now_epoch;
  }
  id(oq_compressor_cycling_alert_hp1_peak_2h_value) =
      std::max(id(oq_compressor_cycling_alert_hp1_peak_2h_value), hp1_short_starts);
  id(oq_compressor_cycling_alert_hp1_peak_72h_value) =
      std::max(id(oq_compressor_cycling_alert_hp1_peak_72h_value), hp1_long_starts);
  id(oq_compressor_cycling_alert_hp2_peak_2h_value) =
      std::max(id(oq_compressor_cycling_alert_hp2_peak_2h_value), hp2_short_starts);
  id(oq_compressor_cycling_alert_hp2_peak_72h_value) =
      std::max(id(oq_compressor_cycling_alert_hp2_peak_72h_value), hp2_long_starts);
  id(oq_compressor_cycling_alert_alternating) = id(oq_compressor_cycling_alert_alternating) || alternating;
  publish_compressor_cycling_alert_metadata();
  ESP_LOGW("oq_diag", "Compressor cycling incident recorded: HP1=%d/2h %d/72h, HP2=%d/2h %d/72h.", hp1_short_starts,
           hp1_long_starts, hp2_short_starts, hp2_long_starts);
}

inline void acknowledge_compressor_cycling_alert() {
  const uint32_t now_ms = millis();
  const size_t short_warning_limit = compressor_starts_warning_limit_2h();
  const size_t long_warning_limit = compressor_starts_warning_limit_72h();
  if (compressor_cycling_warning(now_ms, short_warning_limit, long_warning_limit)) {
    ESP_LOGW("oq_diag", "Active compressor cycling incident cannot be acknowledged.");
    return;
  }

  id(oq_compressor_cycling_alert_latched) = false;
  id(oq_compressor_cycling_alert_first_seen_epoch) = 0U;
  id(oq_compressor_cycling_alert_last_seen_epoch) = 0U;
  id(oq_compressor_cycling_alert_hp1_peak_2h_value) = 0;
  id(oq_compressor_cycling_alert_hp1_peak_72h_value) = 0;
  id(oq_compressor_cycling_alert_hp2_peak_2h_value) = 0;
  id(oq_compressor_cycling_alert_hp2_peak_72h_value) = 0;
  id(oq_compressor_cycling_alert_alternating) = false;
  publish_compressor_cycling_alert_metadata();
  ESP_LOGI("oq_diag", "Compressor cycling incident acknowledged.");
}

inline bool logged_compressor_cycling_warning_2h(uint32_t now_ms) {
  const bool active =
      installation_monitor().compressor_cycling_warning_2h(now_ms, compressor_starts_warning_limit_2h());
  static bool previous = false;
  if (active != previous) {
    ESP_LOGW("oq_diag", "Compressor cycling warning 2h %s.", active ? "active" : "cleared");
    previous = active;
  }
  return active;
}

inline bool logged_compressor_cycling_warning_72h(uint32_t now_ms) {
  const bool active =
      installation_monitor().compressor_cycling_warning_72h(now_ms, compressor_starts_warning_limit_72h());
  static bool previous = false;
  if (active != previous) {
    ESP_LOGW("oq_diag", "Compressor cycling warning 72h %s.", active ? "active" : "cleared");
    previous = active;
  }
  return active;
}

inline bool logged_alternating_cycling_warning(uint32_t now_ms) {
  const bool active = installation_monitor().alternating_cycling_warning(now_ms, compressor_starts_warning_limit_2h());
  static bool previous = false;
  if (active != previous) {
    ESP_LOGW("oq_diag", "Alternating compressor starts warning %s.", active ? "active" : "cleared");
    previous = active;
  }
  return active;
}

}  // namespace oq_diagnostics
