#pragma once
#include <cstdint>

namespace esphome::openquatt_recovery {

// A physical hold selects an action on release. The recovery capability itself
// is restored only by setup() after a checked software-reset handoff.
class RecoveryState {
 public:
  static constexpr uint32_t WINDOW_MS = 600000;
  static constexpr uint32_t OPEN_HOLD_MS = 5000;
  static constexpr uint32_t WIFI_HOLD_MS = 10000;
  struct Events {
    bool opened{false};  // Request a recovery boot, never activate this process.
    bool expired{false};
    bool wifi_reset_requested{false};
  };
  Events tick(uint32_t now, bool pressed, bool wifi_supported) {
    Events events;
    if (this->active_ && !this->busy_ && !this->storage_failed_ && now - this->opened_at_ >= WINDOW_MS) {
      events.expired = true;
    }
    if (!pressed) {
      if (this->held_ && !this->busy_) {
        const uint32_t duration = now - this->pressed_at_;
        events.wifi_reset_requested = wifi_supported && duration >= WIFI_HOLD_MS;
        events.opened = duration >= OPEN_HOLD_MS && !events.wifi_reset_requested;
      }
      this->armed_ = true;
      this->held_ = false;
    } else if (this->armed_ && !this->held_) {
      this->held_ = true;
      this->pressed_at_ = now;
    }
    return events;
  }
  void restore(uint32_t now, uint32_t previous_generation) {
    this->active_ = true;
    this->opened_at_ = now;
    this->generation_ = previous_generation + 1;
  }
  bool active(uint32_t now) const {
    return this->active_ && (this->busy_ || this->storage_failed_ || now - this->opened_at_ < WINDOW_MS);
  }
  uint32_t remaining_ms(uint32_t now) const {
    const uint32_t elapsed = now - this->opened_at_;
    return this->active_ && elapsed < WINDOW_MS ? WINDOW_MS - elapsed : 0;
  }
  uint32_t generation() const { return this->generation_; }
  bool button_held() const { return this->held_; }
  uint32_t next_threshold_ms(uint32_t now, bool wifi_supported) const {
    if (!this->held_ || this->busy_) return 0;
    const uint32_t elapsed = now - this->pressed_at_;
    const uint32_t threshold = elapsed < OPEN_HOLD_MS ? OPEN_HOLD_MS : (wifi_supported ? WIFI_HOLD_MS : 0);
    return threshold > elapsed ? threshold - elapsed : 0;
  }
  bool begin_job(uint32_t now, uint32_t generation) {
    if (!this->active(now) || this->busy_ || generation != this->generation_) return false;
    this->busy_ = true;
    return true;
  }
  // A failed write may already have changed flash. Stay restricted until the
  // user explicitly chooses an action; expiry must not reboot into that state.
  void job_failed() {
    this->busy_ = false;
    this->storage_failed_ = true;
  }
  bool begin_admin_job() {
    if (this->busy_) return false;
    this->busy_ = true;
    return true;
  }
  bool busy() const { return this->busy_; }

 private:
  uint32_t generation_{0};
  uint32_t opened_at_{0};
  uint32_t pressed_at_{0};
  bool active_{false};
  bool busy_{false};
  bool armed_{false};
  bool held_{false};
  bool storage_failed_{false};
};
}  // namespace esphome::openquatt_recovery
