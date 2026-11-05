#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "openquatt/includes/control/oq_ph_shadow_demand_logic.h"
#include "openquatt/includes/control/oq_ph_learning_json.h"

using namespace oq_power_house;

namespace {
DemandEvaluationFrame frame() {
  DemandEvaluationFrame f;
  f.monotonic_ms = 100000;
  f.cadence_ms = 30000;
  f.input = {100000, 0, -10, 15, 3750, 19.8f, 20, NAN, 1, false};
  f.tuning = {0, 1500, 0.1f, 0.3f, 8, 3, 100};
  f.previous = {2900, 90000, 0.05f};
  f.envelope = house_envelope_from_legacy(3750);
  f.manual_w = decide_demand(f.input, f.tuning, f.previous).requested_w;
  f.reason = ShadowDemandReason::READY;
  return f;
}

learning::AdviceResult batch() {
  learning::AdviceResult b;
  b.status = learning::LearningStatus::ADVICE_READY;
  b.candidate_available = b.advice_ready = true;
  b.context_revision = 7;
  b.candidate = {220, 20};
  b.validated_temp_min_c = -5;
  b.validated_temp_max_c = 15;
  return b;
}

ShadowDemandResult evaluate(const DemandEvaluationFrame& f, const learning::AdviceResult& b, uint64_t now = 100000,
                            ShadowDemandReason gate = ShadowDemandReason::READY) {
  return evaluate_shadow_demand(f, b, {}, true, 7, now, gate);
}

void fail_closed_and_immutable() {
  auto f = frame();
  auto b = batch();
  const auto original_frame = f;
  const auto original_batch = b;
  const auto expected = decide_demand_with_line(f.input, f.tuning, f.previous, b.candidate, f.envelope);
  const auto result = evaluate(f, b);
  assert(result.available() && result.manual_w == f.manual_w && result.candidate_w == expected.requested_w);
  assert(result.delta_w == result.candidate_w - result.manual_w);
  assert(memcmp(&f, &original_frame, sizeof(f)) == 0 && memcmp(&b, &original_batch, sizeof(b)) == 0);
  for (unsigned reason = 1; reason <= static_cast<unsigned>(ShadowDemandReason::INVALID_CANDIDATE); ++reason) {
    const auto blocked = evaluate(f, b, f.monotonic_ms, static_cast<ShadowDemandReason>(reason));
    assert(!blocked.available() && !std::isfinite(blocked.manual_w) && !std::isfinite(blocked.candidate_w));
  }
  assert(evaluate(f, b, 129999).available());
  assert(evaluate(f, b, 130000).reason == ShadowDemandReason::STALE_FRAME);
  assert(evaluate(f, b, 99999).reason == ShadowDemandReason::STALE_FRAME);
  f.monotonic_ms = (1ULL << 32) + 100;
  assert(evaluate(f, b, f.monotonic_ms).available());
  f.cadence_ms = 0;
  assert(evaluate(f, b, f.monotonic_ms).reason == ShadowDemandReason::STALE_FRAME);
  f = frame();
  f.input.external_valid = true;
  f.input.external_w = 0.0f;
  assert(evaluate(f, b).reason == ShadowDemandReason::EXTERNAL_OVERRIDE);
  f = frame();
  b.advice_ready = false;
  assert(evaluate(f, b).reason == ShadowDemandReason::BATCH_NOT_READY);
  b = batch();
  b.status = learning::LearningStatus::FIT_IN_PROGRESS;
  assert(evaluate(f, b).reason == ShadowDemandReason::BATCH_NOT_READY);
  b = batch();
  b.context_revision = 8;
  assert(evaluate(f, b).reason == ShadowDemandReason::CONTEXT_MISMATCH);
  b = batch();
  b.candidate.heat_loss_w_per_k = 8000;  // Thermal C must never masquerade as batch H.
  assert(evaluate(f, b).reason == ShadowDemandReason::INVALID_CANDIDATE);
  b = batch();
  for (const float outside : {-5.0f, 15.0f}) {
    f.input.outside_c = outside;
    assert(evaluate(f, b).available());
  }
  f.input.outside_c = std::nextafter(-5.0f, -INFINITY);
  assert(evaluate(f, b).reason == ShadowDemandReason::OUTSIDE_RANGE);
  f.input.outside_c = std::nextafter(15.0f, INFINITY);
  assert(evaluate(f, b).reason == ShadowDemandReason::OUTSIDE_RANGE);
  f = frame();
  f.input.room_c = NAN;
  assert(evaluate(f, b).reason == ShadowDemandReason::INVALID_INPUTS);
}

void external_override_matches_actual_feedforward_selection() {
  const auto model_frame = frame();
  const auto candidate = batch();
  const auto model_demand = decide_demand(model_frame.input, model_frame.tuning, model_frame.previous);
  const auto model_shadow = evaluate(model_frame, candidate);
  assert(model_demand.valid && !model_demand.external && model_shadow.available());

  // A received disabled/stale value can retain Sensor.has_state() while its
  // numeric state is non-finite. Actual control then uses the house model.
  for (float value : {NAN, INFINITY, -INFINITY}) {
    auto f = frame();
    f.input.external_valid = true;
    f.input.external_w = value;
    const auto actual = decide_demand(f.input, f.tuning, f.previous);
    assert(actual.valid && !actual.external && actual.requested_w == model_demand.requested_w);
    const auto shadow = evaluate(f, candidate);
    assert(shadow.available() && shadow.manual_w == model_shadow.manual_w);
    assert(shadow.candidate_w == model_shadow.candidate_w && shadow.delta_w == model_shadow.delta_w);
    auto not_ready = candidate;
    not_ready.advice_ready = false;
    assert(evaluate(f, not_ready).reason == ShadowDemandReason::BATCH_NOT_READY);
  }

  // Explicit API zero is still an override; negative finite input is likewise
  // selected by control and clamped to zero, rather than selecting the model.
  for (float value : {0.0f, 1250.0f, -500.0f}) {
    auto f = frame();
    f.input.external_valid = true;
    f.input.external_w = value;
    const auto actual = decide_demand(f.input, f.tuning, f.previous);
    assert(actual.valid && actual.external);
    const auto shadow = evaluate(f, candidate);
    assert(!shadow.available() && shadow.reason == ShadowDemandReason::EXTERNAL_OVERRIDE);
    assert(!std::isfinite(shadow.manual_w) && !std::isfinite(shadow.candidate_w) && !std::isfinite(shadow.delta_w));
    f.input.external_valid = false;
    assert(!decide_demand(f.input, f.tuning, f.previous).external);
    assert(evaluate(f, candidate).available());
  }
}

void same_limits_and_no_future_history() {
  auto f = frame();
  auto b = batch();
  f.previous = {};  // First request: no artificial inherited 0 W slew origin.
  f.input.outside_c = -5;
  f.input.water_limit_factor = 0.5f;
  f.manual_w = decide_demand(f.input, f.tuning, f.previous).requested_w;
  const auto limited = evaluate(f, b);
  assert(limited.available());
  assert(limited.candidate_w > 0 && limited.candidate_w < f.input.rated_w);
  f.input.water_limit_factor = 1;
  f.manual_w = decide_demand(f.input, f.tuning, f.previous).requested_w;
  const auto full = evaluate(f, b);
  assert(full.candidate_w == limited.candidate_w * 2 && full.candidate_w > f.input.rated_w);
  const auto repeated = evaluate(f, b);
  assert(repeated.candidate_w == full.candidate_w);  // No shadow state advances.
}

void bounded_json_never_serializes_a_blocked_value() {
  char buffer[kMaxShadowDemandJsonBytes];
  ShadowDemandResult result;
  result.tick_ms = UINT64_MAX;
  result.context_revision = UINT32_MAX;
  result.reason = ShadowDemandReason::READY;
  result.manual_w = result.candidate_w = result.delta_w = -std::numeric_limits<float>::max();
  oq_ph_learning::JsonWriter writer(buffer, sizeof(buffer));
  write_shadow_demand_json(writer, result);
  assert(writer.ok() && writer.size() + 1 < sizeof(buffer));
  assert(strstr(buffer, "18446744073709551615") && strstr(buffer, "\"run_id\":null"));
  size_t maximum = writer.size();
  for (unsigned reason = 1; reason <= static_cast<unsigned>(ShadowDemandReason::INVALID_CANDIDATE); ++reason) {
    result.reason = static_cast<ShadowDemandReason>(reason);
    oq_ph_learning::JsonWriter blocked(buffer, sizeof(buffer));
    write_shadow_demand_json(blocked, result);
    assert(blocked.ok() && blocked.size() < sizeof(buffer));
    assert(strstr(buffer, "\"manual_w\":null,\"candidate_w\":null,\"delta_w\":null"));
    assert(strstr(buffer, "\"auto_apply_allowed\":false"));
    maximum = std::max(maximum, blocked.size());
  }
  std::printf("shadow_frame_bytes=%zu shadow_result_bytes=%zu shadow_json_max_tested=%zu\n",
              sizeof(DemandEvaluationFrame), sizeof(ShadowDemandResult), maximum);
}
}  // namespace

int main() {
  fail_closed_and_immutable();
  external_override_matches_actual_feedforward_selection();
  same_limits_and_no_future_history();
  bounded_json_never_serializes_a_blocked_value();
}
