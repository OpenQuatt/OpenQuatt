import tempfile
import unittest
from pathlib import Path

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]


class HeatingStopContextRuntimeTest(unittest.TestCase):
    def test_low_load_snapshot_survives_demand_loss_until_restart_release(self):
        source = (ROOT / "openquatt/includes/control/oq_power_house_runtime.h").read_text()
        start = source.index("  void record_low_load_stop(")
        opening = source.index("{", start)
        depth = 1
        end = opening + 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        method = source[start:end]
        cpp = '''
#include "components/openquatt_decision_log/OpenQuattHeatingStop.h"
#include <cassert>
using namespace esphome::openquatt_decision_log;
struct Harness {
  heating_stop::Snapshot stop_snapshot_;
  float run_ext_base_w_{1500};
''' + method + '''
};
int main() {
  Harness runtime;
  runtime.record_low_load_stop(1000, true, false, 1500, 1600, 2200);
  assert(runtime.stop_snapshot_.reason == REASON_PH_LOW_LOAD);
  runtime.record_low_load_stop(61000, false, false, 0, 1300, 1900);
  assert(runtime.stop_snapshot_.captured_ms == 1000);
  assert(runtime.stop_snapshot_.effective_w == 1500);
  assert(runtime.stop_snapshot_.supply_stop_c == 1600);
  assert(runtime.stop_snapshot_.supply_restart_c == 2200);
  runtime.record_low_load_stop(121000, true, false, 1700, 1800, 2400);
  assert(runtime.stop_snapshot_.captured_ms == 1000);
  runtime.record_low_load_stop(122000, true, true, 2500, 1800, 2400);
  assert(runtime.stop_snapshot_.reason == 0);
  runtime.record_low_load_stop(123000, true, false, 1700, 1800, 2400);
  assert(runtime.stop_snapshot_.captured_ms == 123000);
  assert(runtime.stop_snapshot_.supply_stop_c == 1800);
}
'''
        with tempfile.TemporaryDirectory(prefix="openquatt-stop-context-") as directory:
            path = Path(directory)
            (path / "runtime.cpp").write_text(cpp)
            run_process(
                compiler_command("-I", str(ROOT), str(path / "runtime.cpp"), "-o", str(path / "test")),
                timeout=COMPILE_TIMEOUT_SECONDS, check=True, capture_output=True, text=True,
            )
            run_process([str(path / "test")], timeout=RUN_TIMEOUT_SECONDS, check=True)


if __name__ == "__main__":
    unittest.main()
