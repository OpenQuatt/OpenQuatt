#include <assert.h>

#include <cmath>
#include <string>

class FakeGenerationSelect {
 public:
  bool has_state() const { return has_state_; }
  const std::string& current_option() const { return option_; }

  bool has_state_{true};
  std::string option_{"V1.5"};
};

FakeGenerationSelect hp_generation;

struct FakeFrequencyContext {
  int hp1_hz{43};
  int hp2_hz{48};
  bool allowed{true};

  int automatic_frequency_hz(bool hp1, int, int) const { return hp1 ? hp1_hz : hp2_hz; }
  bool frequency_allowed(bool, int, int) const { return allowed; }
};

#define id(value) value
#include "../../openquatt/includes/performance/hp_perf_frequency.h"
#undef id

namespace {

void assert_equal_or_nan(float actual, float expected) {
  if (std::isnan(expected)) {
    assert(std::isnan(actual));
    return;
  }
  assert(std::fabs(actual - expected) < 0.01f);
}

void assert_anchor_compatibility(const std::string& generation) {
  hp_generation.option_ = generation;
  const bool v2 = generation == "V2";
  for (int level = 1; level <= 10; ++level) {
    const float frequency_hz = oq_perf::model_frequency_hz(level);
    assert(frequency_hz == oq_perf::model_frequency_hz(v2, level));
    for (const float ambient : {-15.0f, -7.0f, 2.0f, 7.0f, 12.0f}) {
      for (const float supply : {35.0f, 45.0f, 55.0f, 65.0f}) {
        assert_equal_or_nan(oq_perf::interp_power_th_w_hz(frequency_hz, ambient, supply),
                            oq_perf::interp_power_th_w(level, ambient, supply));
        assert_equal_or_nan(oq_perf::interp_cop_hz(frequency_hz, ambient, supply),
                            oq_perf::interp_cop(level, ambient, supply));
        assert_equal_or_nan(oq_perf::interp_power_el_w_hz(frequency_hz, ambient, supply),
                            oq_perf::interp_power_el_w(level, ambient, supply));
      }
    }
  }
}

}  // namespace

int main() {
  constexpr std::array<float, 10> expected_v1 = {30, 39, 49, 55, 61, 67, 72, 79, 85, 90};
  constexpr std::array<float, 10> expected_v2 = {20, 26, 30, 48, 55, 61, 72, 80, 85, 90};
  assert(oq_perf::V1_HEATING_FREQUENCIES_HZ == expected_v1);
  assert(oq_perf::V2_HEATING_FREQUENCIES_HZ == expected_v2);
  assert(oq_perf::model_frequency_hz(false, 0) == 0.0f);
  assert(oq_perf::model_frequency_hz(false, 11) == 90.0f);
  assert(oq_perf::model_frequency_hz(true, 11) == 90.0f);

  assert_anchor_compatibility("V1.5");
  assert_anchor_compatibility("V2");

  hp_generation.option_ = "V2";
  const float lower = oq_perf::interp_power_th_w(4, 2.0f, 45.0f);
  const float upper = oq_perf::interp_power_th_w(5, 2.0f, 45.0f);
  assert_equal_or_nan(oq_perf::interp_power_th_w_hz(51.5f, 2.0f, 45.0f), lower + (upper - lower) * 0.5f);
  assert(std::isnan(oq_perf::interp_power_th_w_hz(19.0f, 2.0f, 45.0f)));
  assert(std::isnan(oq_perf::interp_power_th_w_hz(91.0f, 2.0f, 45.0f)));
  assert(oq_perf::interp_power_th_w_hz(0.0f, 2.0f, 45.0f) == 0.0f);

  const auto issue_point = oq_perf::predict_heating_hz(20.0f, 12.6f, 22.5f);
  assert(issue_point.available);
  assert(std::fabs(issue_point.pth_w - 3072.6185f) < 0.02f);
  assert(std::fabs(issue_point.pth_w / issue_point.cop - issue_point.pel_w) < 0.01f);

  FakeFrequencyContext frequency;
  const auto hp1_slot_four =
      oq_perf::predict_candidate(frequency, oq_odu::Variant::V2_OLD_MODEL, true, 4, 12.6f, 22.5f);
  const auto hp2_slot_four =
      oq_perf::predict_candidate(frequency, oq_odu::Variant::V2_NEW_MODEL, false, 4, 12.6f, 22.5f);
  assert(hp1_slot_four.usable_for_running_optimization());
  assert(hp2_slot_four.usable_for_running_optimization());
  assert(hp1_slot_four.runtime_frequency_hz == 43);
  assert(hp2_slot_four.runtime_frequency_hz == 48);
  assert(hp1_slot_four.performance.pth_w != hp2_slot_four.performance.pth_w);
  frequency.hp1_hz = -1;
  assert(!oq_perf::predict_candidate(frequency, oq_odu::Variant::V2_OLD_MODEL, true, 4, 12.6f, 22.5f)
              .runtime_frequency_known);
  frequency.hp1_hz = 95;
  assert(!oq_perf::predict_candidate(frequency, oq_odu::Variant::V2_OLD_MODEL, true, 4, 12.6f, 22.5f)
              .usable_for_running_optimization());
  frequency.hp1_hz = 43;
  frequency.allowed = false;
  const auto policy_blocked =
      oq_perf::predict_candidate(frequency, oq_odu::Variant::V2_OLD_MODEL, true, 4, 12.6f, 22.5f);
  assert(policy_blocked.runtime_frequency_known);
  assert(policy_blocked.performance.available);
  assert(!policy_blocked.frequency_policy_allowed);
  assert(!policy_blocked.usable_for_running_optimization());
  assert(
      !oq_perf::predict_candidate(frequency, oq_odu::Variant::UNKNOWN, true, 4, 12.6f, 22.5f).runtime_frequency_known);

  frequency.allowed = true;
  const auto confirmed_v1 = oq_perf::predict_candidate(frequency, oq_odu::Variant::V1_5, true, 4, 12.6f, 22.5f);
  assert(confirmed_v1.runtime_frequency_known);
  assert(confirmed_v1.performance.available);

  frequency.hp1_hz = 20;
  const auto unguarded_cold =
      oq_perf::predict_candidate(frequency, oq_odu::Variant::V2_OLD_MODEL, true, 1, 12.6f, 12.0f);
  assert(!unguarded_cold.performance.available);
  assert(!unguarded_cold.performance.low_supply_boundary_estimate);
  const auto guarded_cold =
      oq_perf::predict_candidate(frequency, oq_odu::Variant::V2_OLD_MODEL, true, 1, 12.6f, 12.0f, true);
  assert(guarded_cold.usable_for_running_optimization());
  assert(guarded_cold.performance.low_supply_boundary_estimate);
  assert_equal_or_nan(guarded_cold.performance.pth_w,
                      oq_perf::predict_heating_hz(oq_odu::Variant::V2_OLD_MODEL, 20.0f, 12.6f, 18.0f).pth_w);
  assert(!oq_perf::predict_heating_hz(oq_odu::Variant::V1_5, 43.0f, 12.6f, 12.0f, true).low_supply_boundary_estimate);

  hp_generation.has_state_ = false;
  assert(oq_perf::model_frequency_hz(1) == 30.0f);
  return 0;
}
