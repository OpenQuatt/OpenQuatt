#pragma once

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace openquatt_debug_recorder {

// Caller owns bounded storage (strict PSRAM in the recorder). Never allocates
// or publishes a partial value; a failed append poisons the whole snapshot.
class ConfigurationJsonWriter {
 public:
  ConfigurationJsonWriter(char* buffer, size_t capacity) : buffer_(buffer), capacity_(capacity) {}

  bool write_literal(const char* value) { return this->write_bytes_(value, std::strlen(value)); }
  bool write_char(char value) { return this->write_bytes_(&value, 1); }
  bool write_bool(bool value) { return this->write_literal(value ? "true" : "false"); }

  bool write_float(float value) {
    if (!std::isfinite(value)) return this->write_literal("null");
    char buffer[24];
    const int length = std::snprintf(buffer, sizeof(buffer), "%.7g", static_cast<double>(value));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(buffer)) return this->fail_();
    return this->write_bytes_(buffer, static_cast<size_t>(length));
  }

  bool write_string(const char* value, size_t length) {
    if (value == nullptr) return this->fail_();
    if (!this->write_char('"')) return false;
    for (size_t index = 0; index < length; ++index) {
      const unsigned char c = static_cast<unsigned char>(value[index]);
      if (c == '"' || c == '\\') {
        if (!this->write_char('\\') || !this->write_char(static_cast<char>(c))) return false;
      } else if (c < 0x20) {
        char escaped[7];
        const int written = std::snprintf(escaped, sizeof(escaped), "\\u%04X", static_cast<unsigned>(c));
        if (written != 6 || !this->write_bytes_(escaped, 6)) return this->fail_();
      } else if (!this->write_char(static_cast<char>(c))) {
        return false;
      }
    }
    return this->write_char('"');
  }

  bool ok() const { return this->ok_; }
  size_t size() const { return this->ok_ ? this->used_ : 0; }

 private:
  bool fail_() {
    this->ok_ = false;
    return false;
  }

  bool write_bytes_(const char* value, size_t length) {
    if (!this->ok_ || this->buffer_ == nullptr || this->used_ > this->capacity_ ||
        length > this->capacity_ - this->used_) {
      return this->fail_();
    }
    std::memcpy(this->buffer_ + this->used_, value, length);
    this->used_ += length;
    return true;
  }

  char* buffer_;
  size_t capacity_;
  size_t used_{0};
  bool ok_{true};
};

}  // namespace openquatt_debug_recorder
}  // namespace esphome
