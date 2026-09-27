#include <assert.h>
#include <float.h>
#include <math.h>
#include <initializer_list>

#include "../../openquatt/includes/control/oq_power_house_demand_logic.h"

namespace {
using namespace oq_power_house;
bool near(float a, float b) { return fabsf(a - b) < 0.002f; }

void equivalent_reference_points() {
  const auto first = house_line_from_legacy(-10.0f, 16.0f, 7020.0f);
  const auto second = house_line_from_legacy(-5.0f, 16.0f, 5670.0f);
  for (float outside : {-20.0f, -10.0f, -7.0f, 5.0f, 16.0f, 20.0f})
    assert(near(house_line_power_w(first, outside), house_line_power_w(second, outside)));
  assert(near(house_line_power_w(first, -7.0f), 6210.0f));
  assert(near(house_line_reference_power_w(first, -5.0f), 5670.0f));
  assert(near(modelled_house_power_w(16.0f, -5.0f, -7.0f, 5670.0f), 5670.0f));
}

void line_does_not_own_envelope() {
  const DemandTuning tuning{0.5f, 3000.0f, 0.1f, 0.3f, 10.0f, 5.0f, 20};
  DemandInput in{60001U, -10.0f, -10.0f, 16.0f, 6000.0f, 18.0f, 20.0f, NAN, 1.0f, false};
  const auto envelope = house_envelope_from_legacy(6000.0f);
  // Both lines retain the full warm-up request and exactly the same slew scale.
  for (const HouseLine line : {HouseLine{100.0f, 16.0f}, HouseLine{270.0f, 16.0f}}) {
    const auto start = decide_demand_with_line(in, tuning, {}, line, envelope);
    assert(start.valid && near(start.requested_w, 6000.0f) && start.raw_demand == 20);
    const auto ramp = decide_demand_with_line(in, tuning, {0.0f, 1U, 0.0f}, line, envelope);
    assert(ramp.valid && near(ramp.requested_w, 600.0f) && ramp.raw_demand == 2);
  }
  in.room_c = 20.0f;
  in.external_valid = true;
  in.external_w = 9000.0f;
  const auto external = decide_demand_with_line(in, tuning, {}, {100.0f, 16.0f}, envelope);
  assert(external.external && near(external.requested_w, 6000.0f));
  assert(near(external.contributions.modelled_base_w, 2600.0f));
  assert(near(external.contributions.selected_feedforward_w, 6000.0f));
  assert(external.contributions.adaptive_w == 0.0f);
  assert(external.contributions.room_feedback_w == 0.0f);
  // Each envelope field has its own meaning, including scale/cap ratios > 1.
  const auto separate =
      decide_demand_with_line(in, tuning, {0.0f, 1U, 0.0f}, {100.0f, 16.0f}, {5000.0f, 2000.0f, 3000.0f});
  assert(separate.valid && near(separate.requested_w, 300.0f) && separate.raw_demand == 3);
  const auto saturated = decide_demand_with_line(in, tuning, {}, {100.0f, 16.0f}, {5000.0f, FLT_MIN, 3000.0f});
  assert(saturated.valid && saturated.raw_demand == 20);
  in.water_limit_factor = 0.5f;
  const auto water = decide_demand_with_line(in, tuning, {}, {100.0f, 16.0f}, envelope);
  assert(near(water.requested_w, 3000.0f) && water.raw_demand == 10);
}

void invalid_models_fail_closed() {
  for (float bad : {NAN, INFINITY, -INFINITY, 0.0f, -1.0f}) {
    assert(!valid_house_line(house_line_from_legacy(-10.0f, 16.0f, bad)));
    assert(!valid_house_envelope(house_envelope_from_legacy(bad)));
  }
  assert(!valid_house_line(house_line_from_legacy(16.0f, 16.0f, 6000.0f)));
  assert(!valid_house_line(house_line_from_legacy(16.0f, 16.5f, 6000.0f, 0.5f)));
  assert(!valid_house_line(house_line_from_legacy(-10.0f, 16.0f, 6000.0f, -0.5f)));
  assert(isnan(house_line_power_w({FLT_MAX, 16.0f}, -FLT_MAX)));
  assert(isnan(house_line_reference_power_w({200.0f, 16.0f}, 16.0f)));
  const DemandInput in{1U, 3.0f, -10.0f, 16.0f, 6000.0f, 20.0f, 20.0f, 1000.0f, 1.0f, true};
  const DemandTuning tuning{0.5f, 3000.0f, 0.1f, 0.3f, 10.0f, 5.0f, 20};
  // External feedforward cannot make a corrupt house model valid.
  assert(!decide_demand_with_line(in, tuning, {}, {}, house_envelope_from_legacy(6000.0f)).valid);
  assert(!decide_demand_with_line(in, tuning, {}, {200.0f, 16.0f}, {6000.0f, NAN, 6000.0f}).valid);
}
}  // namespace

int main() {
  equivalent_reference_points();
  line_does_not_own_envelope();
  invalid_models_fail_closed();
}
