import pathlib, unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
FILES = (ROOT / "openquatt/oq_power_house_strategy.yaml", ROOT / "openquatt/includes/control/oq_power_house_runtime.h", ROOT / "openquatt/includes/control/oq_power_house_demand_logic.h", ROOT / "tests/host/oq_power_house_demand_logic_test.cpp", ROOT / "openquatt/includes/control/oq_power_house_dispatch_logic.h", ROOT / "tests/host/oq_power_house_dispatch_logic_test.cpp", ROOT / "openquatt/oq_thermal_request_control.yaml", ROOT / "scripts/tests/test_compressor_frequency_policy_contract.py", ROOT / "scripts/tests/test_electrical_input_limit_contract.py", pathlib.Path(__file__))
class PowerHouseDemandContractTest(unittest.TestCase):
    def test_delegation_and_line_budget(self) -> None:
        yaml = FILES[0].read_text()
        text = FILES[1].read_text()
        positions = [text.index(marker) for marker in ("observe_protection(", "decide_cadence(", "decide_demand(", "filter_demand(", "decide_dispatch(")]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("id(oq_ph_request_last_loop_ms) = now_ms == 0 ? UINT32_MAX : now_ms;", text)
        self.assertIn("oq_power_house_runtime::runtime().tick", yaml)
        for marker in ("now_ms > id(oq_ph_request_last_loop_ms)", "float ramp_budget = id(oq_demand_filter_ramp_up_budget)", "fminf(requested_w", "struct DuoCandidate"):
            self.assertNotIn(marker, text)
        self.assertLessEqual(sum(len(path.read_text().splitlines()) for path in FILES), 3120)
