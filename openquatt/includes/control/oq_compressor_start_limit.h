#pragma once

#include <array>
#include <cstdint>

namespace oq_thermal_actuator {

// Command history, not measured ODU starts. Owned by the actuator's main-loop
// runtime; deliberately not persisted. Expire on every tick, including idle
// ticks, so old timestamps cannot reappear after a complete millis() wrap.
class CompressorStartLimit {
 public:
  static constexpr uint32_t WINDOW_MS = 60U * 60U * 1000U;
  static constexpr uint8_t MAX_STARTS = 6U;

  void expire(uint32_t now_ms) {
    while (count_ > 0 && static_cast<uint32_t>(now_ms - starts_[oldest_]) >= WINDOW_MS) {
      oldest_ = (oldest_ + 1U) % MAX_STARTS;
      --count_;
    }
  }

  uint32_t remaining_ms(uint32_t now_ms) {
    expire(now_ms);
    return count_ == MAX_STARTS ? WINDOW_MS - static_cast<uint32_t>(now_ms - starts_[oldest_]) : 0U;
  }

  // Call once for the final applied transition. Rejected requests, stop writes,
  // level changes and retries of an already active command consume no slots.
  void record_transition(int previous_level, int applied_level, uint32_t now_ms) {
    expire(now_ms);
    if (previous_level != 0 || applied_level <= 0) return;
    // Defensive saturation: an unexpected extra start must not open the gate.
    if (count_ == MAX_STARTS) {
      oldest_ = (oldest_ + 1U) % MAX_STARTS;
      --count_;
    }
    starts_[(oldest_ + count_) % MAX_STARTS] = now_ms;
    ++count_;
  }

 private:
  std::array<uint32_t, MAX_STARTS> starts_{};
  uint8_t oldest_{0};
  uint8_t count_{0};
};

}  // namespace oq_thermal_actuator
