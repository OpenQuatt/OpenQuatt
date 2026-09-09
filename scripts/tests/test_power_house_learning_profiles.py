import os
import re
from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]


class LearningProfileContractTest(unittest.TestCase):
    def test_export_budget_covers_endpoint_cache_and_request_snapshot(self):
        writer = (ROOT / "openquatt/includes/control/oq_ph_learning_json.h").read_text()
        endpoint = (ROOT / "components/openquatt_house_learning_status/OpenQuattHouseLearningStatus.h").read_text()
        writer_kib = int(re.search(r"kExportJsonBufferSize = (\d+)U \* 1024U", writer).group(1))
        endpoint_kib = int(re.search(r"EXPORT_BUFFER_SIZE = (\d+)U \* 1024U", endpoint).group(1))
        self.assertGreaterEqual(endpoint_kib, writer_kib)
        self.assertIn("REQUEST_BUFFER_SIZE = EXPORT_BUFFER_SIZE", endpoint)

    def test_only_q_and_waveshare_enable_the_passive_core(self):
        enabled = []
        for profile in sorted((ROOT / "openquatt/profiles").glob("*.yaml")):
            if "-DOQ_POWER_HOUSE_LEARNING_TARGET=1" in profile.read_text():
                enabled.append(profile.stem)
        self.assertEqual(enabled, ["heatpump_controller_q", "waveshare"])

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
        compiler = os.environ.get("CXX", "c++")
        for flags, should_compile in (
            ([], True),
            (["-DESP_PLATFORM", "-DOQ_POWER_HOUSE_LEARNING_TARGET=1"], True),
            (["-DESP_PLATFORM"], False),
            (["-DESP_PLATFORM", "-DOQ_POWER_HOUSE_LEARNING_TARGET=0"], False),
        ):
            with self.subTest(flags=flags):
                result = subprocess.run(
                    [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                     "-x", "c++", "-I.", *flags, "-"],
                    cwd=ROOT, input=source, text=True, capture_output=True, check=False,
                )
                if should_compile:
                    self.assertEqual(result.returncode, 0, result.stderr)
                else:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("oq_power_house", result.stderr)


if __name__ == "__main__":
    unittest.main()
