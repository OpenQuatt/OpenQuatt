#include <assert.h>

#include "../../openquatt/includes/control/oq_thermal_request_logic.h"

int main() {
  using oq_request::make_published_request;
  using oq_request::min_runtime_hold_required;
  using oq_request::min_runtime_window_active;

  const auto automatic_request = make_published_request(2, 20, 20, 1);
  assert(automatic_request.hp1_level == 10);
  assert(automatic_request.hp2_level == 10);

  const auto manual_request = make_published_request(2, 20, 20, 4, 20, 10);
  assert(manual_request.hp1_level == 20);
  assert(manual_request.hp2_level == 10);

  constexpr uint32_t start_ms = 1000UL;
  constexpr uint32_t min_runtime_ms = 300000UL;

  const bool runtime_active = min_runtime_window_active(start_ms + 120000UL, start_ms, min_runtime_ms);
  assert(runtime_active);
  assert(min_runtime_window_active(start_ms, start_ms, min_runtime_ms));

  // Unsigned elapsed-time arithmetic keeps the hold valid across millis() wrap.
  constexpr uint32_t wrap_start_ms = UINT32_MAX - 1000UL;
  assert(min_runtime_window_active(1000UL, wrap_start_ms, 5000UL));
  assert(!min_runtime_window_active(1000UL, 0, min_runtime_ms));
  assert(!min_runtime_window_active(start_ms, start_ms, 0));

  // A normal zero request, including a cooling floor/dew-point stop, keeps an
  // already running compressor active until its minimum runtime expires.
  assert(min_runtime_hold_required(0, false, runtime_active, 1, true));

  // The same policy applies independently to either compressor in a duo setup.
  assert(min_runtime_hold_required(0, false, runtime_active, 4, false));

  // An active request needs no runtime hold, and an idle compressor must never
  // be started only to satisfy a stale runtime timestamp.
  assert(!min_runtime_hold_required(1, false, runtime_active, 1, true));
  assert(!min_runtime_hold_required(0, false, runtime_active, 0, false));

  const bool runtime_expired = min_runtime_window_active(start_ms + min_runtime_ms, start_ms, min_runtime_ms);
  assert(!runtime_expired);
  assert(!min_runtime_hold_required(0, false, runtime_expired, 1, true));

  // Absolute safety and startup inhibition retain priority over the runtime floor.
  assert(!min_runtime_hold_required(0, true, runtime_active, 1, true));
  return 0;
}
