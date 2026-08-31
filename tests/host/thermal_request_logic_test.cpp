#include <assert.h>

#include "../../openquatt/includes/control/oq_thermal_request_logic.h"

int main() {
  using oq_request::make_published_request;
  using oq_request::min_runtime_hold_required;
  using oq_request::min_runtime_window_active;

  const auto automatic_request = make_published_request(2, 20, 20, 1);
  assert(automatic_request.hp1_level == 10 && automatic_request.hp2_level == 10);

  const auto manual_request = make_published_request(2, 20, 20, 4, 20, 10);
  assert(manual_request.hp1_level == 20 && manual_request.hp2_level == 10);

  constexpr uint32_t start_ms = 1000UL;
  constexpr uint32_t min_runtime_ms = 300000UL;

  const bool runtime_active = min_runtime_window_active(start_ms + 120000UL, start_ms, min_runtime_ms);
  assert(runtime_active && min_runtime_window_active(start_ms, start_ms, min_runtime_ms));

  // Unsigned elapsed-time arithmetic keeps the hold valid across millis() wrap.
  constexpr uint32_t wrap_start_ms = UINT32_MAX - 1000UL;
  assert(min_runtime_window_active(1000UL, wrap_start_ms, 5000UL));
  assert(!min_runtime_window_active(1000UL, 0, min_runtime_ms));
  assert(!min_runtime_window_active(start_ms, start_ms, 0));

  const bool runtime_expired = min_runtime_window_active(start_ms + min_runtime_ms, start_ms, min_runtime_ms);
  assert(!runtime_expired);
  struct HoldCase {
    int request, applied;
    bool inhibit, window, cooling, expected;
  };
  const HoldCase cases[] = {{0, 1, false, runtime_active, true, true},   {0, 4, false, runtime_active, false, true},
                            {1, 1, false, runtime_active, true, false},  {0, 0, false, runtime_active, false, false},
                            {0, 1, false, runtime_expired, true, false}, {0, 1, true, runtime_active, true, false}};
  for (const auto& c : cases)
    assert(min_runtime_hold_required(c.request, c.inhibit, c.window, c.applied, c.cooling) == c.expected);
  return 0;
}
