import pathlib, unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
FILES = (ROOT / "openquatt/oq_power_house_strategy.yaml", ROOT / "openquatt/includes/control/oq_power_house_runtime.h", ROOT / "openquatt/includes/control/oq_power_house_demand_logic.h", ROOT / "tests/host/oq_power_house_demand_logic_test.cpp", ROOT / "openquatt/includes/control/oq_power_house_dispatch_logic.h", ROOT / "tests/host/oq_power_house_dispatch_logic_test.cpp", ROOT / "openquatt/oq_thermal_request_control.yaml", ROOT / "scripts/tests/test_compressor_frequency_policy_contract.py", ROOT / "scripts/tests/test_electrical_input_limit_contract.py", ROOT / "openquatt/includes/control/oq_power_house_run_extension_logic.h", ROOT / "tests/host/oq_power_house_run_extension_logic_test.cpp", pathlib.Path(__file__))
class PowerHouseDemandContractTest(unittest.TestCase):
    def test_delegation_and_line_budget(self) -> None:
        yaml = FILES[0].read_text()
        text = FILES[1].read_text()
        positions = [text.index(marker) for marker in ("observe_protection(", "decide_cadence(", "decide_demand(", "decide_dispatch(")]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("id(oq_ph_request_last_loop_ms) = now_ms == 0 ? UINT32_MAX : now_ms;", text)
        self.assertIn("oq_power_house_runtime::runtime().tick", yaml)
        self.assertIn("oq_ph_demand_loop_s", yaml)
        self.assertIn("oq_hp_water_temp_stale_s", yaml)
        self.assertIn("select_performance_supply(", text)
        self.assertIn("hp1_water_out_temp_last_update_ms", text)
        self.assertIn("hp2_water_out_temp_last_update_ms", text)
        self.assertIn("config.hp_water_temp_stale_ms", text)
        dispatch_logic = FILES[4].read_text()
        dispatch_test = FILES[5].read_text()
        self.assertIn("select_performance_supply(", dispatch_logic)
        self.assertIn("test_performance_supply_fallback", dispatch_test)
        # #608 run extension: base house need stays apart from effective HP target.
        self.assertIn("oq_power_house_run_extension::evaluate(", text)
        self.assertIn("house_deficit_w", text)
        self.assertIn("base_requested_w", text)
        self.assertIn("ph_run_extension_enabled", yaml)
        self.assertIn("ph_run_extension_stop_margin_c", yaml)
        self.assertIn("oq_ph_run_extension_status", yaml)
        run_ext_logic = FILES[9].read_text()
        run_ext_test = FILES[10].read_text()
        self.assertIn("enum class Phase", run_ext_logic)
        self.assertIn("WAIT_WARM_RESTART", run_ext_logic)
        self.assertIn("test_comfort_stop_at_threshold", run_ext_test)
        for marker in ("filter_demand(", "oq_demand_filter_ramp_up", "now_ms > id(oq_ph_request_last_loop_ms)", "fminf(requested_w", "struct DuoCandidate"):
            self.assertNotIn(marker, text)
            self.assertNotIn(marker, yaml)
        self.assertLessEqual(sum(len(path.read_text().splitlines()) for path in FILES), 3800)
