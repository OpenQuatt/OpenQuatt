"""Exercise the real MAX31865 driver with fault injection and muted logging."""

from pathlib import Path
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/fixtures/max31865_counters"


class Max31865CountersTest(unittest.TestCase):
    def test_fault_bits_and_measurement_sequence(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            for name in (
                "esphome/core/component.h",
                "esphome/core/log.h",
                "esphome/components/sensor/sensor.h",
                "esphome/components/spi/spi.h",
            ):
                stub = temp / name
                stub.parent.mkdir(parents=True, exist_ok=True)
                stub.write_text('#include "fake_esphome.h"\n')
            executable = temp / "counters_test"
            run_process(
                [*compiler_command("-Wno-unused-variable"), f"-I{temp}", f"-I{FIXTURE}",
                 str(FIXTURE / "counters_test.cpp"),
                 str(ROOT / "components/max31865/max31865.cpp"), "-o", str(executable)],
                check=True, timeout=COMPILE_TIMEOUT_SECONDS,
            )
            run_process([str(executable)], check=True, timeout=RUN_TIMEOUT_SECONDS)


if __name__ == "__main__":
    unittest.main()
