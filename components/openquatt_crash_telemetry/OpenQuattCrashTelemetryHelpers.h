#pragma once

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "esp_system.h"

namespace esphome::openquatt_crash_telemetry::detail {

class FixedWriter {
 public:
  FixedWriter(char* data, size_t capacity) : data_(data), capacity_(capacity) {
    if (this->data_ == nullptr || this->capacity_ == 0U) {
      this->ok_ = false;
    } else {
      this->data_[0] = '\0';
    }
  }

  void append(const char* value) {
    if (value != nullptr) this->append(value, std::strlen(value));
  }

  void append(const char* value, size_t length) {
    if (!this->ok_ || value == nullptr || length >= this->capacity_ - this->size_) {
      this->ok_ = false;
      return;
    }
    std::memcpy(this->data_ + this->size_, value, length);
    this->size_ += length;
    this->data_[this->size_] = '\0';
  }

  void append_char(char value) { this->append(&value, 1U); }

  void append_uint(uint64_t value) {
    char buffer[24];
    const int written = std::snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    if (written <= 0) {
      this->ok_ = false;
      return;
    }
    this->append(buffer, static_cast<size_t>(written));
  }

  void append_json_string(const char* value, size_t length) {
    this->append_char('"');
    for (size_t index = 0U; index < length && this->ok_; ++index) {
      const unsigned char c = static_cast<unsigned char>(value[index]);
      switch (c) {
        case '"':
          this->append("\\\"");
          break;
        case '\\':
          this->append("\\\\");
          break;
        case '\b':
          this->append("\\b");
          break;
        case '\f':
          this->append("\\f");
          break;
        case '\n':
          this->append("\\n");
          break;
        case '\r':
          this->append("\\r");
          break;
        case '\t':
          this->append("\\t");
          break;
        default:
          if (c < 0x20U) {
            char escaped[7];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            this->append(escaped);
          } else {
            this->append_char(static_cast<char>(c));
          }
          break;
      }
    }
    this->append_char('"');
  }

  void append_json_string(const char* value) {
    if (value == nullptr) {
      this->append("null");
      return;
    }
    this->append_json_string(value, std::strlen(value));
  }

  bool ok() const { return this->ok_; }
  size_t size() const { return this->size_; }

 private:
  char* data_{nullptr};
  size_t capacity_{0U};
  size_t size_{0U};
  bool ok_{true};
};

inline void append_json_key(FixedWriter& writer, const char* key) {
  writer.append(",\"");
  writer.append(key);
  writer.append("\":");
}

inline const char* reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power_on";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:
      return "task_watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep_sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    default:
      return "unknown";
  }
}

inline bool valid_installation_id(const char* value) { return value != nullptr && std::strlen(value) == 36U; }

}  // namespace esphome::openquatt_crash_telemetry::detail
