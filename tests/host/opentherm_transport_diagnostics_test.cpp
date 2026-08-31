#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../../components/opentherm/opentherm_transport_diagnostics.h"

int main() {
  using esphome::opentherm::transport_diagnostics::classify_poll;
  using esphome::opentherm::transport_diagnostics::is_slow_poll;
  using esphome::opentherm::transport_diagnostics::poll_result_to_str;
  using esphome::opentherm::transport_diagnostics::PollObservation;
  using esphome::opentherm::transport_diagnostics::PollResult;
  using esphome::opentherm::transport_diagnostics::record_slow_poll;
  using esphome::opentherm::transport_diagnostics::SlowPollStats;

  assert(classify_poll({}) == PollResult::NO_WORK);

  PollObservation observation;
  observation.tx_timed_out = true;
  assert(classify_poll(observation) == PollResult::TX_TIMEOUT);

  observation = {};
  observation.rx_timed_out = true;
  assert(classify_poll(observation) == PollResult::RX_TIMEOUT_NO_FRAME);

  observation = {};
  observation.rx_frame_after_deadline = true;
  assert(classify_poll(observation) == PollResult::RX_FRAME_AFTER_DEADLINE);

  observation = {};
  observation.frame_processed = true;
  observation.frame_accepted = true;
  assert(classify_poll(observation) == PollResult::RX_FRAME_ACCEPTED);

  observation.frame_accepted = false;
  assert(classify_poll(observation) == PollResult::RX_FRAME_REJECTED);

  assert(!is_slow_poll(49999U));
  assert(is_slow_poll(50000U));

  SlowPollStats stats;
  assert(!record_slow_poll(stats, PollResult::NO_WORK, 49999U));
  assert(stats.count == 0U);

  assert(record_slow_poll(stats, PollResult::NO_WORK, 50000U));
  assert(record_slow_poll(stats, PollResult::TX_TIMEOUT, 60000U));
  assert(record_slow_poll(stats, PollResult::RX_TIMEOUT_NO_FRAME, 65000U));
  assert(record_slow_poll(stats, PollResult::RX_FRAME_AFTER_DEADLINE, 70000U));
  assert(record_slow_poll(stats, PollResult::RX_FRAME_ACCEPTED, 105000U));
  assert(record_slow_poll(stats, PollResult::RX_FRAME_REJECTED, 75000U));
  assert(stats.count == 6U);
  assert(stats.no_work == 1U);
  assert(stats.tx_timeout == 1U);
  assert(stats.rx_timeout_no_frame == 1U);
  assert(stats.rx_frame_after_deadline == 1U);
  assert(stats.rx_frame_accepted == 1U);
  assert(stats.rx_frame_rejected == 1U);
  assert(stats.max_elapsed_us == 105000U);
  assert(stats.last_result == PollResult::RX_FRAME_REJECTED);
  assert(strcmp(poll_result_to_str(stats.last_result), "rx_frame_rejected") == 0);

  return 0;
}
