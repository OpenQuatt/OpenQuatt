#pragma once

#include <stddef.h>
#include <stdint.h>

namespace esphome::opentherm::rmt_decoder {

static constexpr size_t FRAME_BITS = 34;

// A boundary transition divides the 900-1150 us interval between two
// mandatory mid-bit transitions. Keep the proven short-pulse capture window,
// but validate the complete mid-bit interval separately below.
static constexpr uint16_t BOUNDARY_INTERVAL_MIN_US = 380;
static constexpr uint16_t BOUNDARY_INTERVAL_MAX_US = 620;
static constexpr uint16_t MID_BIT_INTERVAL_MIN_US = 900;
static constexpr uint16_t MID_BIT_INTERVAL_MAX_US = 1150;

inline bool completion_is_within_deadline(uint32_t completed_us, uint32_t deadline_us) {
  return static_cast<int32_t>(completed_us - deadline_us) <= 0;
}

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

enum class CaptureFailure : uint8_t {
  NONE = 0,
  PULSE_DURATION,
  HALF_BIT_OVERFLOW,
  HALF_BIT_COUNT,
};

struct DecodeResult {
  uint32_t data{0};
  DecodeError error{DecodeError::NONE};
  uint8_t bit_position{0};
  size_t pulse_count{0};
  size_t failure_pulse_index{0};
  uint16_t failure_pulse_duration_us{0};
  size_t half_bit_count{0};
  CaptureFailure capture_failure{CaptureFailure::NONE};
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

inline DecodeResult decode(const Pulse* pulses, size_t pulse_count, InputPolarity polarity) {
  DecodeResult result{};
  result.pulse_count = pulse_count;
  bool have_pulse = false;
  bool previous_level = false;
  bool boundary_seen = false;
  bool frame_complete = false;
  uint16_t time_since_mid_bit_us = 0;
  size_t bit_count = 0;

  if (pulses == nullptr && pulse_count != 0U) {
    result.error = DecodeError::TIMING;
    result.capture_failure = CaptureFailure::HALF_BIT_COUNT;
    return result;
  }

  for (size_t pulse_index = 0; pulse_index < pulse_count; pulse_index++) {
    const Pulse& pulse = pulses[pulse_index];
    if (pulse.duration_us == 0U) {
      continue;
    }

    if (frame_complete) {
      result.error = DecodeError::TIMING;
      result.failure_pulse_index = pulse_index;
      result.failure_pulse_duration_us = pulse.duration_us;
      result.capture_failure = CaptureFailure::HALF_BIT_OVERFLOW;
      return result;
    }

    const bool short_interval =
        pulse.duration_us >= BOUNDARY_INTERVAL_MIN_US && pulse.duration_us <= BOUNDARY_INTERVAL_MAX_US;
    const bool full_interval =
        pulse.duration_us >= MID_BIT_INTERVAL_MIN_US && pulse.duration_us <= MID_BIT_INTERVAL_MAX_US;
    if (!short_interval && !full_interval) {
      result.error = pulse.duration_us < BOUNDARY_INTERVAL_MIN_US ? DecodeError::GLITCH : DecodeError::TIMING;
      result.failure_pulse_index = pulse_index;
      result.failure_pulse_duration_us = pulse.duration_us;
      result.capture_failure = CaptureFailure::PULSE_DURATION;
      return result;
    }

    if (have_pulse && pulse.level == previous_level) {
      result.error = DecodeError::MANCHESTER;
      result.failure_pulse_index = pulse_index;
      result.failure_pulse_duration_us = pulse.duration_us;
      return result;
    }
    previous_level = pulse.level;

    bool is_mid_bit = false;
    if (!have_pulse) {
      // RMT starts after the idle-to-active frame-start transition. The first
      // captured run is therefore the active first half of the start bit and
      // must end at its mandatory mid-bit transition.
      have_pulse = true;
      if (!short_interval) {
        result.error = DecodeError::TIMING;
        result.failure_pulse_index = pulse_index;
        result.failure_pulse_duration_us = pulse.duration_us;
        result.capture_failure = CaptureFailure::PULSE_DURATION;
        return result;
      }
      if (!is_active(pulse.level, polarity)) {
        result.error = DecodeError::START_BIT;
        result.failure_pulse_index = pulse_index;
        result.failure_pulse_duration_us = pulse.duration_us;
        return result;
      }
      is_mid_bit = true;
    } else if (boundary_seen) {
      if (!short_interval) {
        result.error = DecodeError::MANCHESTER;
        result.failure_pulse_index = pulse_index;
        result.failure_pulse_duration_us = pulse.duration_us;
        return result;
      }
      time_since_mid_bit_us = static_cast<uint16_t>(time_since_mid_bit_us + pulse.duration_us);
      if (time_since_mid_bit_us < MID_BIT_INTERVAL_MIN_US || time_since_mid_bit_us > MID_BIT_INTERVAL_MAX_US) {
        result.error = DecodeError::TIMING;
        result.failure_pulse_index = pulse_index;
        result.failure_pulse_duration_us = pulse.duration_us;
        result.capture_failure = CaptureFailure::PULSE_DURATION;
        return result;
      }
      boundary_seen = false;
      is_mid_bit = true;
    } else if (short_interval) {
      boundary_seen = true;
      time_since_mid_bit_us = pulse.duration_us;
    } else {
      is_mid_bit = true;
    }

    const size_t elapsed_half_bits = short_interval ? 1U : 2U;
    result.half_bit_count += elapsed_half_bits;

    if (!is_mid_bit) {
      continue;
    }

    time_since_mid_bit_us = 0;
    result.bit_position = static_cast<uint8_t>(bit_count);
    // At a mid-bit edge, an active-to-idle transition is one and an
    // idle-to-active transition is zero. The pulse level is the pre-edge level.
    const bool bit_value = is_active(pulse.level, polarity);
    if (bit_count == 0U) {
      if (!bit_value) {
        result.error = DecodeError::START_BIT;
        return result;
      }
    } else if (bit_count == FRAME_BITS - 1U) {
      if (!bit_value) {
        result.error = DecodeError::STOP_BIT;
        return result;
      }
      frame_complete = true;
    } else {
      result.data = (result.data << 1U) | static_cast<uint32_t>(bit_value);
    }
    bit_count++;
  }

  if (!frame_complete || boundary_seen || bit_count != FRAME_BITS) {
    result.error = DecodeError::TIMING;
    result.failure_pulse_index = pulse_count;
    result.capture_failure = CaptureFailure::HALF_BIT_COUNT;
    return result;
  }

  if (!has_even_parity(result.data)) {
    result.error = DecodeError::PARITY;
    return result;
  }

  result.error = DecodeError::NONE;
  return result;
}

}  // namespace esphome::opentherm::rmt_decoder
