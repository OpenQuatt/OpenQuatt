import re
from pathlib import Path
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]


class LearningProfileContractTest(unittest.TestCase):
    def test_export_budget_covers_endpoint_cache_and_request_snapshot(self):
        writer = (ROOT / "openquatt/includes/control/oq_ph_learning_json.h").read_text()
        endpoint = (ROOT / "components/openquatt_house_learning_status/OpenQuattHouseLearningStatus.h").read_text()
        writer_kib = int(re.search(r"kExportJsonBufferSize = (\d+)U \* 1024U", writer).group(1))
        endpoint_kib = int(re.search(r"EXPORT_BUFFER_SIZE = (\d+)U \* 1024U", endpoint).group(1))
        self.assertGreaterEqual(endpoint_kib, writer_kib)
        self.assertIn("REQUEST_BUFFER_SIZE = EXPORT_BUFFER_SIZE", endpoint)

    def test_source_changes_are_not_physical_context_boundaries(self):
        runtime = (ROOT / "openquatt/includes/control/oq_ph_learning_runtime.h").read_text()
        context_builder = runtime.split("void build_context_(RuntimeStorage& state)", 1)[1].split(
            "static size_t slot_offset_", 1
        )[0]
        for source in (
            "room_temp_source", "room_setpoint_source", "outside_temp_source", "flow_source",
            "oq_duo_outdoor_flow_mode", "oq_q_flow_source",
        ):
            self.assertNotIn(f"watch_measurement_select_(id({source}))", runtime)
            self.assertNotIn(source, context_builder)
        self.assertNotIn("cic_feed_url", context_builder)
        # These listeners may revoke a diagnostic snapshot. They must not
        # change the physical measurement context or discard a learning day.
        cic_callback = runtime.split("id(cic_feed_url).add_on_state_callback", 1)[1].split("});", 1)[0]
        source_watch = runtime.split("void watch_shadow_source_", 1)[1]
        source_callback = source_watch.split("entity.add_on_state_callback", 1)[1].split("});", 1)[0]
        for callback in (cic_callback, source_callback):
            self.assertEqual(callback.split("{", 1)[1].strip(),
                             "this->invalidate_shadow_(oq_power_house::ShadowDemandReason::SOURCE_CHANGED);")
        invalidate = runtime.split("void invalidate_shadow_", 1)[1].split("}", 1)[0]
        self.assertEqual(invalidate.split("{", 1)[1].strip(),
                         "oq_power_house_runtime::runtime().invalidate_demand_evaluation(reason);")
        self.assertNotIn("watch_source_select_", runtime)
        self.assertNotIn("observe_source_revisions(", runtime)
        self.assertNotIn("observe_valid_source_revisions(", runtime)
        self.assertIn("watch_measurement_select_(id(hp_generation))", runtime)
        self.assertIn("watch_measurement_select_(id(oq_controller_flow_meter))", runtime)
        self.assertIn("watch_measurement_number_(id(hp1_water_in_temp_offset))", runtime)
        self.assertIn("kLearningMeasurementContextMarker", context_builder)

    def test_only_q_enables_the_passive_core(self):
        enabled = []
        for profile in sorted((ROOT / "openquatt/profiles").glob("*.yaml")):
            if "-DOQ_POWER_HOUSE_LEARNING_TARGET=1" in profile.read_text():
                enabled.append(profile.stem)
        self.assertEqual(enabled, ["heatpump_controller_q"])

    def test_shadow_references_require_the_explicitly_enabled_target(self):
        for file in ("includes/control/oq_power_house_runtime.h", "oq_common.yaml", "oq_warmup.yaml",
                     "oq_power_house_strategy.yaml"):
            with self.subTest(file=file):
                source = (ROOT / "openquatt" / file).read_text()
                guards = re.findall(r"^\s*(#if [^\n]*OQ_POWER_HOUSE_LEARNING_TARGET[^\n]*)$", source, re.M)
                self.assertTrue(guards)
                for guard in guards:
                    self.assertEqual(guard,
                                     "#if defined(OQ_POWER_HOUSE_LEARNING_TARGET) && OQ_POWER_HOUSE_LEARNING_TARGET == 1")

    def test_shadow_publication_runs_after_control_returns(self):
        source = (ROOT / "openquatt/oq_power_house_strategy.yaml").read_text()
        control = source.index("oq_power_house_runtime::runtime().tick(")
        refresh = source.index("oq_ph_learning::runtime().refresh_shadow();")
        self.assertIn("});", source[control:refresh])
        self.assertNotIn("delay:", source[control:refresh])
        runtime = (ROOT / "openquatt/includes/control/oq_ph_learning_runtime.h").read_text()
        self.assertIn("__attribute__((noinline)) void refresh_shadow()", runtime)

    def test_firmware_without_explicit_profile_has_no_learning_types(self):
        source = '''
#include "openquatt/includes/learning/oq_ph_passive_runtime_logic.h"
#include "openquatt/includes/learning/oq_ph_model_validation.h"
int main() {
  oq_power_house::learning::ThermalModelState thermal;
  oq_power_house::learning::PassiveRuntimeStorage learner;
  return thermal.initialized || learner.initialized;
}
'''
        for flags, should_compile in (
            ([], True),
            (["-DESP_PLATFORM", "-DOQ_POWER_HOUSE_LEARNING_TARGET=1"], True),
            (["-DESP_PLATFORM"], False),
            (["-DESP_PLATFORM", "-DOQ_POWER_HOUSE_LEARNING_TARGET=0"], False),
        ):
            with self.subTest(flags=flags):
                result = run_process(
                    compiler_command("-fsyntax-only", "-x", "c++", "-I.", *flags, "-"),
                    timeout=COMPILE_TIMEOUT_SECONDS, cwd=ROOT, input=source, text=True, capture_output=True, check=False,
                )
                if should_compile:
                    self.assertEqual(result.returncode, 0, result.stderr)
                else:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("oq_power_house", result.stderr)


if __name__ == "__main__":
    unittest.main()
