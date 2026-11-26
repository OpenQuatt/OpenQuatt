#include <cassert>
#include <cmath>
#include <initializer_list>

#include "openquatt/includes/control/oq_power_house_demand_logic.h"
#include "openquatt/includes/control/oq_power_house_dispatch_logic.h"
#include "openquatt/includes/control/oq_supervisory_power_limiter_logic.h"
#include "openquatt/includes/control/oq_supervisory_state_logic.h"

namespace {
using namespace oq_supervisory_power;

void test_continuous_request_and_start_boundary() {
  // Recorded installation: 7020 W reference, approximately 2161 W Pmin.
  const oq_power_house::DemandInput source{60000, 7.9f, -10, 16, 7020, 20.02f, 20, NAN, 1, false};
  const oq_power_house::DemandTuning tuning{0, 3000, 0, 0.1f, 5, 2};
  const auto house = oq_power_house::decide_demand(source, tuning, {});
  assert(house.valid && std::fabs(house.requested_w - 2187.0f) < 0.01f);
  for (float reference : {1000.0f, 7020.0f, 12000.0f})
    for (float watts : {0.0f, 0.1f, 100.0f, 2106.0f, 2161.437f, 2162.7f, 2187.0f, 2281.49f, 8370.0f}) {
      const float effective = limit_request_w(watts, request_limit_w(reference, 20, 20));
      assert(effective == watts);
      oq_supervisory_state::LowLoadInput input;
      input.now_ms = 60000;
      input.active = input.raw_heating_request = input.openquatt_enabled = true;
      input.requested_power_w = effective;
      input.live_minimum_power_w = 2161.437f;
      const auto gate = oq_supervisory_state::update_low_load(input, {});
      assert(gate.heating_request == (watts >= 2161.437f));
      const auto pending = oq_supervisory_state::power_house_start(60000, gate.heating_request, true, false, 30000, {});
      assert(!pending.heating_request && pending.preflow_request == gate.heating_request);
      assert(oq_supervisory_state::power_house_start(90000, gate.heating_request, true, false, 30000, pending.state)
                 .heating_request == gate.heating_request);
    }
  // A non-binding electrical reduction also leaves the exact request intact.
  assert(limit_request_w(house.requested_w, request_limit_w(7020, 19, 20)) == house.requested_w);
  assert(!request_limit_active(house.requested_w, request_limit_w(7020, 19, 20)));
}

void test_electrical_limits_and_failure_boundaries() {
  for (int cap = 0; cap <= 20; ++cap) {
    const float ceiling = request_limit_w(7020, cap, 20);
    for (float request : {0.0f, 0.1f, 2187.0f, 8370.0f}) {
      const float effective = limit_request_w(request, ceiling);
      assert(effective >= 0.0f && effective <= request && effective <= ceiling);
      assert(request_limit_active(request, ceiling) == (request > ceiling));
    }
  }
  assert(limit_request_w(2187, request_limit_w(7020, 6, 20)) == 2106.0f);
  assert(limit_request_w(2187, request_limit_w(7020, 0, 20)) == 0.0f);
  for (float bad : {NAN, INFINITY, -INFINITY, 0.0f, -1.0f}) assert(std::isnan(request_limit_w(bad, 20, 20)));
  assert(std::isnan(request_limit_w(7020, 20, 0)));
  for (float bad : {NAN, INFINITY, -INFINITY, -1.0f}) assert(std::isnan(limit_request_w(bad, INFINITY)));
  for (float bad : {NAN, -INFINITY, -1.0f}) {
    assert(std::isnan(limit_request_w(2187, bad)));
    assert(request_limit_active(2187, bad));
  }
  // Existing measurement fallback and recovery feed the same watt ceiling.
  Config config{true, 5, 15, 300, 60, 60000, 16, 20, 3400, 3650, 3300};
  Input measured{1000, {true, true, true, 1000, 1000}, {true, true, true, 1000, 1000}, true, 3700};
  State state{20, 0, 0, 0};
  for (int tick = 0; tick < 3; ++tick) state = step(measured, config, state).state;
  assert(state.cap_f == 18 && request_limit_w(7020, state.cap_f, 20) < 7020);
  measured.now_ms = 61001;
  const auto stale = step(measured, config, state);
  assert(!stale.measurement_valid && stale.state.cap_f == 16);
  assert(request_limit_w(7020, stale.state.cap_f, 20) == 5616.0f);
  measured.now_ms = measured.hp1.voltage_updated_ms = measured.hp1.current_updated_ms = 62000;
  measured.hp2 = measured.hp1;
  measured.total_power_w = 3000;
  state = stale.state;
  for (int tick = 0; tick < 12; ++tick) state = step(measured, config, state).state;
  assert(state.cap_f == 17);
}

void test_dispatch_uses_watts_and_keeps_electrical_and_incident_guards() {
  using namespace oq_power_house_dispatch;
  for (bool duo : {false, true}) {
    DispatchInput in;
    in.requested_w = 2187;
    in.duo = duo;
    in.performance_valid = true;
    in.hp1.candidate.available_for_start = in.hp2.candidate.available_for_start = true;
    in.hp1.candidate.minimum_off_ready = in.hp2.candidate.minimum_off_ready = true;
    for (auto* hp : {&in.hp1, &in.hp2}) {
      hp->levels[0] = {true, true, true, 0, 0};
      hp->levels[1] = {true, true, true, 2161.437f, 450};
    }
    DispatchTuning limits{3400, 3650};
    auto out = decide_dispatch(in, limits, {});
    assert(out.output_valid && topology(out.hp1_level, out.hp2_level) == 1);
    assert(out.expected_w == 2161.437f);
    limits.soft_limit_w = limits.peak_limit_w = 449;
    out = decide_dispatch(in, limits, {});
    assert(out.hp1_level == 0 && out.hp2_level == 0);
    limits = {3400, 3650};
    in.hp1.candidate.must_stop = in.hp2.candidate.must_stop = true;
    out = decide_dispatch(in, limits, {});
    assert(out.hp1_level == 0 && out.hp2_level == 0);
    in.hp1.candidate.must_stop = in.hp2.candidate.must_stop = false;
    in.hp1.candidate.minimum_off_ready = in.hp2.candidate.minimum_off_ready = false;
    out = decide_dispatch(in, limits, {});
    assert(out.hp1_level == 0 && out.hp2_level == 0);
    in.hp1.candidate.minimum_off_ready = in.hp2.candidate.minimum_off_ready = true;
    for (float stop : {0.0f, NAN, INFINITY, -1.0f}) {
      in.requested_w = stop;
      out = decide_dispatch(in, limits, {});
      assert(out.hp1_level == 0 && out.hp2_level == 0);
    }
  }
}

void test_defrost_threshold_preserves_recovery_and_electrical_boundaries() {
  using namespace oq_power_house_dispatch;
  DispatchInput in;
  in.requested_w = 4300;
  in.duo = in.performance_valid = true;
  for (auto* hp : {&in.hp1, &in.hp2}) {
    hp->candidate = {1, true, false, false};
    hp->stop_inhibited = true;
    hp->levels[0] = {true, true, true, 0, 0};
    hp->levels[1] = {true, true, true, 2528, 450, 30};
  }
  in.hp1.valve_defrost = true;
  in.hp1.levels[1].thermal_w *= 0.55f;
  in.hp2.levels[2] = {true, true, true, 3428, 650, 39};
  DispatchTuning limits{3400, 3650, 5, 150, 450, 4675, 1};
  auto out = decide_dispatch(in, limits, {});
  assert(out.hp1_level == 1 && out.hp2_level == 1 && out.reason != Reason::DEFROST_BOOST);

  // Recovery previously rounded upward: 4300 / 17000 * 20 -> 6.
  // Its boundary is strictly above 4250 W, rather than the normal 4675 W.
  limits.defrost_boost_min_w = 4250;
  limits.defrost_boost_min_exclusive = true;
  out = decide_dispatch(in, limits, {});
  assert(out.hp1_level == 1 && out.hp2_level == 2 && out.reason == Reason::DEFROST_BOOST);
  assert(std::fabs(out.expected_w - 4818.4f) < 0.01f);
  in.requested_w = limit_request_w(4300, request_limit_w(17000, 5, 20));
  assert(in.requested_w == limits.defrost_boost_min_w);
  assert(decide_dispatch(in, limits, {}).reason != Reason::DEFROST_BOOST);
  in.requested_w = limit_request_w(4300, request_limit_w(17000, 6, 20));
  assert(decide_dispatch(in, limits, {}).reason == Reason::DEFROST_BOOST);
  assert(!defrost_boost_demand_allowed(4250, limits));
  assert(defrost_boost_demand_allowed(std::nextafter(4250.0f, INFINITY), limits));
  for (float bad : {NAN, INFINITY, -INFINITY, -1.0f, 0.0f}) assert(!defrost_boost_demand_allowed(bad, limits));
  for (float bad : {NAN, INFINITY, -INFINITY, -1.0f}) {
    limits.defrost_boost_min_w = bad;
    assert(!defrost_boost_demand_allowed(4300, limits));
  }

  // Compare the old gate, including electrical caps, on both floor paths
  // and ordinary requests. The request itself remains unrounded throughout.
  for (float reference : {6400.0f, 17000.0f})
    for (int minimum : {1, 6, 20})
      for (bool floor : {false, true}) {
        const float boundary = reference * ((minimum - (floor ? 1.0f : 0.5f)) / 20.0f);
        limits.defrost_boost_min_w = boundary;
        limits.defrost_boost_min_exclusive = floor;
        for (float request : {0.0f, 1.0f, boundary, boundary + 1.0f, boundary - 1.0f, reference * 1.2f})
          for (int cap = 0; cap <= 20; ++cap) {
            if (request < 0) continue;
            const float scaled = 20.0f * (request / reference);
            const int legacy = std::min(cap, static_cast<int>(floor ? std::ceil(scaled) : std::round(scaled)));
            const bool expected = request > 0 && legacy >= minimum;
            const float effective = limit_request_w(request, request_limit_w(reference, cap, 20));
            assert(defrost_boost_demand_allowed(effective, limits) == expected);
          }
      }
}
}  // namespace

int main() {
  test_continuous_request_and_start_boundary();
  test_electrical_limits_and_failure_boundaries();
  test_dispatch_uses_watts_and_keeps_electrical_and_incident_guards();
  test_defrost_threshold_preserves_recovery_and_electrical_boundaries();
}
