#pragma once

#include <stdint.h>

namespace esphome {
namespace openquatt_decision_log {

class UrgentFlushPolicy {
 public:
  void request(uint64_t now_us, uint32_t event_seq) {
    if (!this->pending_) {
      this->requested_us_ = now_us;
      this->requested_event_seq_ = event_seq;
    } else if (sequence_newer_(event_seq, this->requested_event_seq_)) {
      this->requested_event_seq_ = event_seq;
    }
    this->pending_ = true;
  }

  bool should_attempt(uint64_t now_us, uint64_t coalesce_us, uint64_t min_interval_us) const {
    const bool continuation_due = this->continuation_pending_ && now_us >= this->retry_after_us_;
    return this->pending_ && now_us >= this->requested_us_ && now_us - this->requested_us_ >= coalesce_us &&
           (continuation_due || !this->attempted_ ||
            (now_us >= this->last_attempt_us_ && now_us - this->last_attempt_us_ >= min_interval_us)) &&
           (this->retry_after_us_ == 0U || now_us >= this->retry_after_us_);
  }

  void mark_attempt(uint64_t now_us) {
    this->attempted_ = true;
    this->last_attempt_us_ = now_us;
  }

  void mark_target_persisted(uint64_t now_us, uint32_t persisted_event_seq) {
    this->attempted_ = true;
    this->last_attempt_us_ = now_us;
    this->retry_after_us_ = 0U;
    this->continuation_pending_ = false;
    if (this->pending_ && persisted_event_seq == this->requested_event_seq_) {
      this->pending_ = false;
      this->requested_us_ = 0U;
      this->requested_event_seq_ = 0U;
    } else if (this->pending_) {
      // An urgent event arrived while the previous batch was being written.
      // Give that newer edge its own coalescing window.
      this->requested_us_ = now_us;
    }
  }

  void mark_failure(uint64_t now_us, uint64_t retry_delay_us) {
    this->attempted_ = true;
    this->last_attempt_us_ = now_us;
    this->retry_after_us_ = now_us + retry_delay_us;
    this->continuation_pending_ = false;
  }

  // A bounded flush made progress but has not reached its exact urgent target.
  // Permit the next ESPHome loop to continue, without allowing multiple flash
  // operations in the current loop or treating progress as a failed write.
  void schedule_continuation(uint64_t now_us) {
    this->retry_after_us_ = now_us;
    this->continuation_pending_ = true;
  }

  void clear(uint64_t now_us) {
    this->pending_ = false;
    this->requested_us_ = 0U;
    this->requested_event_seq_ = 0U;
    this->retry_after_us_ = 0U;
    this->continuation_pending_ = false;
    this->attempted_ = true;
    this->last_attempt_us_ = now_us;
  }

  bool pending() const { return this->pending_; }
  uint32_t requested_event_seq() const { return this->requested_event_seq_; }
  bool protects_unpersisted_sequence(uint32_t sequence, uint32_t persisted_sequence) const {
    return this->pending_ && sequence_newer_(sequence, persisted_sequence) &&
           sequence_at_least_(this->requested_event_seq_, sequence);
  }

 private:
  static bool sequence_newer_(uint32_t value, uint32_t baseline) { return static_cast<int32_t>(value - baseline) > 0; }

  static bool sequence_at_least_(uint32_t value, uint32_t baseline) {
    return static_cast<int32_t>(value - baseline) >= 0;
  }

  bool pending_{false};
  bool attempted_{false};
  uint64_t requested_us_{0U};
  uint64_t last_attempt_us_{0U};
  uint64_t retry_after_us_{0U};
  uint32_t requested_event_seq_{0U};
  bool continuation_pending_{false};
};

}  // namespace openquatt_decision_log
}  // namespace esphome
