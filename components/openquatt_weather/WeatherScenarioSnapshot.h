#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace esphome::openquatt_weather {

// Immutable issue context, copied by the main-loop owner from the ticket at
// issuance. Later observations, model fits and downloads cannot rewrite it.
struct IssuedScenario {
  uint64_t monotonic_ms = 0;
  uint32_t epoch = 0, configuration = 0, weather_generation = 0;
  uint32_t weather_fetched_epoch = 0, weather_start_epoch = 0;
  uint32_t model_context = 0, model_samples = 0;
  float room_start_c = NAN, held_heat_w = NAN, h_w_per_k = NAN, c_wh_per_k = NAN;
  float temperature_c[3]{NAN, NAN, NAN};
};
static_assert(sizeof(IssuedScenario) <= 64);

enum class ScenarioReason : uint8_t {
  READY,
  DISABLED,
  PAUSED,
  LOCATION_SAVING,
  LOCATION_INVALID,
  CLOCK_INVALID,
  WEATHER_UNAVAILABLE,
  NO_ISSUE,
  OBSERVATION_INVALID,
  MODEL_UNAVAILABLE,
  CONFIGURATION_CHANGED,
  CONTEXT_CHANGED,
  EXPIRED,
  SNAPSHOT_STALE,
};

inline const char* scenario_reason_name(ScenarioReason reason) {
  switch (reason) {
    case ScenarioReason::READY:
      return "ready";
    case ScenarioReason::DISABLED:
      return "disabled";
    case ScenarioReason::PAUSED:
      return "paused";
    case ScenarioReason::LOCATION_SAVING:
      return "location_saving";
    case ScenarioReason::LOCATION_INVALID:
      return "location_invalid";
    case ScenarioReason::CLOCK_INVALID:
      return "clock_invalid";
    case ScenarioReason::WEATHER_UNAVAILABLE:
      return "weather_unavailable";
    case ScenarioReason::NO_ISSUE:
      return "waiting_for_scenario";
    case ScenarioReason::OBSERVATION_INVALID:
      return "observation_invalid";
    case ScenarioReason::MODEL_UNAVAILABLE:
      return "model_unavailable";
    case ScenarioReason::CONFIGURATION_CHANGED:
      return "configuration_changed";
    case ScenarioReason::CONTEXT_CHANGED:
      return "context_changed";
    case ScenarioReason::EXPIRED:
      return "expired";
    case ScenarioReason::SNAPSHOT_STALE:
      return "snapshot_stale";
  }
  return "snapshot_stale";
}

struct ScenarioSnapshot {
  IssuedScenario issued;
  uint64_t observation_ms = 0;
  uint32_t observation_epoch = 0;
  ScenarioReason reason = ScenarioReason::NO_ISSUE;
  uint8_t horizon_valid_mask = 0;
  bool valid = false;
};
static_assert(std::is_trivially_copyable_v<ScenarioSnapshot>);
static_assert(sizeof(ScenarioSnapshot) <= 80);

inline void invalidate_scenario(ScenarioSnapshot& s, ScenarioReason reason) {
  s.valid = false;
  s.horizon_valid_mask = 0;
  s.reason = reason;
}

inline bool scenario_clock_matches(uint64_t captured_ms, uint32_t captured_epoch, uint64_t now_ms, uint32_t now_epoch) {
  if (!captured_ms || !captured_epoch || now_ms < captured_ms || now_epoch < captured_epoch) return false;
  const uint64_t monotonic_elapsed = now_ms - captured_ms;
  const uint64_t wall_elapsed = static_cast<uint64_t>(now_epoch - captured_epoch) * 1000U;
  return wall_elapsed >= monotonic_elapsed ? wall_elapsed - monotonic_elapsed <= 2000U
                                           : monotonic_elapsed - wall_elapsed <= 2000U;
}

// Only lower published validity. A later weather generation in the same
// configuration does not invalidate a legitimate frozen issue from that config.
inline void age_scenario_snapshot(ScenarioSnapshot& s, uint32_t now_epoch, uint64_t now_ms, bool weather_valid,
                                  uint32_t configuration) {
  if (!s.valid) {
    s.horizon_valid_mask = 0;
    return;
  }
  const auto& issue = s.issued;
  if (!weather_valid) {
    invalidate_scenario(s, ScenarioReason::WEATHER_UNAVAILABLE);
    return;
  }
  if (!configuration || issue.configuration != configuration) {
    invalidate_scenario(s, ScenarioReason::CONFIGURATION_CHANGED);
    return;
  }
  if (!scenario_clock_matches(issue.monotonic_ms, issue.epoch, now_ms, now_epoch) ||
      !scenario_clock_matches(s.observation_ms, s.observation_epoch, now_ms, now_epoch)) {
    invalidate_scenario(s, ScenarioReason::CLOCK_INVALID);
    return;
  }
  if (now_ms - s.observation_ms > 30000U) {
    invalidate_scenario(s, ScenarioReason::OBSERVATION_INVALID);
    return;
  }
  const uint64_t coverage_end = static_cast<uint64_t>(issue.weather_start_epoch) + 35U * 3600U;
  if (!issue.weather_generation || !issue.weather_fetched_epoch || !issue.model_context || !issue.model_samples ||
      issue.weather_fetched_epoch > issue.epoch || issue.weather_start_epoch > issue.epoch ||
      coverage_end < static_cast<uint64_t>(issue.epoch) + 10800U || !std::isfinite(issue.room_start_c) ||
      !std::isfinite(issue.held_heat_w) || !std::isfinite(issue.h_w_per_k) || !std::isfinite(issue.c_wh_per_k) ||
      issue.room_start_c < 0 || issue.room_start_c > 40 || std::fabs(issue.held_heat_w) > 50000 ||
      issue.h_w_per_k < 20 || issue.h_w_per_k > 1500 || issue.c_wh_per_k < 500 || issue.c_wh_per_k > 100000) {
    invalidate_scenario(s, ScenarioReason::NO_ISSUE);
    return;
  }
  static constexpr uint32_t horizons[]{1800, 3600, 10800};
  for (size_t i = 0; i < 3; ++i) {
    if (!std::isfinite(issue.temperature_c[i])) {
      invalidate_scenario(s, ScenarioReason::NO_ISSUE);
      return;
    }
    if (static_cast<uint64_t>(issue.epoch) + horizons[i] <= now_epoch)
      s.horizon_valid_mask &= static_cast<uint8_t>(~(1U << i));
  }
  if (!s.horizon_valid_mask) invalidate_scenario(s, ScenarioReason::EXPIRED);
}
}  // namespace esphome::openquatt_weather
