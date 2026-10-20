#include <assert.h>
#include <math.h>

#define OQ_TOPOLOGY_DUO 1

struct Preference {
  float value{NAN};
  bool writable{true};
  unsigned saves{0};
  bool load(float* output) {
    if (!isfinite(value)) return false;
    *output = value;
    return true;
  }
  bool save(const float* input) {
    if (!writable) return false;
    value = *input;
    ++saves;
    return true;
  }
};

struct Sensor {
  float raw_state{0.0f};
  float state{0.0f};
  unsigned corrections{0};
  Preference preference;
  void publish_state(float value) {
    raw_state = value;
    state = value * 0.001f;
  }
  void publish_state_and_save(float value) {
    ++corrections;
    publish_state(value);
    preference.save(&value);
  }
  void set_value(float value) { publish_state_and_save(value); }
  template <typename T>
  Preference& make_entity_preference() {
    return preference;
  }
};

Sensor test_oq_electrical_energy_daily;
Sensor test_oq_electrical_energy_cumulative;
Sensor test_oq_heating_electrical_energy_daily;
Sensor test_oq_cooling_electrical_energy_daily;
Sensor test_oq_heating_electrical_energy_cumulative;
Sensor test_oq_cooling_electrical_energy_cumulative;

#define id(name) test_##name
#include "../../openquatt/includes/performance/oq_energy_accounting_runtime.h"
#undef id

int main() {
  auto& daily_checkpoint = oq_energy_accounting_runtime::daily_checkpoint();
  auto& cumulative_checkpoint = oq_energy_accounting_runtime::cumulative_checkpoint();
  // Upgrade: restore raw Wh, not the filtered kWh exposed to HA.
  daily_checkpoint.value = 310.0f;
  cumulative_checkpoint.value = 131000.0f;
  test_oq_heating_electrical_energy_daily.publish_state(275.0f);
  test_oq_heating_electrical_energy_cumulative.publish_state(100000.0f);
  test_oq_cooling_electrical_energy_cumulative.publish_state(1000.0f);
  assert(oq_energy_accounting_runtime::restore_totals());
  assert(test_oq_heating_electrical_energy_daily.raw_state == 310.0f);
  assert(test_oq_heating_electrical_energy_cumulative.raw_state == 130000.0f);
  assert(fabsf(test_oq_electrical_energy_cumulative.state - 131.0f) < 0.001f);
  assert(oq_energy_accounting_runtime::restore_totals());
  assert(test_oq_heating_electrical_energy_daily.corrections == 1);
  assert(test_oq_heating_electrical_energy_cumulative.corrections == 1);

  // A counter write lost on power failure is repaired again from the checkpoint.
  test_oq_heating_electrical_energy_daily.publish_state(275.0f);
  assert(oq_energy_accounting_runtime::restore_totals());
  assert(test_oq_heating_electrical_energy_daily.raw_state == 310.0f);
  // A stale checkpoint must never decrease an already-accounted counter.
  daily_checkpoint.value = 100.0f;
  test_oq_cooling_electrical_energy_daily.publish_state(50.0f);
  assert(oq_energy_accounting_runtime::restore_totals());
  assert(test_oq_electrical_energy_daily.raw_state == 360.0f);
  assert(oq_energy_accounting_runtime::publish_totals(20261020));
  assert(oq_energy_accounting_runtime::daily_totals_current(20261020));

  // Midnight and the user reset coalesce both source counters before publishing.
  test_oq_heating_electrical_energy_daily.publish_state(0.0f);
  assert(oq_energy_accounting_runtime::publish_totals(20261021));
  assert(!oq_energy_accounting_runtime::daily_totals_current(20261021));
  assert(test_oq_electrical_energy_daily.raw_state == 360.0f);
  test_oq_cooling_electrical_energy_daily.publish_state(0.0f);
  test_oq_heating_electrical_energy_cumulative.publish_state(0.0f);
  test_oq_cooling_electrical_energy_cumulative.publish_state(0.0f);
  assert(oq_energy_accounting_runtime::publish_totals(20261021));
  assert(test_oq_electrical_energy_daily.raw_state == 0.0f);
  assert(oq_energy_accounting_runtime::daily_totals_current(20261021));
  assert(test_oq_electrical_energy_cumulative.raw_state == 0.0f);
  assert(daily_checkpoint.value == 0.0f && cumulative_checkpoint.value == 0.0f);

  // Observe the actual reset callback before a larger next-day power sample.
  test_oq_heating_electrical_energy_daily.publish_state(1000.0f);
  test_oq_cooling_electrical_energy_daily.publish_state(0.1f);
  assert(oq_energy_accounting_runtime::publish_totals(20261021));
  test_oq_cooling_electrical_energy_daily.publish_state(0.0f);
  oq_energy_accounting_runtime::observe_daily_source(20261022, true);
  test_oq_cooling_electrical_energy_daily.publish_state(1.0f);
  oq_energy_accounting_runtime::observe_daily_source(20261022, true);
  assert(oq_energy_accounting_runtime::publish_totals(20261022));
  assert(!oq_energy_accounting_runtime::daily_totals_current(20261022));
  test_oq_heating_electrical_energy_daily.publish_state(0.0f);
  oq_energy_accounting_runtime::observe_daily_source(20261022, false);
  assert(oq_energy_accounting_runtime::publish_totals(20261022));
  assert(oq_energy_accounting_runtime::daily_totals_current(20261022));
  assert(test_oq_electrical_energy_daily.raw_state == 1.0f);

  // Missing source data does not overwrite a valid recovery checkpoint with zero.
  test_oq_cooling_electrical_energy_daily.publish_state(NAN);
  assert(oq_energy_accounting_runtime::publish_totals());
  assert(isnan(test_oq_electrical_energy_daily.raw_state));
  assert(daily_checkpoint.value == 1.0f);
  test_oq_cooling_electrical_energy_daily.publish_state(35.0f);
  daily_checkpoint.writable = false;
  assert(!oq_energy_accounting_runtime::publish_totals());
  assert(daily_checkpoint.value == 1.0f);
  daily_checkpoint.writable = true;
  assert(oq_energy_accounting_runtime::publish_totals());
  assert(daily_checkpoint.value == 35.0f);
  daily_checkpoint.value = 50.0f;
  assert(oq_energy_accounting_runtime::restore_totals(true));
  assert(test_oq_cooling_electrical_energy_daily.raw_state == 50.0f);
  assert(test_oq_heating_electrical_energy_daily.raw_state == 0.0f);
  return 0;
}
