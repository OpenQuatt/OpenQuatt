#pragma once

#include <stdint.h>

namespace esphome::opentherm::transport_diagnostics {

static constexpr uint32_t SLOW_POLL_THRESHOLD_US = 50000;

enum class PollResult : uint8_t {
  NO_WORK = 0,
  TX_TIMEOUT,
  RX_TIMEOUT_NO_FRAME,
  RX_FRAME_AFTER_DEADLINE,
  RX_FRAME_ACCEPTED,
  RX_FRAME_REJECTED,
};

struct PollObservation {
  bool tx_timed_out{false};
  bool rx_timed_out{false};
  bool rx_frame_after_deadline{false};
  bool frame_processed{false};
  bool frame_accepted{false};
};

inline PollResult classify_poll(const PollObservation& observation) {
  if (observation.tx_timed_out) {
    return PollResult::TX_TIMEOUT;
  }
  if (observation.rx_frame_after_deadline) {
    return PollResult::RX_FRAME_AFTER_DEADLINE;
  }
  if (observation.rx_timed_out) {
    return PollResult::RX_TIMEOUT_NO_FRAME;
  }
  if (observation.frame_processed) {
    return observation.frame_accepted ? PollResult::RX_FRAME_ACCEPTED : PollResult::RX_FRAME_REJECTED;
  }
  return PollResult::NO_WORK;
}

inline const char* poll_result_to_str(PollResult result) {
  switch (result) {
    case PollResult::NO_WORK:
      return "no_work";
    case PollResult::TX_TIMEOUT:
      return "tx_timeout";
    case PollResult::RX_TIMEOUT_NO_FRAME:
      return "rx_timeout_no_frame";
    case PollResult::RX_FRAME_AFTER_DEADLINE:
      return "rx_frame_after_deadline";
    case PollResult::RX_FRAME_ACCEPTED:
      return "rx_frame_accepted";
    case PollResult::RX_FRAME_REJECTED:
      return "rx_frame_rejected";
  }
  return "unknown";
}

struct SlowPollStats {
  uint32_t count{0};
  uint32_t max_elapsed_us{0};
  uint32_t no_work{0};
  uint32_t tx_timeout{0};
  uint32_t rx_timeout_no_frame{0};
  uint32_t rx_frame_after_deadline{0};
  uint32_t rx_frame_accepted{0};
  uint32_t rx_frame_rejected{0};
  PollResult last_result{PollResult::NO_WORK};
};

inline bool is_slow_poll(uint32_t elapsed_us) { return elapsed_us >= SLOW_POLL_THRESHOLD_US; }

inline bool record_slow_poll(SlowPollStats& stats, PollResult result, uint32_t elapsed_us) {
  if (!is_slow_poll(elapsed_us)) {
    return false;
  }

  stats.count++;
  if (elapsed_us > stats.max_elapsed_us) {
    stats.max_elapsed_us = elapsed_us;
  }
  stats.last_result = result;
  switch (result) {
    case PollResult::NO_WORK:
      stats.no_work++;
      break;
    case PollResult::TX_TIMEOUT:
      stats.tx_timeout++;
      break;
    case PollResult::RX_TIMEOUT_NO_FRAME:
      stats.rx_timeout_no_frame++;
      break;
    case PollResult::RX_FRAME_AFTER_DEADLINE:
      stats.rx_frame_after_deadline++;
      break;
    case PollResult::RX_FRAME_ACCEPTED:
      stats.rx_frame_accepted++;
      break;
    case PollResult::RX_FRAME_REJECTED:
      stats.rx_frame_rejected++;
      break;
  }
  return true;
}

}  // namespace esphome::opentherm::transport_diagnostics
