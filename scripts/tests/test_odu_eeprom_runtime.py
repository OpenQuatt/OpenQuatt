from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "scripts/tests/fixtures"


class OduEepromRuntimeTest(unittest.TestCase):
    def test_production_runtime_with_durable_storage_and_modbus_failures(self):
        compiler = shutil.which("c++")
        if compiler is None:
            self.skipTest("C++ compiler unavailable")
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
            compiler_args = [compiler, "-std=c++20"]
            if sys.platform == "darwin":
                sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
                compiler_args += ["-isystem", str(Path(sdk) / "usr/include/c++/v1")]
            compiled = subprocess.run(compiler_args + [
                "-I", str(temporary), "-I", str(FIXTURES),
                "-I", str(ROOT), "-I", str(ROOT / "openquatt"),
                str(FIXTURES / "odu_eeprom_runtime.cpp"), "-o", str(binary),
            ], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            completed = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            self.assertIn("production runtime: PASS", completed.stdout)


if __name__ == "__main__":
    unittest.main()
