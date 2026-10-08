#pragma once

#include "oq_warmup_logic.h"

namespace oq_warmup_runtime {

struct ControlTarget {
  float effective_c;
  bool limited;
};

class Runtime {
 public:
  // Callbacks invalidate the published result immediately, but state-machine
  // transitions and baseline establishment belong exclusively to update().
  void invalidate(oq_warmup::Status reason) {
    this->reset_pending_ = true;
    this->reset_reason_ = reason;
  }

  void update(const oq_warmup::Input& input, const oq_warmup::Settings& settings) {
    const bool was_active = this->state_.active;
    const uint32_t started_ms = this->state_.started_ms;
    if (this->reset_pending_) {
      this->state_ = oq_warmup::cancel(this->reset_reason_);
      this->reset_pending_ = false;
    }
    this->requested_c_ = input.requested_c;
    this->state_ = oq_warmup::evaluate(input, settings, this->state_);
    if (this->state_.active) {
      this->last_session_status_ = oq_warmup::Status::WARMING;
      this->last_duration_ms_ = 0;
    } else if (was_active) {
      this->last_session_status_ = this->state_.status;
      this->last_duration_ms_ = std::min(static_cast<uint32_t>(input.now_ms - started_ms), oq_warmup::MAX_DURATION_MS);
    }
  }

  // Read-only diagnostics: keep the last result until the next session or boot.
  oq_warmup::Status session_status() const {
    if (this->reset_pending_ && this->state_.active) return this->reset_reason_;
    return this->active() ? oq_warmup::Status::WARMING : this->last_session_status_;
  }
  uint32_t session_elapsed_s(uint32_t now_ms) const {
    const uint32_t duration_ms = this->state_.active ? std::min(static_cast<uint32_t>(now_ms - this->state_.started_ms),
                                                                oq_warmup::MAX_DURATION_MS)
                                                     : this->last_duration_ms_;
    return duration_ms / 1000U;
  }

  bool active() const { return !this->reset_pending_ && this->state_.active; }
  float effective_target() const { return this->effective_target(this->requested_c_); }
  // Clamp to the latest selected goal even between lifecycle ticks.
  float effective_target(float requested_c) const {
    return this->active() ? oq_warmup::effective_target(this->state_, requested_c) : requested_c;
  }
  // A consumer may run after a selected goal changes but before update().
  // Hold the previous goal for a qualifying raise; only update starts a session.
  ControlTarget control_target(float requested_c, float trigger_c, bool enabled) const {
    // A lower thermostat goal cancels the limiter for consumers immediately;
    // update() remains the sole owner of the session/baseline transition.
    const bool active = enabled && this->active() && std::isfinite(requested_c) &&
                        requested_c >= this->state_.last_requested_c - 0.0001f;
    const bool pending_raise = enabled && !this->reset_pending_ && this->state_.initialized && !this->state_.active &&
                               std::isfinite(requested_c) && std::isfinite(this->state_.last_requested_c) &&
                               std::isfinite(trigger_c) && trigger_c >= 0.5f && trigger_c <= 5.0f &&
                               requested_c - this->state_.last_requested_c > trigger_c + 0.0001f;
    return {pending_raise ? this->state_.last_requested_c
            : active      ? oq_warmup::effective_target(this->state_, requested_c)
                          : requested_c,
            active || pending_raise};
  }
  float offset() const { return this->active() ? this->state_.offset_c : 0.0f; }
  oq_warmup::Status status() const { return this->reset_pending_ ? this->reset_reason_ : this->state_.status; }

 private:
  oq_warmup::State state_;
  float requested_c_ = NAN;
  bool reset_pending_ = false;
  oq_warmup::Status reset_reason_ = oq_warmup::Status::IDLE;
  uint32_t last_duration_ms_ = 0;
  oq_warmup::Status last_session_status_ = oq_warmup::Status::IDLE;
};

static_assert(sizeof(Runtime) <= 56U, "Session diagnostics must remain small and allocation-free");

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_warmup_runtime
