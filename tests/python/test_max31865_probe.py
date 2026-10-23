from pathlib import Path
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/fixtures/max31865_probe"
COMPONENT = ROOT / "components/max31865"


class Max31865ProbeTest(unittest.TestCase):
    def test_production_driver_sequence_and_fault_boundaries(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            for name in (
                "esphome/core/component.h",
                "esphome/core/log.h",
                "esphome/components/sensor/sensor.h",
                "esphome/components/spi/spi.h",
            ):
                stub = temp / name
                stub.parent.mkdir(parents=True, exist_ok=True)
                stub.write_text('#include "fake_esphome.h"\n')
            executable = temp / "probe_test"
            run_process(
                [
                    *compiler_command(),
                    f"-I{temp}", f"-I{FIXTURE}", str(FIXTURE / "probe_test.cpp"),
                    str(COMPONENT / "max31865.cpp"), "-o", str(executable),
                ],
                check=True,
                timeout=COMPILE_TIMEOUT_SECONDS,
            )
            run_process([str(executable)], check=True, timeout=RUN_TIMEOUT_SECONDS)


if __name__ == "__main__":
    unittest.main()
