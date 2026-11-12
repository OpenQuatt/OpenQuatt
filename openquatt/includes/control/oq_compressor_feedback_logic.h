#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace oq_compressor_feedback {
constexpr unsigned kLevels = 21;
constexpr uint32_t kSettlingMs = 30000, kMaximumSettlingMs = 180000, kStartupMs = 120000, kResponseLifetimeMs = 600000;

// Model inputs only. The command table, caps and actuator safety gates are unchanged.
struct Snapshot {
  std::array<uint8_t, kLevels> hz{};
  uint32_t confirmed_mask = 0;
  bool missing = false, settling = false, response_changed = false, qualified_run = false;
  uint8_t requested_hz = 0, actual_hz = 0;
  bool blocks_dispatch(bool must_stop) const { return this->missing && !must_stop; }
  int hold_level(int level, bool policy_allowed, bool current_power_safe) const {
    return this->settling && level > 0 && policy_allowed && current_power_safe ? level : 0;
  }
  int frequency(unsigned level, int nominal) const {
    if (level > 0 && requested_hz > 0 && nominal == requested_hz && actual_hz > 0) return actual_hz;
    return level > 0 && level < kLevels && hz[level] > 0 ? hz[level] : nominal;
  }
  int confirmed_frequency(unsigned level, int nominal) const {
    const bool measured = level > 0 && level < kLevels && (confirmed_mask & (1U << level)) != 0;
    int confirmed = measured && hz[level] > 0 ? hz[level] : nominal;
    if (requested_hz > 0 && actual_hz > 0 && nominal <= requested_hz && confirmed > actual_hz + 2)
      confirmed = std::min(confirmed, nominal);
    if (requested_hz > 0 && nominal == requested_hz && actual_hz > 0) confirmed = std::min(confirmed, int(actual_hz));
    // Only this command's measurement in the current continuous run can prove
    // extra capacity. Inferred points and previous runs remain planning inputs.
    return confirmed;
  }
};
struct Observation {
  uint32_t now_ms = 0;
  int applied_level = 0, working_mode = 0;
  float actual_hz = NAN, outside_c = NAN, outlet_c = NAN;
  bool fresh = false, defrost = false, may_learn = false;
  uint32_t received_ms = 0, applied_ms = 0;
  // Source-clock age avoids comparing esp_timer receipts with FreeRTOS millis.
  // The default keeps callers whose receipt and control clocks are identical.
  uint32_t receipt_age_ms = UINT32_MAX;
};
struct Response {
  uint32_t observed_ms = 0;
  uint8_t requested_hz = 0, actual_hz = 0;
  int8_t outside_c = 0, outlet_c = 0;
};

class Observer {
 public:
  Snapshot observe(const std::array<uint8_t, kLevels>& nominal, uint8_t variant, const Observation& in) {
    if (nominal != this->nominal_ || variant != this->variant_) {
      *this = {};
      this->nominal_ = nominal;
      this->variant_ = variant;
      this->identity_ms_ = in.now_ms;
    }
    // Expire entries during telemetry loss as well, before a clock wrap can
    // make an obsolete response appear recent again.
    for (unsigned level = 1; level < kLevels; ++level)
      if (static_cast<uint32_t>(in.now_ms - this->responses_[level].observed_ms) > kResponseLifetimeMs) {
        this->responses_[level] = {};
        this->confirmed_mask_ &= ~(1U << level);
      }
    Snapshot out;
    const bool running = in.applied_level > 0 && in.applied_level < static_cast<int>(kLevels);
    if (!running || !in.fresh || in.defrost || in.working_mode != 2) this->end_run_();
    if (!in.fresh || in.defrost) {
      this->tracking_ = false;
      out.missing = running && !in.defrost;
      return out;
    }
    if (!running) this->tracking_ = false;
    const bool context_valid = std::isfinite(in.outside_c) && in.outside_c >= -40 && in.outside_c <= 60 &&
                               std::isfinite(in.outlet_c) && in.outlet_c >= -20 && in.outlet_c <= 100;
    if (running) {
      const int requested = nominal[in.applied_level];
      if (in.working_mode != 2 || requested <= 0 || !std::isfinite(in.actual_hz) || in.actual_hz < 1 ||
          in.actual_hz > 90) {
        out.missing = true;
        this->end_run_();
        return out;
      }
      const int actual = static_cast<int>(std::lround(in.actual_hz));
      out.requested_hz = requested;
      out.actual_hz = actual;
      // Require a measurement taken after the command's response window, not
      // merely a recently received value from before that command.
      const uint32_t command_age = std::min(static_cast<uint32_t>(in.now_ms - this->identity_ms_),
                                            static_cast<uint32_t>(in.now_ms - in.applied_ms));
      const uint32_t receipt_age = in.receipt_age_ms == UINT32_MAX ? in.now_ms - in.received_ms : in.receipt_age_ms;
      out.missing = receipt_age >= command_age;
      // An unobserved stop may hide behind unchanged Heating mode and command.
      // Start a new local startup window after any loss of run continuity.
      if (this->continuous_run_ && static_cast<uint32_t>(in.received_ms - this->run_receipt_ms_) > kSettlingMs)
        this->end_run_();
      if (!this->continuous_run_) {
        this->continuous_run_ = true;
        this->run_since_ms_ = in.now_ms - receipt_age;
      }
      this->run_receipt_ms_ = in.received_ms;
      for (unsigned level = 1; level < kLevels; ++level)
        if (nominal[level] <= requested && this->responses_[level].actual_hz > actual + 2)
          this->confirmed_mask_ &= ~(1U << level);
      // V2.1's default startup stages are 45 and 55 Hz. They are unflagged
      // compressor plateaus, so elapsed time or a different qualified command
      // cannot prove that they are sustainable. Matching commands remain valid.
      const bool startup_plateau = (variant == 3 || variant == 4) && actual > requested + 2 &&
                                   (std::abs(actual - 45) <= 1 || std::abs(actual - 55) <= 1);
      // A stop can fall between receipts. A startup-shaped override withdraws
      // old capacity even when the previous normal run was qualified.
      if (startup_plateau) this->end_run_();
      const bool may_learn =
          in.may_learn && !startup_plateau && static_cast<uint32_t>(in.now_ms - this->run_since_ms_) >= kStartupMs;
      if (out.missing || !may_learn || !context_valid) this->tracking_ = false;
      if (!out.missing && may_learn && context_valid) {
        if (!this->tracking_ || this->level_ != in.applied_level || this->applied_ms_ != in.applied_ms) {
          this->tracking_ = true;
          this->level_ = in.applied_level;
          this->applied_ms_ = in.applied_ms;
          this->window_ms_ = this->last_receipt_ms_ = in.received_ms;
          this->minimum_hz_ = this->maximum_hz_ = actual;
        } else if (in.received_ms != this->last_receipt_ms_) {
          this->minimum_hz_ = std::min<int>(this->minimum_hz_, actual);
          this->maximum_hz_ = std::max<int>(this->maximum_hz_, actual);
          if (this->maximum_hz_ - this->minimum_hz_ > 2 ||
              static_cast<uint32_t>(in.received_ms - this->last_receipt_ms_) > kSettlingMs) {
            this->window_ms_ = in.received_ms;
            this->minimum_hz_ = this->maximum_hz_ = actual;
          }
          this->last_receipt_ms_ = in.received_ms;
        }
      }
      const bool stable = this->tracking_ && static_cast<uint32_t>(in.received_ms - this->window_ms_) >= kSettlingMs;
      // Keep modulation still while observing the response, but do not freeze
      // a continuously moving ODU indefinitely. Unsettled points are never saved.
      out.settling = out.missing || (!stable && command_age < kMaximumSettlingMs);
      if (stable && may_learn && context_valid) {
        this->qualified_run_ = true;
        this->confirmed_mask_ |= 1U << in.applied_level;
        auto& response = this->responses_[in.applied_level];
        out.response_changed =
            response.actual_hz != actual && (std::abs(actual - requested) > 2 ||
                                             (response.actual_hz > 0 && std::abs(response.actual_hz - requested) > 2));
        response = {in.now_ms - receipt_age, static_cast<uint8_t>(requested), static_cast<uint8_t>(actual),
                    static_cast<int8_t>(std::lround(in.outside_c)), static_cast<int8_t>(std::lround(in.outlet_c))};
      }
    }
    out.qualified_run = this->qualified_run_;
    const auto usable = [&](const Response& response) {
      return context_valid && response.actual_hz > 0 &&
             static_cast<uint32_t>(in.now_ms - response.observed_ms) <= kResponseLifetimeMs &&
             std::fabs(in.outside_c - response.outside_c) <= 2 && std::fabs(in.outlet_c - response.outlet_c) <= 5;
    };
    for (unsigned level = 1; level < kLevels; ++level) {
      const auto& response = this->responses_[level];
      if (response.requested_hz == nominal[level] && usable(response)) {
        out.hz[level] = response.actual_hz;
        out.confirmed_mask |= this->confirmed_mask_ & (1U << level);
      }
    }
    // A smaller, unobserved command is no evidence that an ODU override will end.
    // Keep independently observed responses (including a working 30 Hz point).
    for (unsigned level = 1; level < kLevels; ++level) {
      if (nominal[level] == 0 || out.hz[level] > 0) continue;
      for (const auto& response : this->responses_)
        if (usable(response) && response.actual_hz > response.requested_hz + 2 &&
            nominal[level] <= response.requested_hz)
          out.hz[level] = std::max(out.hz[level], response.actual_hz);
    }
    // Live frequency is overlaid by frequency(); hz includes planning estimates.
    return out;
  }

 private:
  void end_run_() {
    this->tracking_ = this->qualified_run_ = this->continuous_run_ = false;
    this->confirmed_mask_ = 0;
  }
  std::array<Response, kLevels> responses_{};
  std::array<uint8_t, kLevels> nominal_{};
  uint8_t variant_ = 0;
  uint32_t identity_ms_ = 0;
  uint32_t window_ms_ = 0, last_receipt_ms_ = 0, applied_ms_ = 0;
  uint32_t run_since_ms_ = 0, run_receipt_ms_ = 0, confirmed_mask_ = 0;
  uint8_t level_ = 0, minimum_hz_ = 0, maximum_hz_ = 0;
  bool tracking_ = false, qualified_run_ = false, continuous_run_ = false;
};
static_assert(sizeof(Observer) <= 232, "Keep the per-HP response history bounded and allocation-free");
}  // namespace oq_compressor_feedback
