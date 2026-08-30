import pathlib, unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
FILES = (ROOT / "openquatt/oq_power_house_strategy.yaml", ROOT / "openquatt/includes/control/oq_power_house_demand_logic.h", ROOT / "tests/host/oq_power_house_demand_logic_test.cpp", pathlib.Path(__file__))
class PowerHouseDemandContractTest(unittest.TestCase):
    def test_delegation_and_line_budget(self) -> None:
        text = FILES[0].read_text()
        positions = [text.index(marker) for marker in ("decide_cadence(", "decide_demand(", "filter_demand(")]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("id(oq_ph_request_last_loop_ms) = now_ms == 0 ? UINT32_MAX : now_ms;", text)
        self.assertNotIn("now_ms > id(oq_ph_request_last_loop_ms)", text)
        self.assertNotIn("float ramp_budget = id(oq_demand_filter_ramp_up_budget)", text)
        self.assertLessEqual(sum(len(path.read_text().splitlines()) for path in FILES), 1415)
