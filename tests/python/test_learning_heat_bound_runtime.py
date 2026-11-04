"""Run the actual generation/topology adapter, including its owner config copy."""
from pathlib import Path
import tempfile
import unittest

from tests.python.test_learning_status_json_runtime import production_function
from tests.support.cpp import compiler_command, COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]


class LearningHeatBoundRuntimeTest(unittest.TestCase):
    def test_actual_adapter_single_and_duo_preserves_existing_evidence(self):
        source = (ROOT / "openquatt/includes/control/oq_ph_learning_runtime.h").read_text()
        adapter = production_function(source, "static void update_daily_heat_bound_(")
        capture = production_function(source, "void capture_(")
        self.assertLess(capture.index("update_daily_heat_bound_(state)"), capture.index("state.input ="))
        tick = production_function(source, "void tick() {")
        self.assertLess(tick.index("capture_(state,"), tick.index("state.journal.load("))
        # Existing generation/topology ownership keeps hardware changes distinct
        # from ordinary selected-source changes; the new bound is not serialized.
        self.assertIn("watch_measurement_select_(id(hp_generation))", source)
        context = production_function(source, "void build_context_(")
        self.assertIn("str(id(hp_generation).current_option())", context)
        self.assertIn("state.input.topology", context)
        with tempfile.TemporaryDirectory(prefix="oq-learning-heat-bound-") as directory:
            temporary = Path(directory)
            (temporary / "production_daily_heat_bound.inc").write_text(adapter)
            for duo in (0, 1):
                with self.subTest(duo=duo):
                    binary = temporary / f"heat-bound-{duo}"
                    built = run_process(compiler_command(
                        "-fsanitize=undefined", "-fno-sanitize-recover=all", f"-DOQ_TOPOLOGY_DUO={duo}",
                        "-I", str(temporary), str(ROOT / "tests/fixtures/learning/heat_bound_runtime.cpp"),
                        "-o", str(binary)), timeout=COMPILE_TIMEOUT_SECONDS,
                        capture_output=True, text=True)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    run = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
