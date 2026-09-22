#pragma once

#include <cstdint>

namespace esphome::openquatt_recovery {

// Main-loop owned. HTTP requests must be serialized onto that same owner.
// Input is already debounced by the GPIO binary sensor. This class owns no
// credentials, timers, HTTP handlers or storage; it only decides transitions.
class RecoveryState {
 public:
  static constexpr uint32_t WINDOW_MS = 600000;
  static constexpr uint32_t OPEN_HOLD_MS = 5000;
  static constexpr uint32_t WIFI_HOLD_MS = 10000;

  struct Events {
    bool opened{false};
    bool expired{false};
    bool wifi_reset_requested{false};
  };

  Events tick(uint32_t now, bool pressed, bool wifi_supported) {
    Events events;
    if (this->active_ && !this->busy_ && now - this->opened_at_ >= WINDOW_MS) {
      this->active_ = false;
      events.expired = true;
    }
    // A button held during boot must be released before it can trigger recovery.
    if (!pressed) {
      this->armed_ = true;
      this->held_ = false;
      this->opened_this_press_ = false;
      this->reset_this_press_ = false;
      return events;
    }
    if (!this->armed_) return events;
    if (!this->held_) {
      this->held_ = true;
      this->pressed_at_ = now;
    }
    const uint32_t held_ms = now - this->pressed_at_;
    if (!this->opened_this_press_ && held_ms >= OPEN_HOLD_MS) {
      this->opened_this_press_ = true;
      if (!this->busy_) {
        this->open(now);
        events.opened = true;
      }
    }
    if (!this->reset_this_press_ && held_ms >= WIFI_HOLD_MS) {
      this->reset_this_press_ = true;
      events.wifi_reset_requested = wifi_supported && this->active_ && !this->busy_;
    }
    return events;
  }

  // Called after a physical hold or a validated one-shot RTC handoff.
  bool open(uint32_t now) {
    if (this->busy_) return false;
    this->active_ = true;
    this->opened_at_ = now;
    ++this->generation_;
    return true;
  }

  bool end() {
    if (this->busy_) return false;
    this->active_ = false;
    return true;
  }

  void restore(uint32_t now, uint32_t previous_generation) {
    this->open(now);
    this->generation_ = previous_generation + 1;
  }

  bool active(uint32_t now) const { return this->active_ && (this->busy_ || now - this->opened_at_ < WINDOW_MS); }
  uint32_t remaining_ms(uint32_t now) const {
    const uint32_t elapsed = now - this->opened_at_;
    return this->active_ && elapsed < WINDOW_MS ? WINDOW_MS - elapsed : 0;
  }
  uint32_t generation() const { return this->generation_; }
  bool button_held() const { return this->held_; }
  uint32_t next_threshold_ms(uint32_t now, bool wifi_supported) const {
    if (!this->held_ || this->busy_) return 0;
    const uint32_t threshold =
        !this->opened_this_press_ ? OPEN_HOLD_MS : (wifi_supported && !this->reset_this_press_ ? WIFI_HOLD_MS : 0);
    const uint32_t elapsed = now - this->pressed_at_;
    return threshold > elapsed ? threshold - elapsed : 0;
  }

  // Reserve one job before any credential mutation. A committed job keeps the
  // recovery guard until reboot; only a failed job releases the reservation.
  bool begin_job(uint32_t now, uint32_t generation) {
    if (!this->active(now) || this->busy_ || generation != this->generation_) return false;
    this->busy_ = true;
    return true;
  }
  void job_failed() { this->busy_ = false; }
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
  bool opened_this_press_{false};
  bool reset_this_press_{false};
};

}  // namespace esphome::openquatt_recovery
