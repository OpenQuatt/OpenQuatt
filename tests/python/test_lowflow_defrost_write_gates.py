"""Execute the final Modbus YAML gates and real defrost permission method."""

from pathlib import Path
import re
import tempfile
import textwrap
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]
try:
    from .test_manual_hp_start_runtime import fixture_prefix
except ImportError:
    from test_manual_hp_start_runtime import fixture_prefix


class LowflowDefrostWriteGatesTest(unittest.TestCase):
    def test_pending_stop_survives_global_clear_at_every_final_gate(self) -> None:
        source = (ROOT / "openquatt/oq_HP_io.yaml").read_text()
        lambdas = []
        for suffix in ("set_working_mode", "compressor_level"):
            section = source[source.index("id: ${hp_id}_" + suffix + "\n"):]
            match = re.search(r"    write_lambda: \|-\n((?:      [^\n]*\n|\n)+)", section)
            self.assertIsNotNone(match, suffix)
            body = textwrap.dedent(match.group(1))
            lambdas.append(body.replace("${hp_id}", "hp1").replace("${hp_index}", "1"))
            lambdas.append(body.replace("${hp_id}", "hp2").replace("${hp_index}", "2"))
        runtime = (ROOT / "openquatt/includes/control/oq_thermal_actuator_runtime.h").read_text()
        safety = re.search(r"  bool defrost_safety_stop_\(.*?\n  }", runtime, re.S)
        self.assertIsNotNone(safety)
        gates = "\n".join("std::optional<int> gate%d(int value) {\n%s\n}" % (i, body)
                          for i, body in enumerate(lambdas))
        main = r'''
int main() {
  using Gate=std::optional<int> (*)(int);
  Gate gates[2][2]={{gate0,gate2},{gate1,gate3}};
  for (int hp=0;hp<2;++hp) {
    auto& engine=oq_incident_manager.engines[hp];
    auto& service=hp==0?hp1_odu_defrost:hp2_odu_defrost;
    engine.observe_run({100000,true,true,false,true,90.0F});
    service.cycle.observe_mode(2,clock_ms);
    service.cycle.observe_bit(true,clock_ms);
    service.cycle.step(clock_ms);
    assert(!runtime.defrost_safety_stop_(hp==0));
    for (auto gate:gates[hp]) { assert(!gate(0)); assert(!gate(17)); assert(!gate(2)); }
    // Initial global stop interrupts ownership but does not clear the ODU bit.
    oq_lowflow_fault_active=true;
    assert(runtime.defrost_safety_stop_(hp==0));
    for (auto gate:gates[hp]) { assert(gate(0)==0); assert(!gate(17)); assert(!gate(2)); }
    assert(service.cycle.observed_active() && service.cycle.interrupted);
    oq_lowflow_fault_active=false;
    oq_water_temp_hard_trip_active=true;
    assert(runtime.defrost_safety_stop_(hp==0));
    for (auto gate:gates[hp]) { assert(gate(0)==0); assert(!gate(17)); assert(!gate(2)); }
    assert(engine.request_stop(clock_ms));
    oq_water_temp_hard_trip_active=false;
    assert(engine.outputs().run_state==oq_incidents::RunState::STOPPING);
    assert(!engine.outputs().must_stop && !engine.outputs().stop_confirmation_pending);
    // Acknowledgements are lost; ordinary STOPPING remains authoritative.
    assert(runtime.defrost_safety_stop_(hp==0));
    assert(service.normal_write_allowed(0,runtime.defrost_safety_stop_(hp==0)));
    for (auto gate:gates[hp]) { assert(gate(0)==0); assert(!gate(17)); assert(!gate(2)); }
    clock_ms+=10000;
    service.cycle.observe_mode(2,clock_ms); service.cycle.observe_bit(true,clock_ms);
    assert(!engine.request_stop(clock_ms));
    for (auto gate:gates[hp]) assert(gate(0)==0);
    // Stop watchdog release still protects STOP_UNCONFIRMED.
    clock_ms+=50000;
    assert(!engine.request_stop(clock_ms));
    assert(engine.outputs().run_state==oq_incidents::RunState::STOP_UNCONFIRMED);
    service.cycle.observe_mode(2,clock_ms); service.cycle.observe_bit(true,clock_ms);
    for (auto gate:gates[hp]) { assert(gate(0)==0); assert(!gate(17)); assert(!gate(2)); }
    // With no pending stop/global fault, normal defrost hold remains intact.
    engine=oq_incidents::HpIncidentEngine{};
    service.cycle=oq_defrost::Cycle{};
    service.cycle.observe_mode(2,clock_ms); service.cycle.observe_bit(true,clock_ms);
    service.cycle.step(clock_ms);
    assert(!runtime.defrost_safety_stop_(hp==0));
    for (auto gate:gates[hp]) { assert(!gate(0)); assert(!gate(17)); assert(!gate(2)); }
    service.cycle.observe_bit(false,clock_ms); service.cycle.phase=oq_defrost::Phase::IDLE;
    (hp==0?hp1_compressor_level:hp2_compressor_level).selected=8;
    for (auto gate:gates[hp]) assert(gate(0)==0);
    assert(gates[hp][0](2)==2); assert(gates[hp][1](8)==8);
  }
}
'''
        # Execute the unified production guarded_write, not a stand-in gate.
        setup = """
  hp_generation.option="V2";
  hp1_is_online=hp2_is_online=true;
  hp1_odu_generation_detection_complete=hp2_odu_generation_detection_complete=true;
  hp1_generation_variant_code=hp2_generation_variant_code=static_cast<int>(oq_odu::Variant::V2_NEW_MODEL);
  oq_day_max_frequency_hz.state=90;
  oq_odu::RuntimeFrequencySnapshot snapshot;
  snapshot.variant=oq_odu::Variant::V2_NEW_MODEL; snapshot.heating.valid=true; snapshot.heating.level_count=21;
  snapshot.heating.hz={0,20,26,30,36,40,45,48,52,55,60,65,68,72,76,82,85,90,95,102,110};
  hp1_runtime_frequency_snapshot_storage=hp2_runtime_frequency_snapshot_storage=oq_odu::encode_runtime_frequency_snapshot(snapshot);
"""
        main = main.replace("int main() {", "int main() {" + setup)
        unit = fixture_prefix(real_incidents=True) + "\n#define clock_ms test_now_ms\n#define OQ_ACTUATOR_SECONDARY_ID(x) hp2_##x\nstruct Runtime {\n" + safety.group() + "\n} runtime;\n" + gates + "\n" + main
        args = compiler_command("-I", str(ROOT))
        with tempfile.TemporaryDirectory(prefix="openquatt-defrost-write-gates-") as directory:
            source, binary = Path(directory) / "test.cpp", Path(directory) / "test"
            source.write_text(unit)
            built = run_process(
                args + [str(source), "-o", str(binary)],
                timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            result = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
