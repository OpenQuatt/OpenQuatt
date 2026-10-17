"""Execute actual actuator mode helpers, final command gates and restart policy."""
from pathlib import Path
import re
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]


def method(source, name):
    match = re.search(r"  (?:int|void|bool) " + name + r"\(.*?\n  }", source, re.S)
    if not match:
        raise AssertionError(name)
    return match.group()


def fixture_prefix(real_incidents=False):
    # Reuse peripheral declarations from the executable command-runtime fixture;
    # the command gate itself and the defrost permission remain production code.
    host = (ROOT / "tests/host/oq_compressor_command_runtime_test.cpp").read_text()
    prefix = host[:host.index("namespace {")].replace('../../openquatt/', 'openquatt/')
    service = (ROOT / "components/openquatt_odu_defrost/OpenQuattOduDefrost.cpp").read_text()
    permission = re.search(r"bool OpenQuattOduDefrost::normal_write_allowed\(.*?\n}", service, re.S).group()
    permission = permission.replace("OpenQuattOduDefrost::", "")
    old = re.search(r"  bool normal_write_allowed\(.*?\n  }", prefix, re.S).group()
    prefix = prefix.replace(old, permission)
    prefix = prefix.replace("bool holding() const { return hold; }", "bool holding() const { return hold || cycle.owns(); }")
    prefix = prefix.replace("  oq_defrost::Cycle cycle{};", """  bool loading_{}, trigger_ready_{}, save_after_load_{}, save_ready_{}, save_writing_{}, save_verifying_{};
  bool forced_write_pending_{};
  void clear_tx_queue_for_device() { ++queue_clear_calls; }
  void finish_profile_(bool) {}
  void release() {}
  oq_defrost::Cycle cycle{};""")
    prefix = prefix.replace('#undef id', '')
    prefix = '#include <cstdint>\nuint32_t millis();\n' + prefix
    if real_incidents:
        old = re.search(r"struct FakeIncidentManager \{.*?\n};", prefix, re.S).group()
        prefix = prefix.replace(old, """struct FakeIncidentManager {
  oq_incidents::HpIncidentEngine engines[2]{oq_incidents::HpIncidentEngine{}, oq_incidents::HpIncidentEngine{}};
  auto get_outputs(unsigned hp) const { return engines[hp-1].outputs(); }
  bool startup_inhibited(unsigned) const { return false; }
};""")
    else:
        prefix = prefix.replace('struct FakeIncidentManager {', """#include "openquatt/includes/control/oq_hp_restart_guard.h"
struct FakeIncidentManager {
  oq_hp_restart_guard::Policy credit[2];
  void invalidate_restart_credit(unsigned hp) { credit[hp-1].invalidate(); }""")
    return prefix


def compile_run(test, unit, expected_success=True, duo=True):
    args = compiler_command("-DOQ_TOPOLOGY_DUO=" + str(int(duo)), "-I", str(ROOT))
    with tempfile.TemporaryDirectory(prefix="openquatt-manual-start-") as directory:
        source, binary = Path(directory) / "test.cpp", Path(directory) / "test"
        source.write_text(unit)
        built = run_process(
            args + [str(source), "-o", str(binary)],
            timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
        )
        test.assertEqual(built.returncode, 0, built.stderr)
        result = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
        if expected_success:
            test.assertEqual(result.returncode, 0, result.stderr)
        else:
            test.assertNotEqual(result.returncode, 0, "pre-fix negative control unexpectedly passed")
            test.assertIn("policy.remaining_ms(test_now_ms)==0", result.stderr)


class ManualHpStartRuntimeTest(unittest.TestCase):
    def unit(self):
        prefix = fixture_prefix()
        prefix = prefix.replace('struct FakeSelect {', 'struct FakeSelect;\nstruct ModeCall { FakeSelect* target; std::string option; void set_option(const char* value) { option=value; } void perform(); };\nstruct FakeSelect {')
        prefix = prefix.replace('  bool has_state() const { return present; }\n  const std::string&', '  bool has_state() const { return present; }\n  ModeCall make_call() { return {this, {}}; }\n  const std::string&')
        prefix += r'''
#include <cstring>
#include "openquatt/includes/control/oq_thermal_request_logic.h"
#define OQ_ACTUATOR_SECONDARY_ID(x) hp2_##x
void ModeCall::perform() {
  const bool hp1 = target == &hp1_set_working_mode;
  const int value = option=="Heating" ? 2 : option=="Cooling" ? 1 : 0;
  if (oq_command_runtime::guarded_write(hp1, 3999, value)) target->option=option;
}
struct Runtime {
// METHODS
};
int main() {
  Runtime runtime;
  oq_manual_hp_mode_allowed=true;
  for (const bool hp1 : {true,false}) {
    #if !OQ_TOPOLOGY_DUO
    if (!hp1) continue;
    #endif
    const unsigned index=hp1 ? 0 : 1;
    auto& policy=oq_incident_manager.credit[index];
    auto& select=hp1 ? hp1_set_working_mode : hp2_set_working_mode;
    auto& levels=hp1 ? hp1_compressor_level : hp2_compressor_level;
    auto& service=hp1 ? hp1_odu_defrost : hp2_odu_defrost;
    auto& revalidate=hp1 ? hp1_compressor_command_revalidation_required : hp2_compressor_command_revalidation_required;
    (hp1 ? oq_manual_hp1_mode_code : oq_manual_hp2_mode_code)=2;
    service.cycle.observe_mode(0,test_now_ms);
    service.cycle.observe_bit(0,test_now_ms);
    select.option="Standby"; levels.selected=0;
    assert(policy.configure(240000,30000));
    assert(policy.restore_credit(240000,true)); policy.observe_stopped(test_now_ms);
    // F0 manual selection and a rejected queued Heating write set the retry flag.
    const uint8_t queued_heating[]{6,15,159,0,2};
    assert(!oq_command_runtime::queued_command(hp1,queued_heating,sizeof(queued_heating)));
    assert(revalidate);
    for (int retry=0;retry<3;++retry) {
      runtime.set_mode(hp1,false,false,2,true,true);
      assert(select.option=="Standby");
      assert(policy.remaining_ms(test_now_ms)==0);
      test_now_ms+=1000; policy.observe_stopped(test_now_ms);
    }
    // A startup-inhibited positive intent also remains Standby without consuming credit.
    oq_incident_manager.inhibited[index]=true;
    runtime.set_mode(hp1,true,true,2,true,true);
    assert(select.option=="Standby" && policy.can_start(test_now_ms));
    oq_incident_manager.inhibited[index]=false;
    // A blocked minimum-off actuator supplies active=false; manual intent cannot bypass it.
    policy.invalidate(); policy.observe_stopped(test_now_ms);
    runtime.set_mode(hp1,false,true,2,true,true);
    assert(select.option=="Standby" && policy.remaining_ms(test_now_ms)==240000);
    // Actual permitted positive output consumes credit, then the real final gate allows mode.
    assert(policy.configure(240000,30000)); assert(policy.restore_credit(240000,true));
    policy.observe_stopped(test_now_ms);
    levels.selected=17;
    (hp1 ? hp1_is_online : hp2_is_online)=true;
    (hp1 ? hp1_odu_generation_detection_complete : hp2_odu_generation_detection_complete)=true;
    (hp1 ? hp1_generation_variant_code : hp2_generation_variant_code)=static_cast<int>(oq_odu::Variant::V2_NEW_MODEL);
    oq_odu::RuntimeFrequencySnapshot snapshot;
    snapshot.variant=oq_odu::Variant::V2_NEW_MODEL; snapshot.heating.valid=true; snapshot.heating.level_count=21;
    snapshot.heating.hz={0,20,26,30,36,40,45,48,52,55,60,65,68,72,76,82,85,90,95,102,110};
    (hp1 ? hp1_runtime_frequency_snapshot_storage : hp2_runtime_frequency_snapshot_storage)=oq_odu::encode_runtime_frequency_snapshot(snapshot);
    oq_day_max_frequency_hz.state=90;
    runtime.set_mode(hp1,true,true,2,true,true);
    assert(select.option=="Heating"); assert(policy.remaining_ms(test_now_ms)==240000);
    // Safety stop wins even when stale positive manual intent survives.
    oq_lowflow_fault_active=true;
    runtime.set_mode(hp1,false,true,2,true,true);
    assert(select.option=="Standby");
    assert(!oq_command_runtime::guarded_write(hp1,3999,2));
    oq_lowflow_fault_active=false;
  }
}
'''
        source = (ROOT / "openquatt/includes/control/oq_thermal_actuator_runtime.h").read_text()
        methods = "\n".join(method(source, name) for name in ("requested_mode_code", "set_mode", "write_mode_option_", "defrost_safety_stop_"))
        return prefix.replace("// METHODS", methods)

    def test_real_helpers_preserve_credit_until_authorized_positive_output(self):
        for duo in (False, True):
            with self.subTest(duo=duo):
                compile_run(self, self.unit(), duo=duo)

    def test_pre_fix_manual_override_reproduces_lost_credit(self):
        unit = self.unit().replace("if (active && manual_service_active &&", "if (manual_service_active &&")
        self.assertNotEqual(unit, self.unit())
        compile_run(self, unit, expected_success=False)


if __name__ == "__main__":
    unittest.main()
