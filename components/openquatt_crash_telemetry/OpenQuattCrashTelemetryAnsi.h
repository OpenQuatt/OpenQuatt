#pragma once

#include <cstdint>

namespace esphome::openquatt_crash_telemetry::detail {

class AnsiSequenceFilter {
 public:
  bool should_skip(uint8_t value) {
    if (this->state_ == State::TEXT) {
      if (value != 0x1BU) return false;
      this->state_ = State::ESCAPE;
      return true;
    }

    if (this->state_ == State::ESCAPE) {
      this->state_ = value == '[' ? State::CSI : State::TEXT;
      return true;
    }

    if (value >= '@' && value <= '~') this->state_ = State::TEXT;
    return true;
  }

 private:
  enum class State : uint8_t {
    TEXT,
    ESCAPE,
    CSI,
  };

  State state_{State::TEXT};
};

}  // namespace esphome::openquatt_crash_telemetry::detail
