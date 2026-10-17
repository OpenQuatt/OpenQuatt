"""Execute actual request/actuator methods with peripheral-only host stubs."""

from pathlib import Path
import re
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]
CONTROL = ROOT / "openquatt/includes/control"


def method(source: str, name: str) -> str:
    # The runtime's methods end at the class's two-space closing brace.
    match = re.search(r"  (?:int|void|bool) " + name + r"\(.*?\n  }", source, re.S)
    if match is None:
        raise AssertionError(f"Production method not found: {name}")
    return match.group()


class LowflowDefrostRuntimeTest(unittest.TestCase):
    def test_production_safety_chain_and_retry_ordering(self) -> None:
        request = (CONTROL / "oq_thermal_request_runtime.h").read_text()
        actuator = (CONTROL / "oq_thermal_actuator_runtime.h").read_text()
        request_methods = "\n".join(method(request, name) for name in (
            "defrost_hold_", "minimum_runtime_hold_", "run_inactive_"))
        actuator_methods = "\n".join(method(actuator, name) for name in (
            "defrost_safety_stop_", "apply_level_"))
        harness = r'''
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "openquatt/includes/control/oq_power_house_dispatch_logic.h"
#include "openquatt/includes/control/oq_thermal_request_logic.h"
#include "openquatt/includes/control/oq_thermal_actuator_logic.h"
#include "openquatt/includes/control/oq_hp_boot_resume.h"
#include "openquatt/includes/control/oq_incident_actuator_logic.h"
#include "openquatt/includes/odu/oq_odu_compressor_levels.h"
#include "openquatt/includes/incidents/oq_hp_incident_engine.h"
#define OQ_TOPOLOGY_DUO 1
#include "openquatt/includes/sources/oq_source_receipt_runtime.h"
// This regression exercises the established safety chain after ordinary boot.
// Active boot acquisition is executed in oq_compressor_command_runtime_test.
namespace oq_boot_resume_runtime {
struct Safety { bool abort=false, ready=false; };
inline void update(bool) {}
inline bool ota_hold(bool) { return false; }
inline Safety capture(bool) { return {false, true}; }
}
#define id(x) x
#define OQ_REQUEST_SECONDARY_ID(x) hp2_##x
#define OQ_ACTUATOR_SECONDARY_ID(x) hp2_##x
bool oq_water_temp_hard_trip_active=false, oq_lowflow_fault_active=false;
uint32_t clock_ms=100000;
uint32_t millis() { return clock_ms; }
struct Sensor { bool state=true; } hp1_4_way_valve, hp2_4_way_valve;
// F8 is valid in both the dev ten-level model and frequency-native heating.
int hp1_last_commanded_physical_level=8, hp2_last_commanded_physical_level=8;
uint32_t hp1_last_real_heat_start_ms=90000, hp2_last_real_heat_start_ms=90000;
bool oq_dual_hp_enabled=false;
float oq_dual_hp_enable_hold_elapsed_accum_min=0, oq_dual_hp_disable_hold_elapsed_accum_min=0;
float oq_dual_hp_emergency_hold_elapsed_accum_min=0;
int oq_curve_single_owner_hp=0, oq_control_mode_code=1, oq_actuator_hp1_req=0, oq_actuator_hp2_req=0;
uint32_t oq_duo_request_hold_until_ms=0;
namespace oq_frequency_runtime {
struct Context {
  bool configured_v2=true;
  int cap_hz=90;
  oq_odu::RuntimeFrequencySnapshot table;
  const auto& snapshot(bool) const { return table; }
  int automatic_level_limit(bool, int) const { return 17; }
  bool frequency_allowed(bool, int, int) const { return true; }
  int pick_allowed_level(bool, int, int level, int, int) const { return level; }
};
}
struct TickConfig { uint32_t now_ms=100000, minimum_off_s=240; };
struct RequestRuntime {
  int previous[2]={8,8};
  int selected_level_(bool hp) const { return previous[hp?0:1]; }
  int last_applied_(bool hp) const { return previous[hp?0:1]; }
  bool mode_is_cooling_(bool) const { return false; }
  bool target_is_cooling_(bool) const { return false; }
  bool measured_thermal_(bool) const { return true; }
  bool startup_blocked_(bool) const { return false; }
  int hold_mode_(int, int) const { return 2; }
  void publish_optimizer_reason_(const char*) {}
  template<class... T> void update_startup_event_(T...) {}
  template<class... T> void publish_request_(T...) {}
  template<class... T> void publish_debug_(T...) {}
  // REQUEST_METHODS
};
struct Incident {
  bool available_for_start=true, must_stop=false, running_confirmed=true;
  bool stop_confirmed=false, stop_confirmation_pending=false;
  oq_incidents::RunState run_state=oq_incidents::RunState::RUNNING;
};
std::vector<std::string> events;
struct IncidentManager {
  Incident hp[2];
  bool startup[2]={false,false};
  Incident get_outputs(int i) const { return hp[i-1]; }
  bool startup_inhibited(int i) const { return startup[i-1]; }
  oq_boot_resume::Policy boot[2];
  bool boot_resume_pending(int i, uint32_t now) const { return boot[i-1].pending(now); }
  uint32_t rest_ms[2]={240000,240000};
  bool boot_protection_hold(int i, uint32_t now) const { return boot[i-1].protection_hold(now); }
  uint32_t minimum_off_remaining_ms(int i, uint32_t) const { return rest_ms[i-1]; }
  bool request_start(int, uint8_t, uint32_t) { events.push_back("start"); return true; }
  void request_stop(int i, uint32_t) {
    events.push_back("stop"); auto& incident=hp[i-1];
    incident.run_state=oq_incidents::RunState::STOPPING;
  }
} oq_incident_manager;
struct DefrostCycle {
  bool fault=false, is_fresh=true, active=true;
  bool telemetry_fault(uint32_t) const { return fault; }
  bool fresh(uint32_t) const { return is_fresh; }
  bool observed_active() const { return active; }
};
struct Defrost { DefrostCycle cycle; bool hold=true; bool holding() const { return hold; } } hp1_odu_defrost, hp2_odu_defrost;
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
  TickConfig config;
  oq_frequency_runtime::Context frequency;
  int request_mode_code=2;
  bool manual_service_active=false, request_thermal_active=true;
  bool cooling_stop_armed=false, restart_by_minimum_off_time=false, cooling_stop_planned=false;
  struct { bool confirmation_pending=false; uint32_t remaining_ms=0; } cooling;
  std::string request_mode_option="Heating";
};
struct ActuatorRuntime {
  int previous[2]={17,17}, physical[2]={17,17};
  uint32_t safe_write[2]={0,0};
  bool confirmation[2]={false,false}, allow_active_writes=false;
  std::string guard;
  oq_odu::RetainedLevel retained[2]={{17,17},{17,17}};
  struct Limit {
    uint32_t remaining=3600000;
    uint32_t remaining_ms(uint32_t) const { return remaining; }
  } start_limits_[2];
  int previous_applied_(bool hp) const { return previous[hp?0:1]; }
  uint32_t& last_safe_stop_write_ms(bool hp) { return safe_write[hp?0:1]; }
  void clear_retained_level(bool hp) { retained[hp?0:1]={}; events.push_back("clear"); }
  oq_odu::RetainedLevel retained_level(bool hp, const oq_frequency_runtime::Context&) { return retained[hp?0:1]; }
  int requested_mode_code(bool, bool, int mode, bool, bool) { return mode; }
  std::string& service_guard_(Cycle&, bool) { return guard; }
  uint16_t cap_seconds_(uint32_t v) { return static_cast<uint16_t>(v); }
  void log_block_change(bool, const std::string&) {}
  template<class... T> void publish_defrost_hold_start(T...) {}
  void apply_active_mode_hold(bool, const oq_odu::RetainedLevel&, const std::string&) { events.push_back("retained"); }
  void publish_optimizer_reason(const char*) {}
  bool& cooling_confirmation_pending_(bool hp) { return confirmation[hp?0:1]; }
  void set_mode(bool, bool active, bool force, int, bool, bool) {
    assert(!active || allow_active_writes);
    events.push_back(active?"mode-active":(force?"mode0-force":"mode0"));
  }
  template<class... T> void publish_candidate_blocked(T...) {}
  template<class... T> void note_cooling_start_refuse_(T...) {}
  void write_level(bool hp, int level, bool force) {
    assert(level==0 || allow_active_writes);
    events.push_back(level>0?"level-active":(force?"level0-force":"level0"));
    if(force || physical[hp?0:1]!=level) physical[hp?0:1]=level;
  }
  // ACTUATOR_METHODS
};
oq_frequency_runtime::Context frequency() {
  oq_frequency_runtime::Context f;
  f.table.variant=oq_odu::Variant::V2_NEW_MODEL;
  f.table.heating.valid=true; f.table.heating.level_count=21;
  f.table.heating.hz={0,20,26,30,36,40,45,48,52,55,60,65,68,72,76,82,85,90,95,102,110};
  return f;
}
void reset() {
  events.clear(); oq_water_temp_hard_trip_active=oq_lowflow_fault_active=false;
  oq_incident_manager={}; hp1_odu_defrost={}; hp2_odu_defrost={}; clock_ms=100000;
}
int main() {
  const auto f=frequency();
  for(bool lowflow : {true,false}) {
    reset(); RequestRuntime r;
    // Fault-free inactive requests preserve defrost even after minimum runtime ends.
    assert(r.defrost_hold_(true,f)==8 && r.defrost_hold_(false,f)==8);
    r.run_inactive_({},{},false,0,f,0,0,20);
    assert(oq_actuator_hp1_req==8 && oq_actuator_hp2_req==8);
    (lowflow?oq_lowflow_fault_active:oq_water_temp_hard_trip_active)=true;
    // A still-active minimum-runtime window cannot reintroduce either HP hold.
    assert(r.minimum_runtime_hold_(true,100000,300000,false)==0);
    assert(r.minimum_runtime_hold_(false,100000,300000,false)==0);
    r.run_inactive_({},{},false,300000,f,0,0,20);
    assert(oq_actuator_hp1_req==0 && oq_actuator_hp2_req==0);
    for(bool hp : {true,false}) for(int requested : {0,17}) {
      reset(); (lowflow?oq_lowflow_fault_active:oq_water_temp_hard_trip_active)=true;
      ActuatorRuntime a; Cycle c; c.frequency=f;
      assert(a.apply_level_(hp,requested,false,c)==0);
      assert(a.retained[hp?0:1].control_level==0 && a.physical[hp?0:1]==0);
      assert((events==std::vector<std::string>{"clear","stop","mode0-force","level0-force"}));
      // tick's transition recorder sets previous to the returned applied level.
      a.previous[hp?0:1]=0; events.clear();
      c.config.now_ms=109999; clock_ms=109999;
      assert(a.apply_level_(hp,17,false,c)==0);
      assert(events.back()=="level0");
      assert(a.safe_write[hp?0:1]==100000);
      // Pending running telemetry must not slide the retry deadline each tick.
      events.clear(); c.config.now_ms=110000; clock_ms=110000;
      assert(a.apply_level_(hp,17,false,c)==0);
      assert((events==std::vector<std::string>{"clear","stop","mode0-force","level0-force"}));
      // A normal request_stop does not set the engine's incident revalidation
      // pending flag. STOPPING itself must preserve retries after global clear.
      oq_lowflow_fault_active=oq_water_temp_hard_trip_active=false;
      events.clear(); c.config.now_ms=120000; clock_ms=120000;
      assert(a.apply_level_(hp,17,false,c)==0);
      assert((events==std::vector<std::string>{"clear","stop","mode0-force","level0-force"}));
      // Already-confirmed incident stop must not disable global-fault retry.
      auto& incident=oq_incident_manager.hp[hp?0:1]; incident.running_confirmed=false;
      incident.run_state=oq_incidents::RunState::STOPPED; incident.stop_confirmed=true;
      incident.stop_confirmation_pending=false; incident.must_stop=false;
      (lowflow?oq_lowflow_fault_active:oq_water_temp_hard_trip_active)=true;
      events.clear(); c.config.now_ms=130000; clock_ms=130000;
      assert(a.apply_level_(hp,17,false,c)==0);
      assert((events==std::vector<std::string>{"clear","mode0-force","level0-force"}));
      // Clearing the fault while an old defrost/readback remains cannot restart F0.
      oq_lowflow_fault_active=oq_water_temp_hard_trip_active=false; events.clear();
      assert(a.apply_level_(hp,17,false,c)==0 && events.empty());
    }
  }
  // Normal holds, stale defrost telemetry and the pre-existing local safety route.
  for(bool hp : {true,false}) {
    reset(); ActuatorRuntime a; Cycle c; c.frequency=f;
    assert(a.apply_level_(hp,0,false,c)==17 && events.empty());
    (hp?hp1_odu_defrost:hp2_odu_defrost).cycle.is_fresh=false;
    assert(a.apply_level_(hp,17,false,c)==17 && events.empty());
    (hp?hp1_odu_defrost:hp2_odu_defrost).cycle.fault=true;
    assert(a.apply_level_(hp,17,false,c)==0);
    assert((events==std::vector<std::string>{"clear","stop","mode0-force","level0-force"}));
  }
  // Adoption can happen after the tick snapshot, including across millis wrap.
  // Execute the real actuator with CM1's zero request: its first hold must not
  // underflow and fall through to a physical stop. Safety still wins.
  for(bool hp : {true,false}) for(uint32_t tick : {100000U, UINT32_MAX}) {
    reset(); ActuatorRuntime a; Cycle c; c.frequency=f; c.config.now_ms=tick;
    const auto slot=hp?0:1;
    a.retained[slot]={};
    auto& defrost=hp?hp1_odu_defrost:hp2_odu_defrost;
    defrost.cycle.active=defrost.hold=false;
    clock_ms=tick+1U;
    auto& boot=oq_incident_manager.boot[slot];
    boot.begin(true,tick-1000U);
    boot.observe(3999,2,clock_ms); boot.observe(1999,17,clock_ms);
    boot.observe(2099,2,clock_ms); boot.observe(2103,40,clock_ms);
    assert(boot.update(clock_ms,false,true,true,true)==17);
    assert(a.apply_level_(hp,0,false,c)==17 && events.empty());
    oq_lowflow_fault_active=true;
    assert(a.apply_level_(hp,0,false,c)==0);
    assert((events==std::vector<std::string>{"clear","stop","mode0-force","level0-force"}));
    oq_lowflow_fault_active=false; events.clear();
    oq_incident_manager.hp[slot]={};
    clock_ms+=oq_boot_resume::PROTECTION_HOLD_MS;
    c.config.now_ms=clock_ms;
    assert(a.apply_level_(hp,0,false,c)==0); // The hold expires at its real boundary.
    assert(!events.empty());
  }
  using namespace oq_power_house_dispatch;
  const DispatchTuning tuning{9000,10000,5,150,450,6,1};
  auto capture_boot = [](DispatchInput in) {
    const bool pending[2]={oq_incident_manager.boot[0].pending(in.now_ms),
                           oq_incident_manager.boot[1].pending(in.now_ms)};
    for(int slot : {0,1}) {
      auto& hp=slot==0?in.hp1:in.hp2;
      const auto& boot=oq_incident_manager.boot[slot];
      hp.candidate=candidate_during_boot_resume(hp.candidate,pending[slot],boot.level(),pending[0] || pending[1]);
      const bool held=boot.protection_hold(in.now_ms);
      hp.stop_inhibited=pending[slot] || held;
      hp.fixed_level=pending[slot]?boot.level():(held?applied_level(hp):0);
    }
    return in;
  };
  // Real readback precedes software adoption in either Duo direction. The
  // economically better idle peer must never receive a stale start request.
  for(bool hp1_running : {false,true}) {
    reset();
    hp1_odu_defrost.cycle.active=hp1_odu_defrost.hold=false;
    hp2_odu_defrost.cycle.active=hp2_odu_defrost.hold=false;
    oq_incident_manager.rest_ms[0]=oq_incident_manager.rest_ms[1]=0;
    const int slot=hp1_running?0:1, peer=1-slot;
    auto& idle_incident=oq_incident_manager.hp[peer];
    idle_incident.running_confirmed=false; idle_incident.stop_confirmed=true;
    idle_incident.run_state=oq_incidents::RunState::STOPPED;
    ActuatorRuntime a; Cycle c; c.frequency=f; a.allow_active_writes=true;
    a.previous[0]=a.previous[1]=0; a.physical[slot]=1; a.physical[peer]=0;
    a.retained[0]=a.retained[1]={};
    a.start_limits_[0].remaining=a.start_limits_[1].remaining=0;
    DispatchInput in{clock_ms,3,2000.0f,true,true,true,{},{},nullptr,nullptr};
    in.hp1.candidate.available_for_start=in.hp2.candidate.available_for_start=true;
    auto& running=hp1_running?in.hp1:in.hp2;
    auto& idle=hp1_running?in.hp2:in.hp1;
    running.levels[1]={true,true,true,2500,400,30};
    idle.levels[1]={true,true,true,2000,200,30};
    const auto unobserved=decide_dispatch(in,tuning,{});
    assert(unobserved.output_valid && unobserved.owner_hp==peer+1);
    auto& boot=oq_incident_manager.boot[slot];
    boot.begin(true,clock_ms-1000);
    boot.observe(3999,2,clock_ms); boot.observe(1999,1,clock_ms);
    boot.observe(2099,2,clock_ms); boot.observe(2103,30,clock_ms);
    assert(boot.pending(clock_ms) && a.previous[slot]==0);
    const auto captured=capture_boot(in);
    assert(!captured.hp1.candidate.available_for_start && !captured.hp2.candidate.available_for_start);
    const auto requested=decide_dispatch(captured,tuning,{});
    assert(requested.output_valid && requested.owner_hp==slot+1);
    assert((hp1_running?requested.hp2_level:requested.hp1_level)==0);
    const int adopted=boot.update(clock_ms,false,true,true,true);
    assert(adopted==1); a.previous[slot]=adopted;
    a.previous[0]=a.apply_level_(true,requested.hp1_level,false,c);
    a.previous[1]=a.apply_level_(false,requested.hp2_level,false,c);
    assert(a.previous[slot]==1 && a.previous[peer]==0);
    assert(a.physical[slot]==1 && a.physical[peer]==0);
    assert(std::find(events.begin(),events.end(),"start")==events.end());
    // A hard incident overrides both the fixed level and stop inhibition.
    running.candidate.previous_applied_level=1; running.candidate.must_stop=true;
    oq_incident_manager.hp[slot].must_stop=true; events.clear();
    const auto stopped=decide_dispatch(capture_boot(in),tuning,{});
    assert(stopped.output_valid && stopped.owner_hp==peer+1);
    assert(a.apply_level_(hp1_running,hp1_running?stopped.hp1_level:stopped.hp2_level,false,c)==0 &&
           a.physical[slot]==0);
    assert(a.apply_level_(!hp1_running,hp1_running?stopped.hp2_level:stopped.hp1_level,false,c)==1 &&
           a.physical[peer]==1);
    assert(std::count(events.begin(),events.end(),"start")==1);
  }
  // An unknown pending level blocks idle starts until abort or acquisition
  // expiry releases the one-shot policy; neither path leaves a topology lock.
  for(bool hp1_running : {false,true}) for(bool abort : {false,true}) {
    reset();
    const int slot=hp1_running?0:1, peer=1-slot;
    auto& boot=oq_incident_manager.boot[slot]; boot.begin(true,clock_ms);
    DispatchInput in{clock_ms,3,2000.0f,true,true,true,{},{},nullptr,nullptr};
    in.hp1.candidate.available_for_start=in.hp2.candidate.available_for_start=true;
    (hp1_running?in.hp1:in.hp2).levels[1]={true,true,true,2500,400,30};
    (hp1_running?in.hp2:in.hp1).levels[1]={true,true,true,2000,200,30};
    assert(boot.level()==0 && boot.pending(clock_ms));
    const auto unknown=decide_dispatch(capture_boot(in),tuning,{});
    assert(unknown.hp1_level==0 && unknown.hp2_level==0);
    if(abort) assert(boot.update(clock_ms,true,true,true,true)==0);
    else clock_ms+=oq_boot_resume::INPUT_ACQUISITION_MS;
    assert(!boot.pending(clock_ms) && boot.stop_required(clock_ms));
    in.now_ms=clock_ms;
    const auto released=decide_dispatch(capture_boot(in),tuning,{});
    assert(released.output_valid && released.owner_hp==peer+1);
    hp1_odu_defrost.cycle.active=hp1_odu_defrost.hold=false;
    hp2_odu_defrost.cycle.active=hp2_odu_defrost.hold=false;
    oq_incident_manager.rest_ms[0]=oq_incident_manager.rest_ms[1]=0;
    ActuatorRuntime a; Cycle c; c.frequency=f; c.config.now_ms=clock_ms;
    a.allow_active_writes=true; a.previous[0]=a.previous[1]=0;
    a.physical[0]=a.physical[1]=0; a.retained[0]=a.retained[1]={};
    a.start_limits_[0].remaining=a.start_limits_[1].remaining=0;
    assert(a.apply_level_(!hp1_running,hp1_running?released.hp2_level:released.hp1_level,false,c)==1 &&
           a.physical[peer]==1);
    assert(std::count(events.begin(),events.end(),"start")==1);
  }
  // The optimizer must score the boot-held F1 that the actuator will apply,
  // allowing the idle HP to supply the 2000 W missing from the held owner.
  reset();
  hp1_odu_defrost.cycle.active=hp1_odu_defrost.hold=false;
  hp2_odu_defrost.cycle.active=hp2_odu_defrost.hold=false;
  oq_incident_manager.rest_ms[0]=oq_incident_manager.rest_ms[1]=0;
  oq_incident_manager.hp[0].running_confirmed=false;
  oq_incident_manager.hp[0].stop_confirmed=true;
  oq_incident_manager.hp[0].run_state=oq_incidents::RunState::STOPPED;
  auto& boot=oq_incident_manager.boot[1]; boot.begin(true,clock_ms-1000);
  boot.observe(3999,2,clock_ms); boot.observe(1999,1,clock_ms);
  boot.observe(2099,2,clock_ms); boot.observe(2103,30,clock_ms);
  assert(boot.update(clock_ms,false,true,true,true)==1);
  DispatchInput in{clock_ms,3,4500.0f,true,true,true,{},{},nullptr,nullptr};
  in.hp1.candidate.available_for_start=in.hp2.candidate.available_for_start=true;
  in.hp2.candidate.previous_applied_level=1; in.hp2.stop_inhibited=true;
  in.hp1.levels[1]={true,true,true,2000,200,30};
  in.hp2.levels[1]={true,true,true,2500,400,30};
  in.hp2.levels[2]={true,true,true,4500,600,40};
  const auto fictitious=decide_dispatch(in,tuning,{});
  assert(fictitious.hp1_level==0 && fictitious.hp2_level==2 && fictitious.expected_w==4500);
  const auto held=capture_boot(in);
  assert(held.hp2.fixed_level==1);
  const auto feasible=decide_dispatch(held,tuning,{});
  assert(feasible.output_valid && feasible.hp1_level==1 && feasible.hp2_level==1 && feasible.expected_w==4500);
  ActuatorRuntime a; Cycle c; c.frequency=f; a.allow_active_writes=true;
  a.previous[0]=a.physical[0]=0; a.previous[1]=a.physical[1]=1;
  a.retained[0]=a.retained[1]={};
  a.start_limits_[0].remaining=a.start_limits_[1].remaining=0;
  a.previous[0]=a.apply_level_(true,feasible.hp1_level,false,c);
  a.previous[1]=a.apply_level_(false,feasible.hp2_level,false,c);
  assert(a.previous[0]==1 && a.previous[1]==1 && a.physical[0]==1 && a.physical[1]==1);
  assert(in.hp1.levels[a.previous[0]].thermal_w+in.hp2.levels[a.previous[1]].thermal_w==feasible.expected_w);
  assert(std::count(events.begin(),events.end(),"start")==1);
  clock_ms+=oq_boot_resume::PROTECTION_HOLD_MS; c.config.now_ms=clock_ms;
  in.now_ms=clock_ms; in.requested_w=6500; in.hp1.candidate.previous_applied_level=1;
  const auto expired=capture_boot(in); assert(expired.hp2.fixed_level==0);
  const auto modulated=decide_dispatch(expired,tuning,{});
  assert(modulated.output_valid && modulated.hp1_level==1 && modulated.hp2_level==2 && modulated.expected_w==6500);
  events.clear();
  assert(a.apply_level_(true,modulated.hp1_level,false,c)==1);
  assert(a.apply_level_(false,modulated.hp2_level,false,c)==2 && a.physical[1]==2);
  assert(std::find(events.begin(),events.end(),"start")==events.end());
  // Execute the real engine: repeated notification cannot rebase its stop
  // watchdog. Normal STOPPING does not imply its revalidation pending flag.
  oq_incidents::HpIncidentEngine engine;
  engine.observe_run({100000,true,true,false,true,90.0F});
  assert(engine.request_stop(100000));
  assert(!engine.outputs().must_stop && !engine.outputs().stop_confirmation_pending);
  assert(!engine.request_stop(159999));
  assert(engine.outputs().run_state==oq_incidents::RunState::STOPPING);
  assert(!engine.request_stop(160000));
  assert(engine.outputs().run_state==oq_incidents::RunState::STOP_UNCONFIRMED);
  assert(engine.outputs().must_stop && engine.outputs().stop_unconfirmed);
  puts("PASS production inactive/defrost and actuator global-stop/notification/retry lifecycle");
}
'''
        harness = harness.replace("// REQUEST_METHODS", request_methods)
        harness = harness.replace("// ACTUATOR_METHODS", actuator_methods)
        args = compiler_command("-I", str(ROOT))
        with tempfile.TemporaryDirectory(prefix="openquatt-lowflow-defrost-") as directory:
            unit, binary = Path(directory) / "test.cpp", Path(directory) / "test"
            unit.write_text(harness)
            compiled = run_process(
                args + [str(unit), "-o", str(binary)],
                timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(executed.returncode, 0, executed.stderr)


if __name__ == "__main__":
    unittest.main()
