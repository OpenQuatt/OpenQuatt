#include <assert.h>
#include <stdint.h>

#include "../../openquatt/includes/control/oq_compressor_start_limit.h"
#include "../../openquatt/includes/control/oq_thermal_actuator_logic.h"
#include "../../openquatt/includes/odu/oq_odu_compressor_levels.h"

int main() {
  using oq_thermal_actuator::CompressorStartLimit;
  using oq_thermal_actuator::decide_preflight;
  using oq_thermal_actuator::PreflightBlock;
  constexpr uint32_t minute = 60000U;
  constexpr uint32_t hour = CompressorStartLimit::WINDOW_MS;
  CompressorStartLimit hp1;
  CompressorStartLimit hp2;
  static_assert(sizeof(CompressorStartLimit) <= 28U, "Start history must remain fixed and small");

  // Six admitted starts nine minutes apart, including a valid timestamp zero.
  for (uint32_t n = 0; n < 6; ++n) {
    const uint32_t now = n * 9 * minute;
    assert(hp1.remaining_ms(now) == 0);
    hp1.record_transition(0, 1, now);
    hp1.record_transition(1, 1, now + 1000);  // repeated active command / retry
    hp1.record_transition(1, 3, now + 2000);  // modulation
    hp1.record_transition(3, 0, now + 5 * minute);
  }
  const uint32_t seventh = 54 * minute;
  assert(hp1.remaining_ms(seventh) == 6 * minute);
  assert(hp2.remaining_ms(seventh) == 0);  // Independent Duo quota.
  const auto block = [&](int requested, int previous, uint32_t rest, bool safety) {
    return decide_preflight(requested, previous, false, 2, rest, safety, false, hp1.remaining_ms(seventh));
  };
  assert(block(1, 0, 0, false) == PreflightBlock::START_LIMIT);
  assert(block(1, 0, minute, false) == PreflightBlock::HP_REST);
  assert(block(1, 1, 0, false) == PreflightBlock::NONE);
  assert(block(0, 1, 0, false) == PreflightBlock::SAFE_ZERO);
  assert(block(1, 1, 0, true) == PreflightBlock::SAFE_ZERO);
  for (uint32_t now = seventh; now < hour; now += 5000) {
    hp1.record_transition(0, 0, now);  // Blocked or cancelled demand never consumes a slot.
    assert(hp1.remaining_ms(now) == hour - now);
  }
  assert(hp1.remaining_ms(hour - 1) == 1);
  // Positive/stale ODU readback must not resurrect a stopped command after
  // quota expiry, especially when the demand disappeared during the wait.
  const auto stale_retained =
      oq_odu::update_retained_level_snapshot({}, true, false, 3, 0, 0, false, oq_odu::RuntimeFrequencySnapshot{});
  assert(stale_retained.control_level > 0);
  for (uint32_t now : {hour - 1, hour}) {
    for (int previous : {0, 1}) {
      for (int demand : {0, 1}) {
        for (bool safety_stop : {false, true}) {
          const bool may_retain = oq_thermal_actuator::may_retain_command(previous, safety_stop);
          const bool retained = may_retain && stale_retained.control_level > 0;
          const auto decision =
              decide_preflight(demand, previous, retained, 2, 0, safety_stop, false, hp1.remaining_ms(now));
          if (safety_stop || (previous == 0 && demand == 0)) {
            assert(decision == PreflightBlock::SAFE_ZERO);
          } else if (previous > 0) {
            assert(decision == PreflightBlock::DEFROST);
          } else {
            assert(decision == (now < hour ? PreflightBlock::START_LIMIT : PreflightBlock::NONE));
          }
        }
      }
    }
  }
  assert(hp1.remaining_ms(hour) == 0);
  hp1.record_transition(0, 1, hour);
  assert(hp1.remaining_ms(hour) == 9 * minute);
  assert(hp1.remaining_ms(hour + 9 * minute) == 0);

  // A long idle period is expired on ticks, before any complete millis wrap.
  hp1.expire(2 * hour);
  assert(hp1.remaining_ms(seventh) == 0);
  // No persistence by design: a new runtime starts a fresh history.
  CompressorStartLimit rebooted;
  assert(rebooted.remaining_ms(0) == 0);

  CompressorStartLimit wrapping;
  const uint32_t first = UINT32_MAX - 30 * minute;
  for (uint32_t n = 0; n < 6; ++n) wrapping.record_transition(0, 1, first + n * 9 * minute);
  assert(wrapping.remaining_ms(first + seventh) == 6 * minute);
  assert(wrapping.remaining_ms(first + hour - 1) == 1);
  assert(wrapping.remaining_ms(first + hour) == 0);
  wrapping.record_transition(0, 1, first + hour);
  assert(wrapping.remaining_ms(first + hour) == 9 * minute);

  // Simulate recurring demand across many ring-buffer turns: no rolling hour
  // admits a seventh command. Test a second run across clock wrap as well.
  for (uint32_t origin : {0U, UINT32_MAX - hour}) {
    CompressorStartLimit limit;
    uint32_t admitted[100]{};
    unsigned count = 0;
    for (uint32_t elapsed = 0; elapsed < 12 * hour; elapsed += 9 * minute) {
      const uint32_t now = origin + elapsed;
      if (limit.remaining_ms(now) != 0) continue;
      limit.record_transition(0, 1, now);
      admitted[count++] = now;
      unsigned in_window = 0;
      for (unsigned n = 0; n < count; ++n) {
        if (static_cast<uint32_t>(now - admitted[n]) < hour) ++in_window;
      }
      assert(in_window <= 6);
    }
  }
}
