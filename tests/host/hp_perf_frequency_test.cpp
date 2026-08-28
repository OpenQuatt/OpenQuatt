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

  hp_generation.has_state_ = false;
  assert(oq_perf::model_frequency_hz(1) == 30.0f);
  return 0;
}
