#include <cassert>
#include <cmath>

#include "../../openquatt/includes/control/oq_power_house_peer_pump_logic.h"

namespace {

using namespace oq_peer_pump;

Observation sample(uint64_t now_ms) {
  Observation result;
  result.now_ms = now_ms;
  result.active = true;
  result.online = true;
  result.variant = oq_odu::Variant::V1_5;
  result.flow_target_lph = 800.0f;
  result.flow.generation = 1U;
  result.flow.route = 1U;
  result.flow.contributors = 1U;
  result.feedback.observe(300.0f, now_ms - 100, true);
  result.status.observe(0x0800U, now_ms - 200, true);
  result.flow.receipt.observe(800.0f, now_ms - 300, true);
  return result;
}

void expect_estimate(const Estimate& estimate, float watts = 35.150232f) {
  assert(estimate.available());
  assert(estimate.state == EstimateState::ESTIMATED);
  assert(std::fabs(estimate.watts - watts) < 0.001f);
}

void warm(Estimator& estimator) {
  for (int i = 1; i < 6; ++i) {
    auto first = sample(i * 10000);
    assert(!estimator.observe(first).available());
    // The dispatch scheduler may run twice on the same physical poll.
    first.now_ms += 5000U;
    assert(!estimator.observe(first).available());
  }
  expect_estimate(estimator.observe(sample(60000U)));
}

void lease_expiry_requires_six_new_polls() {
  Estimator estimator;
  warm(estimator);
  auto duplicate = sample(71900U);
  duplicate.feedback.received_ms = 59900U;
  expect_estimate(estimator.observe(duplicate));
  ++duplicate.now_ms;
  assert(!estimator.observe(duplicate).available());
  for (int i = 1; i <= 6; ++i) {
    assert(estimator.observe(sample(72000U + i * 10000U)).available() == (i == 6));
  }
}

void changing_poll_phase_preserves_the_estimate() {
  Estimator estimator;
  warm(estimator);
  auto duplicate = sample(70000U);
  duplicate.feedback.received_ms = 59900U;
  expect_estimate(estimator.observe(duplicate));
  for (uint64_t t = 80000U; t < 150000U; t += 10000U) {
    auto next = sample(t);
    next.feedback.received_ms = t - 9100U;
    expect_estimate(estimator.observe(next));
  }
}

void does_not_count_retries_as_independent_polls() {
  Estimator estimator;
  for (uint64_t t = 10000U; t < 50000U; t += 1000U) {
    assert(!estimator.observe(sample(t)).available());
  }
  expect_estimate(estimator.observe(sample(55000U)));
}

template <typename Mutate>
void context_change_restarts_the_window(Mutate mutate) {
  Estimator estimator;
  warm(estimator);
  auto changed = sample(70000U);
  mutate(changed);
  assert(!estimator.observe(changed).available());
  for (int i = 1; i <= 6; ++i) {
    auto next = sample(70000U + i * 10000U);
    mutate(next);
    assert(estimator.observe(next).available() == (i == 6));
  }
}

template <typename Mutate>
void rejects_bad_sample(Mutate mutate) {
  Estimator estimator;
  warm(estimator);
  auto bad = sample(70000U);
  mutate(bad);
  assert(!estimator.observe(bad).available());
}

void overwritten_flow_failure_revokes_the_estimate(float failed_flow, bool online) {
  Estimator estimator;
  warm(estimator);

  uint64_t revoked_ms = 0;
  auto evidence = sample(60000U).flow.receipt;
  record_flow(evidence, revoked_ms, failed_flow, 800.0f, 63000U, online);
  record_flow(evidence, revoked_ms, 800.0f, 800.0f, 69000U, true);
  assert(evidence.valid && evidence.value == 800.0f && revoked_ms == 63000U);

  // No estimator call ran while the flow was bad or its provider was offline.
  // The three newest values are all valid when the next tick runs.
  for (int i = 0; i < 6; ++i) {
    auto recovered = sample(70000U + i * 10000U);
    if (i == 0) recovered.flow.receipt = evidence;
    recovered.invalidated_ms = revoked_ms;
    assert(estimator.observe(recovered).available() == (i == 5));
  }
}

void register_gap_between_ticks_restarts_the_window(bool feedback_gap) {
  Estimator estimator;
  warm(estimator);
  auto recovered = sample(73000U);
  const auto prior = sample(60000U);
  auto& receipt = feedback_gap ? recovered.feedback : recovered.status;
  receipt = feedback_gap ? prior.feedback : prior.status;
  const auto record = feedback_gap ? &record_feedback : &record_status;
  const float raw = receipt.value;
  const auto lease_end = receipt.received_ms + kLeaseMs;

  record(receipt, recovered.invalidated_ms, raw, lease_end + 1);
  record(receipt, recovered.invalidated_ms, raw, 72950U);
  assert(receipt.fresh(recovered.now_ms, kLeaseMs) && recovered.invalidated_ms == lease_end);
  assert(!estimator.observe(recovered).available());
  for (int i = 1; i <= 5; ++i) {
    assert(estimator.observe(sample(73000U + i * 10000U)).available() == (i == 5));
  }
}

void overwritten_feedback_excursion_restarts_the_window() {
  Estimator estimator;
  warm(estimator);
  auto recovered = sample(70000U);
  recovered.feedback = sample(60000U).feedback;
  record_feedback(recovered.feedback, recovered.invalidated_ms, 330.0f, 61000U);
  assert(recovered.invalidated_ms == 0U);  // Exactly 3 W is still stable.
  record_feedback(recovered.feedback, recovered.invalidated_ms, 400.0f, 65000U);
  assert(recovered.invalidated_ms == 65000U);
  record_feedback(recovered.feedback, recovered.invalidated_ms, 300.0f, 69900U);
  assert(recovered.invalidated_ms == 69900U && recovered.feedback.value == 300.0f);
  assert(!estimator.observe(recovered).available());
  for (int i = 1; i <= 6; ++i) {
    assert(estimator.observe(sample(70000U + i * 10000U)).available() == (i == 6));
  }
}

void handles_gaps_and_power_steps() {
  Estimator estimator;
  warm(estimator);
  assert(!estimator.observe(sample(73000U)).available());
  for (int i = 1; i <= 5; ++i) {
    assert(estimator.observe(sample(73000U + i * 10000U)).available() == (i == 5));
  }

  for (int i = 1; i <= 6; ++i) {
    auto step = sample(123000U + i * 10000U);
    step.feedback.value = 350.0f;
    const auto estimate = estimator.observe(step);
    assert(estimate.available() == (i == 6));
    if (i == 6) expect_estimate(estimate, 40.150232f);
  }
}

}  // namespace

int main() {
  const auto zero = Estimate::zero();
  assert(zero.available() && zero.state == EstimateState::CONFIRMED_ZERO && zero.watts == 0.0f);
  assert(!Estimate{}.available());
  lease_expiry_requires_six_new_polls();
  changing_poll_phase_preserves_the_estimate();
  does_not_count_retries_as_independent_polls();
  context_change_restarts_the_window([](auto& in) { ++in.flow.generation; });
  context_change_restarts_the_window([](auto& in) { ++in.flow.route; });
  context_change_restarts_the_window([](auto& in) { in.flow.contributors = 2U; });
  context_change_restarts_the_window([](auto& in) { in.flow_target_lph = in.flow.receipt.value = 850.0f; });
  rejects_bad_sample([](auto& in) { in.active = false; });
  rejects_bad_sample([](auto& in) { in.online = false; });
  rejects_bad_sample([](auto& in) { in.variant = oq_odu::Variant::V2_NEW_MODEL; });
  rejects_bad_sample([](auto& in) { in.status.received_ms = in.now_ms - kLeaseMs - 1; });
  rejects_bad_sample([](auto& in) { in.flow.receipt.received_ms = in.now_ms - kLeaseMs - 1; });
  rejects_bad_sample([](auto& in) { in.feedback.received_ms = in.now_ms + 1; });
  rejects_bad_sample([](auto& in) { in.invalidated_ms = in.now_ms - 50; });
  rejects_bad_sample([](auto& in) { in.status.value = 0.0f; });
  rejects_bad_sample([](auto& in) { in.status.value = 0x0804U; });
  rejects_bad_sample([](auto& in) { in.status.value = 0x0808U; });
  rejects_bad_sample([](auto& in) { in.feedback.value = 40.0f; });
  rejects_bad_sample([](auto& in) { in.feedback.value = 300.5f; });
  rejects_bad_sample([](auto& in) { in.flow.receipt.value = 841.0f; });
  rejects_bad_sample([](auto& in) { in.flow.receipt.invalidate(); });
  rejects_bad_sample([](auto& in) {
    in.feedback.received_ms = 59900U;
    in.feedback.value = 310.0f;
  });
  overwritten_flow_failure_revokes_the_estimate(NAN, true);
  overwritten_flow_failure_revokes_the_estimate(841.0f, true);
  overwritten_flow_failure_revokes_the_estimate(800.0f, false);
  register_gap_between_ticks_restarts_the_window(true);
  register_gap_between_ticks_restarts_the_window(false);
  overwritten_feedback_excursion_restarts_the_window();
  handles_gaps_and_power_steps();
}
