"""Exercise the complete production boot adapter before an ESPHome logger exists."""

from pathlib import Path
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]


class NvsBootLoggingTest(unittest.TestCase):
    def test_ready_migration_and_halt_without_esphome_logger(self):
        with tempfile.TemporaryDirectory(prefix="openquatt-nvs-boot-") as directory:
            temp = Path(directory)
            headers = ["esphome/core/defines.h", "esp_flash.h", "esp_flash_encrypt.h",
                       "esp_heap_caps.h", "esp_image_format.h", "esp_log.h", "esp_rom_sys.h",
                       "esp_chip_info.h", "esp_ota_ops.h", "esp_secure_boot.h", "esp_system.h",
                       "freertos/FreeRTOS.h", "freertos/task.h", "nvs.h", "nvs_flash.h"]
            for name in headers:
                path = temp / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("#pragma once\n")
            binary = temp / "boot"
            command = compiler_command("-I", str(temp), "-I", str(ROOT),
                str(ROOT / "tests/fixtures/nvs_boot_logging.cpp"), "-o", str(binary))
            compiled = run_process(command, capture_output=True, text=True,
                                   timeout=COMPILE_TIMEOUT_SECONDS)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = run_process([str(binary)], capture_output=True, text=True,
                                  timeout=RUN_TIMEOUT_SECONDS)
            self.assertEqual(executed.returncode, 0, executed.stderr)
            self.assertIn("all seven ROM log sites", executed.stdout)


if __name__ == "__main__":
    unittest.main()
