#include <cassert>
#include <cmath>
#include "openquatt/includes/control/oq_heat_intent_logic.h"
#include "openquatt/includes/control/oq_power_house_demand_logic.h"
#include "openquatt/includes/control/oq_warmup_runtime.h"
#include "components/openquatt_service_status/warmup_progress_snapshot.h"
#include <thread>

static void test_session_diagnostics() {
  for (auto reason :
       {oq_warmup::Status::COMFORT_REACHED, oq_warmup::Status::SETPOINT_LOWERED, oq_warmup::Status::TIME_LIMIT,
        oq_warmup::Status::INPUT_UNAVAILABLE, oq_warmup::Status::SOURCE_CHANGED, oq_warmup::Status::MODE_CHANGED,
        oq_warmup::Status::SETTINGS_CHANGED, oq_warmup::Status::DISABLED}) {
    oq_warmup_runtime::Runtime runtime;
    oq_warmup::Input input{UINT32_MAX - 2000U, true, true, true, 1, 1, 17, 17, 0.2f};
    oq_warmup::Settings settings;
    assert(runtime.session_status() == oq_warmup::Status::IDLE && runtime.session_elapsed_s(input.now_ms) == 0);
    runtime.update(input, settings);
    input.requested_c = 20;
    runtime.update(input, settings);
    input.now_ms += 95000U;  // Cross millis rollover.
    assert(runtime.session_elapsed_s(input.now_ms) == 95);
    const float target = runtime.effective_target();
    assert(runtime.session_status() == oq_warmup::Status::WARMING);
    assert(runtime.effective_target() == target);  // Diagnostic reads do not advance steps.
    if (reason == oq_warmup::Status::COMFORT_REACHED)
      input.room_c = 20;
    else if (reason == oq_warmup::Status::SETPOINT_LOWERED)
      input.requested_c = 16;
    else if (reason == oq_warmup::Status::TIME_LIMIT)
      input.now_ms += oq_warmup::MAX_DURATION_MS;
    else if (reason == oq_warmup::Status::INPUT_UNAVAILABLE)
      input.fresh = false;
    else if (reason == oq_warmup::Status::DISABLED)
      input.enabled = false;
    else
      runtime.invalidate(reason);
    runtime.update(input, settings);
    const uint32_t duration_s = reason == oq_warmup::Status::TIME_LIMIT ? 28800U : 95U;
    assert(!runtime.active() && runtime.session_status() == reason);
    assert(runtime.session_elapsed_s(input.now_ms) == duration_s);
    input.now_ms += 3600000U;
    runtime.update(input, settings);
    assert(runtime.session_status() == reason && runtime.session_elapsed_s(input.now_ms) == duration_s);
    input.enabled = input.fresh = true;
    input.room_c = input.requested_c = 17;
    runtime.update(input, settings);
    input.requested_c = 20;
    runtime.update(input, settings);
    assert(runtime.active() && runtime.session_status() == oq_warmup::Status::WARMING);
    assert(runtime.session_elapsed_s(input.now_ms) == 0);
    oq_warmup_runtime::Runtime rebooted;
    assert(rebooted.session_status() == oq_warmup::Status::IDLE && rebooted.session_elapsed_s(input.now_ms) == 0);
  }
}

static void test_coherent_http_snapshot() {
  using esphome::openquatt_service_status::WarmupProgressSnapshot;
  WarmupProgressSnapshot snapshot;
  assert(snapshot.read() == WarmupProgressSnapshot::UNAVAILABLE);
  std::thread publisher([&snapshot]() {
    for (uint32_t seconds = 0; seconds < 20000; ++seconds) snapshot.publish(seconds % 10U, seconds);
  });
  for (int i = 0; i < 50000; ++i) {
    const uint32_t word = snapshot.read();
    if (word != WarmupProgressSnapshot::UNAVAILABLE) assert((word >> 24U) == (word & 0x00ffffffU) % 10U);
  }
  publisher.join();
  snapshot.publish(5, UINT32_MAX);
  assert((snapshot.read() & 0x00ffffffU) == 28800U);
}

static void test_consumer_before_lifecycle_tick() {
  oq_warmup_runtime::Runtime runtime;
  oq_warmup::Input input{1000, true, true, true, 1, 1, 17.0f, 17.0f, 0.2f};
  oq_warmup::Settings settings;
  runtime.update(input, settings);
  oq_heat_intent::Input intent{1000, true, true, true, true, true, false, 1, 17, 17, 0.05f, 0.2f, 0};
  intent.requested_setpoint_c = 17;
  auto decision = oq_heat_intent::evaluate(intent, {});
  // The thermostat publishes first; the Power House consumer runs before the
  // 1s owner. Repeated reads must suppress both full room feedback and fast start.
  input.requested_c = 20;
  oq_power_house::DemandInput demand{1000, 5, -10, 18, 10000, 17, 20, NAN, 1, false};
  oq_power_house::DemandTuning tuning{0, 2000, 0.2f, 0.3f, 8, 3, 20};
  const auto normal = oq_power_house::decide_demand(demand, tuning, {});
  for (int i = 0; i < 100; ++i) {
    const auto target = runtime.control_target(20, settings.trigger_c, true);
    assert(target.limited && target.effective_c == 17 && !runtime.active());
    demand.setpoint_c = target.effective_c;
    assert(oq_power_house::decide_demand(demand, tuning, {}).requested_w < normal.requested_w);
    intent.setpoint_c = target.effective_c;
    intent.requested_setpoint_c = 20;
    intent.allow_setpoint_raise = !target.limited;
    decision = oq_heat_intent::evaluate(intent, decision.next);
    assert(!decision.setpoint_raise_edge && decision.reason != oq_heat_intent::SETPOINT_RAISE);
  }
  runtime.update(input, settings);
  auto target = runtime.control_target(20, settings.trigger_c, true);
  assert(runtime.active() && target.limited && std::fabs(target.effective_c - 17.1f) < 0.0001f);
  intent.setpoint_c = target.effective_c;
  decision = oq_heat_intent::evaluate(intent, decision.next);
  assert(!decision.setpoint_raise_edge);
  input.now_ms += oq_warmup::MAX_DURATION_MS;
  runtime.update(input, settings);
  target = runtime.control_target(20, settings.trigger_c, true);
  assert(!target.limited && target.effective_c == 20);
  intent.setpoint_c = target.effective_c;
  intent.allow_setpoint_raise = !target.limited;
  decision = oq_heat_intent::evaluate(intent, decision.next);
  assert(!decision.setpoint_raise_edge);  // Releasing the limiter is no user raise.
}

static void test_readiness_boundaries_and_invalidation() {
  oq_warmup_runtime::Runtime runtime;
  oq_warmup::Input input{1000, true, true, true, 1, 1, 17.0f, 17.0f, 0.2f};
  oq_warmup::Settings settings;
  assert(!runtime.control_target(20, 1.5f, true).limited);  // No boot baseline yet.
  runtime.update(input, settings);
  for (float goal : {16.0f, 17.1f, 18.5f}) {
    const auto target = runtime.control_target(goal, 1.5f, true);
    assert(!target.limited && target.effective_c == goal);
  }
  assert(!runtime.control_target(20, 1.5f, false).limited);
  for (float trigger : {NAN, 0.0f, 5.1f}) assert(!runtime.control_target(20, trigger, true).limited);
  assert(!runtime.control_target(NAN, 1.5f, true).limited);
  assert(runtime.control_target(20, 1.5f, true).limited);
  // An off/on or service start/stop between ticks cancels the pending raise.
  runtime.invalidate(oq_warmup::Status::MODE_CHANGED);
  auto target = runtime.control_target(20, 1.5f, true);
  assert(!target.limited && target.effective_c == 20);
  input.requested_c = 20;
  runtime.update(input, settings);
  assert(!runtime.active() && !runtime.control_target(20, 1.5f, true).limited);
}

int main() {
  test_session_diagnostics();
  test_coherent_http_snapshot();
  test_consumer_before_lifecycle_tick();
  test_readiness_boundaries_and_invalidation();
  oq_warmup_runtime::Runtime runtime;
  oq_warmup::Input input{1000, true, true, true, 1, 1, 17.0f, 17.0f, 0.2f};
  oq_warmup::Settings settings;
  runtime.update(input, settings);
  input.requested_c = 20.0f;
  input.now_ms += 1000;
  runtime.update(input, settings);
  assert(runtime.active());
  const float target = runtime.effective_target();
  assert(runtime.effective_target(16.0f) == 16.0f);
  assert(runtime.active() && runtime.effective_target() == target);
  // Timers, room values and edges cannot be processed by reads.
  input.now_ms += settings.step_ms;
  input.room_c = target;
  for (int i = 0; i < 100; ++i) {
    assert(runtime.active() && runtime.effective_target() == target);
    assert(runtime.offset() == settings.step_c);
    assert(runtime.status() == oq_warmup::Status::WARMING);
  }
  runtime.update(input, settings);
  assert(runtime.effective_target() > target);
  runtime.invalidate(oq_warmup::Status::SOURCE_CHANGED);
  assert(!runtime.active() && runtime.effective_target() == 20.0f && runtime.offset() == 0.0f);
  assert(runtime.status() == oq_warmup::Status::SOURCE_CHANGED);
  runtime.update(input, settings);
  assert(!runtime.active());  // A reset establishes a baseline, never replays a raise.
  input.requested_c = 17.0f;
  runtime.update(input, settings);
  input.requested_c = 20.0f;
  runtime.update(input, settings);
  assert(runtime.active());
  runtime.invalidate(oq_warmup::Status::MODE_CHANGED);
  assert(!runtime.active());
  runtime.update(input, settings);
  assert(!runtime.active() && runtime.status() == oq_warmup::Status::MODE_CHANGED);
  input.requested_c = 17.0f;
  runtime.update(input, settings);
  input.requested_c = 20.0f;
  runtime.update(input, settings);
  assert(runtime.active());
  input.fresh = false;
  runtime.update(input, settings);
  assert(!runtime.active());
  input.fresh = true;
  runtime.update(input, settings);
  assert(!runtime.active());
}
