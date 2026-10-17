from pathlib import Path
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures"


class OduEepromRuntimeTest(unittest.TestCase):
    def test_production_runtime_with_durable_storage_and_modbus_failures(self):
        wrappers = (
            "esp_partition.h", "nvs.h", "PsramBuffer.h", "esphome/core/log.h",
            "esphome/components/factory_reset/factory_reset.h",
            "esphome/components/openquatt_odu_settings/OpenQuattOduSettings.h",
            "esphome/components/openquatt_odu_defrost/OpenQuattOduDefrost.h",
        )
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            for wrapper in wrappers:
                header = temporary / wrapper
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#include "odu_eeprom_stubs.h"\n')
            binary = temporary / "odu_eeprom_runtime"
            compiled = run_process(compiler_command("-std=c++20") + [
                "-I", str(temporary), "-I", str(FIXTURES),
                "-I", str(ROOT), "-I", str(ROOT / "openquatt"),
                str(FIXTURES / "odu_eeprom_runtime.cpp"), "-o", str(binary),
            ], timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            completed = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            self.assertIn("production runtime: PASS", completed.stdout)


if __name__ == "__main__":
    unittest.main()
