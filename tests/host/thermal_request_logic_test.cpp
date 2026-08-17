#include <assert.h>

#include "../../openquatt/includes/control/oq_thermal_request_logic.h"

int main() {
  using oq_request::min_runtime_hold_required;
  using oq_request::min_runtime_window_active;

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

  // Normal strategies retain the production 0..10 contract. The explicit
  // CM100 manual-HP strategy only gets the experimental 0..20 range for ODU v2.
  const auto normal_request = oq_request::make_published_request(2, 20, 11, 3);
  assert(normal_request.hp1_level == oq_request::NORMAL_MAX_COMPRESSOR_LEVEL);
  assert(normal_request.hp2_level == oq_request::NORMAL_MAX_COMPRESSOR_LEVEL);

  assert(oq_request::manual_hp_max_compressor_level(false) == oq_request::NORMAL_MAX_COMPRESSOR_LEVEL);
  assert(oq_request::manual_hp_max_compressor_level(true) == oq_request::MANUAL_HP_MAX_COMPRESSOR_LEVEL);

  const auto manual_v15_request = oq_request::make_published_request(2, 20, 21, 4);
  assert(manual_v15_request.hp1_level == oq_request::NORMAL_MAX_COMPRESSOR_LEVEL);
  assert(manual_v15_request.hp2_level == oq_request::NORMAL_MAX_COMPRESSOR_LEVEL);

  const auto manual_v2_request =
      oq_request::make_published_request(2, 20, 21, 4, oq_request::manual_hp_max_compressor_level(true));
  assert(manual_v2_request.hp1_level == oq_request::MANUAL_HP_MAX_COMPRESSOR_LEVEL);
  assert(manual_v2_request.hp2_level == oq_request::MANUAL_HP_MAX_COMPRESSOR_LEVEL);

  const auto bounded_manual_request = oq_request::make_published_request(2, 20, 20, 4, 99);
  assert(bounded_manual_request.hp1_level == oq_request::MANUAL_HP_MAX_COMPRESSOR_LEVEL);
  assert(bounded_manual_request.hp2_level == oq_request::MANUAL_HP_MAX_COMPRESSOR_LEVEL);

  const auto invalid_strategy_request =
      oq_request::make_published_request(2, 20, -1, 99, oq_request::MANUAL_HP_MAX_COMPRESSOR_LEVEL);
  assert(invalid_strategy_request.strategy_code == 0);
  assert(invalid_strategy_request.hp1_level == oq_request::NORMAL_MAX_COMPRESSOR_LEVEL);
  assert(invalid_strategy_request.hp2_level == 0);
  return 0;
}
