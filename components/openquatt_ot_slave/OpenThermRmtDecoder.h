#pragma once

#include <stddef.h>
#include <stdint.h>

namespace openquatt_ot_slave::rmt_decoder {

static constexpr size_t FRAME_BITS = 34;

// Boundary transitions split the 900-1150 us interval between mandatory
// mid-bit transitions. Validate the complete interval rather than treating
// every individual RMT run as a complete OpenTherm bit.
static constexpr uint16_t BOUNDARY_INTERVAL_MIN_US = 380;
static constexpr uint16_t BOUNDARY_INTERVAL_MAX_US = 620;
static constexpr uint16_t MID_BIT_INTERVAL_MIN_US = 900;
static constexpr uint16_t MID_BIT_INTERVAL_MAX_US = 1150;

struct Pulse {
  uint16_t duration_us;
  bool level;  // GPIO level held during this interval, before its ending edge.
};

enum class InputPolarity : uint8_t {
  ACTIVE_LOW = 0,
  ACTIVE_HIGH,
};

enum class DecodeError : uint8_t {
  NONE = 0,
  GLITCH,
  TIMING,
  START_BIT,
  MANCHESTER,
  STOP_BIT,
  PARITY,
};

struct DecodeResult {
  uint32_t data{0};
  DecodeError error{DecodeError::NONE};
  uint8_t bit_position{0};
  size_t pulse_count{0};
  size_t failure_pulse_index{0};
  uint16_t failure_pulse_duration_us{0};
};

inline bool has_even_parity(uint32_t value) {
  value ^= value >> 16;
  value ^= value >> 8;
  value ^= value >> 4;
  value ^= value >> 2;
  value ^= value >> 1;
  return (~value) & 1U;
}

inline bool is_active(bool input_level, InputPolarity polarity) {
  return input_level == (polarity == InputPolarity::ACTIVE_HIGH);
}

class Decoder {
 public:
  explicit Decoder(InputPolarity polarity) : polarity_(polarity) {}

  bool consume(Pulse pulse) {
    const size_t pulse_index = result_.pulse_count++;
    if (pulse.duration_us == 0U) {
      return true;
    }
    if (result_.error != DecodeError::NONE) {
      return false;
    }
    if (frame_complete_) {
      return this->fail_(DecodeError::TIMING, pulse_index, pulse.duration_us);
    }

    const bool short_interval =
        pulse.duration_us >= BOUNDARY_INTERVAL_MIN_US && pulse.duration_us <= BOUNDARY_INTERVAL_MAX_US;
    const bool full_interval =
        pulse.duration_us >= MID_BIT_INTERVAL_MIN_US && pulse.duration_us <= MID_BIT_INTERVAL_MAX_US;
    if (!short_interval && !full_interval) {
      return this->fail_(pulse.duration_us < BOUNDARY_INTERVAL_MIN_US ? DecodeError::GLITCH : DecodeError::TIMING,
                         pulse_index, pulse.duration_us);
    }

    if (have_pulse_ && pulse.level == previous_level_) {
      return this->fail_(DecodeError::MANCHESTER, pulse_index, pulse.duration_us);
    }
    previous_level_ = pulse.level;

    bool is_mid_bit = false;
    if (!have_pulse_) {
      // RMT starts after the idle-to-active frame-start transition. The first
      // captured run is the active first half of the start bit and ends at its
      // mandatory mid-bit transition.
      have_pulse_ = true;
      if (!short_interval) {
        return this->fail_(DecodeError::TIMING, pulse_index, pulse.duration_us);
      }
      if (!is_active(pulse.level, polarity_)) {
        return this->fail_(DecodeError::START_BIT, pulse_index, pulse.duration_us);
      }
      is_mid_bit = true;
    } else if (boundary_seen_) {
      if (!short_interval) {
        return this->fail_(DecodeError::MANCHESTER, pulse_index, pulse.duration_us);
      }
      time_since_mid_bit_us_ = static_cast<uint16_t>(time_since_mid_bit_us_ + pulse.duration_us);
      if (time_since_mid_bit_us_ < MID_BIT_INTERVAL_MIN_US || time_since_mid_bit_us_ > MID_BIT_INTERVAL_MAX_US) {
        return this->fail_(DecodeError::TIMING, pulse_index, pulse.duration_us);
      }
      boundary_seen_ = false;
      is_mid_bit = true;
    } else if (short_interval) {
      boundary_seen_ = true;
      time_since_mid_bit_us_ = pulse.duration_us;
    } else {
      is_mid_bit = true;
    }

    if (!is_mid_bit) {
      return true;
    }

    time_since_mid_bit_us_ = 0;
    result_.bit_position = static_cast<uint8_t>(bit_count_);
    // At a mid-bit edge, active-to-idle is one and idle-to-active is zero.
    // The RMT pulse level is the level immediately before that edge.
    const bool bit_value = is_active(pulse.level, polarity_);
    if (bit_count_ == 0U) {
      if (!bit_value) {
        return this->fail_(DecodeError::START_BIT, pulse_index, pulse.duration_us);
      }
    } else if (bit_count_ == FRAME_BITS - 1U) {
      if (!bit_value) {
        return this->fail_(DecodeError::STOP_BIT, pulse_index, pulse.duration_us);
      }
      frame_complete_ = true;
    } else {
      result_.data = (result_.data << 1U) | static_cast<uint32_t>(bit_value);
    }
    bit_count_++;
    return true;
  }

  DecodeResult finish() {
    if (result_.error != DecodeError::NONE) {
      return result_;
    }
    if (!frame_complete_ || boundary_seen_ || bit_count_ != FRAME_BITS) {
      result_.error = DecodeError::TIMING;
      result_.failure_pulse_index = result_.pulse_count;
      return result_;
    }
    if (!has_even_parity(result_.data)) {
      result_.error = DecodeError::PARITY;
    }
    return result_;
  }

 private:
  bool fail_(DecodeError error, size_t pulse_index, uint16_t pulse_duration_us) {
    result_.error = error;
    result_.failure_pulse_index = pulse_index;
    result_.failure_pulse_duration_us = pulse_duration_us;
    return false;
  }

  const InputPolarity polarity_;
  DecodeResult result_{};
  bool have_pulse_{false};
  bool previous_level_{false};
  bool boundary_seen_{false};
  bool frame_complete_{false};
  uint16_t time_since_mid_bit_us_{0};
  size_t bit_count_{0};
};

inline DecodeResult decode(const Pulse* pulses, size_t pulse_count, InputPolarity polarity) {
  Decoder decoder(polarity);
  if (pulses == nullptr && pulse_count != 0U) {
    DecodeResult result{};
    result.error = DecodeError::TIMING;
    return result;
  }
  for (size_t pulse_index = 0; pulse_index < pulse_count; pulse_index++) {
    if (!decoder.consume(pulses[pulse_index])) {
      break;
    }
  }
  return decoder.finish();
}

}  // namespace openquatt_ot_slave::rmt_decoder
