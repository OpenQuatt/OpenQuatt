#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::openquatt_crash_telemetry::detail {

static constexpr uint32_t WATCHDOG_DETAILS_MAGIC = 0x4F515744U;
static constexpr uint32_t WATCHDOG_DETAILS_VERSION = 1U;

struct WatchdogDetails {
  uint32_t magic;
  uint32_t version;
  uint32_t build_time;
  uint32_t loop_age_us;
  uint32_t checksum;
};
static_assert(sizeof(WatchdogDetails) == 20U, "Keep the internal watchdog buffer budget explicit");

__attribute__((always_inline)) inline uint32_t watchdog_details_checksum(const volatile WatchdogDetails& record) {
  uint32_t hash = 2166136261U;
  const auto* bytes = reinterpret_cast<const volatile uint8_t*>(&record);
  for (size_t i = offsetof(WatchdogDetails, version); i < offsetof(WatchdogDetails, checksum); ++i) {
    hash = (hash ^ bytes[i]) * 16777619U;
  }
  return hash;
}

__attribute__((always_inline)) inline void capture_watchdog_age(volatile WatchdogDetails& record, uint32_t build_time,
                                                                uint32_t now_us, uint32_t last_loop_us) {
  record.magic = 0U;
  record.version = WATCHDOG_DETAILS_VERSION;
  record.build_time = build_time;
  record.loop_age_us = now_us - last_loop_us;
  if (build_time == 0U || last_loop_us == 0U) return;
  record.checksum = watchdog_details_checksum(record);
  __atomic_thread_fence(__ATOMIC_RELEASE);
  record.magic = WATCHDOG_DETAILS_MAGIC;
}

inline bool take_watchdog_details(volatile WatchdogDetails& record, uint32_t build_time, WatchdogDetails& snapshot) {
  const bool valid = record.magic == WATCHDOG_DETAILS_MAGIC && record.version == WATCHDOG_DETAILS_VERSION &&
                     build_time != 0U && record.build_time == build_time &&
                     record.checksum == watchdog_details_checksum(record);
  record.magic = 0U;
  if (valid) {
    const auto* source = reinterpret_cast<const volatile uint8_t*>(&record);
    auto* destination = reinterpret_cast<uint8_t*>(&snapshot);
    for (size_t i = 0; i < sizeof(WatchdogDetails); ++i) destination[i] = source[i];
  }
  return valid;
}

}  // namespace esphome::openquatt_crash_telemetry::detail

namespace esphome::openquatt_crash_telemetry {
void note_crash_telemetry_loop();
void log_watchdog_details();
}  // namespace esphome::openquatt_crash_telemetry
