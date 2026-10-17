import pathlib, unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
YAML = (ROOT / "openquatt/oq_power_house_strategy.yaml").read_text()
RUNTIME = (ROOT / "openquatt/includes/control/oq_power_house_runtime.h").read_text()
DISPATCH_LOGIC = (ROOT / "openquatt/includes/control/oq_power_house_dispatch_logic.h").read_text()
RUN_EXTENSION_LOGIC = (ROOT / "openquatt/includes/control/oq_power_house_run_extension_logic.h").read_text()
class PowerHouseDemandContractTest(unittest.TestCase):
    def test_demand_delegates_to_runtime(self) -> None:
        yaml = YAML
        text = RUNTIME
        positions = [text.index(marker) for marker in ("observe_protection(", "decide_cadence(", "decide_startup_demand(", "decide_dispatch(")]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("id(oq_ph_request_last_loop_ms) = now_ms == 0 ? UINT32_MAX : now_ms;", text)
        self.assertIn("oq_power_house_runtime::runtime().tick", yaml)
        self.assertIn("oq_ph_demand_loop_s", yaml)
        self.assertIn("oq_hp_water_temp_stale_s", yaml)
        self.assertIn("select_performance_supply(", text)
        self.assertIn("hp1_water_out_temp_last_update_ms", text)
        self.assertIn("hp2_water_out_temp_last_update_ms", text)
        self.assertIn("config.hp_water_temp_stale_ms", text)
        self.assertIn("select_performance_supply(", DISPATCH_LOGIC)
        # #608 run extension: base house need stays apart from effective HP target.
        self.assertIn("oq_power_house_run_extension::evaluate(", text)
        self.assertIn("run_ext_input.inputs_valid = demand.valid && room_inputs_valid;", text)
        self.assertIn("id(oq_ph_run_ext_state_code) = 0;", text)
        self.assertIn("house_deficit_w", text)
        self.assertIn("base_requested_w", text)
        self.assertIn("ph_run_extension_enabled", yaml)
        self.assertIn("const bool run_ext_enabled = id(ph_run_extension_enabled).state;", text)
        self.assertNotIn("ph_run_extension_enabled).has_state()", text)
        self.assertIn("ph_run_extension_stop_margin_c", yaml)
        self.assertIn("ph_run_extension_restart_cooldown_c", yaml)
        self.assertIn("id(ph_run_extension_restart_cooldown_c).state, room_resume_delta_c", text)
        cooldown = yaml.split("id: ph_run_extension_restart_cooldown_c", 1)[1].split("text_sensor:", 1)[0]
        for marker in ("restore_value: true", "disabled_by_default: true", "initial_value: ${oq_ph_run_extension_restart_hysteresis_c}", "min_value: 0.1", "max_value: 3.0"):
            self.assertIn(marker, cooldown)
        self.assertIn("oq_ph_run_extension_status", yaml)
        self.assertIn("enum class Phase", RUN_EXTENSION_LOGIC)
        self.assertIn("WAIT_WARM_RESTART", RUN_EXTENSION_LOGIC)
        self.assertIn("compute_house_deficit_w(", text)
        # Review #726: topology-optimizer hold is not a #608 run right.
        self.assertNotIn("topology_hold_active", text)
        for marker in ("filter_demand(", "oq_demand_filter_ramp_up", "now_ms > id(oq_ph_request_last_loop_ms)", "fminf(requested_w", "struct DuoCandidate"):
            self.assertNotIn(marker, text)
            self.assertNotIn(marker, yaml)

    def test_startup_wait_clears_shared_outputs_before_adopting_demand(self) -> None:
        text = RUNTIME
        self.assertLess(text.index("decide_startup_demand("), text.index("this->demand_state_ = demand.next;"))
        self.assertLess(text.index("this->publish_startup_wait_("), text.index("this->demand_state_ = demand.next;"))
        wait = text[text.index("void publish_startup_wait_("):text.index("static bool near_(")]
        for marker in ("oq_ph_request_hp1_level", "oq_ph_request_hp2_level", "oq_demand_raw",
                       "oq_strategy_requested_power_w", "oq_strategy_heat_request_active) = false",
                       "oq_strategy_output_valid) = false", "waiting_inputs", "input_timeout"):
            self.assertIn(marker, wait)
        reset = text[text.index("void reset()"):text.index("std::string run_extension_status()")]
        self.assertNotIn("startup_phase_", reset)
