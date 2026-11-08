#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace esphome::openquatt_service_status {

// Bound serialization time and PSRAM use, including pathological status text.
static constexpr size_t MAX_SNAPSHOT_BYTES = 32768U;

// Serialization runs only on the main loop; overflow never publishes a partial frame.
class SnapshotBufferWriter {
 public:
  explicit SnapshotBufferWriter(char* destination, size_t capacity)
      : destination_(destination), capacity_(std::min(capacity, MAX_SNAPSHOT_BYTES)) {}

  bool write(const char* value) {
    if (this->failed_ || this->destination_ == nullptr) return false;
    const size_t count = std::strlen(value);
    if (count > this->capacity_ - this->used_) {
      this->failed_ = true;
      return false;
    }
    std::memcpy(this->destination_ + this->used_, value, count);
    this->used_ += count;
    return true;
  }

  size_t size() const { return this->used_; }

 private:
  char* destination_;
  size_t capacity_;
  size_t used_{0U};
  bool failed_{false};
};

}  // namespace esphome::openquatt_service_status
