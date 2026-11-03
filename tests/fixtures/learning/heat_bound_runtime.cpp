#include <cassert>
#include <cmath>
#include <string>

struct FakeGenerationSelect {
  bool present = false;
  std::string option;
  bool has_state() const { return present; }
  std::string current_option() const { return option; }
} hp_generation;
#define id(value) value
#include "../../../openquatt/includes/performance/hp_perf_limits.h"
#define OQ_PH_LEARNING_HOST_TEST 1
#include "../../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

using namespace oq_power_house::learning;
struct RuntimeStorage {
  PassiveRuntimeConfig config;
  PassiveRuntimeStorage learner;
};
struct Production {
#include "production_daily_heat_bound.inc"
};

int main() {
  RuntimeStorage state;
  state.learner.initialized = true;
  state.learner.record_count = 17;
  state.learner.thermal_state.accepted_samples = 1516;
  state.learner.batch_accumulator.missing_energy_uncertainty_ws = 123456.0;
  state.config.quality.max_abs_heat_w = state.learner.config.quality.max_abs_heat_w = 50000.0f;
  for (const auto option : {"V1", "V2", "V1.5", "future", "", "V2"}) {
    hp_generation.present = true;
    hp_generation.option = option;
    Production::update_daily_heat_bound_(state);
    const float single = hp_generation.option == "V1" || hp_generation.option == "V1.5" ? 6115.28f : 9172.15f;
    const float expected = single * (OQ_TOPOLOGY_DUO ? 2.0f : 1.0f);
    assert(std::fabs(state.config.quality.max_daily_missing_heat_w - expected) < 0.001f);
    assert(state.learner.config.quality.max_daily_missing_heat_w == state.config.quality.max_daily_missing_heat_w);
    const float limit = 1.20f * expected;
    assert(std::fabs(configured_measurement_heat_limit_w(state.config.quality) - limit) < 0.003f);
    assert(state.learner.config.quality.max_observed_heat_w == state.config.quality.max_observed_heat_w);
    assert(valid_quality_config(state.config.quality) && valid_quality_config(state.learner.config.quality));
    assert(state.learner.record_count == 17 && state.learner.thermal_state.accepted_samples == 1516);
    assert(state.learner.batch_accumulator.missing_energy_uncertainty_ws == 123456.0);
    assert(state.config.quality.max_abs_heat_w == 50000.0f);
    assert(state.learner.config.quality.max_abs_heat_w == 50000.0f);
  }
  hp_generation.present = false;
  Production::update_daily_heat_bound_(state);
  const float fallback = 9172.15f * (OQ_TOPOLOGY_DUO ? 2.0f : 1.0f);
  assert(configured_daily_missing_heat_bound_w(state.config.quality) == fallback);
  assert(configured_daily_missing_heat_bound_w(state.learner.config.quality) == fallback);
  assert(std::fabs(configured_measurement_heat_limit_w(state.config.quality) - 1.20f * fallback) < 0.003f);
}
