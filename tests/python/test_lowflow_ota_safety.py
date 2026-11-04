"""Execute the actual capture, YAML OTA reader and OTA policy together."""
from pathlib import Path
import re
import tempfile
import textwrap
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

try:
    from .test_manual_hp_start_runtime import fixture_prefix
except ImportError:
    from test_manual_hp_start_runtime import fixture_prefix

ROOT = Path(__file__).resolve().parents[2]


def source_unit():
    prefix = fixture_prefix()
    # Peripheral incident outputs are controlled inputs; candidate eligibility
    # itself and the boot readback/adoption policy remain production code.
    prefix = prefix.replace("struct FakeIncidentOutput {", """struct FakeIncidentOutput {
  oq_incidents::LinkState link_state{oq_incidents::LinkState::HEALTHY};
  bool available_for_start{true}, fault_active{false}, protection_active{false};""")
    manager = (ROOT / "components/openquatt_incident_manager/OpenQuattIncidentManager.cpp").read_text()
    candidate = re.search(r"bool OpenQuattIncidentManager::ota_resume_candidate\(.*?\n}", manager, re.S)
    if candidate is None:
        raise AssertionError("Production ota_resume_candidate not found")
    candidate = candidate.group().replace("OpenQuattIncidentManager::", "")
    old_manager = re.search(r"struct FakeIncidentManager \{.*?\n};", prefix, re.S)
    if old_manager is None:
        raise AssertionError("Host incident peripheral not found")
    replacement = r'''struct FakeIncidentManager {
  struct UnitState { oq_boot_resume::Policy boot_resume{}; };
  std::array<UnitState,2> units{};
  std::array<FakeIncidentOutput,2> outputs{};
  std::array<bool,2> inhibited{};
  const UnitState* unit_(uint8_t hp) const { return hp>=1 && hp<=2 ? &units[hp-1] : nullptr; }
  FakeIncidentOutput get_outputs(unsigned hp) const { return outputs[hp-1]; }
  bool startup_inhibited(unsigned hp) const { return inhibited[hp-1]; }
  bool boot_resume_pending(unsigned hp,uint32_t now) const { return units[hp-1].boot_resume.pending(now); }
  int boot_resume_level(unsigned hp) const { return units[hp-1].boot_resume.level(); }
  int update_boot_resume(unsigned hp,bool abort,bool ready,bool inputs,bool adopt,uint32_t now) {
    return units[hp-1].boot_resume.update(now,abort,ready,inputs,adopt);
  }
''' + candidate + "\n};"
    prefix = prefix.replace(old_manager.group(), replacement)
    common = (ROOT / "openquatt/oq_common.yaml").read_text()
    reader = re.search(r"set_ota_resume_guard_reader\((\[\]\(\) \{.*?\n          \})\);", common, re.S)
    if reader is None:
        raise AssertionError("Production oq_common OTA guard reader not found")
    handoff = (ROOT / "components/openquatt_incident_manager/OpenQuattRestartHandoff.cpp").read_text()
    hold = re.search(r"bool OpenQuattOtaHandoff::cycle_hold\(.*?\n}", handoff, re.S)
    if hold is None:
        raise AssertionError("Production cycle_hold not found")
    hold = hold.group().replace("OpenQuattOtaHandoff::", "")
    return prefix + r'''
#include <cstdio>
#include "components/openquatt_incident_manager/OpenQuattRestartHandoffPolicy.h"
auto production_reader = ''' + textwrap.dedent(reader.group(1)) + r''';
uint8_t (*ota_guard_reader)() = production_reader;
struct Handoff {
  esphome::openquatt_incident_manager::restart_handoff::OtaResumePolicy ota_resume_;
''' + hold + r'''
} handoff;
bool permission_valid=true, permission_on=true, flow_fresh=true;
float selected_flow=1000;
int failures=0;
void require(bool okay,const char* what,unsigned hp,const char* cause) {
  if(!okay) { ++failures; std::fprintf(stderr,"HP%u %s: %s\n",hp,cause,what); }
}
void setup() {
  test_now_ms=100000;
  oq_lowflow_fault_active=oq_water_temp_hard_trip_active=false;
  oq_commissioning_active=oq_manual_hp_active=false;
  oq_enabled.state=true; oq_control_mode_code=2; oq_strategy_active_code=3;
  permission_valid=permission_on=flow_fresh=true; selected_flow=1000;
  oq_incident_manager={}; hp1_odu_defrost={}; hp2_odu_defrost={}; handoff={};
  hp_generation.option="V2"; oq_day_max_frequency_hz.state=90;
  hp1_is_online=hp2_is_online=true;
  hp1_odu_generation_detection_complete=hp2_odu_generation_detection_complete=true;
  hp1_odu_generation_revalidation_required=hp2_odu_generation_revalidation_required=false;
  hp1_runtime_frequency_revalidation_required=hp2_runtime_frequency_revalidation_required=false;
  hp1_generation_variant_code=hp2_generation_variant_code=static_cast<int>(oq_odu::Variant::V2_NEW_MODEL);
  oq_odu::RuntimeFrequencySnapshot frequency;
  frequency.variant=oq_odu::Variant::V2_NEW_MODEL; frequency.heating.valid=true; frequency.heating.level_count=21;
  frequency.heating.hz={0,20,26,30,36,40,45,48,52,55,60,65,68,72,76,82,85,90,95,102,110};
  hp1_runtime_frequency_snapshot_storage=hp2_runtime_frequency_snapshot_storage=oq_odu::encode_runtime_frequency_snapshot(frequency);
  oq_sources::hp1={}; oq_sources::hp2={};
  for(auto* raw : {&oq_sources::hp1,&oq_sources::hp2}) {
    raw->water_in.observe(32,test_now_ms,true); raw->water_out.observe(38,test_now_ms,true);
    raw->outside.observe(8,test_now_ms,true); raw->working_mode.observe(2,test_now_ms,true);
    raw->compressor_frequency.observe(35,test_now_ms,true); raw->defrost.observe(0,test_now_ms,true);
    raw->status_2119.observe(0,test_now_ms,true);
  }
  // Real boot readbacks and adoption, not a stubbed boot_resume_level/ready flag.
  for(auto& unit : oq_incident_manager.units) {
    auto& boot=unit.boot_resume; boot.begin(true,test_now_ms);
    boot.observe(3999,2,test_now_ms); boot.observe(1999,8,test_now_ms);
    boot.observe(2099,2,test_now_ms); boot.observe(2103,35,test_now_ms);
    assert(boot.readback_ready(test_now_ms));
    assert(boot.update(test_now_ms,false,true,true,true)==8);
    assert(!boot.pending(test_now_ms) && boot.protection_hold(test_now_ms));
  }
  oq_boot_resume_runtime::minimum_flow_lph=700;
  oq_boot_resume_runtime::read_sources=[](uint64_t) {
    return oq_boot_resume_runtime::Sources{permission_valid,permission_on,flow_fresh,selected_flow};
  };
  oq_boot_resume_runtime::ota_hold_reader=[](uint8_t hp,uint32_t now) { return handoff.cycle_hold(hp,now); };
}
void start_healthy_ota(unsigned hp) {
  const auto before=oq_boot_resume_runtime::capture(hp==1);
  assert(!before.abort && before.ready);
  assert(oq_incident_manager.ota_resume_candidate(hp,test_now_ms));
  const uint8_t mask=production_reader(); assert((mask & (1U<<(hp-1)))!=0);
  handoff.ota_resume_.begin(123,test_now_ms,mask);
  assert(oq_boot_resume_runtime::ota_hold(hp==1));
}
int main() {
  for(unsigned hp : {1U,2U}) {
    setup(); start_healthy_ota(hp);
    test_now_ms+=1000;
    require(oq_boot_resume_runtime::ota_hold(hp==1),"healthy OTA must retain hold",hp,"healthy");
    for(const bool lowflow : {true,false}) {
      setup(); start_healthy_ota(hp);
      (lowflow?oq_lowflow_fault_active:oq_water_temp_hard_trip_active)=true;
      const char* cause=lowflow?"lowflow":"water-trip";
      const auto safety=oq_boot_resume_runtime::capture(hp==1);
      // Independent source readiness can remain true; the explicit abort must
      // nevertheless be honored by the real YAML reader and OTA holding policy.
      require(safety.abort && safety.ready,"fixture must expose actual abort=true/ready=true",hp,cause);
      const uint8_t mask=production_reader();
      require((mask & (1U<<(hp-1)))==0,"safety abort must remove OTA resume eligibility",hp,cause);
      require(!oq_boot_resume_runtime::ota_hold(hp==1),"safety abort must release OTA hold",hp,cause);
    }
    for(unsigned invalid=0;invalid<5;++invalid) {
      setup(); start_healthy_ota(hp);
      if(invalid==0) permission_valid=false;
      if(invalid==1) permission_on=false;
      if(invalid==2) flow_fresh=false;
      if(invalid==3) selected_flow=300;
      if(invalid==4) (hp==1?oq_sources::hp1:oq_sources::hp2).water_in.observe(NAN,test_now_ms,false);
      require(!oq_boot_resume_runtime::capture(hp==1).ready,"invalid inputs must reject readiness",hp,"invalid-input");
      require((production_reader() & (1U<<(hp-1)))==0,"invalid inputs must reject eligibility",hp,"invalid-input");
      require(!oq_boot_resume_runtime::ota_hold(hp==1),"invalid inputs must release hold",hp,"invalid-input");
    }
  }
  if(failures) std::fprintf(stderr,"TOTAL_OTA_SAFETY_FAILURES=%d\n",failures);
  return failures?1:0;
}
'''


class LowflowOtaSafetyTest(unittest.TestCase):
    def test_actual_ota_reader_releases_hold_on_lowflow_and_water_trip(self):
        with tempfile.TemporaryDirectory(prefix="openquatt-ota-safety-") as directory:
            source, binary = Path(directory) / "test.cpp", Path(directory) / "test"
            source.write_text(source_unit())
            built = run_process(compiler_command("-DOQ_TOPOLOGY_DUO=1", "-I", str(ROOT), str(source), "-o", str(binary)),
                                timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
