#include <assert.h>

#include "../../openquatt/includes/control/oq_cooling_limiter_logic.h"

namespace {

using oq_cooling::apply_hp2_before_hp1_for_cooling_handover;
using oq_cooling::cooling_minimum_off_stop_is_pending;
using oq_cooling::cooling_stop_is_planned;
using oq_cooling::global_minimum_off_time_blocks_start;
using oq_cooling::global_minimum_off_time_remaining_ms;
using oq_cooling::record_confirmed_cooling_stop;
using oq_cooling::record_pi_zero_stop;
using oq_cooling::water_restart_gap_recovered;
using oq_cooling::WATER_STOP_DEW;
using oq_cooling::WATER_STOP_LIMITER;
using oq_cooling::WATER_STOP_NONE;
using oq_cooling::WATER_STOP_PROJECTED_FLOOR;
using oq_cooling::WATER_STOP_REQUEST_CLEARED;
using oq_cooling::WaterCycleState;

void test_pi_zero_stop_requires_an_active_nonzero_run() {
  WaterCycleState inactive;
  assert(!record_pi_zero_stop(1, 0, 0.0f, inactive));

  WaterCycleState not_started{true, 0.0f, WATER_STOP_NONE};
  assert(!record_pi_zero_stop(0, 0, 0.0f, not_started));
  assert(not_started.active);

  WaterCycleState still_running{true, 0.0f, WATER_STOP_NONE};
  assert(!record_pi_zero_stop(1, 1, 0.0f, still_running));
  assert(still_running.active);
}

void test_low_load_pi_zero_stop_waits_for_restart_delta() {
  constexpr float stop_gap_c = 0.05f;
  constexpr float restart_delta_c = 1.0f;
  WaterCycleState cycle{true, 0.0f, WATER_STOP_NONE};

  assert(record_pi_zero_stop(1, 0, stop_gap_c, cycle));
  assert(!cycle.active);
  assert(cycle.stop_reason_code == WATER_STOP_LIMITER);
  assert(cycle.stop_buffer_gap_c == stop_gap_c);

  // Repeated PI recovery attempts stay blocked, so no Duo owner can receive a
  // new request while the just-stopped owner is still in minimum off-time.
  assert(!water_restart_gap_recovered(cycle, 0.20f, restart_delta_c));
  assert(!water_restart_gap_recovered(cycle, 0.60f, restart_delta_c));
  assert(!water_restart_gap_recovered(cycle, 1.04f, restart_delta_c));
  assert(water_restart_gap_recovered(cycle, 1.05f, restart_delta_c));
}

void test_pi_zero_stop_preserves_higher_priority_reason() {
  WaterCycleState dew_stop{true, 0.0f, WATER_STOP_DEW};

  assert(record_pi_zero_stop(1, 0, 0.10f, dew_stop));
  assert(!dew_stop.active);
  assert(dew_stop.stop_reason_code == WATER_STOP_DEW);
  assert(dew_stop.stop_buffer_gap_c == 0.10f);
}

void test_existing_restart_exceptions_remain_unchanged() {
  const WaterCycleState no_water_stop{false, 0.0f, WATER_STOP_NONE};
  assert(water_restart_gap_recovered(no_water_stop, -1.0f, 1.0f));

  const WaterCycleState projected_floor_pause{false, 0.0f, WATER_STOP_PROJECTED_FLOOR};
  assert(water_restart_gap_recovered(projected_floor_pause, -1.0f, 1.0f));
}

void test_global_minimum_off_time_blocks_boot_and_both_owner_candidates() {
  constexpr uint32_t minimum_off_ms = 600000UL;

  assert(global_minimum_off_time_remaining_ms(false, 1000UL, false, 0, false, minimum_off_ms) == 0);
  assert(global_minimum_off_time_remaining_ms(true, 300000UL, false, 0, false, minimum_off_ms) == 300000UL);
  assert(global_minimum_off_time_remaining_ms(true, minimum_off_ms, false, 0, true, minimum_off_ms) == 0);

  constexpr uint32_t stop_ms = 900000UL;
  assert(global_minimum_off_time_remaining_ms(true, stop_ms + 120000UL, true, stop_ms, true, minimum_off_ms) ==
         480000UL);
  assert(global_minimum_off_time_remaining_ms(true, stop_ms + minimum_off_ms, true, stop_ms, true, minimum_off_ms) ==
         0);
  assert(global_minimum_off_time_blocks_start(480000UL, false, false, 0));
  assert(global_minimum_off_time_blocks_start(480000UL, false, false, -1));
  assert(!global_minimum_off_time_blocks_start(480000UL, false, false, 1));
  assert(!global_minimum_off_time_blocks_start(0, false, false, 0));
}

void test_pending_or_same_tick_cooling_stop_blocks_duo_replacement_start() {
  // The original strategy request remains authoritative when the downstream
  // minimum-runtime floor temporarily keeps the outgoing HP at level 1.
  const bool outgoing_stop_planned = cooling_stop_is_planned(true, 3, 0);
  assert(outgoing_stop_planned);
  assert(!cooling_stop_is_planned(false, 3, 0));
  assert(!cooling_stop_is_planned(true, 0, 0));
  assert(!cooling_stop_is_planned(true, 3, 2));

  assert(global_minimum_off_time_blocks_start(0, true, false, 0));
  assert(global_minimum_off_time_blocks_start(0, false, outgoing_stop_planned, 0));
  assert(!global_minimum_off_time_blocks_start(0, true, true, 2));
}

void test_active_cooling_owner_is_applied_before_a_stopped_duo_candidate() {
  assert(!apply_hp2_before_hp1_for_cooling_handover(true, false));
  assert(apply_hp2_before_hp1_for_cooling_handover(false, true));
  assert(!apply_hp2_before_hp1_for_cooling_handover(true, true));
  assert(!apply_hp2_before_hp1_for_cooling_handover(false, false));
}

void test_minimum_off_time_waits_for_confirmed_physical_stop() {
  uint32_t last_confirmed_stop_ms = 1234UL;
  bool confirmed_stop_seen = false;
  assert(!record_confirmed_cooling_stop(false, true, 2000UL, last_confirmed_stop_ms, confirmed_stop_seen));
  assert(last_confirmed_stop_ms == 1234UL);
  assert(!confirmed_stop_seen);
  assert(!record_confirmed_cooling_stop(true, false, 3000UL, last_confirmed_stop_ms, confirmed_stop_seen));
  assert(last_confirmed_stop_ms == 1234UL);
  assert(!confirmed_stop_seen);
  assert(record_confirmed_cooling_stop(true, true, 4000UL, last_confirmed_stop_ms, confirmed_stop_seen));
  assert(last_confirmed_stop_ms == 4000UL);
  assert(confirmed_stop_seen);
}

void test_global_minimum_off_time_is_millis_wrap_safe() {
  constexpr uint32_t stop_ms = UINT32_MAX - 3000UL;
  constexpr uint32_t now_ms = 2000UL;
  assert(global_minimum_off_time_remaining_ms(true, now_ms, true, stop_ms, true, 10000UL) == 4999UL);
}

void test_minimum_off_time_preserves_an_applied_water_stop_during_mode_switch() {
  // An applied stop or an active confirmation/countdown preserves the water
  // stop latch while minimum-off-time mode is selected.
  assert(cooling_minimum_off_stop_is_pending(true, false, WATER_STOP_LIMITER, true));
  assert(!cooling_minimum_off_stop_is_pending(true, false, WATER_STOP_REQUEST_CLEARED, true));
  assert(!cooling_minimum_off_stop_is_pending(true, false, WATER_STOP_LIMITER, false));
  assert(!cooling_minimum_off_stop_is_pending(true, true, WATER_STOP_LIMITER, true));
  assert(!cooling_minimum_off_stop_is_pending(false, false, WATER_STOP_LIMITER, true));
}

}  // namespace

int main() {
  test_pi_zero_stop_requires_an_active_nonzero_run();
  test_low_load_pi_zero_stop_waits_for_restart_delta();
  test_pi_zero_stop_preserves_higher_priority_reason();
  test_existing_restart_exceptions_remain_unchanged();
  test_global_minimum_off_time_blocks_boot_and_both_owner_candidates();
  test_pending_or_same_tick_cooling_stop_blocks_duo_replacement_start();
  test_active_cooling_owner_is_applied_before_a_stopped_duo_candidate();
  test_minimum_off_time_waits_for_confirmed_physical_stop();
  test_global_minimum_off_time_is_millis_wrap_safe();
  test_minimum_off_time_preserves_an_applied_water_stop_during_mode_switch();
  return 0;
}
