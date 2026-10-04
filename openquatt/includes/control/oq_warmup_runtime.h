#pragma once

#include "oq_warmup_logic.h"

namespace oq_warmup_runtime {

class Runtime {
 public:
  // Callbacks invalidate the published result immediately, but state-machine
  // transitions and baseline establishment belong exclusively to update().
  void invalidate(oq_warmup::Status reason) {
    this->reset_pending_ = true;
    this->reset_reason_ = reason;
  }

  void update(const oq_warmup::Input& input, const oq_warmup::Settings& settings) {
    if (this->reset_pending_) {
      this->state_ = oq_warmup::cancel(this->reset_reason_);
      this->reset_pending_ = false;
    }
    this->requested_c_ = input.requested_c;
    this->state_ = oq_warmup::evaluate(input, settings, this->state_);
  }

  bool active() const { return !this->reset_pending_ && this->state_.active; }
  float effective_target() const { return this->effective_target(this->requested_c_); }
  // Clamp to the latest selected goal even between lifecycle ticks.
  float effective_target(float requested_c) const {
    return this->active() ? oq_warmup::effective_target(this->state_, requested_c) : requested_c;
  }
  float offset() const { return this->active() ? this->state_.offset_c : 0.0f; }
  const char* status() const {
    return oq_warmup::status_name(this->reset_pending_ ? this->reset_reason_ : this->state_.status);
  }

 private:
  oq_warmup::State state_;
  float requested_c_ = NAN;
  bool reset_pending_ = false;
  oq_warmup::Status reset_reason_ = oq_warmup::Status::IDLE;
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_warmup_runtime
