#include <assert.h>
#include <math.h>

#include <string>

#ifndef OQ_TOPOLOGY_DUO
#define OQ_TOPOLOGY_DUO 1
#endif

struct Sensor {
  float state{NAN};
};
struct Generation {
  bool has_state() const { return true; }
  std::string current_option() const { return "V1"; }
};

Generation test_hp_generation;
int test_oq_control_mode_code = 2;
bool test_hp1_odu_generation_detection_complete = true;
int test_hp1_generation_variant_code = 1;
bool test_hp1_is_online = true;
bool test_oq_cooling_energy_session_active = false;
Sensor test_hp1_power_input;
Sensor test_hp1_working_mode;
Sensor test_hp1_heat_power;
Sensor test_hp1_cooling_power;
#if OQ_TOPOLOGY_DUO
bool test_hp2_odu_generation_detection_complete = true;
int test_hp2_generation_variant_code = 1;
bool test_hp2_is_online = true;
Sensor test_hp2_power_input;
Sensor test_hp2_working_mode;
Sensor test_hp2_heat_power;
Sensor test_hp2_cooling_power;
#endif

#define id(name) test_##name
#include "../../openquatt/includes/performance/oq_energy_runtime.h"
#undef id

void observe(oq_sources::HeatPumpReceipts& receipts, float mode, float hz, float defrost, uint64_t now_ms) {
  receipts.working_mode.observe(mode, now_ms, true);
  receipts.compressor_frequency.observe(hz, now_ms, true);
  receipts.defrost.observe(defrost, now_ms, true);
  receipts.water_in.observe(30.0f, now_ms, true);
  receipts.water_out.observe(33.0f, now_ms, true);
}

int main() {
  constexpr uint64_t now = 1000;
  constexpr uint64_t age = 100;
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  observe(oq_sources::hp1, 2.0f, 0.0f, 0.0f, now);
  test_hp1_power_input.state = 36.0f;
  test_hp1_heat_power.state = -233.0f;
#if OQ_TOPOLOGY_DUO
  observe(oq_sources::hp2, 0.0f, 0.0f, 0.0f, now);
  test_hp2_power_input.state = 0.0f;
  test_hp2_heat_power.state = 0.0f;
#endif
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  test_hp1_heat_power.state = 233.0f;
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  observe(oq_sources::hp1, 2.0f, 30.0f, 0.0f, now - age);
  test_hp1_power_input.state = 600.0f;
  test_hp1_heat_power.state = 3000.0f;
  assert(oq_energy_runtime::total_cop(now, age, 10.0f) == 5.0f);
  // CM1 suppresses COP even while the last operating samples still indicate heating.
  test_oq_control_mode_code = 1;
  const auto cm1_state = oq_energy_runtime::heating_cop_state(oq_sources::hp1, true, now, age);
  assert(isnan(oq_energy::heating_cop_or_nan(3000.0f, 600.0f, 5.0f, cm1_state)));
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  test_oq_control_mode_code = 3;
  assert(oq_energy_runtime::total_cop(now, age, 10.0f) == 5.0f);
  test_oq_control_mode_code = 2;
  assert(isnan(oq_energy_runtime::total_cop(now + 1, age, 10.0f)));
  observe(oq_sources::hp1, 2.0f, 30.0f, 0.0f, now);
  test_hp1_is_online = false;
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  test_hp1_is_online = true;
  oq_sources::hp1.compressor_frequency.invalidate();
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  observe(oq_sources::hp1, 2.0f, 30.0f, 0.0f, now);
  oq_sources::hp1.water_out.observe(NAN, now, false);
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  observe(oq_sources::hp1, 2.0f, 30.0f, 0.0f, now);
  oq_sources::hp1.defrost.observe(1.0f, now, true);
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  observe(oq_sources::hp1, 2.0f, 30.0f, 0.0f, now);
  assert(oq_energy_runtime::total_cop(now, age, 10.0f) == 5.0f);
#if OQ_TOPOLOGY_DUO
  observe(oq_sources::hp2, 4.0f, 30.0f, 0.0f, now);
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  observe(oq_sources::hp2, 0.0f, 0.0f, 0.0f, now);
  oq_sources::hp2.defrost.invalidate();
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  observe(oq_sources::hp2, 2.0f, 0.0f, 0.0f, now);
  test_hp2_power_input.state = 36.0f;
  test_hp2_heat_power.state = -233.0f;
  assert(oq_energy_runtime::total_heat_power() == 2767.0f);
  assert(oq_energy_runtime::total_cop(now, age, 10.0f) > 4.0f);
#endif
  const float hp1_input = test_hp1_power_input.state;
  test_hp1_power_input.state = NAN;
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  test_hp1_power_input.state = hp1_input;
  const float hp1_heat = test_hp1_heat_power.state;
  test_hp1_heat_power.state = NAN;
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  test_hp1_heat_power.state = hp1_heat;
#if OQ_TOPOLOGY_DUO
  test_hp2_power_input.state = NAN;
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  test_hp2_power_input.state = 36.0f;
#endif
  // A compressor stop suppresses a previously positive thermal sample immediately.
  observe(oq_sources::hp1, 2.0f, 0.0f, 0.0f, now);
  assert(isnan(oq_energy_runtime::total_cop(now, age, 10.0f)));
  return 0;
}
