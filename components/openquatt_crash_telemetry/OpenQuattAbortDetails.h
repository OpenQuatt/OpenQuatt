#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::openquatt_crash_telemetry::detail {

static constexpr uint32_t ABORT_DETAILS_MAGIC = 0x4F514144U;
static constexpr uint32_t ABORT_DETAILS_VERSION = 1U;
static constexpr size_t ABORT_DETAILS_CAPACITY = 256U;

struct AbortReplayContext {
  bool is_abort{false};
  uint8_t core{0xFFU};

  // Consume the ANSI-stripped, newline-terminated report lines, not logger
  // messages that may still contain terminal color sequences.
  void observe(const char* line) {
    if (std::strcmp(line, "  Reason: Abort\n") == 0) is_abort = true;
    if (std::strcmp(line, "  Crashed core: 0\n") == 0) core = 0U;
    if (std::strcmp(line, "  Crashed core: 1\n") == 0) core = 1U;
  }
};

// Separate from the ESPHome record and the persisted MQTT record. Only this
// small panic-time buffer requires internal RAM; no PSRAM/flash access here.
struct AbortDetails {
  uint32_t magic;
  uint32_t version;
  uint32_t build_time;
  uint32_t core;
  uint32_t length;
  uint32_t truncated;
  char text[ABORT_DETAILS_CAPACITY];
  uint32_t checksum;
};
static_assert(sizeof(AbortDetails) == 284U, "Keep the internal panic buffer budget explicit");

// Inline these routines into the IRAM wrapper. Do not introduce libc calls,
// allocation or flash-resident lookup tables in this path.
__attribute__((always_inline)) inline uint32_t abort_details_checksum(const volatile AbortDetails& record) {
  uint32_t hash = 2166136261U;
  const auto* bytes = reinterpret_cast<const volatile uint8_t*>(&record);
  for (size_t i = offsetof(AbortDetails, version); i < offsetof(AbortDetails, checksum); ++i) {
    hash = (hash ^ bytes[i]) * 16777619U;
  }
  return hash;
}

template <typename Readable>
__attribute__((always_inline)) inline void capture_abort_details(volatile AbortDetails& record, const char* text,
                                                                 uint32_t build_time, uint32_t core,
                                                                 Readable readable) {
  record.magic = 0U;
  record.version = ABORT_DETAILS_VERSION;
  record.build_time = build_time;
  record.core = core;
  record.length = 0U;
  record.truncated = 0U;
  for (size_t i = 0; i < ABORT_DETAILS_CAPACITY; ++i) record.text[i] = '\0';
  if (text == nullptr) return;
  const uintptr_t start = reinterpret_cast<uintptr_t>(text);
  for (size_t i = 0; i < ABORT_DETAILS_CAPACITY; ++i) {
    // Validate every byte, including the final byte used to detect truncation.
    // An unreadable source invalidates the entire detail instead of risking a
    // second panic or attaching a misleading partial diagnostic.
    if (start > UINTPTR_MAX - i || !readable(start + i)) return;
    const char c = *reinterpret_cast<const volatile char*>(start + i);
    if (c == '\0') break;
    if (i == ABORT_DETAILS_CAPACITY - 1U) {
      record.truncated = 1U;
      break;
    }
    record.text[i] = c >= 0x20 && c <= 0x7E ? c : '?';
    record.length = static_cast<uint32_t>(i + 1U);
  }
  if (record.length == 0U) return;
  record.checksum = abort_details_checksum(record);
  __atomic_thread_fence(__ATOMIC_RELEASE);
  record.magic = ABORT_DETAILS_MAGIC;  // Commit last; interrupted copies fail closed.
}

inline bool valid_abort_details(const volatile AbortDetails& record, uint32_t build_time) {
  return record.magic == ABORT_DETAILS_MAGIC && record.version == ABORT_DETAILS_VERSION && build_time != 0U &&
         record.build_time == build_time && record.core < 2U && record.length > 0U &&
         record.length < ABORT_DETAILS_CAPACITY && record.truncated <= 1U && record.text[record.length] == '\0' &&
         record.checksum == abort_details_checksum(record);
}

// Take ownership once at boot, before any new crash. Never carry a stale valid
// marker through a later watchdog/fault/normal restart, even if setup fails.
inline bool take_abort_details(volatile AbortDetails& record, uint32_t build_time) {
  const bool valid = valid_abort_details(record, build_time);
  record.magic = 0U;
  return valid;
}

inline bool snapshot_abort_details(volatile AbortDetails& record, uint32_t build_time, AbortDetails& snapshot) {
  const bool valid = take_abort_details(record, build_time);
  if (valid) {
    const auto* source = reinterpret_cast<const volatile uint8_t*>(&record);
    auto* destination = reinterpret_cast<uint8_t*>(&snapshot);
    for (size_t i = 0; i < sizeof(AbortDetails); ++i) destination[i] = source[i];
  }
  return valid;
}

}  // namespace esphome::openquatt_crash_telemetry::detail

namespace esphome::openquatt_crash_telemetry {
void log_abort_details(uint8_t crashed_core);
}
