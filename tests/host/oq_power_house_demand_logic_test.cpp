#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/control/oq_power_house_demand_logic.h"

namespace {
using namespace oq_power_house;
bool near(float actual, float expected) { return fabsf(actual - expected) < 0.01f; }
DemandInput input() { return {60001U, 3.0f, -10.0f, 16.0f, 6000.0f, 20.0f, 20.0f, NAN, 1.0f, false}; }
DemandTuning tuning() { return {0.5f, 3000.0f, 0.1f, 0.3f, 10.0f, 5.0f, 20}; }
void test_model_and_feedforward() {
  struct ModelCase {
    float outside_c, expected_w;
  };
  const ModelCase models[] = {{21.0f, 0.0f}, {16.0f, 0.0f}, {3.0f, 3000.0f}, {-10.0f, 6000.0f}, {-20.0f, 6000.0f}};
  for (const auto& test : models)
    assert(near(modelled_house_power_w(16.0f, -10.0f, test.outside_c, 6000.0f), test.expected_w));
  assert(isnan(modelled_house_power_w(16.0f, -10.0f, NAN, 6000.0f)));
  assert(isnan(modelled_house_power_w(16.0f, -10.0f, 3.0f, 0.0f)));
  assert(isnan(modelled_house_power_w(16.0f, 16.0f, 3.0f, 6000.0f)));
  assert(isnan(modelled_house_power_w(-10.0f, 16.0f, 3.0f, 6000.0f)));
  struct FeedCase {
    float external_w, rated_w, expected_w;
    bool present, external;
  };
  const FeedCase cases[] = {{3000, 6000, 3000, true, true}, {0, 6000, 0, true, true},
                            {-500, 6000, 0, true, true},    {6000, 6000, 6000, true, true},
                            {9000, 6000, 6000, true, true}, {3000, 6000, 2500, false, false},
                            {NAN, 6000, 2500, true, false}, {INFINITY, 6000, 2500, true, false},
                            {3000, 0, 2500, true, false},   {3000, NAN, 2500, true, false}};
  for (const auto& test : cases) {
    const auto result = select_feedforward(2500.0f, test.external_w, test.present, test.rated_w);
    assert(result.external == test.external && near(result.house_power_w, test.expected_w));
  }
}
void test_cache_and_cadence_rollover() {
  constexpr uint32_t hold_ms = 300000U;
  struct CacheCase {
    uint8_t cached, current;
    float watts;
    uint32_t cached_ms, now_ms, window_ms;
    bool expected;
  };
  const CacheCase cache[] = {
      {kDemandSourceHaInput, kDemandSourceHaInput, 3000, UINT32_MAX - 1000U, 1000U, hold_ms, true},
      {kDemandSourceApiInput, kDemandSourceHaInput, 3000, 1000U, 2000U, hold_ms, false},
      {kDemandSourceHaInput, kDemandSourceApiInput, 3000, 1000U, 2000U, hold_ms, false},
      {kDemandSourceNone, kDemandSourceHaInput, 3000, 1000U, 2000U, hold_ms, false},
      {kDemandSourceHaInput, kDemandSourceNone, 3000, 1000U, 2000U, hold_ms, false},
      {kDemandSourceHaInput, kDemandSourceHaInput, 3000, 0, 2000U, hold_ms, false},
      {kDemandSourceHaInput, kDemandSourceHaInput, 3000, 1000U, 1000U + hold_ms, hold_ms, false},
      {kDemandSourceHaInput, kDemandSourceHaInput, 3000, 1000U, 2000U, 0, false},
      {kDemandSourceHaInput, kDemandSourceHaInput, NAN, 1000U, 2000U, hold_ms, false}};
  for (const auto& test : cache)
    assert(hold_cached_demand(test.cached, test.current, test.watts, test.cached_ms, test.now_ms, test.window_ms) ==
           test.expected);
  struct Case {
    uint32_t now_ms, last_ms, target_ms;
    bool due;
    float dt_s;
  };
  const Case cases[] = {{4000, 0, 60000, true, 60},
                        {59999, 1, 60000, false, 0},
                        {60001, 1, 60000, true, 60},
                        {1000, UINT32_MAX - 1000U, 3000, false, 0},
                        {29999, UINT32_MAX - 30000U, 60000, true, 60},
                        {5000, UINT32_MAX, 60000, false, 0},
                        {60000, 1, 0, false, 0}};
  for (const auto& test : cases) {
    const auto result = decide_cadence(test.now_ms, test.last_ms, test.target_ms);
    assert(result.due == test.due && near(result.dt_s, test.dt_s));
  }
}
void test_demand_finite_contract() {
  const float bad[] = {NAN, INFINITY, -INFINITY};
  float DemandInput::* input_fields[] = {
      &DemandInput::outside_c, &DemandInput::cold_c,     &DemandInput::zero_power_c,      &DemandInput::rated_w,
      &DemandInput::room_c,    &DemandInput::setpoint_c, &DemandInput::water_limit_factor};
  float DemandTuning::* tuning_fields[] = {&DemandTuning::temperature_guard_c, &DemandTuning::reaction_w_per_k,
                                           &DemandTuning::comfort_below_c,     &DemandTuning::comfort_above_c,
                                           &DemandTuning::rise_time_min,       &DemandTuning::fall_time_min};
  for (float value : bad) {
    for (auto field : input_fields) {
      auto in = input();
      in.*field = value;
      const auto out = decide_demand(in, tuning(), {1000, 1000, 0.1f});
      assert(!out.valid && out.requested_w == 0 && out.raw_demand == 0 && out.next.last_w == 0);
      assert(out.next.last_ms == in.now_ms && out.next.comfort_memory_c == 0);
    }
    for (auto field : tuning_fields) {
      auto cfg = tuning();
      cfg.*field = value;
      assert(!decide_demand(input(), cfg, {}).valid);
    }
  }
  for (float value : bad) {
    auto in = input();
    in.external_valid = true;
    in.external_w = value;
    const auto out = decide_demand(in, tuning(), {});
    assert(out.valid && !out.external && near(out.requested_w, 3000));
  }
  auto invalid = input();
  invalid.room_c = NAN;
  const auto stopped = decide_demand(invalid, tuning(), {3000, 1U, 0.1f});
  assert(stopped.next.last_w == 0 && stopped.next.comfort_memory_c == 0 && stopped.next.last_ms == invalid.now_ms);
  auto recovered = input();
  recovered.now_ms = stopped.next.last_ms + 60000U;
  assert(near(decide_demand(recovered, tuning(), stopped.next).requested_w, 600.0f));
  assert(!decide_demand({60001U, 3, 10, 9, 6000, 20, 20, NAN, 1, false}, {-2, 3000, 0.1f, 0.3f, 10, 5, 20}, {}).valid);
  assert(!decide_demand({60001U, 3, -10, 16, 6000, -0x1.fffffep+127f, 0x1.fffffep+127f, NAN, 1, false},
                        {0.5f, 0, 0.1f, 0.3f, 10, 5, 20}, {})
              .valid);
}
void test_demand_slew_and_limits() {
  auto rise = input();
  rise.room_c = 18.0f;
  assert(near(decide_demand(rise, tuning(), {0, 1U, 0}).requested_w, 600.0f));
  auto fall = input();
  fall.room_c = 22.0f;
  assert(near(decide_demand(fall, tuning(), {6000, 1U, 0}).requested_w, 4800.0f));
  rise.now_ms = 29999U;
  assert(near(decide_demand(rise, tuning(), {0, UINT32_MAX - 30000U, 0}).requested_w, 600.0f));
  auto limited = input();
  limited.water_limit_factor = 0.5f;
  const auto out = decide_demand(limited, tuning(), {});
  assert(out.valid && near(out.requested_w, 1500.0f) && out.raw_demand == 5);
  const auto colder = decide_demand(rise, tuning(), {3000, 1U, 0});
  assert(colder.next.comfort_memory_c > 0.0f);
  auto warm = input();
  warm.room_c = 22.0f;
  assert(decide_demand(warm, tuning(), {3000, 1U, 0.1f}).next.comfort_memory_c < 0.1f);
}
void test_filter_table() {
  struct Case {
    int raw, current;
    float budget, step_min, dt_s;
    int expected;
    float expected_budget;
  };
  const Case cases[] = {{10, 2, 0, 1, 60, 3, 0},   {10, 2, 0, 1, 30, 2, 0.5f},     {10, 2, 0, 20, 60, 10, 0},
                        {4, 5, 0, 1, 60, 5, 0},    {3, 5, 0, 1, 60, 3, 0},         {0, 1, 0, 1, 60, 0, 0},
                        {10, 2, NAN, 1, 60, 2, 0}, {10, 2, 0, INFINITY, 60, 2, 0}, {10, 2, 0, 1, INFINITY, 2, 0}};
  for (const auto& test : cases) {
    const auto out = filter_demand(test.raw, test.current, test.budget, test.step_min, test.dt_s, 20);
    assert(out.previous == test.current && out.filtered == test.expected &&
           near(out.ramp_budget, test.expected_budget));
  }
}
}  // namespace
int main() {
  test_model_and_feedforward();
  test_cache_and_cadence_rollover();
  test_demand_finite_contract();
  test_demand_slew_and_limits();
  test_filter_table();
  return 0;
}
