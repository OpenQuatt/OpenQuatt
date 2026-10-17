"""Run production curve dispatch and boot-adoption/actuator methods together.

Performance predictions, hardware writes and ESPHome entities are peripheral
stubs. The candidate selection, boot policy and actuator safety gates are real.
Both topology builds compile from the same harness so Single cannot access HP2.
"""

from pathlib import Path
import re
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]
CONTROL = ROOT / "openquatt/includes/control"


def production_method(source: str, name: str) -> str:
    match = re.search(r"  (?:int|void|bool) " + name + r"\(.*?\n  }", source, re.S)
    if match is None:
        raise AssertionError(f"Production method not found: {name}")
    return match.group()


HARNESS = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "openquatt/includes/control/oq_heating_curve_logic.h"
#include "openquatt/includes/control/oq_heating_curve_duo_floor_logic.h"
#include "openquatt/includes/control/oq_hp_candidate_logic.h"
#include "openquatt/includes/control/oq_hp_boot_resume.h"
#include "openquatt/includes/control/oq_thermal_request_logic.h"
#include "openquatt/includes/control/oq_thermal_actuator_logic.h"
#include "openquatt/includes/control/oq_incident_actuator_logic.h"
#include "openquatt/includes/incidents/oq_hp_incident_engine.h"
#include "openquatt/includes/odu/oq_odu_compressor_levels.h"
#include "components/openquatt_decision_log/OpenQuattHeatingStop.h"
// CURVE_SUPPORT
#define id(x) x
#if OQ_TOPOLOGY_DUO
#define OQ_ACTUATOR_SECONDARY_ID(x) hp2_##x
#else
#define OQ_ACTUATOR_SECONDARY_ID(x) hp1_##x
#endif
uint32_t clock_ms=100000;
uint32_t millis() { return clock_ms; }
struct Sensor { float state=0; };
int oq_control_mode_code=2, oq_heat_mode_code=1;
int hp1_last_applied_level=0, hp1_last_commanded_physical_level=0, hp1_minutes=100;
uint32_t hp1_last_stop_ms=0, hp1_last_start_ms=0;
uint32_t hp1_last_real_heat_start_ms=0, hp1_last_level_change_ms=0;
bool hp1_prev_heating_mode_active=false;
Sensor hp1_4_way_valve, hp1_defrost, hp1_prot_oil_return;
#if OQ_TOPOLOGY_DUO
int hp2_last_applied_level=0, hp2_last_commanded_physical_level=0, hp2_minutes=100;
uint32_t hp2_last_stop_ms=0, hp2_last_start_ms=0;
uint32_t hp2_last_real_heat_start_ms=0, hp2_last_level_change_ms=0;
bool hp2_prev_heating_mode_active=false;
Sensor hp2_4_way_valve, hp2_defrost, hp2_prot_oil_return;
#else
// Production Single intentionally still owns these generic per-slot clocks.
uint32_t hp2_last_real_heat_start_ms=0, hp2_last_level_change_ms=0;
bool hp2_prev_heating_mode_active=false;
#endif
bool oq_curve_heat_request_active=true, oq_dual_hp_enabled=false;
bool oq_cold_start_session_active=false, oq_cold_start_hp_blocked=false;
bool oq_water_temp_hard_trip_active=false, oq_lowflow_fault_active=false;
int oq_curve_single_owner_hp=0, oq_curve_regime_code=1;
int oq_demand_raw=5, oq_power_cap_f=20, oq_demand_filtered=0, oq_demand_filtered_prev=0;
int oq_heating_demand_filtered=0, oq_last_lead_hp=0;
float oq_P_hp_cap_w=0, oq_P_deficit_w=0, oq_curve_demand_continuous=5;
float oq_dual_hp_enable_hold_elapsed_accum_min=0, oq_dual_hp_disable_hold_elapsed_accum_min=0;
float oq_dual_hp_emergency_hold_elapsed_accum_min=0;
uint32_t oq_curve_request_last_loop_ms=0, oq_ph_request_last_loop_ms=555;
uint32_t oq_duo_request_hold_until_ms=0;
int oq_curve_request_total_level=0, oq_curve_request_owner_hp=0;
int oq_curve_dispatch_hp1_level=0, oq_curve_dispatch_hp2_level=0, oq_curve_capacity_mode_code=0;
Sensor oq_supply_target_temp{40}, oq_system_supply_temp{39}, outside_temp_selected{5};
Sensor oq_dual_hp_enable_hold_min{1}, oq_dual_hp_disable_hold_min{1};
int physical[2]={0,0}, starts[2]={0,0};
unsigned missing_model_mask=0, forbidden_frequency_mask=0;
bool coupled_model=false;
struct Incident {
  bool available_for_start=true, must_stop=false, running_confirmed=false;
  bool stop_confirmed=true, stop_confirmation_pending=false;
  oq_incidents::RunState run_state=oq_incidents::RunState::STOPPED;
  enum class Link { HEALTHY, SUSPECT } link_state=Link::HEALTHY;
};
struct IncidentManager {
  Incident hp[2];
  oq_boot_resume::Policy boot[2];
  Incident get_outputs(unsigned unit) const {
    assert(OQ_TOPOLOGY_DUO || unit==1); return hp[unit-1];
  }
  bool boot_resume_pending(unsigned unit,uint32_t now) const {
    assert(OQ_TOPOLOGY_DUO || unit==1); return boot[unit-1].pending(now);
  }
  int boot_resume_level(unsigned unit) const {
    assert(OQ_TOPOLOGY_DUO || unit==1); return boot[unit-1].level();
  }
  bool boot_protection_hold(unsigned unit,uint32_t now) const { return boot[unit-1].protection_hold(now); }
  int update_boot_resume(unsigned unit,bool abort,bool safety,bool inputs,bool adopt,uint32_t now) {
    return boot[unit-1].update(now,abort || hp[unit-1].must_stop,safety,inputs,
                               adopt && hp[unit-1].available_for_start);
  }
  bool startup_inhibited(unsigned unit) const { return boot[unit-1].stop_required(clock_ms); }
  uint32_t minimum_off_remaining_ms(unsigned,uint32_t) const { return 0; }
  bool request_start(unsigned unit,uint8_t,uint32_t) { ++starts[unit-1]; return true; }
  void request_stop(unsigned unit,uint32_t) { hp[unit-1].run_state=oq_incidents::RunState::STOPPING; }
} oq_incident_manager;
namespace oq_boot_resume_runtime {
struct Safety { bool abort=false, ready=true; } safety[2];
inline bool ota_hold(bool) { return false; }
inline Safety capture(bool hp1) { return safety[hp1?0:1]; }
// BOOT_UPDATE
}
namespace oq_sources {
struct Raw { struct { bool valid=false; int value=0; } status_2119; } hp1;
#if OQ_TOPOLOGY_DUO
Raw hp2;
#endif
}
namespace oq_frequency_runtime {
struct Context {
  bool configured_v2=true;
  int cap_hz=90;
  oq_odu::RuntimeFrequencySnapshot table;
  const auto& snapshot(bool hp1) const { assert(OQ_TOPOLOGY_DUO || hp1); return table; }
  int automatic_level_limit(bool hp1,int) const { assert(OQ_TOPOLOGY_DUO || hp1); return 2; }
  bool frequency_allowed(bool hp1,int,int level) const {
    assert(OQ_TOPOLOGY_DUO || hp1);
    return level>0 && level<=2 && !(level==1 && (forbidden_frequency_mask & (hp1?1U:2U)));
  }
  int pick_allowed_level(bool,int,int level,int,int) const { return level; }
  int performance_variant(bool) const { return 1; }
};
Context capture() {
  Context f; f.table.variant=oq_odu::Variant::V2_NEW_MODEL;
  f.table.heating.valid=true; f.table.heating.level_count=21;
  f.table.heating.hz.fill(110); f.table.heating.hz[0]=0;
  f.table.heating.hz[1]=20; f.table.heating.hz[2]=40;
  return f;
}
}
namespace oq_perf {
struct Prediction { bool runtime_frequency_known=true,frequency_policy_allowed=true; };
template<class... T> Prediction predict_candidate(T...) { return {}; }
}
namespace oq_water_performance {
struct Power { float thermal_w,electrical_w; bool valid() const { return std::isfinite(thermal_w); } };
struct Model {
  bool coupled=coupled_model;
  Power unit(bool hp1,int level) const {
    if(level==1 && (missing_model_mask & (hp1?1U:2U))) return {NAN,NAN};
    return {2000.0f*level,400.0f*level};
  }
  Power pair(int first,int second) const {
    // In series, HP1 can warm HP2's inlet into the supported map domain even
    // when the standalone HP2 prediction at the common inlet is unavailable.
    if(coupled && first>0 && second>0) return {2000.0f*(first+second),400.0f*(first+second)};
    const auto a=unit(true,first),b=unit(false,second);
    return a.valid() && b.valid()?Power{a.thermal_w+b.thermal_w,a.electrical_w+b.electrical_w}:Power{NAN,NAN};
  }
};
template<class... T> Model capture(T...) { return {}; }
}
struct DispatchConfig { uint32_t loop_ms=60000,minimum_off_ms=240000; int demand_max_f=20; };
struct CurveRuntime {
  // CURVE_FIELDS
  uint8_t dispatch_stop_reason_=0;
  oq_curve::ControlProfileTuning tuning_() const { return {}; }
  float effective_supply_target(float value) const { return value; }
  // CURVE_METHODS
};
struct DefrostCycle {
  bool observed_active() const { return false; }
  bool fresh(uint32_t) const { return true; }
  bool telemetry_fault(uint32_t) const { return false; }
};
struct Defrost { DefrostCycle cycle; bool holding() const { return false; } } hp1_odu_defrost;
#if OQ_TOPOLOGY_DUO
Defrost hp2_odu_defrost;
#endif
namespace oq_cooling {
template<class... T> bool cooling_stop_confirmation_blocks_start(T...) { return false; }
template<class... T> bool global_minimum_off_time_blocks_start(T...) { return false; }
template<class... T> bool cooling_stop_requires_confirmation(T...) { return false; }
}
namespace openquatt_decision_log {
enum { REASON_UNKNOWN, REASON_HP_FAULT, REASON_CANDIDATE_UNAVAILABLE, REASON_CANDIDATE_IN_REST,
       REASON_START_STOP_RATE_HIGH, REASON_NO_CANDIDATE };
}
struct Cycle {
  struct { uint32_t now_ms=clock_ms; } config;
  oq_frequency_runtime::Context frequency=oq_frequency_runtime::capture();
  int request_mode_code=2;
  bool manual_service_active=false,request_thermal_active=true;
  bool cooling_stop_armed=false,restart_by_minimum_off_time=false,cooling_stop_planned=false;
  struct { bool confirmation_pending=false; uint32_t remaining_ms=0; } cooling;
  std::string request_mode_option="Heating";
};
struct ActuatorRuntime {
  uint32_t safe_write[2]={0,0}; bool confirmation[2]={false,false}; std::string guard;
  struct Limit { uint32_t remaining_ms(uint32_t) const { return 0; } } start_limits_[2];
  int previous_applied_(bool hp1) const {
#if OQ_TOPOLOGY_DUO
    return hp1?hp1_last_applied_level:hp2_last_applied_level;
#else
    assert(hp1); return hp1_last_applied_level;
#endif
  }
  bool defrost_safety_stop_(bool hp1) const {
    return oq_water_temp_hard_trip_active || oq_lowflow_fault_active || oq_incident_manager.hp[hp1?0:1].must_stop;
  }
  uint32_t& last_safe_stop_write_ms(bool hp1) { return safe_write[hp1?0:1]; }
  void clear_retained_level(bool) {}
  oq_odu::RetainedLevel retained_level(bool,const oq_frequency_runtime::Context&) { return {}; }
  int requested_mode_code(bool,bool,int mode,bool,bool) { return mode; }
  std::string& service_guard_(Cycle&,bool) { return guard; }
  uint16_t cap_seconds_(uint32_t value) { return static_cast<uint16_t>(value); }
  template<class... T> void log_block_change(T...) {}
  template<class... T> void publish_defrost_hold_start(T...) {}
  template<class... T> void apply_active_mode_hold(T...) { assert(false); }
  template<class... T> void publish_optimizer_reason(T...) {}
  bool& cooling_confirmation_pending_(bool hp1) { return confirmation[hp1?0:1]; }
  template<class... T> void set_mode(T...) {}
  template<class... T> void publish_candidate_blocked(T...) {}
  template<class... T> void note_cooling_start_refuse_(T...) {}
  void write_level(bool hp1,int level,bool) { physical[hp1?0:1]=level; }
  // ACTUATOR_METHODS
};
void seed_boot(int slot,uint32_t started_ms) {
  auto& boot=oq_incident_manager.boot[slot]; boot.begin(true,started_ms);
  for(const auto [address,value]:std::array<std::pair<uint16_t,float>,4>{{{3999,2},{1999,1},{2099,2},{2103,20}}})
    boot.observe(address,value,clock_ms);
}
void reset(bool hp1_running) {
  clock_ms=100000; oq_incident_manager={};
  hp1_last_applied_level=hp1_last_commanded_physical_level=0;
#if OQ_TOPOLOGY_DUO
  hp2_last_applied_level=hp2_last_commanded_physical_level=0;
  // Prefer the idle peer by runtime to expose an unobserved physical owner.
  hp1_minutes=hp1_running?100:0; hp2_minutes=hp1_running?0:100;
#endif
  oq_curve_single_owner_hp=0; oq_curve_heat_request_active=true;
  oq_demand_raw=5; oq_curve_demand_continuous=5; oq_curve_regime_code=1;
  oq_curve_request_last_loop_ms=0; oq_ph_request_last_loop_ms=555;
  oq_dual_hp_enabled=false;
  oq_dual_hp_enable_hold_elapsed_accum_min=oq_dual_hp_disable_hold_elapsed_accum_min=0;
  oq_dual_hp_emergency_hold_elapsed_accum_min=0;
  oq_curve_dispatch_hp1_level=oq_curve_dispatch_hp2_level=0;
  oq_water_temp_hard_trip_active=oq_lowflow_fault_active=false;
  physical[0]=physical[1]=starts[0]=starts[1]=0;
  missing_model_mask=forbidden_frequency_mask=0;
  coupled_model=false;
  oq_boot_resume_runtime::safety[0]=oq_boot_resume_runtime::safety[1]={};
  physical[hp1_running?0:1]=1;
  seed_boot(hp1_running?0:1,clock_ms-1000);
}
void actuator_tick(ActuatorRuntime& actuator) {
  actuator.resume_boot_cycle_(true,true,clock_ms);
#if OQ_TOPOLOGY_DUO
  actuator.resume_boot_cycle_(false,true,clock_ms);
#endif
  Cycle cycle;
  hp1_last_applied_level=actuator.apply_level_(true,oq_curve_dispatch_hp1_level,false,cycle);
#if OQ_TOPOLOGY_DUO
  hp2_last_applied_level=actuator.apply_level_(false,oq_curve_dispatch_hp2_level,false,cycle);
#endif
}
int main() {
  for(bool running_hp1:{true,false}) {
    if(!OQ_TOPOLOGY_DUO && !running_hp1) continue;
    const int slot=running_hp1?0:1,peer=1-slot;
    reset(running_hp1); CurveRuntime curve; ActuatorRuntime actuator;
    // A dispatch calculated before software adoption must target the observed
    // owner. Otherwise this very actuator tick adopts it AND starts its peer.
    curve.dispatch_tick({});
    assert((running_hp1?hp1_last_applied_level:0)==0);
    actuator.resume_boot_cycle_(running_hp1,false,clock_ms);
    assert(oq_incident_manager.boot[slot].pending(clock_ms));
    Cycle pending_cycle;
    assert(actuator.apply_level_(running_hp1,running_hp1?oq_curve_dispatch_hp1_level:oq_curve_dispatch_hp2_level,
                                 false,pending_cycle)==0);
    assert(physical[slot]==1 && physical[peer]==0 && starts[0]==0 && starts[1]==0);
    oq_incident_manager.hp[slot].available_for_start=false;
    actuator.resume_boot_cycle_(running_hp1,true,clock_ms);
    assert(oq_incident_manager.boot[slot].pending(clock_ms));
    oq_incident_manager.hp[slot].available_for_start=true;
    actuator_tick(actuator);
    assert(physical[slot]==1 && physical[peer]==0 && starts[0]==0 && starts[1]==0);
    assert((running_hp1?oq_curve_dispatch_hp1_level:oq_curve_dispatch_hp2_level)==1);
    assert((running_hp1?oq_curve_dispatch_hp2_level:oq_curve_dispatch_hp1_level)==0);
    assert(oq_curve_request_last_loop_ms==0 && oq_ph_request_last_loop_ms==0);
    // Adoption must invalidate cadence even without a candidate-availability
    // change. The next strategy tick can use fresh demand immediately.
    ++clock_ms;
    curve.dispatch_tick({});
    assert(oq_curve_request_last_loop_ms==clock_ms);
    assert((running_hp1?oq_curve_dispatch_hp1_level:oq_curve_dispatch_hp2_level)==1);
    actuator_tick(actuator);
    assert(physical[slot]==1); // A valid three-minute boot protection still wins.
    clock_ms+=oq_boot_resume::PROTECTION_HOLD_MS;
    oq_demand_raw=20; oq_curve_demand_continuous=20;
    curve.dispatch_tick({}); actuator_tick(actuator);
    assert(physical[slot]==2 && physical[peer]==0 && starts[0]==0 && starts[1]==0);
    // Independent safety always stops an owner, before or after adoption.
    for(bool adopted:{false,true}) {
      reset(running_hp1); curve={}; actuator={}; curve.dispatch_tick({});
      if(adopted) actuator_tick(actuator);
      oq_boot_resume_runtime::safety[slot].abort=true;
      oq_incident_manager.hp[slot].must_stop=true;
      actuator_tick(actuator);
      assert(physical[slot]==0 && physical[peer]==0 && starts[0]==0 && starts[1]==0);
      if(!adopted) assert(oq_incident_manager.boot[slot].stop_required(clock_ms));
    }
    // Once abort/expiry ends acquisition, an available peer must be allowed
    // to serve ordinary demand; the temporary start veto leaves no lock.
    for(bool abort:{true,false}) {
      reset(running_hp1); curve={}; actuator={};
      auto& boot=oq_incident_manager.boot[slot];
      boot={}; boot.begin(true,clock_ms); // The requested level is still unknown.
      curve.dispatch_tick({});
      assert(oq_curve_dispatch_hp1_level==0 && oq_curve_dispatch_hp2_level==0);
      actuator_tick(actuator);
      assert(physical[slot]==1 && physical[peer]==0 && starts[0]==0 && starts[1]==0);
      if(abort) boot.update(clock_ms,true,true,true,true);
      else clock_ms+=oq_boot_resume::INPUT_ACQUISITION_MS;
      oq_incident_manager.hp[slot].must_stop=true;
      ++clock_ms; curve.dispatch_tick({}); actuator_tick(actuator);
      assert(physical[slot]==0);
#if OQ_TOPOLOGY_DUO
      assert(physical[peer]>0 && starts[peer]==1);
#else
      assert(physical[peer]==0 && starts[peer]==0);
#endif
    }
    // A forecast for F2 is not a forecast for the boot-held F1. Missing only
    // that current level must preserve existing capacity without a peer start.
    reset(running_hp1); curve={}; actuator={}; actuator_tick(actuator);
    missing_model_mask=1U<<slot;
    curve.dispatch_tick({}); actuator_tick(actuator);
    assert((running_hp1?oq_curve_dispatch_hp1_level:oq_curve_dispatch_hp2_level)==1);
    assert((running_hp1?oq_curve_dispatch_hp2_level:oq_curve_dispatch_hp1_level)==0);
    assert(physical[slot]==1 && physical[peer]==0 && starts[0]==0 && starts[1]==0);
    // A forbidden frequency is different: the missing-model fallback must
    // not authorize it, and the actuator's live boot safety must stop it.
    forbidden_frequency_mask=1U<<slot;
    oq_boot_resume_runtime::safety[slot]={true,false};
    clock_ms+=60000; curve.dispatch_tick({}); actuator_tick(actuator);
    assert((running_hp1?oq_curve_dispatch_hp1_level:oq_curve_dispatch_hp2_level)==0 && physical[slot]==0);
#if OQ_TOPOLOGY_DUO
    assert(physical[peer]>0 && starts[slot]==0 && starts[peer]==1); // Genuine replacement capacity is allowed.
#else
    assert(physical[peer]==0 && starts[peer]==0);
#endif
#if OQ_TOPOLOGY_DUO
    // Genuine extra demand after adoption is scored at the executable F1.
    // A fictitious F2 must not hide missing capacity, and the idle peer can
    // satisfy it through the normal Duo hold/availability gates.
    reset(running_hp1); curve={}; actuator={}; actuator_tick(actuator);
    oq_demand_raw=20; oq_curve_demand_continuous=20;
    curve.dispatch_tick({}); actuator_tick(actuator);
    clock_ms+=60000; curve.dispatch_tick({}); actuator_tick(actuator);
    assert(physical[slot]==1 && physical[peer]>0 && starts[slot]==0 && starts[peer]==1);
    assert(oq_curve_capacity_mode_code==2 && physical[0]+physical[1]==2);
#endif
  }
#if OQ_TOPOLOGY_DUO
  for(bool hp1_first:{true,false}) {
    reset(hp1_first); CurveRuntime curve; ActuatorRuntime actuator;
    const int first=hp1_first?0:1,second=1-first;
    physical[second]=1;
    seed_boot(second,clock_ms-1000);
    curve.dispatch_tick({});
    assert(oq_curve_dispatch_hp1_level==1 && oq_curve_dispatch_hp2_level==1 && oq_curve_capacity_mode_code==2);
    actuator.resume_boot_cycle_(hp1_first,true,clock_ms);
    const auto first_adoption_ms=clock_ms;
    ++clock_ms; actuator.resume_boot_cycle_(!hp1_first,true,clock_ms);
    actuator_tick(actuator); curve.dispatch_tick({});
    assert(physical[0]==1 && physical[1]==1 && starts[0]==0 && starts[1]==0);
    assert(oq_curve_dispatch_hp1_level==1 && oq_curve_dispatch_hp2_level==1 && oq_curve_capacity_mode_code==2);
    // Demand removal still wins in the request; protection remains an actuator
    // responsibility. Restoring demand observes the same existing topology.
    oq_curve_heat_request_active=false; oq_demand_raw=0; oq_curve_demand_continuous=0;
    curve.dispatch_tick({});
    assert(oq_curve_dispatch_hp1_level==0 && oq_curve_dispatch_hp2_level==0 && oq_curve_capacity_mode_code==0);
    oq_curve_heat_request_active=true; oq_demand_raw=5; oq_curve_demand_continuous=5;
    curve.dispatch_tick({});
    assert(oq_curve_dispatch_hp1_level==1 && oq_curve_dispatch_hp2_level==1 && oq_curve_capacity_mode_code==2);
    // The runtime lead is now the first expired owner. The still-protected HP
    // must remain the Single owner rather than forcing an impossible request.
    hp1_minutes=hp1_first?0:100; hp2_minutes=hp1_first?100:0;
    clock_ms=first_adoption_ms+oq_boot_resume::PROTECTION_HOLD_MS;
    curve.dispatch_tick({}); actuator_tick(actuator);
    assert(physical[first]==0 && physical[second]==1 && starts[0]==0 && starts[1]==0);
    assert(oq_curve_request_owner_hp==second+1 && oq_curve_capacity_mode_code==1);
    clock_ms+=60000; curve.dispatch_tick({}); actuator_tick(actuator);
    assert(physical[first]==0 && physical[second]==1 && starts[0]==0 && starts[1]==0);
  }
  for(bool forbidden_hp1:{true,false}) {
    reset(true); CurveRuntime curve; ActuatorRuntime actuator;
    physical[1]=1; seed_boot(1,clock_ms-1000);
    actuator_tick(actuator); curve.dispatch_tick({});
    assert(oq_curve_dispatch_hp1_level==1 && oq_curve_dispatch_hp2_level==1);
    const int forbidden=forbidden_hp1?0:1,healthy=1-forbidden;
    forbidden_frequency_mask=1U<<forbidden;
    oq_incident_manager.hp[forbidden].link_state=Incident::Link::SUSPECT;
    oq_boot_resume_runtime::safety[forbidden]={true,false};
    // This also tests urgent recalculation: one millisecond is below cadence.
    ++clock_ms; curve.dispatch_tick({}); actuator_tick(actuator);
    assert((forbidden_hp1?oq_curve_dispatch_hp1_level:oq_curve_dispatch_hp2_level)==0);
    assert((forbidden_hp1?oq_curve_dispatch_hp2_level:oq_curve_dispatch_hp1_level)==1);
    assert(physical[forbidden]==0 && physical[healthy]==1 && starts[0]==0 && starts[1]==0);
    assert(oq_curve_capacity_mode_code==1 && oq_curve_request_owner_hp==healthy+1);
  }
  // Only downstream HP2 can have an invalid standalone F1 while the physical
  // F1/F1 pair is valid. Once HP1's hold expires, HP2 still needs that existing
  // upstream heat; the missing Single must not collapse the request to 0/0.
  {
    reset(true); CurveRuntime curve; ActuatorRuntime actuator;
    physical[1]=1; seed_boot(1,clock_ms-1000);
    actuator.resume_boot_cycle_(true,true,clock_ms);
    const auto first_adoption_ms=clock_ms;
    ++clock_ms; actuator.resume_boot_cycle_(false,true,clock_ms);
    actuator_tick(actuator);
    coupled_model=true; missing_model_mask=2;
    clock_ms=first_adoption_ms+oq_boot_resume::PROTECTION_HOLD_MS;
    curve.dispatch_tick({}); actuator_tick(actuator);
    assert(oq_curve_dispatch_hp1_level==1 && oq_curve_dispatch_hp2_level==1 && oq_curve_capacity_mode_code==2);
    assert(physical[0]==1 && physical[1]==1 && starts[0]==0 && starts[1]==0);
    // If the upstream command becomes forbidden, its old valid pair can no
    // longer mask the healthy downstream hold's standalone missing model.
    forbidden_frequency_mask=1; oq_boot_resume_runtime::safety[0]={true,false};
    oq_incident_manager.hp[0].must_stop=true;
    // HP2 remains in its hold for this recalculation.
    oq_curve_request_last_loop_ms=0; curve.dispatch_tick({}); actuator_tick(actuator);
    assert(oq_curve_dispatch_hp1_level==0 && oq_curve_dispatch_hp2_level==1);
    assert(physical[0]==0 && physical[1]==1 && starts[0]==0 && starts[1]==0);
  }
#endif
  puts("PASS production curve boot acquisition/adoption and actuator lifecycle");
}
'''


class HeatingCurveBootResumeRuntimeTest(unittest.TestCase):
    def test_curve_dispatch_and_actuator_boot_adoption(self) -> None:
        curve = (CONTROL / "oq_heating_curve_runtime.h").read_text()
        actuator = (CONTROL / "oq_thermal_actuator_runtime.h").read_text()
        boot = (CONTROL / "oq_boot_resume_runtime.h").read_text()
        update = re.search(r"inline int update\(.*?\n}", boot, re.S)
        self.assertIsNotNone(update, "Production boot update not found")
        methods = ["dispatch_tick", "reset_request_"]
        if re.search(r"  void reset_dual_holds_\(", curve):
            methods.append("reset_dual_holds_")
        members = re.findall(
            r"^  (?:bool|int|oq_curve::DuoHoldState) "
            r"(?:dispatch_snapshot_initialized_|last_dispatch_demand_|last_hp1_available_|"
            r"last_hp2_available_|previous_capped_demand_|duo_holds_)[^;\n]*;", curve, re.M)
        self.assertGreaterEqual(len(members), 5, "Production dispatch members not found")
        harness = HARNESS.replace("// CURVE_FIELDS", "\n".join(members))
        hold_support = ""
        if (CONTROL / "oq_heating_curve_duo_hold_logic.h").is_file():
            hold_support = '#include "openquatt/includes/control/oq_heating_curve_duo_hold_logic.h"'
        harness = harness.replace("// CURVE_SUPPORT", hold_support)
        harness = harness.replace("// CURVE_METHODS", "\n".join(
            production_method(curve, name) for name in methods))
        harness = harness.replace("// ACTUATOR_METHODS", "\n".join(
            production_method(actuator, name) for name in ("resume_boot_cycle_", "apply_level_")))
        harness = harness.replace("// BOOT_UPDATE", update.group())
        with tempfile.TemporaryDirectory(prefix="openquatt-curve-boot-") as directory:
            source = Path(directory) / "test.cpp"
            source.write_text(harness)
            for duo in (1, 0):
                with self.subTest(duo=duo):
                    binary = Path(directory) / f"test-{duo}"
                    compiled = run_process(
                        compiler_command("-I", str(ROOT), f"-DOQ_TOPOLOGY_DUO={duo}") +
                        [str(source), "-o", str(binary)], timeout=COMPILE_TIMEOUT_SECONDS,
                        capture_output=True, text=True,
                    )
                    self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                    result = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
