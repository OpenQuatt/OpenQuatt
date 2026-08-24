#include <assert.h>
#include <stdint.h>

#include "../../components/openquatt_ot_slave/room_signal_freshness.h"

namespace {

using oq_ot_slave::room_signal_fresh;
using oq_ot_slave::ROOM_SIGNAL_TIMEOUT_MS;

void test_requires_an_active_runtime_and_received_value() {
  assert(!room_signal_fresh(false, true, false, 100U, 101U));
  assert(!room_signal_fresh(true, false, false, 100U, 101U));
  assert(!room_signal_fresh(true, true, true, 100U, 101U));
  assert(!room_signal_fresh(true, true, false, 0U, 101U));
}

void test_value_is_fresh_through_timeout_boundary() {
  constexpr uint32_t last_update_ms = 1000U;

  assert(room_signal_fresh(true, true, false, last_update_ms, last_update_ms));
  assert(room_signal_fresh(true, true, false, last_update_ms, last_update_ms + ROOM_SIGNAL_TIMEOUT_MS));
  assert(!room_signal_fresh(true, true, false, last_update_ms, last_update_ms + ROOM_SIGNAL_TIMEOUT_MS + 1U));
}

void test_zero_timeout_fails_closed() { assert(!room_signal_fresh(true, true, false, 100U, 100U, 0U)); }

void test_millis_wrap_preserves_elapsed_age() {
  constexpr uint32_t last_update_ms = UINT32_MAX - 100U;
  constexpr uint32_t now_ms = 50U;

  assert(room_signal_fresh(true, true, false, last_update_ms, now_ms, 151U));
  assert(!room_signal_fresh(true, true, false, last_update_ms, now_ms, 150U));
}

}  // namespace

int main() {
  test_requires_an_active_runtime_and_received_value();
  test_value_is_fresh_through_timeout_boundary();
  test_zero_timeout_fails_closed();
  test_millis_wrap_preserves_elapsed_age();
  return 0;
}
