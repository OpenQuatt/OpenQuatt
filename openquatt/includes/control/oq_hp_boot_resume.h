#pragma once

#include <cmath>
#include <cstdint>

namespace oq_boot_resume {
// These are controller deadlines, independent of the ODU's nominal 120 s
// watchdog: successful Modbus reads also feed that watchdog.
constexpr uint32_t SAFETY_ACQUISITION_MS = 45000U;
constexpr uint32_t INPUT_ACQUISITION_MS = 120000U;
constexpr uint32_t TELEMETRY_FRESH_MS = 15000U;
constexpr uint32_t COMMAND_FRESH_MS = 45000U;  // Existing slow polling: 30 s.
constexpr uint32_t PROTECTION_HOLD_MS = 180000U;

enum class Phase : uint8_t { DISABLED, ACQUIRING, RESUMED, STOP_REQUIRED };

struct Sample {
  float value{NAN};
  uint32_t at_ms{0};
  bool seen{false};
  void observe(float next, uint32_t now) {
    value = next;
    at_ms = now;
    seen = true;
  }
  bool fresh(uint32_t now, uint32_t age) const {
    return seen && std::isfinite(value) && static_cast<uint32_t>(now - at_ms) <= age;
  }
};

// One-shot, main-loop-owned acquisition. No persistence, allocation or task.
class Policy {
 public:
  void begin(bool eligible, uint32_t now) {
    phase_ = eligible ? Phase::ACQUIRING : Phase::DISABLED;
    started_ms_ = now;
  }
  void observe(uint16_t address, float value, uint32_t now) {
    Sample* sample = address == 3999   ? &requested_mode_
                     : address == 1999 ? &requested_level_
                     : address == 2099 ? &actual_mode_
                     : address == 2103 ? &frequency_
                                       : nullptr;
    if (sample == nullptr) return;
    sample->observe(value, now);
    if (phase_ != Phase::ACQUIRING) return;
    const bool valid =
        std::isfinite(value) && ((address == 3999 || address == 2099) ? value == 2.0f
                                 : address == 1999 ? value >= 1.0f && value <= 20.0f && std::floor(value) == value
                                                   : value > 0.5f);
    // A stopping/stopped ODU must never be restarted by acquiring more inputs.
    if (!valid) cancel();
  }
  void cancel() {
    if (phase_ == Phase::ACQUIRING) phase_ = Phase::STOP_REQUIRED;
  }
  bool pending(uint32_t now) const {
    return phase_ == Phase::ACQUIRING && static_cast<uint32_t>(now - started_ms_) < INPUT_ACQUISITION_MS;
  }
  bool stop_required(uint32_t now) const {
    return phase_ == Phase::STOP_REQUIRED || (phase_ == Phase::ACQUIRING && !pending(now));
  }
  bool readback_ready(uint32_t now) const {
    return requested_mode_.fresh(now, TELEMETRY_FRESH_MS) && requested_mode_.value == 2.0f &&
           requested_level_.fresh(now, COMMAND_FRESH_MS) && requested_level_.value >= 1.0f &&
           requested_level_.value <= 20.0f && actual_mode_.fresh(now, TELEMETRY_FRESH_MS) &&
           actual_mode_.value == 2.0f && frequency_.fresh(now, TELEMETRY_FRESH_MS) && frequency_.value > 0.5f;
  }
  int level() const {
    return requested_level_.seen && std::isfinite(requested_level_.value) && requested_level_.value >= 1.0f &&
                   requested_level_.value <= 20.0f
               ? static_cast<int>(requested_level_.value)
               : 0;
  }
  int update(uint32_t now, bool abort, bool safety_ready, bool inputs_ready, bool allow_resume) {
    if (!pending(now)) {
      cancel();
      return 0;
    }
    safety_ready = safety_ready && readback_ready(now);
    if (abort || (safety_seen_ && !safety_ready) ||
        (!safety_ready && static_cast<uint32_t>(now - started_ms_) >= SAFETY_ACQUISITION_MS)) {
      cancel();
      return 0;
    }
    safety_seen_ = safety_seen_ || safety_ready;
    if (!safety_ready || !inputs_ready || !allow_resume) return 0;
    phase_ = Phase::RESUMED;
    resumed_ms_ = now;
    return level();
  }
  bool protection_hold(uint32_t now) const {
    return phase_ == Phase::RESUMED && static_cast<uint32_t>(now - resumed_ms_) < PROTECTION_HOLD_MS;
  }

 private:
  Phase phase_{Phase::DISABLED};
  bool safety_seen_{false};
  uint32_t started_ms_{0}, resumed_ms_{0};
  Sample requested_mode_, requested_level_, actual_mode_, frequency_;
};
}  // namespace oq_boot_resume
