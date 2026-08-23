#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace esphome {
namespace openquatt_log_history {

inline constexpr uint32_t CRASH_SNAPSHOT_MAGIC = 0x4F514353UL;  // OQCS
inline constexpr uint16_t CRASH_SNAPSHOT_VERSION = 1U;
inline constexpr size_t CRASH_BACKTRACE_CAPACITY = 16U;

inline constexpr size_t CRASH_ELF_SHA256_HEX_LENGTH = 64U;
inline constexpr size_t CRASH_SOURCE_COMMIT_LENGTH = 40U;
inline constexpr size_t CRASH_SOURCE_REPOSITORY_MAX_LENGTH = 97U;
inline constexpr size_t CRASH_BUILD_TARGET_MAX_LENGTH = 96U;
inline constexpr size_t CRASH_FIRMWARE_VERSION_MAX_LENGTH = 32U;
inline constexpr size_t CRASH_RELEASE_CHANNEL_MAX_LENGTH = 16U;
inline constexpr size_t CRASH_HARDWARE_PROFILE_MAX_LENGTH = 32U;
inline constexpr size_t CRASH_TOPOLOGY_MAX_LENGTH = 16U;
inline constexpr size_t CRASH_CONNECTION_MAX_LENGTH = 16U;
inline constexpr size_t CRASH_EXCEPTION_TYPE_NAME_MAX_LENGTH = 24U;
inline constexpr size_t CRASH_REASON_MAX_LENGTH = 63U;

enum CrashBuildIdentityFlags : uint32_t {
  CRASH_BUILD_IDENTITY_ELF_SHA256_VALID = 1U << 0U,
  CRASH_BUILD_IDENTITY_SOURCE_COMMIT_VALID = 1U << 1U,
  CRASH_BUILD_IDENTITY_SOURCE_REPOSITORY_VALID = 1U << 2U,
  CRASH_BUILD_IDENTITY_BUILD_TARGET_VALID = 1U << 3U,
  CRASH_BUILD_IDENTITY_BUILD_EPOCH_VALID = 1U << 4U,
  CRASH_BUILD_IDENTITY_FIRMWARE_VERSION_VALID = 1U << 5U,
  CRASH_BUILD_IDENTITY_RELEASE_CHANNEL_VALID = 1U << 6U,
  CRASH_BUILD_IDENTITY_HARDWARE_PROFILE_VALID = 1U << 7U,
  CRASH_BUILD_IDENTITY_TOPOLOGY_VALID = 1U << 8U,
  CRASH_BUILD_IDENTITY_CONNECTION_VALID = 1U << 9U,
};

enum CrashSnapshotFlags : uint32_t {
  CRASH_SNAPSHOT_PENDING = 1U << 0U,
  CRASH_SNAPSHOT_TIMESTAMP_VALID = 1U << 1U,
  CRASH_SNAPSHOT_RAW_CAUSE_VALID = 1U << 2U,
  CRASH_SNAPSHOT_FAULT_ADDR_VALID = 1U << 3U,
  CRASH_SNAPSHOT_OTHER_CORE_BACKTRACE_VALID = 1U << 4U,
  CRASH_SNAPSHOT_CAPTURED_BY_CURRENT_BUILD = 1U << 5U,
  CRASH_SNAPSHOT_ESPHOME_FOREIGN_BUILD = 1U << 6U,
  CRASH_SNAPSHOT_ADDRESSES_FOREIGN = 1U << 7U,
  CRASH_SNAPSHOT_BUILD_GUARD_CONFLICT = 1U << 8U,
};

enum class CrashExceptionType : uint8_t {
  DEBUG_EXCEPTION = 0U,
  INTERRUPT_WATCHDOG = 1U,
  TASK_WATCHDOG = 2U,
  ABORT = 3U,
  FAULT = 4U,
  UNKNOWN = 0xFFU,
};

struct CrashBuildIdentity {
  uint32_t flags{0U};
  uint64_t build_epoch{0U};
  char elf_sha256[CRASH_ELF_SHA256_HEX_LENGTH + 1U]{};
  char source_commit[CRASH_SOURCE_COMMIT_LENGTH + 1U]{};
  char source_repository[CRASH_SOURCE_REPOSITORY_MAX_LENGTH + 1U]{};
  char build_target[CRASH_BUILD_TARGET_MAX_LENGTH + 1U]{};
  char firmware_version[CRASH_FIRMWARE_VERSION_MAX_LENGTH + 1U]{};
  char release_channel[CRASH_RELEASE_CHANNEL_MAX_LENGTH + 1U]{};
  char hardware_profile[CRASH_HARDWARE_PROFILE_MAX_LENGTH + 1U]{};
  char topology[CRASH_TOPOLOGY_MAX_LENGTH + 1U]{};
  char connection[CRASH_CONNECTION_MAX_LENGTH + 1U]{};
};

struct CrashBacktrace {
  uint8_t core{0U};
  uint8_t count{0U};
  uint16_t reserved{0U};
  std::array<uint32_t, CRASH_BACKTRACE_CAPACITY> addresses{};
};

struct CrashSnapshot {
  uint32_t magic{CRASH_SNAPSHOT_MAGIC};
  uint16_t version{CRASH_SNAPSHOT_VERSION};
  uint16_t size{0U};
  uint32_t flags{0U};
  std::array<uint8_t, 16U> crash_id{};
  std::array<uint8_t, 32U> marker_fingerprint{};
  uint64_t timestamp_s{0U};
  uint32_t uptime_s{0U};
  uint32_t breadcrumb_sequence{0U};
  uint32_t reset_reason{0U};
  CrashBuildIdentity captured_build{};
  CrashBuildIdentity current_build{};
  CrashExceptionType exception_type{CrashExceptionType::UNKNOWN};
  uint8_t crashed_core{0U};
  uint16_t reserved{0U};
  char exception_type_name[CRASH_EXCEPTION_TYPE_NAME_MAX_LENGTH + 1U]{};
  char reason[CRASH_REASON_MAX_LENGTH + 1U]{};
  uint32_t raw_cause{0U};
  uint32_t pc{0U};
  uint32_t fault_addr{0U};
  CrashBacktrace crashed_core_backtrace{};
  CrashBacktrace other_core_backtrace{};
  uint32_t crc{0U};
};

static_assert(std::is_trivially_copyable<CrashBuildIdentity>::value, "Crash build identity must remain persistable");
static_assert(std::is_trivially_copyable<CrashSnapshot>::value, "Crash snapshot must remain persistable");
static_assert(offsetof(CrashSnapshot, crc) + sizeof(uint32_t) == sizeof(CrashSnapshot),
              "Crash snapshot CRC must remain the final field");

inline uint32_t crash_crc32_update(uint32_t crc, const uint8_t* data, size_t length) {
  if (data == nullptr) {
    return crc;
  }
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320UL : 0U);
    }
  }
  return crc;
}

inline uint32_t crash_crc32(const void* data, size_t length) {
  return crash_crc32_update(0xFFFFFFFFUL, static_cast<const uint8_t*>(data), length) ^ 0xFFFFFFFFUL;
}

inline uint32_t crash_snapshot_crc(const CrashSnapshot& snapshot) {
  return crash_crc32(&snapshot, offsetof(CrashSnapshot, crc));
}

inline bool crash_snapshot_is_valid(const CrashSnapshot& snapshot) {
  return snapshot.magic == CRASH_SNAPSHOT_MAGIC && snapshot.version == CRASH_SNAPSHOT_VERSION &&
         snapshot.size == sizeof(CrashSnapshot) && snapshot.crc == crash_snapshot_crc(snapshot);
}

inline bool crash_snapshot_is_pending(const CrashSnapshot& snapshot) {
  return crash_snapshot_is_valid(snapshot) && (snapshot.flags & CRASH_SNAPSHOT_PENDING) != 0U;
}

inline void crash_snapshot_finalize(CrashSnapshot* snapshot) {
  if (snapshot == nullptr) {
    return;
  }
  snapshot->magic = CRASH_SNAPSHOT_MAGIC;
  snapshot->version = CRASH_SNAPSHOT_VERSION;
  snapshot->size = sizeof(CrashSnapshot);
  snapshot->crc = 0U;
  snapshot->crc = crash_snapshot_crc(*snapshot);
}

inline bool crash_id_matches(const std::array<uint8_t, 16U>& lhs, const std::array<uint8_t, 16U>& rhs) {
  return lhs == rhs;
}

inline bool crash_marker_fingerprint_matches(const CrashSnapshot& lhs, const CrashSnapshot& rhs) {
  return crash_snapshot_is_pending(lhs) && crash_snapshot_is_pending(rhs) &&
         lhs.marker_fingerprint == rhs.marker_fingerprint;
}

inline bool crash_snapshot_reuse_durable(const CrashSnapshot& persisted, CrashSnapshot* candidate) {
  if (candidate == nullptr || !crash_marker_fingerprint_matches(persisted, *candidate)) {
    return false;
  }
  *candidate = persisted;
  return true;
}

inline void crash_snapshot_resolve_build_guard(CrashSnapshot* snapshot) {
  if (snapshot == nullptr) {
    return;
  }
  snapshot->flags &= ~(CRASH_SNAPSHOT_CAPTURED_BY_CURRENT_BUILD | CRASH_SNAPSHOT_ADDRESSES_FOREIGN |
                       CRASH_SNAPSHOT_BUILD_GUARD_CONFLICT);
  const bool captured_id_valid = (snapshot->captured_build.flags & CRASH_BUILD_IDENTITY_ELF_SHA256_VALID) != 0U;
  const bool current_id_valid = (snapshot->current_build.flags & CRASH_BUILD_IDENTITY_ELF_SHA256_VALID) != 0U;
  const bool esphome_marked_foreign = (snapshot->flags & CRASH_SNAPSHOT_ESPHOME_FOREIGN_BUILD) != 0U;
  if (!captured_id_valid || !current_id_valid) {
    if (esphome_marked_foreign) {
      snapshot->flags |= CRASH_SNAPSHOT_ADDRESSES_FOREIGN;
    }
    if (captured_id_valid && !current_id_valid) {
      snapshot->flags |= CRASH_SNAPSHOT_ADDRESSES_FOREIGN | CRASH_SNAPSHOT_BUILD_GUARD_CONFLICT;
      snapshot->captured_build = CrashBuildIdentity{};
    }
    return;
  }

  const bool identifiers_match =
      std::strcmp(snapshot->captured_build.elf_sha256, snapshot->current_build.elf_sha256) == 0;
  if (identifiers_match && !esphome_marked_foreign) {
    snapshot->flags |= CRASH_SNAPSHOT_CAPTURED_BY_CURRENT_BUILD;
    return;
  }

  // Any disagreement is ambiguous. In particular, a crash before the new
  // firmware initializes its breadcrumb can leave the previous build identity
  // next to addresses from the new ELF. Never offer that identity for
  // symbolization, even when ESPHome conservatively labels the record foreign.
  snapshot->flags |= CRASH_SNAPSHOT_ADDRESSES_FOREIGN | CRASH_SNAPSHOT_BUILD_GUARD_CONFLICT;
  snapshot->captured_build = CrashBuildIdentity{};
}

inline bool crash_copy_text(char* destination, size_t destination_size, const char* source, size_t source_length) {
  if (destination == nullptr || destination_size == 0U || source == nullptr || source_length >= destination_size) {
    return false;
  }
  std::memcpy(destination, source, source_length);
  destination[source_length] = '\0';
  return true;
}

inline bool crash_is_hex_string(const char* value, size_t required_length) {
  if (value == nullptr || std::strlen(value) != required_length) {
    return false;
  }
  for (size_t index = 0; index < required_length; ++index) {
    const char c = value[index];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  return true;
}

inline CrashExceptionType crash_exception_type_from_name(const char* name) {
  if (name == nullptr) {
    return CrashExceptionType::UNKNOWN;
  }
  if (std::strcmp(name, "Debug exception") == 0) return CrashExceptionType::DEBUG_EXCEPTION;
  if (std::strcmp(name, "Interrupt wdt") == 0) return CrashExceptionType::INTERRUPT_WATCHDOG;
  if (std::strcmp(name, "Task wdt") == 0) return CrashExceptionType::TASK_WATCHDOG;
  if (std::strcmp(name, "Abort") == 0) return CrashExceptionType::ABORT;
  if (std::strcmp(name, "Fault") == 0) return CrashExceptionType::FAULT;
  return CrashExceptionType::UNKNOWN;
}

class EspHomeCrashLogParser {
 public:
  void reset() {
    this->snapshot_ = CrashSnapshot{};
    this->started_ = false;
    this->failed_ = false;
    this->saw_reason_ = false;
    this->saw_core_ = false;
    this->saw_pc_ = false;
    this->parsing_other_core_ = false;
    this->saw_other_core_header_ = false;
  }

  bool consume(const char* line) {
    if (line == nullptr || this->failed_) {
      this->failed_ = true;
      return false;
    }
    while (*line == ' ') ++line;

    if (std::strcmp(line, "*** CRASH DETECTED ON PREVIOUS BOOT ***") == 0) {
      if (this->started_) return this->fail_();
      this->started_ = true;
      return true;
    }
    if (!this->started_) return this->fail_();

    if (std::strncmp(line, "Reason: ", 8U) == 0) return this->parse_reason_(line + 8U);
    if (std::strncmp(line, "Crashed core: ", 14U) == 0) return this->parse_crashed_core_(line + 14U);
    if (std::strcmp(line, "Captured by a different firmware build; addresses belong to that build's ELF") == 0) {
      this->snapshot_.flags |= CRASH_SNAPSHOT_ESPHOME_FOREIGN_BUILD;
      return true;
    }
    if (std::strncmp(line, "PC:", 3U) == 0 || std::strncmp(line, "pc:", 3U) == 0)
      return this->parse_pc_(std::strchr(line, ':') + 1U);
    if (std::strncmp(line, "EXCVADDR:", 9U) == 0 || std::strncmp(line, "excvaddr:", 9U) == 0 ||
        std::strncmp(line, "MTVAL:", 6U) == 0 || std::strncmp(line, "mtval:", 6U) == 0)
      return this->parse_fault_addr_(std::strchr(line, ':') + 1U);
    if (std::strncmp(line, "Other core (", 12U) == 0 || std::strncmp(line, "other core (", 12U) == 0)
      return this->parse_other_core_(line);
    if (line[0] != '\0' && line[1] != '\0' && (line[0] == 'B' || line[0] == 'b') && (line[1] == 'T' || line[1] == 't'))
      return this->parse_backtrace_(line);
    if (std::strncmp(line, "Use: addr2line -pfiaC -e firmware.elf ",
                     sizeof("Use: addr2line -pfiaC -e firmware.elf ") - 1U) == 0 ||
        std::strncmp(line, "Other core: addr2line -pfiaC -e firmware.elf ",
                     sizeof("Other core: addr2line -pfiaC -e firmware.elf ") - 1U) == 0)
      return true;

    return this->fail_();
  }

  bool finish(CrashSnapshot* snapshot) {
    if (snapshot == nullptr || this->failed_ || !this->started_ || !this->saw_reason_ || !this->saw_core_ ||
        !this->saw_pc_ || (this->saw_other_core_header_ && this->snapshot_.other_core_backtrace.count == 0U) ||
        (this->saw_other_core_header_ && this->snapshot_.other_core_backtrace.core == this->snapshot_.crashed_core)) {
      return false;
    }
    this->snapshot_.crashed_core_backtrace.core = this->snapshot_.crashed_core;
    if (this->snapshot_.other_core_backtrace.count > 0U) {
      this->snapshot_.flags |= CRASH_SNAPSHOT_OTHER_CORE_BACKTRACE_VALID;
    }
    *snapshot = this->snapshot_;
    return true;
  }

 private:
  bool fail_() {
    this->failed_ = true;
    return false;
  }

  static bool parse_unsigned_(const char* value, uint32_t maximum, uint32_t* out, const char** end = nullptr) {
    if (value == nullptr || out == nullptr) return false;
    while (*value == ' ') ++value;
    if (*value < '0' || *value > '9') return false;
    uint64_t parsed = 0U;
    do {
      parsed = parsed * 10U + static_cast<uint64_t>(*value - '0');
      if (parsed > maximum) return false;
      ++value;
    } while (*value >= '0' && *value <= '9');
    *out = static_cast<uint32_t>(parsed);
    if (end != nullptr) *end = value;
    return true;
  }

  static bool parse_hex_address_(const char* value, uint32_t* out, const char** end = nullptr) {
    if (value == nullptr || out == nullptr) return false;
    while (*value == ' ') ++value;
    if (value[0] != '0' || value[1] != 'x') return false;
    value += 2U;
    uint32_t parsed = 0U;
    uint8_t digits = 0U;
    while (digits < 8U) {
      const char c = *value;
      uint8_t nibble = 0U;
      if (c >= '0' && c <= '9') {
        nibble = static_cast<uint8_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        nibble = static_cast<uint8_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        nibble = static_cast<uint8_t>(c - 'A' + 10);
      } else {
        break;
      }
      parsed = (parsed << 4U) | nibble;
      ++digits;
      ++value;
    }
    if (digits == 0U ||
        ((*value >= '0' && *value <= '9') || (*value >= 'a' && *value <= 'f') || (*value >= 'A' && *value <= 'F'))) {
      return false;
    }
    *out = parsed;
    if (end != nullptr) *end = value;
    return true;
  }

  static bool address_suffix_is_(const char* suffix, const char* expected) {
    return suffix != nullptr && expected != nullptr && std::strcmp(suffix, expected) == 0;
  }

  bool parse_reason_(const char* value) {
    if (this->saw_reason_ || value == nullptr) return this->fail_();
    const char* separator = std::strstr(value, " - ");
    const char* cause_marker = std::strstr(value, " (cause ");
    size_t type_length = separator == nullptr ? std::strlen(value) : static_cast<size_t>(separator - value);
    if (type_length == 0U || !crash_copy_text(this->snapshot_.exception_type_name,
                                              sizeof(this->snapshot_.exception_type_name), value, type_length))
      return this->fail_();
    this->snapshot_.exception_type = crash_exception_type_from_name(this->snapshot_.exception_type_name);
    if (this->snapshot_.exception_type == CrashExceptionType::UNKNOWN) return this->fail_();

    if ((separator == nullptr) != (cause_marker == nullptr)) return this->fail_();
    if (cause_marker != nullptr && (separator == nullptr || cause_marker <= separator)) return this->fail_();
    if (separator != nullptr) {
      const char* reason_start = separator + 3U;
      const char* reason_end = cause_marker != nullptr ? cause_marker : reason_start + std::strlen(reason_start);
      if (reason_end <= reason_start || !crash_copy_text(this->snapshot_.reason, sizeof(this->snapshot_.reason),
                                                         reason_start, static_cast<size_t>(reason_end - reason_start)))
        return this->fail_();
    }
    if (cause_marker != nullptr) {
      uint32_t cause = 0U;
      const char* end = nullptr;
      if (!parse_unsigned_(cause_marker + 8U, UINT32_MAX, &cause, &end) || end == nullptr || std::strcmp(end, ")") != 0)
        return this->fail_();
      this->snapshot_.raw_cause = cause;
      this->snapshot_.flags |= CRASH_SNAPSHOT_RAW_CAUSE_VALID;
    }
    this->saw_reason_ = true;
    return true;
  }

  bool parse_crashed_core_(const char* value) {
    if (this->saw_core_) return this->fail_();
    uint32_t core = 0U;
    const char* end = nullptr;
    if (!parse_unsigned_(value, 1U, &core, &end) || end == nullptr || end[0] != '\0') return this->fail_();
    this->snapshot_.crashed_core = static_cast<uint8_t>(core);
    this->snapshot_.crashed_core_backtrace.core = static_cast<uint8_t>(core);
    this->saw_core_ = true;
    return true;
  }

  bool parse_pc_(const char* value) {
    const char* end = nullptr;
    if (this->saw_pc_ || !parse_hex_address_(value, &this->snapshot_.pc, &end) ||
        !(address_suffix_is_(end, "") || address_suffix_is_(end, "  (fault location)")))
      return this->fail_();
    this->saw_pc_ = true;
    return true;
  }

  bool parse_fault_addr_(const char* value) {
    const char* end = nullptr;
    if ((this->snapshot_.flags & CRASH_SNAPSHOT_FAULT_ADDR_VALID) != 0U ||
        !parse_hex_address_(value, &this->snapshot_.fault_addr, &end) ||
        !(address_suffix_is_(end, "") || address_suffix_is_(end, "  (faulting address)")))
      return this->fail_();
    this->snapshot_.flags |= CRASH_SNAPSHOT_FAULT_ADDR_VALID;
    return true;
  }

  bool parse_other_core_(const char* line) {
    if (this->parsing_other_core_ || !this->saw_core_) return this->fail_();
    const char* open = std::strchr(line, '(');
    if (open == nullptr) return this->fail_();
    uint32_t core = 0U;
    const char* end = nullptr;
    if (!parse_unsigned_(open + 1U, 1U, &core, &end) || end == nullptr || core == this->snapshot_.crashed_core ||
        !(std::strcmp(end, ") backtrace:") == 0 || std::strcmp(end, "):") == 0))
      return this->fail_();
    this->snapshot_.other_core_backtrace.core = static_cast<uint8_t>(core);
    this->parsing_other_core_ = true;
    this->saw_other_core_header_ = true;
    return true;
  }

  bool parse_backtrace_(const char* line) {
    const char* colon = std::strchr(line, ':');
    if (colon == nullptr) return this->fail_();
    uint32_t index = 0U;
    const char* index_end = nullptr;
    if (!parse_unsigned_(line + 2U, 31U, &index, &index_end) || index_end != colon) return this->fail_();
    CrashBacktrace* backtrace =
        this->parsing_other_core_ ? &this->snapshot_.other_core_backtrace : &this->snapshot_.crashed_core_backtrace;
    if (backtrace->count >= backtrace->addresses.size()) return this->fail_();
    const uint32_t expected_index =
        this->parsing_other_core_ && (this->snapshot_.flags & CRASH_SNAPSHOT_ESPHOME_FOREIGN_BUILD) != 0U
            ? static_cast<uint32_t>(this->snapshot_.crashed_core_backtrace.count) + backtrace->count
            : backtrace->count;
    if (index != expected_index) return this->fail_();
    uint32_t address = 0U;
    const char* end = nullptr;
    if (!parse_hex_address_(colon + 1U, &address, &end) ||
        !(address_suffix_is_(end, "") || address_suffix_is_(end, "  (backtrace)") ||
          address_suffix_is_(end, "  (stack scan)")))
      return this->fail_();
    backtrace->addresses[backtrace->count++] = address;
    return true;
  }

  CrashSnapshot snapshot_{};
  bool started_{false};
  bool failed_{false};
  bool saw_reason_{false};
  bool saw_core_{false};
  bool saw_pc_{false};
  bool parsing_other_core_{false};
  bool saw_other_core_header_{false};
};

}  // namespace openquatt_log_history
}  // namespace esphome
