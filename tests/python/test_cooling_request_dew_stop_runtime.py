"""Execute full production request arbitration and retained-hold methods."""
from pathlib import Path
import re
import tempfile
import unittest
from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process
ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "openquatt/includes/control/oq_thermal_request_runtime.h"
FIXTURE = ROOT / "tests/fixtures/cooling/request_dew_stop.cpp"
METHODS = ("measured_thermal_", "selected_level_", "last_applied_", "mode_is_cooling_", "target_is_cooling_", "dew_stop_blocks_retained_", "cooling_hold_", "hold_mode_", "defrost_hold_", "minimum_runtime_hold_", "run_manual_", "run_inactive_", "run_automatic_", "apply_minimum_runtime_")
class CoolingRequestDewStopRuntimeTest(unittest.TestCase):
    def test_full_request_paths_do_not_resurrect_confirmed_dew_stop(self):
        source = RUNTIME.read_text()
        methods = []
        for name in METHODS:
            matches = re.findall(r"  (?:int|void|bool) " + name + r"\(.*?\n  }", source, re.S)
            self.assertEqual(len(matches), 1, name)
            methods.append(matches[0])
        harness = FIXTURE.read_text().replace("// PRODUCTION_METHODS", "\n".join(methods))
        with tempfile.TemporaryDirectory(prefix="openquatt-cooling-request-") as directory:
            src = Path(directory) / "test.cpp"
            src.write_text(harness)
            for duo in (1, 0):
                with self.subTest(duo=duo):
                    binary = Path(directory) / f"test-{duo}"
                    built = run_process(compiler_command("-I", str(ROOT), f"-DOQ_TOPOLOGY_DUO={duo}", str(src), "-o", str(binary)), capture_output=True, text=True, timeout=COMPILE_TIMEOUT_SECONDS)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    ran = run_process([str(binary)], capture_output=True, text=True, timeout=RUN_TIMEOUT_SECONDS)
                    self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
if __name__ == "__main__":
    unittest.main()
