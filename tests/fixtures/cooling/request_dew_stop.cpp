#include <cassert>
#include <cmath>
#include <optional>
#include <string>
#include "openquatt/includes/control/oq_cooling_limiter_logic.h"
#include "openquatt/includes/control/oq_thermal_request_logic.h"
#include "openquatt/includes/control/oq_heating_curve_logic.h"
#include "openquatt/includes/odu/oq_odu_compressor_levels.h"
#define id(x) x
#if OQ_TOPOLOGY_DUO
#define OQ_REQUEST_SECONDARY_ID(x) hp2_##x
#else
#define OQ_REQUEST_SECONDARY_ID(x) hp1_##x
#endif
void log_message(const char*, const char*, ...) {}
#define ESP_LOGI(...) log_message(__VA_ARGS__)
struct Sensor {
  float state = 0;
  bool has_state() const { return true; }
};
struct Select {
  std::string option = "Cooling";
  bool known = true;
  bool has_state() const { return known; }
  const std::string& current_option() const { return option; }
  std::optional<size_t> active_index() const { return 1; }
};
Sensor hp1_working_mode{1}, hp2_working_mode{1}, hp1_4_way_valve{1}, hp2_4_way_valve{1};
Sensor flow_rate_selected{800}, oq_manual_hp1_level{0}, oq_manual_hp2_level{0};
Select hp1_set_working_mode, hp2_set_working_mode, hp1_compressor_level, hp2_compressor_level, oq_curve_control_profile;
int hp1_last_applied_level = 1, hp2_last_applied_level = 1, hp1_last_commanded_physical_level = 1,
    hp2_last_commanded_physical_level = 1;
int hp1_minutes = 0, hp2_minutes = 0;
uint32_t hp1_last_real_heat_start_ms = 1000, hp2_last_real_heat_start_ms = 1000, hp1_last_level_change_ms = 0,
         hp2_last_level_change_ms = 0;
int oq_cooling_stop_reason_code = 0, oq_cooling_request_hp1_level = 0, oq_cooling_request_hp2_level = 0;
int oq_cooling_request_owner_hp = 0, oq_cooling_request_reason_code = 0, oq_curve_dispatch_hp1_level = 0,
    oq_curve_dispatch_hp2_level = 0;
int oq_curve_request_owner_hp = 0, oq_curve_capacity_mode_code = 0, oq_ph_request_hp1_level = 0,
    oq_ph_request_hp2_level = 0, oq_ph_request_owner_hp = 0;
int oq_ph_request_reason_code = 0, oq_request_hp1_level = 0, oq_request_hp2_level = 0, oq_curve_regime_code = 0,
    oq_control_mode_code = 5;
int oq_actuator_hp1_req = 0, oq_actuator_hp2_req = 0, oq_curve_single_owner_hp = 0;
int oq_manual_hp1_mode_code = 1, oq_manual_hp2_mode_code = 1;
bool oq_manual_hp_stop_requested = false, oq_manual_hp_mode_allowed = false;
bool oq_water_temp_hard_trip_active = false, oq_lowflow_fault_active = false, oq_cold_start_hp_blocked = false,
     oq_dual_hp_enabled = false;
uint32_t oq_duo_request_hold_until_ms = 0;
float oq_dual_hp_enable_hold_elapsed_accum_min = 0, oq_dual_hp_disable_hold_elapsed_accum_min = 0,
      oq_dual_hp_emergency_hold_elapsed_accum_min = 0;
namespace oq_power_house_dispatch {
const char* request_reason_name(int) { return "power_house"; }
}  // namespace oq_power_house_dispatch
namespace oq_frequency_runtime {
struct Context {
  bool configured_v2 = false;
  oq_odu::RuntimeFrequencySnapshot table;
  Context() {
    table.heating.valid = true;
    table.heating.level_count = 11;
    table.heating.hz.fill(30);
    table.heating.hz[0] = 0;
    table.cooling = table.heating;
  }
  const auto& snapshot(bool first) const {
    assert(OQ_TOPOLOGY_DUO || first);
    return table;
  }
  int limit_heating_slew(bool, int level, int, uint32_t, uint32_t, uint32_t, uint32_t) const { return level; }
};
}  // namespace oq_frequency_runtime
struct TickConfig {
  uint32_t now_ms = 10000, minimum_off_s = 10;
  float minimum_flow_lph = 300;
};
struct Runtime {
  bool last_hp1_min_runtime_hold_ = false, last_hp2_min_runtime_hold_ = false;
  bool startup_blocked_(bool) const { return false; }
  int allowed_(bool first, int, int level, const oq_frequency_runtime::Context&) const {
    assert(OQ_TOPOLOGY_DUO || first);
    return level;
  }
  int physical_limit_(bool, int, const oq_frequency_runtime::Context&) const { return 10; }
  const char* manual_reason_(oq_request::ManualReason) const { return "manual"; }
  void publish_request_(int, int first, int second, int, const char*, const oq_frequency_runtime::Context&) {
    oq_request_hp1_level = first;
    oq_request_hp2_level = second;
  }
  template <class... T>
  void publish_lead_(T...) {}
  template <class... T>
  void publish_optimizer_reason_(T...) {}
  template <class... T>
  void update_startup_event_(T...) {}
  template <class... T>
  void publish_debug_(T...) {}
  // PRODUCTION_METHODS
};
void reset(int physical_mode = 1, const char* target = "Cooling") {
  hp1_working_mode.state = hp2_working_mode.state = static_cast<float>(physical_mode);
  hp1_set_working_mode.option = hp2_set_working_mode.option = target;
  hp1_set_working_mode.known = hp2_set_working_mode.known = true;
  hp1_last_applied_level = hp2_last_applied_level = 1;
  hp1_4_way_valve.state = hp2_4_way_valve.state = 1;
  oq_cooling_stop_reason_code = oq_cooling::WATER_STOP_DEW;
  oq_cooling_request_hp1_level = oq_cooling_request_hp2_level = 0;
  oq_ph_request_hp1_level = oq_ph_request_hp2_level = 0;
  oq_curve_dispatch_hp1_level = oq_curve_dispatch_hp2_level = 0;
  oq_actuator_hp1_req = oq_actuator_hp2_req = -1;
  oq_manual_hp_stop_requested = false;
  oq_manual_hp1_level.state = oq_manual_hp2_level.state = 0;
}
int main() {
  const oq_frequency_runtime::Context frequency;
  const TickConfig tick;
  // Real automatic arbitration must not turn the confirmed zero back into F1,
  // including Duo's one-change slew and both physical minimum-runtime clocks.
  for (bool cooling : {true, false}) {
    reset(cooling ? 1 : 2, cooling ? "Cooling" : "Heating");
    Runtime runtime;
    runtime.run_automatic_(tick, oq_request::resolve_mode_context(cooling ? 5 : 2, 0), false, 300000, frequency, 0, 0,
                           10);
    assert(oq_actuator_hp1_req == (cooling ? 0 : 1));
#if OQ_TOPOLOGY_DUO
    assert(oq_actuator_hp2_req == (cooling ? 0 : 1));
#endif
  }
  reset();
  oq_cooling_stop_reason_code = oq_cooling::WATER_STOP_LIMITER;
  Runtime ordinary;
  ordinary.run_automatic_(tick, oq_request::resolve_mode_context(5, 0), false, 300000, frequency, 0, 0, 10);
  assert(oq_actuator_hp1_req == 1);  // Ordinary soft-stop minimum runtime is preserved.
  for (int physical_mode : {0, 1, 2}) {
    reset(physical_mode, physical_mode == 2 ? "Heating" : physical_mode == 1 ? "Cooling" : "Standby");
    Runtime runtime;
    if (physical_mode == 0) hp1_last_applied_level = hp2_last_applied_level = 0;  // stale selected F1/valve tail
    runtime.run_inactive_(tick, oq_request::resolve_mode_context(0, 0), false, 300000, frequency, 0, 0, 10);
    assert(oq_actuator_hp1_req == (physical_mode == 2 ? 1 : 0));
#if OQ_TOPOLOGY_DUO
    assert(oq_actuator_hp2_req == (physical_mode == 2 ? 1 : 0));
#endif
  }
  reset();
  Runtime manual;
  oq_manual_hp1_level.state = 2;
  oq_manual_hp_stop_requested = true;
  manual.run_manual_(tick, oq_request::resolve_mode_context(100, 0), false, 300000, frequency, 0, 0, 10);
  assert(oq_actuator_hp1_req == 0 && oq_actuator_hp2_req == 0);
  oq_manual_hp_stop_requested = false;
  oq_manual_hp1_level.state = 2;
  manual.run_manual_(tick, oq_request::resolve_mode_context(100, 0), false, 300000, frequency, 0, 0, 10);
  assert(oq_actuator_hp1_req == 2);  // Explicit positive manual cooling remains available.
}
