#pragma once

#include <cstdint>

namespace oq_hp_restart_guard {

class Policy {
 public:
  bool configure(uint32_t minimum_off_ms, uint32_t freshness_ms) {
    minimum_off_ms_ = minimum_off_ms;
    freshness_ms_ = freshness_ms;
    configured_ = minimum_off_ms <= MAX_INTERVAL_MS && valid_interval_(freshness_ms);
    reset_();
    restore_open_ = configured_;
    return configured_;
  }

  bool configured() const { return configured_; }

  bool restore_credit(uint32_t credit_ms, bool valid) {
    if (!configured_ || !restore_open_) return false;
    pending_credit_ms_ = valid ? clamp_credit_(credit_ms) : 0;
    restore_open_ = false;
    return true;
  }

  void observe_stopped(uint32_t now_ms) {
    restore_open_ = false;
    if (!configured_) {
      reset_();
      return;
    }

    if (!stop_confirmed_) {
      credit_at_observation_ms_ = pending_credit_ms_;
      stop_confirmed_ = true;
    } else {
      const uint32_t elapsed_ms = static_cast<uint32_t>(now_ms - last_observation_ms_);
      if (elapsed_ms > freshness_ms_) {
        credit_at_observation_ms_ = 0;
      } else {
        credit_at_observation_ms_ = add_credit_(credit_at_observation_ms_, elapsed_ms);
      }
    }

    pending_credit_ms_ = 0;
    last_observation_ms_ = now_ms;
  }

  void invalidate() {
    restore_open_ = false;
    reset_();
  }

  bool can_start(uint32_t now_ms) const {
    return configured_ && fresh_(now_ms) && credit_at_(now_ms) >= minimum_off_ms_;
  }

  uint32_t remaining_ms(uint32_t now_ms) const {
    if (!configured_) return UINT32_MAX;
    if (!fresh_(now_ms)) return minimum_off_ms_;
    const uint32_t credit_ms = credit_at_(now_ms);
    return credit_ms >= minimum_off_ms_ ? 0 : minimum_off_ms_ - credit_ms;
  }

  uint32_t snapshot_credit_ms(uint32_t now_ms) const {
    if (!configured_ || !fresh_(now_ms)) return 0;
    return credit_at_observation_ms_;
  }

 private:
  static constexpr uint32_t MAX_INTERVAL_MS = 0x7FFFFFFFU;

  static bool valid_interval_(uint32_t interval_ms) { return interval_ms > 0 && interval_ms <= MAX_INTERVAL_MS; }

  void reset_() {
    stop_confirmed_ = false;
    pending_credit_ms_ = 0;
    credit_at_observation_ms_ = 0;
    last_observation_ms_ = 0;
  }

  uint32_t clamp_credit_(uint32_t credit_ms) const {
    return credit_ms >= minimum_off_ms_ ? minimum_off_ms_ : credit_ms;
  }

  uint32_t add_credit_(uint32_t credit_ms, uint32_t elapsed_ms) const {
    const uint32_t remaining_ms = minimum_off_ms_ - credit_ms;
    return elapsed_ms >= remaining_ms ? minimum_off_ms_ : credit_ms + elapsed_ms;
  }

  bool fresh_(uint32_t now_ms) const {
    return stop_confirmed_ && static_cast<uint32_t>(now_ms - last_observation_ms_) <= freshness_ms_;
  }

  uint32_t credit_at_(uint32_t now_ms) const {
    return add_credit_(credit_at_observation_ms_, static_cast<uint32_t>(now_ms - last_observation_ms_));
  }

  uint32_t minimum_off_ms_{0};
  uint32_t freshness_ms_{0};
  uint32_t pending_credit_ms_{0};
  uint32_t credit_at_observation_ms_{0};
  uint32_t last_observation_ms_{0};
  bool configured_{false};
  bool restore_open_{false};
  bool stop_confirmed_{false};
};

}  // namespace oq_hp_restart_guard
