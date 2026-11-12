#include <cassert>
#include <cmath>
#include <cstdint>

#include "includes/control/oq_compressor_feedback_logic.h"

using namespace oq_compressor_feedback;
namespace {
std::array<uint8_t, kLevels> table() { return {0, 20, 26, 30, 48, 55, 61, 72, 80, 85, 90}; }
Observation reading(uint32_t now, int level = 2, float actual = 48) {
  return {now, level, 2, actual, 2, 35, true, false, true, now, 0};
}
Snapshot confirm(Observer& observer, uint32_t start = 1000, int level = 2, float actual = 48, uint8_t variant = 3) {
  Snapshot result;
  for (uint32_t elapsed = 0; elapsed <= kStartupMs + kSettlingMs + 10000; elapsed += 10000)
    result = observer.observe(table(), variant, reading(start + elapsed, level, actual));
  return result;
}
void test_live_feedback_and_nonmonotonic_response() {
  Observer observer;
  auto out = observer.observe(table(), 3, reading(1000));
  assert(out.settling && out.frequency(2, 26) == 48);
  // An unconfirmed transition must not become a learned low-frequency floor.
  assert(out.frequency(1, 20) == 20 && out.frequency(3, 30) == 30);
  out = confirm(observer);
  assert(out.frequency(1, 20) == 48 && out.frequency(2, 26) == 48);
  assert(out.frequency(3, 30) == 30 && out.frequency(4, 48) == 48);
  // Raising the command to a working 30 Hz point must remain possible.
  out = observer.observe(table(), 3, reading(171000, 3, 30));
  assert(out.frequency(2, 26) == 48 && out.frequency(3, 30) == 30);
  // A stop does not immediately erase the evidence and invite the same Duo start.
  auto stopped = reading(181000, 0, 0);
  stopped.working_mode = 0;
  out = observer.observe(table(), 3, stopped);
  assert(!out.missing && out.frequency(2, 26) == 48);
  Observer independent;
  assert(independent.observe(table(), 3, stopped).frequency(2, 26) == 26);
}
void test_transients_and_missing_receipts() {
  for (int transient = 0; transient < 2; ++transient) {
    Observer observer;
    Snapshot out;
    for (uint32_t now = 1000; now <= 241000; now += 10000) {
      auto in = reading(now);
      in.may_learn = transient != 1;
      in.defrost = transient == 0;
      out = observer.observe(table(), 3, in);
    }
    auto stopped = reading(251000, 0, 0);
    stopped.working_mode = 0;
    out = observer.observe(table(), 3, stopped);
    assert(out.frequency(2, 26) == 26);
  }
  Observer observer;
  confirm(observer);
  for (int fault = 0; fault < 5; ++fault) {
    auto in = reading(171000);
    if (fault == 0) in.fresh = false;
    if (fault == 1) in.actual_hz = NAN;
    if (fault == 2) in.actual_hz = 0;
    if (fault == 3) in.actual_hz = 91;
    if (fault == 4) in.working_mode = 0;
    assert(observer.observe(table(), 3, in).missing);
  }
  auto defrost = reading(171000);
  defrost.defrost = true;
  const auto out = observer.observe(table(), 3, defrost);
  assert(!out.missing && out.frequency(2, 26) == 26);
}
void test_expiry_context_identity_and_recovery() {
  Observer original;
  confirm(original);
  auto stopped = reading(171000, 0, 0);
  stopped.working_mode = 0;
  for (int change = 0; change < 5; ++change) {
    auto observer = original;
    auto in = stopped;
    auto nominal = table();
    uint8_t variant = 3;
    if (change == 0) in.now_ms += kResponseLifetimeMs;
    if (change == 1) in.outside_c += 3;
    if (change == 2) in.outlet_c += 6;
    if (change == 3) nominal[2] = 25;
    if (change == 4) variant = 2;
    const auto out = observer.observe(nominal, variant, in);
    assert(out.frequency(2, nominal[2]) == nominal[2]);
  }
  auto observer = original;
  for (uint32_t now = 171000; now <= 211000; now += 10000) observer.observe(table(), 3, reading(now, 2, 26));
  stopped.now_ms = 221000;
  const auto recovered = observer.observe(table(), 3, stopped);
  assert(recovered.frequency(2, 26) == 26 && recovered.frequency(1, 20) == 20);
  // Millis rollover must neither erase a recent response nor skip confirmation.
  Observer rollover;
  Snapshot out;
  const uint32_t changed = UINT32_MAX - 50000;
  for (uint32_t elapsed = 1000; elapsed <= 211000; elapsed += 10000) {
    auto in = reading(changed + elapsed);
    in.applied_ms = changed;
    out = rollover.observe(table(), 3, in);
  }
  assert(out.frequency(1, 20) == 48 && out.frequency(2, 26) == 48);
}
void test_receipt_after_response_window() {
  Observer observer;
  confirm(observer);
  auto in = reading(171000, 3, 48);
  in.applied_ms = 171000;
  in.received_ms = 170999;
  auto out = observer.observe(table(), 3, in);
  assert(out.missing && out.settling);
  assert(out.blocks_dispatch(false) && !out.blocks_dispatch(true));
  in.now_ms = in.received_ms = 181000;
  out = observer.observe(table(), 3, in);
  assert(!out.missing && out.settling);
  assert(out.hold_level(3, true, true) == 3);
  assert(out.hold_level(3, false, true) == 0);  // Caps and excluded ranges take precedence.
  assert(out.hold_level(3, true, false) == 0);  // Peak power takes precedence.
  assert(out.hold_level(0, true, true) == 0);
  auto stopped = reading(191000, 0, 0);
  stopped.working_mode = 0;
  auto inspect = observer;
  assert(inspect.observe(table(), 3, stopped).frequency(3, 30) == 30);
  in.now_ms = in.received_ms = 202000;
  in.actual_hz = 30;
  out = observer.observe(table(), 3, in);
  assert(out.settling);
  for (uint32_t now = 212000; now <= 232000; now += 10000) {
    in.now_ms = in.received_ms = now;
    out = observer.observe(table(), 3, in);
  }
  assert(!out.settling && out.frequency(2, 26) == 48 && out.frequency(3, 30) == 30);
  in = reading(0x90000000U);
  out = observer.observe(table(), 3, in);
  assert(!out.missing && !out.settling);  // A command can be older than half the millis period.
}
void test_unsettled_response_is_not_learned_or_held_forever() {
  Observer observer;
  Snapshot out;
  for (uint32_t now = 1000; now <= 241000; now += 10000) {
    auto in = reading(now, 2, (now / 10000) % 2 == 0 ? 30 : 40);
    out = observer.observe(table(), 3, in);
    assert(!out.response_changed);
  }
  assert(!out.missing && !out.settling);
  auto stopped = reading(251000, 0, 0);
  stopped.working_mode = 0;
  assert(observer.observe(table(), 3, stopped).frequency(2, 26) == 26);
}
void test_receipt_and_control_clock_origins_are_independent() {
  Observer observer;
  Snapshot out;
  for (uint32_t elapsed = 0; elapsed <= 160000; elapsed += 10000) {
    auto in = reading(1000 + elapsed);
    in.applied_ms = 500;
    in.received_ms = 90000 + elapsed;
    in.receipt_age_ms = 5;
    out = observer.observe(table(), 3, in);
    assert(out.missing == (elapsed == 0));
  }
  assert(!out.settling && out.frequency(1, 20) == 48);
  auto stopped = reading(171000, 0, 0);
  stopped.working_mode = 0;
  // Cache lifetime uses the control clock, not the source clock's origin.
  assert(observer.observe(table(), 3, stopped).frequency(2, 26) == 48);
  stopped.now_ms = 801000;
  assert(observer.observe(table(), 3, stopped).frequency(2, 26) == 26);
  auto stale = reading(811000);
  stale.applied_ms = 810000;
  stale.received_ms = 500;
  stale.receipt_age_ms = 2000;
  assert(observer.observe(table(), 3, stale).missing);
  stale.received_ms = 10000;
  stale.receipt_age_ms = 10;
  assert(!observer.observe(table(), 3, stale).missing);
}
void test_startup_plateaus_never_qualify_an_override() {
  for (uint8_t variant : {3, 4}) {
    for (int plateau : {44, 45, 46, 54, 55, 56}) {
      for (uint32_t duration : {60000U, 300000U, 900000U}) {
        Observer observer;
        Snapshot out;
        for (uint32_t now = 1000; now <= duration + 1000; now += 10000) {
          auto in = reading(now, 2, plateau);
          in.may_learn = now >= kStartupMs;
          out = observer.observe(table(), variant, in);
          assert(!out.qualified_run && !out.response_changed);
          assert(out.frequency(2, 26) == plateau);
          assert(out.confirmed_frequency(2, 26) == 26 && out.confirmed_frequency(1, 20) == 20);
        }
        for (uint32_t now = duration + 11000; now <= duration + kStartupMs + 51000; now += 10000)
          out = observer.observe(table(), variant, reading(now, 2, 26));
        assert(out.qualified_run && out.confirmed_frequency(2, 26) == 26);
        auto stopped = reading(duration + kStartupMs + 61000, 0, 0);
        stopped.working_mode = 0;
        out = observer.observe(table(), variant, stopped);
        assert(!out.qualified_run && out.frequency(1, 20) == 20 && out.frequency(2, 26) == 26);
      }
    }
  }
}
void test_confirmed_power_excludes_live_boost_and_old_run() {
  Observer observer;
  auto out = confirm(observer);
  assert(out.qualified_run && out.confirmed_frequency(2, 26) == 48);
  auto oil = reading(171000, 2, 50);
  oil.may_learn = false;
  out = observer.observe(table(), 3, oil);
  assert(out.frequency(2, 26) == 50 && out.confirmed_frequency(2, 26) == 48);
  // Contradictory live feedback withdraws old capacity immediately, before a
  // new 30s learning window can finish (including inferred lower commands).
  out = observer.observe(table(), 3, reading(181000, 2, 26));
  assert(out.confirmed_frequency(2, 26) == 26 && out.confirmed_frequency(1, 20) == 20);
  auto stopped = reading(191000, 0, 0);
  stopped.working_mode = 0;
  out = observer.observe(table(), 3, stopped);
  assert(out.frequency(2, 26) == 48 && out.confirmed_frequency(2, 26) == 26);
  // Keep the prior evidence for planning, but a fresh startup must qualify its
  // own sustainable output before the other running HP may be shed.
  for (uint32_t now = 201000; now <= 601000; now += 10000) {
    out = observer.observe(table(), 3, reading(now, 2, 45));
    assert(!out.qualified_run && out.confirmed_frequency(2, 26) == 26);
  }
  // A stable 48Hz floor still qualifies; it is not a startup plateau ban on
  // all overrides, nor a permanent Single/Duo restriction.
  for (uint32_t now = 611000; now <= 771000; now += 10000) out = observer.observe(table(), 3, reading(now, 2, 48));
  assert(out.qualified_run && out.confirmed_frequency(2, 26) == 48);
}
void test_run_continuity_and_per_command_capacity() {
  for (int fault = 0; fault < 8; ++fault) {
    Observer observer;
    auto out = confirm(observer);
    assert(out.confirmed_frequency(2, 26) == 48);
    // An inferred lower command is useful for planning, never proof for shedding.
    assert(out.frequency(1, 20) == 48 && out.confirmed_frequency(1, 20) == 20);
    auto in = reading(171000);
    if (fault == 0) in.actual_hz = 0;
    if (fault == 1) in.fresh = false;
    if (fault == 2) in.actual_hz = NAN;
    if (fault == 3) in.actual_hz = 91;
    if (fault == 4) in.working_mode = 0;
    if (fault == 5) in.defrost = true;
    if (fault == 6) in.applied_level = 0;
    if (fault < 7) {
      out = observer.observe(table(), 3, in);
      assert(!out.qualified_run && out.confirmed_frequency(2, 26) == 26);
    }
    // fault 7 hides the stop inside a receipt gap, even if fresh stays true.
    for (uint32_t now = 201000; now <= 531000; now += 10000) {
      out = observer.observe(table(), 3, reading(now, 2, 45));
      assert(!out.qualified_run && out.confirmed_frequency(2, 26) == 26);
    }
    // A stable but different command cannot reconfirm the old 26->48 entry.
    for (uint32_t now = 541000; now <= 701000; now += 10000) out = observer.observe(table(), 3, reading(now, 5, 55));
    assert(out.qualified_run && out.frequency(2, 26) == 48 && out.confirmed_frequency(2, 26) == 26);
    for (uint32_t now = 711000; now <= 751000; now += 10000) out = observer.observe(table(), 3, reading(now, 2, 48));
    assert(out.confirmed_frequency(2, 26) == 48 && out.confirmed_frequency(1, 20) == 20);
    // Unflagged 45/55 plateaus cannot become confirmed overrides later either.
    for (uint32_t now = 761000; now <= 801000; now += 10000) {
      out = observer.observe(table(), 3, reading(now, 2, 45));
      assert(out.confirmed_frequency(2, 26) == 26);
    }
  }
}
void test_startup_shape_withdraws_previous_capacity_between_receipts() {
  for (uint8_t variant : {3, 4}) {
    for (int plateau : {44, 45, 46, 54, 55, 56}) {
      Observer observer;
      auto out = confirm(observer, 1000, 2, 48, variant);
      assert(out.confirmed_frequency(2, 26) == 48);
      // No zero sample, stale flag or >30s receipt gap exposes the restart.
      out = observer.observe(table(), variant, reading(171000, 2, plateau));
      assert(!out.qualified_run && out.confirmed_mask == 0 && out.confirmed_frequency(2, 26) == 26);
      for (uint32_t now = 181000; now <= 341000; now += 10000) {
        out = observer.observe(table(), variant, reading(now, 2, 48));
        if (now < 331000) assert(out.confirmed_frequency(2, 26) == 26);
      }
      assert(out.confirmed_frequency(2, 26) == 48);
    }
  }
}
void test_recovery_restarts_local_startup_timer() {
  Observer observer;
  confirm(observer);
  observer.observe(table(), 3, reading(171000, 2, 0));
  Snapshot out;
  for (uint32_t now = 181000; now <= 341000; now += 10000) {
    out = observer.observe(table(), 3, reading(now, 2, 48));
    if (now < 331000) assert(!out.qualified_run && out.confirmed_frequency(2, 26) == 26);
  }
  assert(out.qualified_run && out.confirmed_frequency(2, 26) == 48);
}
}  // namespace
int main() {
  test_live_feedback_and_nonmonotonic_response();
  test_transients_and_missing_receipts();
  test_expiry_context_identity_and_recovery();
  test_receipt_after_response_window();
  test_unsettled_response_is_not_learned_or_held_forever();
  test_receipt_and_control_clock_origins_are_independent();
  test_startup_plateaus_never_qualify_an_override();
  test_confirmed_power_excludes_live_boost_and_old_run();
  test_run_continuity_and_per_command_capacity();
  test_recovery_restarts_local_startup_timer();
  test_startup_shape_withdraws_previous_capacity_between_receipts();
}
