#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "../../components/opentherm/opentherm_rmt_decoder.h"

namespace {

using esphome::opentherm::rmt_decoder::CaptureFailure;
using esphome::opentherm::rmt_decoder::DecodeError;
using esphome::opentherm::rmt_decoder::InputPolarity;
using esphome::opentherm::rmt_decoder::Pulse;

uint32_t with_even_parity(uint32_t value_without_parity) {
  value_without_parity &= 0x7FFFFFFFU;
  if (!esphome::opentherm::rmt_decoder::has_even_parity(value_without_parity)) {
    value_without_parity |= 0x80000000U;
  }
  return value_without_parity;
}

bool input_level(bool active, InputPolarity polarity) {
  return polarity == InputPolarity::ACTIVE_HIGH ? active : !active;
}

struct EncodedFrame {
  Pulse pulses[72]{};
  size_t size{0};

  void erase(size_t index) {
    assert(index < size);
    for (size_t next = index + 1U; next < size; next++) {
      pulses[next - 1U] = pulses[next];
    }
    size--;
  }

  size_t find_duration(uint16_t duration_us, size_t begin = 0U) const {
    for (size_t index = begin; index < size; index++) {
      if (pulses[index].duration_us == duration_us) {
        return index;
      }
    }
    assert(false);
    return size;
  }

  size_t find_short_pair() const {
    for (size_t index = 1U; index + 1U < size; index++) {
      if (pulses[index].duration_us == 500U && pulses[index + 1U].duration_us == 500U) {
        return index;
      }
    }
    assert(false);
    return size;
  }
};

// Build the protocol waveform directly from Bi-phase-L: one is active-to-idle
// and zero is idle-to-active at the mandatory mid-bit transition. Compress the
// resulting levels into the runs that RMT returns after the frame-start edge.
EncodedFrame encode(uint32_t data, InputPolarity polarity = InputPolarity::ACTIVE_HIGH, bool stop_bit = true,
                    uint16_t short_duration_us = 500U, uint16_t full_duration_us = 1000U, int jitter_us = 0) {
  bool half_bits[68]{};
  size_t half_bit_count = 0;
  const auto append_bit = [&](bool value) {
    half_bits[half_bit_count++] = value;
    half_bits[half_bit_count++] = !value;
  };

  append_bit(true);
  for (int bit = 31; bit >= 0; bit--) {
    append_bit(((data >> bit) & 1U) != 0U);
  }
  append_bit(stop_bit);
  assert(half_bit_count == 68U);

  EncodedFrame encoded;
  bool run_level = half_bits[0];
  size_t run_half_bits = 1U;
  for (size_t index = 1U; index < half_bit_count; index++) {
    if (half_bits[index] == run_level) {
      run_half_bits++;
      continue;
    }

    assert(run_half_bits == 1U || run_half_bits == 2U);
    const int base_duration = run_half_bits == 1U ? short_duration_us : full_duration_us;
    const int signed_jitter = encoded.size % 2U == 0U ? jitter_us : -jitter_us;
    encoded.pulses[encoded.size++] =
        Pulse{static_cast<uint16_t>(base_duration + signed_jitter), input_level(run_level, polarity)};
    run_level = half_bits[index];
    run_half_bits = 1U;
  }

  // The final stop-bit half is idle and has no terminating edge. RMT ends the
  // capture on its idle threshold, so it is intentionally not a decoder pulse.
  assert(!run_level || !stop_bit);
  return encoded;
}

}  // namespace

int main() {
  using esphome::opentherm::rmt_decoder::decode;

  assert(esphome::opentherm::rmt_decoder::completion_is_within_deadline(1000U, 1000U));
  assert(esphome::opentherm::rmt_decoder::completion_is_within_deadline(999U, 1000U));
  assert(!esphome::opentherm::rmt_decoder::completion_is_within_deadline(1001U, 1000U));
  assert(esphome::opentherm::rmt_decoder::completion_is_within_deadline(UINT32_MAX - 10U, 5U));
  assert(!esphome::opentherm::rmt_decoder::completion_is_within_deadline(20U, 5U));

  const uint32_t data = with_even_parity(0x40001234U);
  const auto nominal = encode(data);
  auto decoded = decode(nominal.pulses, nominal.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::NONE);
  assert(decoded.data == data);
  assert(decoded.half_bit_count == 67U);

  const auto active_low = encode(data, InputPolarity::ACTIVE_LOW);
  decoded = decode(active_low.pulses, active_low.size, InputPolarity::ACTIVE_LOW);
  assert(decoded.error == DecodeError::NONE);
  assert(decoded.data == data);
  decoded = decode(nominal.pulses, nominal.size, InputPolarity::ACTIVE_LOW);
  assert(decoded.error == DecodeError::START_BIT);

  const auto all_ones = encode(0xFFFFFFFFU);
  decoded = decode(all_ones.pulses, all_ones.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::NONE);
  assert(decoded.data == 0xFFFFFFFFU);
  assert(all_ones.size == 67U);
  for (size_t index = 0; index < all_ones.size; index++) {
    assert(all_ones.pulses[index].duration_us == 500U);
  }

  const auto alternating = encode(0xAAAAAAAAU);
  decoded = decode(alternating.pulses, alternating.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::NONE);
  assert(decoded.data == 0xAAAAAAAAU);
  assert(alternating.find_duration(1000U) < alternating.size);

  uint32_t random_data = 0x12345678U;
  for (size_t iteration = 0; iteration < 4096U; iteration++) {
    random_data = random_data * 1664525U + 1013904223U;
    const uint32_t frame_data = with_even_parity(random_data);
    const auto frame = encode(frame_data);
    decoded = decode(frame.pulses, frame.size, InputPolarity::ACTIVE_HIGH);
    assert(decoded.error == DecodeError::NONE);
    assert(decoded.data == frame_data);
  }

  const auto jittered = encode(data, InputPolarity::ACTIVE_HIGH, true, 500U, 1000U, 19);
  decoded = decode(jittered.pulses, jittered.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::NONE);
  assert(decoded.data == data);

  const auto lower_timing = encode(data, InputPolarity::ACTIVE_HIGH, true, 450U, 900U);
  decoded = decode(lower_timing.pulses, lower_timing.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::NONE);
  const auto upper_timing = encode(data, InputPolarity::ACTIVE_HIGH, true, 575U, 1150U);
  decoded = decode(upper_timing.pulses, upper_timing.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::NONE);

  auto asymmetric_boundary = nominal;
  const size_t short_pair = asymmetric_boundary.find_short_pair();
  asymmetric_boundary.pulses[short_pair].duration_us = 380U;
  asymmetric_boundary.pulses[short_pair + 1U].duration_us = 620U;
  decoded = decode(asymmetric_boundary.pulses, asymmetric_boundary.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::NONE);
  assert(decoded.data == data);

  decoded = decode(nullptr, 0U, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::TIMING);
  assert(decoded.capture_failure == CaptureFailure::HALF_BIT_COUNT);

  auto missing_start = all_ones;
  missing_start.erase(0U);
  decoded = decode(missing_start.pulses, missing_start.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::START_BIT);

  auto missing_end = nominal;
  missing_end.erase(missing_end.size - 1U);
  decoded = decode(missing_end.pulses, missing_end.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::TIMING);
  assert(decoded.capture_failure == CaptureFailure::HALF_BIT_COUNT);

  auto wrong_start_level = nominal;
  wrong_start_level.pulses[0].level = input_level(false, InputPolarity::ACTIVE_HIGH);
  decoded = decode(wrong_start_level.pulses, wrong_start_level.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::START_BIT);

  auto wrong_start_timing = nominal;
  wrong_start_timing.pulses[0].duration_us = 1000U;
  decoded = decode(wrong_start_timing.pulses, wrong_start_timing.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::TIMING);
  assert(decoded.capture_failure == CaptureFailure::PULSE_DURATION);

  const auto invalid_stop = encode(data, InputPolarity::ACTIVE_HIGH, false);
  decoded = decode(invalid_stop.pulses, invalid_stop.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::STOP_BIT);

  const auto invalid_parity = encode(data ^ 0x1U);
  decoded = decode(invalid_parity.pulses, invalid_parity.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::PARITY);

  auto glitch = nominal;
  glitch.pulses[1].duration_us = 200U;
  decoded = decode(glitch.pulses, glitch.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::GLITCH);
  assert(decoded.capture_failure == CaptureFailure::PULSE_DURATION);
  assert(decoded.failure_pulse_index == 1U);

  auto below_short_window = nominal;
  below_short_window.pulses[below_short_window.find_duration(500U)].duration_us = 379U;
  decoded = decode(below_short_window.pulses, below_short_window.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::GLITCH);

  auto above_short_window = nominal;
  above_short_window.pulses[above_short_window.find_duration(500U)].duration_us = 621U;
  decoded = decode(above_short_window.pulses, above_short_window.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::TIMING);

  auto invalid_timing = nominal;
  invalid_timing.pulses[invalid_timing.find_duration(1000U)].duration_us = 899U;
  decoded = decode(invalid_timing.pulses, invalid_timing.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::TIMING);
  assert(decoded.capture_failure == CaptureFailure::PULSE_DURATION);

  invalid_timing = nominal;
  invalid_timing.pulses[invalid_timing.find_duration(1000U)].duration_us = 1151U;
  decoded = decode(invalid_timing.pulses, invalid_timing.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::TIMING);

  auto invalid_mid_bit_period = nominal;
  const size_t invalid_pair = invalid_mid_bit_period.find_short_pair();
  invalid_mid_bit_period.pulses[invalid_pair].duration_us = 380U;
  invalid_mid_bit_period.pulses[invalid_pair + 1U].duration_us = 380U;
  decoded = decode(invalid_mid_bit_period.pulses, invalid_mid_bit_period.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::TIMING);

  auto invalid_manchester = nominal;
  invalid_manchester.pulses[1].level = invalid_manchester.pulses[0].level;
  decoded = decode(invalid_manchester.pulses, invalid_manchester.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::MANCHESTER);

  auto missing_mid_bit = nominal;
  missing_mid_bit.erase(missing_mid_bit.find_short_pair() + 1U);
  decoded = decode(missing_mid_bit.pulses, missing_mid_bit.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::MANCHESTER);

  auto missing_final_mid_bit = all_ones;
  missing_final_mid_bit.pulses[missing_final_mid_bit.size - 1U].duration_us = 1000U;
  decoded = decode(missing_final_mid_bit.pulses, missing_final_mid_bit.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::MANCHESTER);

  auto trailing_pulse = nominal;
  trailing_pulse.pulses[trailing_pulse.size++] = Pulse{500U, false};
  decoded = decode(trailing_pulse.pulses, trailing_pulse.size, InputPolarity::ACTIVE_HIGH);
  assert(decoded.error == DecodeError::TIMING);
  assert(decoded.capture_failure == CaptureFailure::HALF_BIT_OVERFLOW);

  return 0;
}
