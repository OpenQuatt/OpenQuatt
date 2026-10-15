#include "openquatt/includes/control/oq_hp_boot_resume.h"
#include "openquatt/includes/control/oq_hp_restart_guard.h"
#include "openquatt/includes/control/oq_thermal_actuator_logic.h"
#include "openquatt/includes/control/oq_compressor_command_guard.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

using oq_boot_resume::Policy;

static void observe_running(Policy& p, uint32_t now, int level = 7) {
  p.observe(3999, 2, now);
  p.observe(1999, level, now);
  p.observe(2099, 2, now);
  p.observe(2103, 40, now);
}

int main() {
  Policy cold;
  cold.begin(false, 0);
  observe_running(cold, 10000);
  assert(!cold.pending(10000));
  assert(cold.update(10000, false, true, true, true) == 0);

  Policy boot;
  boot.begin(true, 0);
  // Requests can arrive in any order. A missing readback is never a run permit.
  boot.observe(2103, 40, 1000);
  assert(!boot.readback_ready(1000));
  assert(boot.update(1000, false, false, false, true) == 0);
  observe_running(boot, 20000);
  assert(boot.readback_ready(20000));
  // Ordinary/queued writes cannot commit adoption, even with ready inputs.
  assert(boot.update(20000, false, true, true, false) == 0);
  assert(boot.pending(20000));
  assert(boot.update(20000, false, true, true, true) == 7);
  assert(!boot.pending(20000));
  assert(boot.protection_hold(199999));
  assert(!boot.protection_hold(200000));
  assert(boot.update(20001, false, true, true, true) == 0);  // Once only.

  // Comfort can wait while safety is continuously valid. Deadline is fixed.
  Policy waiting;
  waiting.begin(true, 0);
  observe_running(waiting, 20000);
  assert(waiting.update(20000, false, true, false, true) == 0);
  observe_running(waiting, 119999);
  assert(waiting.update(119999, false, true, false, true) == 0);
  assert(waiting.pending(119999));
  assert(!waiting.pending(120000));
  assert(waiting.stop_required(120000));
  assert(waiting.update(120000, false, true, true, true) == 0);
  observe_running(waiting, 120001);
  assert(waiting.update(120001, false, true, true, true) == 0);

  Policy blind;
  blind.begin(true, 0);
  assert(blind.update(44999, false, false, false, true) == 0);
  assert(blind.pending(44999));
  assert(blind.update(45000, false, false, false, true) == 0);
  assert(blind.stop_required(45000));

  // Permission/fault/flow/defrost/oil-return aborts are irreversible. A partial
  // recovery or repeated keepalive must not extend or reopen acquisition.
  Policy aborted;
  aborted.begin(true, 0);
  observe_running(aborted, 10000);
  assert(aborted.update(10000, true, true, true, true) == 0);
  assert(aborted.stop_required(10000));
  observe_running(aborted, 11000);
  assert(aborted.update(11000, false, true, true, true) == 0);

  Policy stale;
  stale.begin(true, 0);
  observe_running(stale, 10000);
  assert(stale.update(10000, false, true, false, true) == 0);
  assert(!stale.readback_ready(25001));
  assert(stale.update(25001, false, false, true, true) == 0);
  assert(stale.stop_required(25001));

  for (auto address : {3999, 1999, 2099, 2103}) {
    Policy stopping;
    stopping.begin(true, 0);
    observe_running(stopping, 10000);
    stopping.observe(address, 0, 10001);
    assert(stopping.stop_required(10001));
    observe_running(stopping, 10002);
    assert(stopping.update(10002, false, true, true, true) == 0);
    Policy invalid;
    invalid.begin(true, 0);
    invalid.observe(address, NAN, 10000);
    assert(invalid.stop_required(10000));
  }
  for (float mode : {1.0f, 3.0f, 4.0f}) {
    Policy other_mode;
    other_mode.begin(true, 0);
    other_mode.observe(2099, mode, 1000);
    assert(other_mode.stop_required(1000));
  }
  Policy hp1, hp2;
  hp1.begin(true, 0);
  hp2.begin(true, 0);
  observe_running(hp1, 10000);
  observe_running(hp2, 10000);
  hp1.cancel();
  assert(hp2.update(10000, false, true, true, true) == 7);

  // millis wrap and exact freshness boundaries.
  Policy wrap;
  constexpr uint32_t start = UINT32_MAX - 10000U;
  wrap.begin(true, start);
  observe_running(wrap, start);
  assert(wrap.readback_ready(start + 15000U));
  assert(!wrap.readback_ready(start + 15001U));
  observe_running(wrap, start + 119999U);
  assert(wrap.update(start + 119999U, false, true, true, true) == 7);

  // Continuation bypasses no new-start rest gate. Once stopped, normal
  // confirmed-off evidence is still required before another start.
  oq_hp_restart_guard::Policy rest;
  rest.configure(240000, 15000);
  rest.invalidate();
  assert(!rest.can_start(20000));
  assert(oq_thermal_actuator::decide_preflight(7, 7, false, 2, 240000, false, false) ==
         oq_thermal_actuator::PreflightBlock::NONE);
  assert(oq_thermal_actuator::decide_preflight(7, 0, false, 2, 240000, false, false) ==
         oq_thermal_actuator::PreflightBlock::HP_REST);

  // Pump and compressor writes queued before inputs change are revalidated;
  // reads remain unmodified (and can therefore feed the ODU watchdog).
  for (uint16_t address : {1999, 3999, 2006, 2010, 2015}) {
    const std::array<uint8_t, 5> write{6, static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address), 0, 0};
    assert(!oq_command_guard::allows(write.data(), write.size(), [](uint16_t, uint16_t) { return false; }));
  }
  const std::array<uint8_t, 5> read{3, 7, 207, 0, 1};
  assert(oq_command_guard::allows(read.data(), read.size(), [](uint16_t, uint16_t) { return false; }));
}
