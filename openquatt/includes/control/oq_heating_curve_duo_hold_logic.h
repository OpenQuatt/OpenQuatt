#pragma once

#include <cstdint>

namespace oq_curve {

// A true observation starts at zero. Subsequent observations credit only the
// monotonic time actually elapsed, including across the uint32_t clock wrap.
class ContinuousHold {
 public:
  uint32_t update(bool condition, uint32_t now_ms) {
    if (!condition) {
      this->reset();
      return 0;
    }
    if (!this->pending_) {
      this->pending_ = true;
      this->last_ms_ = now_ms;
      return 0;
    }
    const uint32_t delta_ms = now_ms - this->last_ms_;
    this->last_ms_ = now_ms;
    this->elapsed_ms_ = UINT32_MAX - this->elapsed_ms_ < delta_ms ? UINT32_MAX : this->elapsed_ms_ + delta_ms;
    return this->elapsed_ms_;
  }
  bool ready(uint32_t hold_ms) const { return this->pending_ && this->elapsed_ms_ >= hold_ms; }
  void reset() { *this = {}; }

 private:
  uint32_t last_ms_{0};
  uint32_t elapsed_ms_{0};
  bool pending_{false};
};

struct DuoHoldState {
  ContinuousHold enable;
  ContinuousHold disable;
  ContinuousHold emergency;
};

}  // namespace oq_curve
