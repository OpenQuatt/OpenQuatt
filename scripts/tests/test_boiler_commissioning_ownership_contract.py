from pathlib import Path
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]
BOILER_TEST_PACKAGE = (REPO_ROOT / "openquatt/oq_boiler_test.yaml").read_text(encoding="utf-8")
BOILER_RUNTIME = (REPO_ROOT / "openquatt/includes/service/tasks/oq_boiler_task_logic.h").read_text(encoding="utf-8")
BOILER_DISPATCH = (REPO_ROOT / "openquatt/oq_boiler_dispatch.yaml").read_text(encoding="utf-8")


class BoilerCommissioningOwnershipContract(unittest.TestCase):
    def test_yaml_only_delegates_runtime_tick(self) -> None:
        self.assertIn("oq_boiler_task::runtime().tick", BOILER_TEST_PACKAGE)
        self.assertNotIn("FlowReachabilityMonitor", BOILER_TEST_PACKAGE)
        self.assertNotIn("oq_commissioning_state_code) =", BOILER_TEST_PACKAGE)
        self.assertNotIn("abort_or_clear();\n              oq_boiler_task::runtime().tick", BOILER_TEST_PACKAGE)

    def test_runtime_owns_flow_reachability_failure(self) -> None:
        self.assertIn("FlowReachabilityMonitor flow_reachability_", BOILER_RUNTIME)
        self.assertIn("bool flow_reachable", BOILER_RUNTIME)
        self.assertIn('finish_task("FAILED: required boiler test flow cannot be reached"', BOILER_RUNTIME)
        self.assertIn("if (!flow_reachable(cfg, now_ms, flow_lph)) return;", BOILER_RUNTIME)

    def test_boiler_test_flow_target_is_runtime_only(self) -> None:
        self.assertIn("active_test_flow_target_lph_ = cfg.target_flow_lph", BOILER_RUNTIME)
        self.assertNotIn("oq_boiler_power_test_flow_lph", BOILER_RUNTIME)
        self.assertIn("publish_transient_number_value(id(oq_flow_setpoint_lph)", BOILER_RUNTIME)
        self.assertNotIn("set_number_value(id(oq_flow_setpoint_lph)", BOILER_RUNTIME)

    def test_boiler_test_has_no_persistent_flow_setting(self) -> None:
        self.assertNotIn("oq_boiler_power_test_flow_lph", BOILER_TEST_PACKAGE)

    def test_opentherm_dhw_interference_fails_and_restores(self) -> None:
        self.assertIn("boiler_test_dhw_interferes", BOILER_RUNTIME)
        self.assertIn('finish_task("FAILED: DHW active; retry without hot water or tap comfort"', BOILER_RUNTIME)

    def test_recovered_opentherm_transport_error_does_not_abort_test(self) -> None:
        self.assertIn("opentherm_status_required", BOILER_RUNTIME)
        self.assertIn("active_test_opentherm_ || opentherm_selected_now", BOILER_RUNTIME)
        self.assertIn("field_is_fresh(oq_otb::FIELD_STATUS", BOILER_RUNTIME)
        self.assertIn('finish_task("FAILED: OpenTherm status unavailable during test"', BOILER_RUNTIME)
        self.assertNotIn("active_test_transport_error_count_", BOILER_RUNTIME)
        self.assertNotIn("FAILED: OpenTherm transport error during test", BOILER_RUNTIME)

    def test_boiler_start_confirmation_allows_one_hundred_fifty_seconds(self) -> None:
        self.assertIn(".max_runtime_ms = 20UL * 60UL * 1000UL", BOILER_RUNTIME)
        self.assertIn(".boiler_start_timeout_ms = 150UL * 1000UL", BOILER_RUNTIME)
        self.assertIn("if (state_age_ms >= cfg.boiler_start_timeout_ms)", BOILER_RUNTIME)
        self.assertIn(".boiler_settle_min_ms = 30UL * 1000UL", BOILER_RUNTIME)
        self.assertIn("BoilerActivationSettleMonitor boiler_activation_settle_", BOILER_RUNTIME)
        self.assertIn(
            "boiler_activation_settle_.update(now_ms, boiler_is_active, cfg.boiler_settle_min_ms)",
            BOILER_RUNTIME,
        )

    def test_measurement_uses_rebasable_power_plateau(self) -> None:
        self.assertIn("PowerPlateauMonitor power_plateau_", BOILER_RUNTIME)
        self.assertIn("POWER_PLATEAU_LOST", BOILER_RUNTIME)
        self.assertIn("reset_power_samples()", BOILER_RUNTIME)
        self.assertNotIn("peak_w_", BOILER_RUNTIME)
        self.assertIn('"FAILED: boiler power did not stabilise"', BOILER_RUNTIME)

    def test_plateau_rebase_keeps_full_measurement_flow_evidence(self) -> None:
        reset_measurement_start = BOILER_RUNTIME.index("void reset_measurement_accumulators()")
        reset_power_start = BOILER_RUNTIME.index("void reset_power_samples()", reset_measurement_start)
        reset_test_start = BOILER_RUNTIME.index("void reset_test_state()", reset_power_start)
        reset_measurement_body = BOILER_RUNTIME[reset_measurement_start:reset_power_start]
        reset_power_body = BOILER_RUNTIME[reset_power_start:reset_test_start]
        self.assertIn("measurement_tick_count_ = 0", reset_measurement_body)
        self.assertIn("measurement_stable_flow_tick_count_ = 0", reset_measurement_body)
        self.assertNotIn("measurement_tick_count_ = 0", reset_power_body)
        self.assertNotIn("measurement_stable_flow_tick_count_ = 0", reset_power_body)

    def test_dispatch_reuses_commissioning_temperature_policy(self) -> None:
        self.assertIn("oq_boiler_commissioning::commissioning_target_temperature_c", BOILER_DISPATCH)
        commissioning_start = BOILER_DISPATCH.index("} else if (commissioning_task_active) {")
        commissioning_block = BOILER_DISPATCH[commissioning_start:]
        self.assertNotIn("max_c - 5.0f", commissioning_block)


if __name__ == "__main__":
    unittest.main()
