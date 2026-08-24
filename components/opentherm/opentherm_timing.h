#pragma once

#include <stdint.h>

namespace esphome::opentherm::timing {

inline uint32_t elapsed_us(uint32_t now_us, uint32_t started_us) { return now_us - started_us; }

inline bool delay_elapsed(uint32_t now_us, uint32_t started_us, uint32_t delay_us) {
  return elapsed_us(now_us, started_us) >= delay_us;
}

inline uint32_t conversation_end_us(bool response_captured, uint32_t response_captured_us, bool receive_timed_out,
                                    uint32_t response_deadline_us, uint32_t processed_us) {
  if (response_captured) {
    return response_captured_us;
  }
  if (receive_timed_out) {
    return response_deadline_us;
  }
  return processed_us;
}

}  // namespace esphome::opentherm::timing
