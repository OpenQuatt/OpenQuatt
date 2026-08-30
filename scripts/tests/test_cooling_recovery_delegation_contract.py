from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
YAML = (ROOT / "openquatt/oq_cooling_strategy.yaml").read_text()
DEFROST_TEST = (ROOT / "tests/host/oq_odu_compressor_levels_test.cpp").read_text()

class CoolingRecoveryDelegationContractTest(unittest.TestCase):
    def test_yaml_delegates_and_live_defrost_path_stays_fail_closed(self) -> None:
        for marker in ("update_oil_return(", "evaluate_water_restart(", "inactive_stop_reason(",
                       "id(cooling_pid_kp).state", "hp_can_take_cooling_start", "publish_cooling_limiter_event"):
            self.assertIn(marker, YAML)
        for marker in ("oq_cooling_oil_return_hold_until_ms", "oil_return_recovery_not_needed"):
            self.assertNotIn(marker, YAML)
        for marker in ("resolve_retained_level(true, true", "no_cooling_hold.control_level == 0",
                       "no_cooling_hold.physical_level == 0"):
            self.assertIn(marker, DEFROST_TEST)
        paths = ("openquatt/oq_cooling_strategy.yaml", "openquatt/includes/control/oq_cooling_limiter_logic.h",
                 "tests/host/cooling_limiter_logic_test.cpp", "tests/host/thermal_request_logic_test.cpp")
        files = [ROOT / path for path in paths] + [Path(__file__)]
        self.assertLessEqual(sum(len(path.read_text().splitlines()) for path in files), 2310)
