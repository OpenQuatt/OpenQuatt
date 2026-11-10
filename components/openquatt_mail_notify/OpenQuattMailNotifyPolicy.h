#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Pure, host-testable mail notification policy. No ESP-IDF or ESPHome headers.
namespace oq_mail {

// ---------------------------------------------------------------------------
// Providers and notification kinds
// ---------------------------------------------------------------------------

// Stored as a byte in the configuration record: values must never be renumbered.
enum class Provider : uint8_t {
  NONE = 0,
  SMTP = 1,
  POSTMARK = 2,
  MAILGUN_EU = 3,
  MAILGUN_US = 4,
  LETTERMINT = 5,
  RESEND = 6,
  BREVO = 7,
};
constexpr uint8_t kProviderCount = 8;

inline const char* provider_key(Provider provider) {
  switch (provider) {
    case Provider::SMTP:
      return "smtp";
    case Provider::POSTMARK:
      return "postmark";
    case Provider::MAILGUN_EU:
      return "mailgun_eu";
    case Provider::MAILGUN_US:
      return "mailgun_us";
    case Provider::LETTERMINT:
      return "lettermint";
    case Provider::RESEND:
      return "resend";
    case Provider::BREVO:
      return "brevo";
    case Provider::NONE:
      break;
  }
  return "none";
}

inline bool parse_provider(const char* text, Provider& provider) {
  if (text == nullptr) return false;
  for (uint8_t value = 0; value < kProviderCount; ++value) {
    if (std::strcmp(text, provider_key(static_cast<Provider>(value))) == 0) {
      provider = static_cast<Provider>(value);
      return true;
    }
  }
  return false;
}

inline bool is_api_provider(Provider provider) { return provider != Provider::NONE && provider != Provider::SMTP; }

inline bool is_mailgun(Provider provider) {
  return provider == Provider::MAILGUN_EU || provider == Provider::MAILGUN_US;
}

// Bit positions are part of the stored kind mask: never renumber.
enum class Kind : uint8_t {
  HP_FAULT = 0,
  HP_WARNING = 1,
  HP_LINK_LOSS = 2,
  HP_START_STOP = 3,
  BOILER_FALLBACK = 4,
  FROST_PROTECTION = 5,
  COMPRESSOR_CYCLING = 6,
  CONTROLLER_RESTART = 7,
};
constexpr uint8_t kKindCount = 8;
constexpr uint16_t kAllKindsMask = static_cast<uint16_t>((1U << kKindCount) - 1U);

constexpr uint16_t kind_bit(Kind kind) { return static_cast<uint16_t>(1U << static_cast<uint8_t>(kind)); }

constexpr uint16_t kDefaultKindMask = kind_bit(Kind::HP_FAULT) | kind_bit(Kind::HP_LINK_LOSS) |
                                      kind_bit(Kind::HP_START_STOP) | kind_bit(Kind::BOILER_FALLBACK) |
                                      kind_bit(Kind::CONTROLLER_RESTART);

inline const char* kind_key(Kind kind) {
  switch (kind) {
    case Kind::HP_FAULT:
      return "hp_fault";
    case Kind::HP_WARNING:
      return "hp_warning";
    case Kind::HP_LINK_LOSS:
      return "hp_link_loss";
    case Kind::HP_START_STOP:
      return "hp_start_stop";
    case Kind::BOILER_FALLBACK:
      return "boiler_fallback";
    case Kind::FROST_PROTECTION:
      return "frost_protection";
    case Kind::COMPRESSOR_CYCLING:
      return "compressor_cycling";
    case Kind::CONTROLLER_RESTART:
      return "controller_restart";
  }
  return "";
}

// Parses a comma-separated list of kind keys. Empty input selects no kinds.
inline bool parse_kind_list(const char* text, uint16_t& mask) {
  mask = 0U;
  if (text == nullptr) return true;
  const char* cursor = text;
  while (*cursor != '\0') {
    while (*cursor == ' ' || *cursor == ',') ++cursor;
    if (*cursor == '\0') break;
    const char* end = cursor;
    while (*end != '\0' && *end != ',' && *end != ' ') ++end;
    const size_t length = static_cast<size_t>(end - cursor);
    bool matched = false;
    for (uint8_t value = 0; value < kKindCount; ++value) {
      const char* key = kind_key(static_cast<Kind>(value));
      if (std::strlen(key) == length && std::strncmp(cursor, key, length) == 0) {
        mask = static_cast<uint16_t>(mask | (1U << value));
        matched = true;
        break;
      }
    }
    if (!matched) return false;
    cursor = end;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Stored configuration
// ---------------------------------------------------------------------------

constexpr size_t kHostMax = 64;
constexpr size_t kUsernameMax = 96;
constexpr size_t kSecretMax = 160;
constexpr size_t kDomainMax = 64;
constexpr size_t kFromMax = 96;
constexpr size_t kRecipientsMax = 255;
constexpr size_t kMaxRecipients = 5;
constexpr uint16_t kDefaultSmtpPort = 465;
constexpr uint32_t kConfigMagic = 0x4F514D4CU;  // "OQML"
constexpr uint16_t kConfigVersion = 1;

struct Config {
  uint32_t magic;
  uint16_t version;
  uint8_t enabled;
  uint8_t provider;
  uint16_t smtp_port;
  uint16_t kind_mask;
  uint8_t recovery;
  uint8_t reserved[3];
  char smtp_host[kHostMax + 1];
  char username[kUsernameMax + 1];
  char secret[kSecretMax + 1];
  char domain[kDomainMax + 1];
  char from[kFromMax + 1];
  char recipients[kRecipientsMax + 1];
  uint32_t checksum;
};

static_assert(sizeof(Config) == 764U, "Mail configuration record layout changed; bump kConfigVersion");

inline uint32_t fnv1a(const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  uint32_t hash = 2166136261U;
  for (size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= 16777619U;
  }
  return hash;
}

inline uint32_t config_checksum(const Config& config) { return fnv1a(&config, offsetof(Config, checksum)); }

inline void seal(Config& config) {
  config.magic = kConfigMagic;
  config.version = kConfigVersion;
  config.checksum = config_checksum(config);
}

inline Config default_config() {
  Config config;
  std::memset(&config, 0, sizeof(config));
  config.provider = static_cast<uint8_t>(Provider::NONE);
  config.smtp_port = kDefaultSmtpPort;
  config.kind_mask = kDefaultKindMask;
  config.recovery = 1U;
  seal(config);
  return config;
}

template <size_t N>
inline bool terminated(const char (&field)[N]) {
  return std::memchr(field, '\0', N) != nullptr;
}

inline bool valid_config_record(const Config& config) {
  return config.magic == kConfigMagic && config.version == kConfigVersion &&
         config.checksum == config_checksum(config) && config.provider < kProviderCount && config.enabled <= 1U &&
         config.recovery <= 1U && (config.kind_mask & ~kAllKindsMask) == 0U && terminated(config.smtp_host) &&
         terminated(config.username) && terminated(config.secret) && terminated(config.domain) &&
         terminated(config.from) && terminated(config.recipients);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

enum class ValidationError : uint8_t {
  NONE = 0,
  INVALID_PROVIDER,
  MISSING_HOST,
  INVALID_HOST,
  INVALID_PORT,
  MISSING_SECRET,
  INVALID_SECRET,
  MISSING_DOMAIN,
  INVALID_DOMAIN,
  INVALID_USERNAME,
  INVALID_FROM,
  MISSING_FROM,
  MISSING_RECIPIENTS,
  INVALID_RECIPIENTS,
  TOO_MANY_RECIPIENTS,
  FIELD_TOO_LONG,
  INVALID_KINDS,
};

// Stable API error keys. Several internal reasons share one public key.
inline const char* validation_error_key(ValidationError error) {
  switch (error) {
    case ValidationError::NONE:
      return "none";
    case ValidationError::INVALID_PROVIDER:
      return "invalid_provider";
    case ValidationError::MISSING_HOST:
    case ValidationError::INVALID_HOST:
      return "missing_host";
    case ValidationError::INVALID_PORT:
      return "invalid_port";
    case ValidationError::MISSING_SECRET:
    case ValidationError::INVALID_SECRET:
    case ValidationError::INVALID_USERNAME:
      return "missing_secret";
    case ValidationError::MISSING_DOMAIN:
    case ValidationError::INVALID_DOMAIN:
      return "missing_domain";
    case ValidationError::INVALID_FROM:
    case ValidationError::MISSING_FROM:
      return "invalid_from";
    case ValidationError::MISSING_RECIPIENTS:
      return "missing_recipients";
    case ValidationError::INVALID_RECIPIENTS:
      return "invalid_recipients";
    case ValidationError::TOO_MANY_RECIPIENTS:
      return "too_many_recipients";
    case ValidationError::FIELD_TOO_LONG:
      return "field_too_long";
    case ValidationError::INVALID_KINDS:
      return "invalid_kinds";
  }
  return "invalid";
}

inline bool is_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

inline char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// DNS hostname with at least one dot; labels of letters, digits and hyphens.
inline bool valid_hostname(const char* text, size_t length) {
  if (text == nullptr || length == 0 || length > 253) return false;
  bool dot = false;
  size_t label = 0;
  for (size_t index = 0; index < length; ++index) {
    const char c = text[index];
    if (c == '.') {
      if (label == 0 || text[index - 1] == '-') return false;
      dot = true;
      label = 0;
      continue;
    }
    if (!is_alnum(c) && c != '-') return false;
    if (label == 0 && c == '-') return false;
    if (++label > 63) return false;
  }
  return dot && label > 0 && text[length - 1] != '-';
}

inline bool valid_hostname(const char* text) { return text != nullptr && valid_hostname(text, std::strlen(text)); }

inline bool local_part_char(char c) {
  if (is_alnum(c)) return true;
  switch (c) {
    case '.':
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '/':
    case '=':
    case '?':
    case '^':
    case '_':
    case '`':
    case '{':
    case '|':
    case '}':
    case '~':
    case '-':
      return true;
    default:
      return false;
  }
}

// Unquoted ASCII address: no display names, comments or SMTPUTF8.
inline bool valid_email(const char* text, size_t length) {
  if (text == nullptr || length < 3 || length > 254) return false;
  const void* at_ptr = std::memchr(text, '@', length);
  if (at_ptr == nullptr) return false;
  const size_t at = static_cast<size_t>(static_cast<const char*>(at_ptr) - text);
  if (at == 0 || at > 64 || std::memchr(text + at + 1, '@', length - at - 1) != nullptr) return false;
  if (text[0] == '.' || text[at - 1] == '.') return false;
  for (size_t index = 0; index < at; ++index) {
    if (!local_part_char(text[index])) return false;
    if (text[index] == '.' && index > 0 && text[index - 1] == '.') return false;
  }
  return valid_hostname(text + at + 1, length - at - 1);
}

inline bool valid_email(const char* text) { return text != nullptr && valid_email(text, std::strlen(text)); }

// Free text stored in credentials: no control characters (header/protocol injection).
inline bool printable(const char* text, bool allow_space, bool allow_non_ascii) {
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    const auto c = static_cast<uint8_t>(*cursor);
    if (c < 0x20U || c == 0x7FU) return false;
    if (c == 0x20U && !allow_space) return false;
    if (c >= 0x80U && !allow_non_ascii) return false;
  }
  return true;
}

// Copies a request field into a fixed configuration field.
template <size_t N>
inline bool copy_field(char (&field)[N], const char* value, size_t length) {
  if (length >= N || (length > 0 && value == nullptr) || std::memchr(value == nullptr ? "" : value, '\0', length)) {
    return false;
  }
  if (length > 0) std::memcpy(field, value, length);
  std::memset(field + length, 0, N - length);
  return true;
}

inline bool is_separator(char c) { return c == ',' || c == ';'; }
inline bool is_blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Normalizes "a@x.nl;b@y.nl , a@x.nl" to "a@x.nl, b@y.nl". Duplicates are
// removed case-insensitively. Empty input yields an empty list.
inline ValidationError canonicalize_recipients(const char* input, char* out, size_t capacity, size_t& count) {
  count = 0;
  if (out == nullptr || capacity == 0) return ValidationError::FIELD_TOO_LONG;
  out[0] = '\0';
  if (input == nullptr) return ValidationError::NONE;
  size_t used = 0;
  const char* cursor = input;
  while (*cursor != '\0') {
    while (is_separator(*cursor) || is_blank(*cursor)) ++cursor;
    if (*cursor == '\0') break;
    const char* start = cursor;
    while (*cursor != '\0' && !is_separator(*cursor)) ++cursor;
    const char* end = cursor;
    while (end > start && is_blank(end[-1])) --end;
    const size_t length = static_cast<size_t>(end - start);
    if (!valid_email(start, length)) return ValidationError::INVALID_RECIPIENTS;
    bool duplicate = false;
    for (size_t offset = 0; offset < used && !duplicate;) {
      size_t next = offset;
      while (next < used && out[next] != ',') ++next;
      if (next - offset == length) {
        duplicate = true;
        for (size_t index = 0; index < length; ++index) {
          if (lower(out[offset + index]) != lower(start[index])) {
            duplicate = false;
            break;
          }
        }
      }
      offset = next < used ? next + 2 : used;
    }
    if (duplicate) continue;
    if (++count > kMaxRecipients) return ValidationError::TOO_MANY_RECIPIENTS;
    const size_t extra = (used > 0 ? 2 : 0) + length;
    if (used + extra >= capacity) return ValidationError::FIELD_TOO_LONG;
    if (used > 0) {
      out[used++] = ',';
      out[used++] = ' ';
    }
    std::memcpy(out + used, start, length);
    used += length;
    out[used] = '\0';
  }
  return ValidationError::NONE;
}

// Calls visit(address, length) for each entry of a canonical recipient list.
template <typename Visit>
inline bool for_each_recipient(const char* canonical, Visit visit) {
  const char* cursor = canonical;
  while (cursor != nullptr && *cursor != '\0') {
    const char* end = std::strchr(cursor, ',');
    const size_t length = end == nullptr ? std::strlen(cursor) : static_cast<size_t>(end - cursor);
    if (length > 0 && !visit(cursor, length)) return false;
    if (end == nullptr) break;
    cursor = end + 1;
    while (*cursor == ' ') ++cursor;
  }
  return true;
}

// Validates a candidate record. With require_complete, every field needed to
// actually deliver a message must be present (used when notifications are on).
inline ValidationError validate(const Config& config, bool require_complete) {
  if (config.provider >= kProviderCount) return ValidationError::INVALID_PROVIDER;
  if ((config.kind_mask & ~kAllKindsMask) != 0U) return ValidationError::INVALID_KINDS;
  const auto provider = static_cast<Provider>(config.provider);
  if (!terminated(config.smtp_host) || !terminated(config.username) || !terminated(config.secret) ||
      !terminated(config.domain) || !terminated(config.from) || !terminated(config.recipients)) {
    return ValidationError::FIELD_TOO_LONG;
  }
  // Fields of another provider are kept as a draft but only checked when used.
  if (provider == Provider::SMTP && config.smtp_host[0] != '\0' && !valid_hostname(config.smtp_host)) {
    return ValidationError::INVALID_HOST;
  }
  if (provider == Provider::SMTP && config.smtp_port == 0U) return ValidationError::INVALID_PORT;
  if (!printable(config.username, true, true)) return ValidationError::INVALID_USERNAME;
  if (!printable(config.secret, provider == Provider::SMTP, provider == Provider::SMTP)) {
    return ValidationError::INVALID_SECRET;
  }
  if (is_mailgun(provider) && config.domain[0] != '\0' && !valid_hostname(config.domain)) {
    return ValidationError::INVALID_DOMAIN;
  }
  if (config.from[0] != '\0' && !valid_email(config.from)) return ValidationError::INVALID_FROM;
  char canonical[kRecipientsMax + 1];
  size_t count = 0;
  const ValidationError recipients = canonicalize_recipients(config.recipients, canonical, sizeof(canonical), count);
  if (recipients != ValidationError::NONE) return recipients;
  if (!require_complete) return ValidationError::NONE;

  if (provider == Provider::NONE) return ValidationError::INVALID_PROVIDER;
  if (provider == Provider::SMTP) {
    if (config.smtp_host[0] == '\0') return ValidationError::MISSING_HOST;
    if (config.username[0] != '\0' && config.secret[0] == '\0') return ValidationError::MISSING_SECRET;
  } else {
    if (config.secret[0] == '\0') return ValidationError::MISSING_SECRET;
    if (is_mailgun(provider) && config.domain[0] == '\0') return ValidationError::MISSING_DOMAIN;
  }
  if (config.from[0] == '\0') return ValidationError::MISSING_FROM;
  if (count == 0) return ValidationError::MISSING_RECIPIENTS;
  return ValidationError::NONE;
}

inline bool config_complete(const Config& config) { return validate(config, true) == ValidationError::NONE; }

// ---------------------------------------------------------------------------
// Decision-log event classification
// ---------------------------------------------------------------------------

// Mirrors of openquatt_decision_log enum values; the component static_asserts
// that they match.
namespace event {
constexpr uint8_t ATTENTION_PATTERN = 14;
constexpr uint8_t FROST_PROTECTION_START = 15;
constexpr uint8_t FROST_PROTECTION_CLEAR = 16;
constexpr uint8_t INCIDENT_START = 23;
constexpr uint8_t INCIDENT_CLEAR = 24;
constexpr uint8_t BOILER_FALLBACK_START = 28;
constexpr uint8_t BOILER_FALLBACK_STOP = 29;
}  // namespace event

namespace reason {
constexpr uint8_t START_STOP_RATE_HIGH = 31;
constexpr uint8_t HP_FAULT = 56;
constexpr uint8_t HP_PROTECTION = 57;
constexpr uint8_t HP_PREHEAT = 58;
constexpr uint8_t HP_LINK_LOSS = 59;
constexpr uint8_t HP_START_FAILED = 60;
constexpr uint8_t HP_STOP_UNCONFIRMED = 61;
constexpr uint8_t HP_PERSISTENCE_FAILURE = 70;
}  // namespace reason

namespace severity {
constexpr uint8_t NORMAL = 1;
constexpr uint8_t LIMITED = 2;
constexpr uint8_t ATTENTION = 3;
constexpr uint8_t FAULT = 4;
}  // namespace severity

namespace subject {
constexpr uint8_t SYSTEM = 1;
constexpr uint8_t HP1 = 2;
constexpr uint8_t HP2 = 3;
constexpr uint8_t BOTH = 4;
constexpr uint8_t CV = 5;
constexpr uint8_t CONTROLLER = 8;
}  // namespace subject

namespace flag {
constexpr uint8_t LATCHED = 0x01;
constexpr uint8_t MANUAL_RESET_REQUIRED = 0x40;
constexpr uint8_t RESTORED_AFTER_BOOT = 0x80;
}  // namespace flag

struct EventInput {
  uint32_t epoch_s{0};
  uint16_t duration_s{0};
  int16_t value_a{0};
  int16_t value_b{0};
  int16_t threshold_a{0};
  uint8_t event_type{0};
  uint8_t subject{0};
  uint8_t reason_code{0};
  uint8_t severity{0};
  uint8_t flags{0};
};

// One line of a notification mail.
struct Notice {
  uint32_t epoch_s{0};
  uint16_t duration_s{0};
  int16_t value_a{0};
  int16_t value_b{0};
  int16_t threshold_a{0};
  uint8_t kind{0};
  uint8_t subject{0};
  uint8_t reason_code{0};
  uint8_t flags{0};
  bool recovery{false};
};

// Returns true and fills notice when the event maps to a notification kind.
// Selection (enabled kinds, recovery mails) is applied separately.
inline bool classify(const EventInput& input, Notice& notice) {
  Kind kind = Kind::HP_FAULT;
  bool recovery = false;
  switch (input.event_type) {
    case event::INCIDENT_START:
    case event::INCIDENT_CLEAR:
      // A latch restored from flash after a reboot is not a new incident.
      if ((input.flags & flag::RESTORED_AFTER_BOOT) != 0U) return false;
      // The raw bit dropped but the latch holds until a confirmed ODU power
      // cycle: the heat pump stays blocked, so this is not a recovery.
      if (input.event_type == event::INCIDENT_CLEAR && (input.flags & (flag::LATCHED | flag::MANUAL_RESET_REQUIRED)) ==
                                                           (flag::LATCHED | flag::MANUAL_RESET_REQUIRED)) {
        return false;
      }
      recovery = input.event_type == event::INCIDENT_CLEAR;
      if (input.reason_code == reason::HP_LINK_LOSS) {
        kind = Kind::HP_LINK_LOSS;
      } else if (input.reason_code == reason::HP_START_FAILED || input.reason_code == reason::HP_STOP_UNCONFIRMED) {
        kind = Kind::HP_START_STOP;
      } else if (input.reason_code == reason::HP_PREHEAT) {
        return false;  // First-start preheat is an expected start condition.
      } else if (input.severity == severity::FAULT) {
        kind = Kind::HP_FAULT;
      } else if (input.severity == severity::LIMITED || input.severity == severity::ATTENTION) {
        kind = Kind::HP_WARNING;
      } else {
        return false;  // Informational ODU status bits.
      }
      break;
    case event::BOILER_FALLBACK_START:
    case event::BOILER_FALLBACK_STOP:
      kind = Kind::BOILER_FALLBACK;
      recovery = input.event_type == event::BOILER_FALLBACK_STOP;
      break;
    case event::FROST_PROTECTION_START:
    case event::FROST_PROTECTION_CLEAR:
      kind = Kind::FROST_PROTECTION;
      recovery = input.event_type == event::FROST_PROTECTION_CLEAR;
      break;
    case event::ATTENTION_PATTERN:
      if (input.reason_code != reason::START_STOP_RATE_HIGH) return false;
      kind = Kind::COMPRESSOR_CYCLING;
      break;
    default:
      return false;
  }
  notice.epoch_s = input.epoch_s;
  notice.duration_s = input.duration_s;
  notice.value_a = input.value_a;
  notice.value_b = input.value_b;
  notice.threshold_a = input.threshold_a;
  notice.kind = static_cast<uint8_t>(kind);
  notice.subject = input.subject;
  notice.reason_code = input.reason_code;
  notice.flags = input.flags;
  notice.recovery = recovery;
  return true;
}

inline bool selected(const Notice& notice, uint16_t kind_mask, bool recovery_enabled) {
  if (notice.kind >= kKindCount || (kind_mask & (1U << notice.kind)) == 0U) return false;
  return !notice.recovery || recovery_enabled;
}

// ---------------------------------------------------------------------------
// Delivery policy
// ---------------------------------------------------------------------------

constexpr uint32_t kCoalesceWindowMs = 60000U;
constexpr uint32_t kRequestDeadlineMs = 20000U;
constexpr uint8_t kMaxSendAttempts = 6U;
constexpr uint32_t kDailyLimit = 24U;
constexpr uint32_t kTestMinIntervalMs = 30000U;

// 1, 2, 4, 8, 16 minutes, capped at 30 minutes.
inline uint32_t retry_delay_ms(uint8_t failed_attempts) {
  if (failed_attempts == 0U) return 0U;
  const uint8_t shift = failed_attempts > 6U ? 6U : static_cast<uint8_t>(failed_attempts - 1U);
  const uint32_t delay = 60000U << shift;
  return delay > 1800000U ? 1800000U : delay;
}

// Rolling 24-hour budget of successfully delivered notification mails, keyed
// by wall-clock seconds so it can be persisted and survive (crash-loop) reboots.
constexpr uint32_t kDaySeconds = 24U * 60U * 60U;

class SendBudget {
 public:
  // Only past sends inside the window count; entries from a clock that later
  // jumped back are ignored instead of blocking forever.
  uint32_t used(uint32_t now_s) const {
    uint32_t count = 0;
    for (uint32_t at : sent_s_) {
      if (at != 0U && at <= now_s && now_s - at < kDaySeconds) ++count;
    }
    return count;
  }
  bool allowed(uint32_t now_s) const { return used(now_s) < kDailyLimit; }
  void record(uint32_t now_s) {
    size_t slot = 0;
    for (size_t index = 0; index < kDailyLimit; ++index) {
      const uint32_t at = sent_s_[index];
      if (at == 0U || at > now_s || now_s - at >= kDaySeconds) {
        slot = index;
        break;
      }
      if (at < sent_s_[slot]) slot = index;
    }
    sent_s_[slot] = now_s == 0U ? 1U : now_s;
  }
  const uint32_t* entries() const { return sent_s_; }
  void load(const uint32_t* entries) {
    for (size_t index = 0; index < kDailyLimit; ++index) sent_s_[index] = entries[index];
  }

 private:
  uint32_t sent_s_[kDailyLimit]{};
};

constexpr uint32_t kBudgetMagic = 0x4F514D42U;  // "OQMB"
constexpr uint16_t kBudgetVersion = 1;

struct BudgetRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t sent_s[kDailyLimit];
  uint32_t checksum;
};
static_assert(sizeof(BudgetRecord) == 108U, "Mail budget record layout changed; bump kBudgetVersion");

inline BudgetRecord budget_record(const SendBudget& budget) {
  BudgetRecord record;
  std::memset(&record, 0, sizeof(record));
  record.magic = kBudgetMagic;
  record.version = kBudgetVersion;
  for (size_t index = 0; index < kDailyLimit; ++index) record.sent_s[index] = budget.entries()[index];
  record.checksum = fnv1a(&record, offsetof(BudgetRecord, checksum));
  return record;
}

inline bool valid_budget_record(const BudgetRecord& record) {
  return record.magic == kBudgetMagic && record.version == kBudgetVersion &&
         record.checksum == fnv1a(&record, offsetof(BudgetRecord, checksum));
}

// Overwrites secrets in a way the optimizer may not elide.
inline void secure_wipe(void* data, size_t size) {
  volatile auto* bytes = static_cast<volatile uint8_t*>(data);
  while (size-- > 0) *bytes++ = 0U;
}

// ---------------------------------------------------------------------------
// Bounded text buffer. With data == nullptr it only counts (dry run).
// ---------------------------------------------------------------------------

class Buffer {
 public:
  Buffer(char* data, size_t capacity) : data_(data), capacity_(capacity) {
    if (data_ != nullptr && capacity_ > 0) data_[0] = '\0';
  }
  static Buffer counter() { return Buffer(nullptr, 0); }

  bool append(const char* text, size_t length) {
    if (length == 0) return ok();
    if (data_ != nullptr) {
      if (overflow_ || length >= capacity_ - length_) {
        overflow_ = true;
        return false;
      }
      std::memcpy(data_ + length_, text, length);
      data_[length_ + length] = '\0';
    }
    length_ += length;
    return ok();
  }
  bool append(const char* text) { return append(text, text == nullptr ? 0 : std::strlen(text)); }
  bool append_char(char c) { return append(&c, 1); }
  bool appendf(const char* format, ...) __attribute__((format(printf, 2, 3))) {
    char scratch[96];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(scratch, sizeof(scratch), format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(scratch)) {
      overflow_ = true;
      return false;
    }
    return append(scratch, static_cast<size_t>(written));
  }

  size_t length() const { return length_; }
  bool ok() const { return !overflow_; }
  const char* data() const { return data_; }

 private:
  char* data_;
  size_t capacity_;
  size_t length_{0};
  bool overflow_{false};
};

inline bool append_json_string(Buffer& out, const char* text) {
  out.append_char('"');
  for (const char* cursor = text == nullptr ? "" : text; *cursor != '\0'; ++cursor) {
    const auto c = static_cast<uint8_t>(*cursor);
    switch (c) {
      case '"':
        out.append("\\\"");
        break;
      case '\\':
        out.append("\\\\");
        break;
      case '\n':
        out.append("\\n");
        break;
      case '\r':
        out.append("\\r");
        break;
      case '\t':
        out.append("\\t");
        break;
      default:
        if (c < 0x20U) {
          out.appendf("\\u%04x", static_cast<unsigned>(c));
        } else {
          out.append_char(static_cast<char>(c));
        }
    }
  }
  return out.append_char('"');
}

inline size_t base64_length(size_t length) { return ((length + 2U) / 3U) * 4U; }

inline bool append_base64(Buffer& out, const uint8_t* data, size_t length, size_t line_length = 0) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t column = 0;
  for (size_t index = 0; index < length; index += 3) {
    const uint32_t a = data[index];
    const uint32_t b = index + 1 < length ? data[index + 1] : 0U;
    const uint32_t c = index + 2 < length ? data[index + 2] : 0U;
    const uint32_t triple = (a << 16) | (b << 8) | c;
    char quad[4] = {alphabet[(triple >> 18) & 0x3F], alphabet[(triple >> 12) & 0x3F],
                    index + 1 < length ? alphabet[(triple >> 6) & 0x3F] : '=',
                    index + 2 < length ? alphabet[triple & 0x3F] : '='};
    out.append(quad, 4);
    column += 4;
    if (line_length > 0 && column >= line_length && index + 3 < length) {
      out.append("\r\n");
      column = 0;
    }
  }
  return out.ok();
}

inline bool append_base64(Buffer& out, const char* text) {
  return append_base64(out, reinterpret_cast<const uint8_t*>(text), text == nullptr ? 0 : std::strlen(text));
}

// application/x-www-form-urlencoded-free multipart part (Mailgun).
inline bool append_multipart_field(Buffer& out, const char* boundary, const char* name, const char* value,
                                   size_t value_length) {
  out.append("--");
  out.append(boundary);
  out.append("\r\nContent-Disposition: form-data; name=\"");
  out.append(name);
  out.append("\"\r\n\r\n");
  out.append(value, value_length);
  return out.append("\r\n");
}

// ---------------------------------------------------------------------------
// Outbound requests
// ---------------------------------------------------------------------------

struct Message {
  const char* from{""};        // plain address
  const char* recipients{""};  // canonical list
  const char* subject{""};     // UTF-8, single line
  const char* body{""};        // UTF-8 text, '\n' line endings
};

inline const char* api_host(Provider provider) {
  switch (provider) {
    case Provider::POSTMARK:
      return "api.postmarkapp.com";
    case Provider::MAILGUN_EU:
      return "api.eu.mailgun.net";
    case Provider::MAILGUN_US:
      return "api.mailgun.net";
    case Provider::LETTERMINT:
      return "api.lettermint.co";
    case Provider::RESEND:
      return "api.resend.com";
    case Provider::BREVO:
      return "api.brevo.com";
    case Provider::NONE:
    case Provider::SMTP:
      break;
  }
  return "";
}

constexpr const char* kSenderName = "OpenQuatt";
constexpr const char* kMultipartBoundary = "oq-mail-7c1f2b9e4d5a";

inline bool append_json_recipient_array(Buffer& out, const char* recipients, bool objects) {
  out.append_char('[');
  bool first = true;
  for_each_recipient(recipients, [&](const char* address, size_t length) {
    char copy[256];
    if (length >= sizeof(copy)) return false;
    std::memcpy(copy, address, length);
    copy[length] = '\0';
    if (!first) out.append_char(',');
    first = false;
    if (objects) out.append("{\"email\":");
    append_json_string(out, copy);
    if (objects) out.append_char('}');
    return true;
  });
  return out.append_char(']');
}

inline bool append_api_body(Buffer& out, Provider provider, const Message& message) {
  char from_header[kFromMax + 16];
  std::snprintf(from_header, sizeof(from_header), "%s <%s>", kSenderName, message.from);
  switch (provider) {
    case Provider::POSTMARK:
      out.append("{\"From\":");
      append_json_string(out, from_header);
      out.append(",\"To\":");
      append_json_string(out, message.recipients);
      out.append(",\"Subject\":");
      append_json_string(out, message.subject);
      out.append(",\"TextBody\":");
      append_json_string(out, message.body);
      return out.append(",\"MessageStream\":\"outbound\"}");
    case Provider::LETTERMINT:
    case Provider::RESEND:
      out.append("{\"from\":");
      append_json_string(out, from_header);
      out.append(",\"to\":");
      append_json_recipient_array(out, message.recipients, false);
      out.append(",\"subject\":");
      append_json_string(out, message.subject);
      out.append(",\"text\":");
      append_json_string(out, message.body);
      return out.append_char('}');
    case Provider::BREVO:
      out.append("{\"sender\":{\"name\":");
      append_json_string(out, kSenderName);
      out.append(",\"email\":");
      append_json_string(out, message.from);
      out.append("},\"to\":");
      append_json_recipient_array(out, message.recipients, true);
      out.append(",\"subject\":");
      append_json_string(out, message.subject);
      out.append(",\"textContent\":");
      append_json_string(out, message.body);
      return out.append_char('}');
    case Provider::MAILGUN_EU:
    case Provider::MAILGUN_US:
      append_multipart_field(out, kMultipartBoundary, "from", from_header, std::strlen(from_header));
      for_each_recipient(message.recipients, [&](const char* address, size_t length) {
        return append_multipart_field(out, kMultipartBoundary, "to", address, length);
      });
      append_multipart_field(out, kMultipartBoundary, "subject", message.subject, std::strlen(message.subject));
      append_multipart_field(out, kMultipartBoundary, "text", message.body, std::strlen(message.body));
      out.append("--");
      out.append(kMultipartBoundary);
      return out.append("--\r\n");
    case Provider::NONE:
    case Provider::SMTP:
      break;
  }
  return false;
}

// Builds the complete HTTP/1.1 request for an API provider.
inline bool build_api_request(Buffer& out, Provider provider, const char* secret, const char* domain,
                              const Message& message) {
  if (!is_api_provider(provider) || secret == nullptr || secret[0] == '\0') return false;
  Buffer counter = Buffer::counter();
  if (!append_api_body(counter, provider, message)) return false;
  const size_t body_length = counter.length();

  out.append("POST ");
  switch (provider) {
    case Provider::POSTMARK:
      out.append("/email");
      break;
    case Provider::MAILGUN_EU:
    case Provider::MAILGUN_US:
      if (domain == nullptr || !valid_hostname(domain)) return false;
      out.append("/v3/");
      out.append(domain);
      out.append("/messages");
      break;
    case Provider::LETTERMINT:
      out.append("/v1/send");
      break;
    case Provider::RESEND:
      out.append("/emails");
      break;
    case Provider::BREVO:
      out.append("/v3/smtp/email");
      break;
    default:
      return false;
  }
  out.append(" HTTP/1.1\r\nHost: ");
  out.append(api_host(provider));
  out.append("\r\nUser-Agent: OpenQuatt-mail\r\nAccept: application/json\r\nConnection: close\r\n");
  switch (provider) {
    case Provider::POSTMARK:
      out.append("X-Postmark-Server-Token: ");
      out.append(secret);
      out.append("\r\n");
      break;
    case Provider::MAILGUN_EU:
    case Provider::MAILGUN_US: {
      char credentials[kSecretMax + 8];
      std::snprintf(credentials, sizeof(credentials), "api:%s", secret);
      out.append("Authorization: Basic ");
      append_base64(out, credentials);
      secure_wipe(credentials, sizeof(credentials));
      out.append("\r\n");
      break;
    }
    case Provider::LETTERMINT:
      out.append("x-lettermint-token: ");
      out.append(secret);
      out.append("\r\n");
      break;
    case Provider::RESEND:
      out.append("Authorization: Bearer ");
      out.append(secret);
      out.append("\r\n");
      break;
    case Provider::BREVO:
      out.append("api-key: ");
      out.append(secret);
      out.append("\r\n");
      break;
    default:
      return false;
  }
  if (is_mailgun(provider)) {
    out.append("Content-Type: multipart/form-data; boundary=");
    out.append(kMultipartBoundary);
    out.append("\r\n");
  } else {
    out.append("Content-Type: application/json; charset=utf-8\r\n");
  }
  out.appendf("Content-Length: %u\r\n\r\n", static_cast<unsigned>(body_length));
  const size_t before = out.length();
  append_api_body(out, provider, message);
  return out.ok() && out.length() - before == body_length;
}

// RFC 5322 message for SMTP DATA, terminated with CRLF.CRLF. The body uses
// base64 so 8-bit text works without 8BITMIME and never needs dot-stuffing.
inline bool build_smtp_message(Buffer& out, const Message& message, const char* date_header, const char* message_id) {
  out.append("From: ");
  out.append(kSenderName);
  out.append(" <");
  out.append(message.from);
  out.append(">\r\nTo: ");
  out.append(message.recipients);
  out.append("\r\nSubject: =?UTF-8?B?");
  append_base64(out, message.subject);
  out.append("?=\r\n");
  if (date_header != nullptr && date_header[0] != '\0') {
    out.append("Date: ");
    out.append(date_header);
    out.append("\r\n");
  }
  if (message_id != nullptr && message_id[0] != '\0') {
    out.append("Message-ID: <");
    out.append(message_id);
    out.append(">\r\n");
  }
  out.append(
      "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n"
      "Content-Transfer-Encoding: base64\r\nAuto-Submitted: auto-generated\r\n\r\n");
  // Convert LF to CRLF before encoding so the decoded text has canonical line ends.
  const char* body = message.body == nullptr ? "" : message.body;
  uint8_t chunk[57];
  size_t used = 0;
  size_t column = 0;
  auto flush = [&](bool final_chunk) {
    if (used == 0) return;
    if (!final_chunk && used < sizeof(chunk)) return;
    append_base64(out, chunk, used);
    column += base64_length(used);
    if (column >= 76) {
      out.append("\r\n");
      column = 0;
    }
    used = 0;
  };
  for (const char* cursor = body; *cursor != '\0'; ++cursor) {
    if (*cursor == '\n') {
      chunk[used++] = '\r';
      flush(false);
    }
    chunk[used++] = static_cast<uint8_t>(*cursor);
    flush(false);
  }
  flush(true);
  if (column != 0) out.append("\r\n");
  return out.append(".\r\n");
}

// ---------------------------------------------------------------------------
// Response parsing
// ---------------------------------------------------------------------------

// Parses the status code from an HTTP response status line. Returns 0 until
// the line is complete, -1 when the line is malformed.
class HttpStatusParser {
 public:
  int feed(const char* data, size_t length) {
    for (size_t index = 0; index < length && status_ == 0; ++index) {
      const char c = data[index];
      if (c == '\n') {
        line_[used_] = '\0';
        status_ = parse_();
        break;
      }
      if (c != '\r' && used_ + 1 < sizeof(line_)) line_[used_++] = c;
    }
    return status_;
  }
  int status() const { return status_; }

 private:
  int parse_() const {
    if (std::strncmp(line_, "HTTP/1.", 7) != 0 || used_ < 12 || line_[8] != ' ') return -1;
    int code = 0;
    for (size_t index = 9; index < 12; ++index) {
      if (line_[index] < '0' || line_[index] > '9') return -1;
      code = code * 10 + (line_[index] - '0');
    }
    return code >= 100 ? code : -1;
  }
  char line_[64]{};
  size_t used_{0};
  int status_{0};
};

// Incremental parser for one (possibly multi-line) SMTP reply. Also records
// whether an EHLO reply advertised AUTH PLAIN or AUTH LOGIN.
class SmtpReplyParser {
 public:
  enum class State : uint8_t { INCOMPLETE, COMPLETE, INVALID };

  void reset() {
    used_ = 0;
    code_ = 0;
    state_ = State::INCOMPLETE;
    auth_plain_ = false;
    auth_login_ = false;
  }

  State feed(const char* data, size_t length) {
    for (size_t index = 0; index < length && state_ == State::INCOMPLETE; ++index) {
      const char c = data[index];
      if (c == '\n') {
        line_[used_] = '\0';
        finish_line_();
        used_ = 0;
        continue;
      }
      if (c != '\r' && used_ + 1 < sizeof(line_)) line_[used_++] = c;
    }
    return state_;
  }

  State state() const { return state_; }
  int code() const { return code_; }
  bool auth_plain() const { return auth_plain_; }
  bool auth_login() const { return auth_login_; }

 private:
  void finish_line_() {
    if (used_ < 3) {
      state_ = State::INVALID;
      return;
    }
    int code = 0;
    for (size_t index = 0; index < 3; ++index) {
      if (line_[index] < '0' || line_[index] > '9') {
        state_ = State::INVALID;
        return;
      }
      code = code * 10 + (line_[index] - '0');
    }
    if (code_ != 0 && code != code_) {
      state_ = State::INVALID;
      return;
    }
    code_ = code;
    if (used_ > 4) inspect_capability_(line_ + 4);
    if (used_ == 3 || line_[3] == ' ') {
      state_ = State::COMPLETE;
    } else if (line_[3] != '-') {
      state_ = State::INVALID;
    }
  }

  void inspect_capability_(const char* text) {
    if (!(lower(text[0]) == 'a' && lower(text[1]) == 'u' && lower(text[2]) == 't' && lower(text[3]) == 'h' &&
          (text[4] == ' ' || text[4] == '='))) {
      return;
    }
    const char* cursor = text + 5;
    while (*cursor != '\0') {
      while (*cursor == ' ') ++cursor;
      const char* start = cursor;
      while (*cursor != '\0' && *cursor != ' ') ++cursor;
      const size_t length = static_cast<size_t>(cursor - start);
      if (token_equals_(start, length, "PLAIN")) auth_plain_ = true;
      if (token_equals_(start, length, "LOGIN")) auth_login_ = true;
    }
  }

  static bool token_equals_(const char* token, size_t length, const char* expected) {
    if (std::strlen(expected) != length) return false;
    for (size_t index = 0; index < length; ++index) {
      if (lower(token[index]) != lower(expected[index])) return false;
    }
    return true;
  }

  char line_[256]{};
  size_t used_{0};
  int code_{0};
  State state_{State::INCOMPLETE};
  bool auth_plain_{false};
  bool auth_login_{false};
};

inline bool smtp_positive(int code) { return code >= 200 && code < 400; }

// Retrying cannot fix rejected credentials, senders or payloads.
inline bool permanent_http_status(int status) {
  return status >= 400 && status < 500 && status != 408 && status != 429;
}
inline bool permanent_smtp_code(int code) { return code >= 500 && code < 600; }

}  // namespace oq_mail
