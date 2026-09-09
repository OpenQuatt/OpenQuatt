import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
YAML = (ROOT / "openquatt/oq_heating_curve_strategy.yaml").read_text()
RUNTIME = (ROOT / "openquatt/includes/control/oq_heating_curve_runtime.h").read_text()
class HeatingCurveDemandContractTest(unittest.TestCase):
    def test_yaml_delegates_demand_core_without_moving_wiring(self) -> None:
        for marker in ("platform: pid", "write_action:", "oq_heating_curve_runtime::runtime().write_pid_output"):
            self.assertIn(marker, YAML)
        for marker in ("oq_heat_intent_runtime::room_temperature_fresh", "oq_heat_intent_runtime::evaluate", "oq_curve::decide_demand("):
            self.assertIn(marker, RUNTIME)
        for old_inline in ("recovery_reenter_min_f", "above_stop_band", "restart_bypass_delta_c", "maintain_cap = demand_max"):
            self.assertNotIn(old_inline, YAML)
        self.assertLess(RUNTIME.index("id(oq_curve_oil_return_hold_until_ms) = oil_return.hold_until_ms"), RUNTIME.index("if (demand.valid)"))
        paths = ("openquatt/oq_heating_curve_strategy.yaml", "openquatt/includes/control/oq_heating_curve_runtime.h", "openquatt/includes/control/oq_heating_curve_logic.h", "tests/host/heating_curve_restart_logic_test.cpp", "scripts/tests/test_electrical_input_limit_contract.py", "scripts/tests/test_heating_curve_demand_contract.py")
        self.assertLessEqual(sum(len((ROOT / path).read_text().splitlines()) for path in paths), 1878)
if __name__ == "__main__":
    unittest.main()
