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
        self.assertIn("id(oq_boiler_power_test_flow_lph).state", BOILER_RUNTIME)
        self.assertNotIn("select_initial_test_flow_lph(id(oq_flow_setpoint_lph)", BOILER_RUNTIME)
        self.assertIn("publish_transient_number_value(id(oq_flow_setpoint_lph)", BOILER_RUNTIME)
        self.assertNotIn("set_number_value(id(oq_flow_setpoint_lph)", BOILER_RUNTIME)

    def test_boiler_test_flow_is_installation_specific_and_defaults_to_800(self) -> None:
        flow_setting = BOILER_TEST_PACKAGE.split("id: oq_boiler_power_test_flow_lph", 1)[1].split(
            "id: oq_boiler_rated_heat_power", 1
        )[0]
        self.assertIn("restore_value: true", flow_setting)
        self.assertIn("initial_value: 800", flow_setting)
        self.assertIn("max_value: 1000", flow_setting)

    def test_opentherm_dhw_interference_fails_and_restores(self) -> None:
        self.assertIn("boiler_test_dhw_interferes", BOILER_RUNTIME)
        self.assertIn('finish_task("FAILED: DHW active; retry without hot water or tap comfort"', BOILER_RUNTIME)

    def test_dispatch_reuses_commissioning_temperature_policy(self) -> None:
        self.assertIn("oq_boiler_commissioning::commissioning_target_temperature_c", BOILER_DISPATCH)
        commissioning_start = BOILER_DISPATCH.index("} else if (commissioning_task_active) {")
        commissioning_block = BOILER_DISPATCH[commissioning_start:]
        self.assertNotIn("max_c - 5.0f", commissioning_block)


if __name__ == "__main__":
    unittest.main()
