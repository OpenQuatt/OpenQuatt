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
        self.assertIn("publish_transient_number_value(id(oq_flow_setpoint_lph)", BOILER_RUNTIME)
        self.assertNotIn("set_number_value(id(oq_flow_setpoint_lph)", BOILER_RUNTIME)

    def test_dispatch_reuses_commissioning_temperature_policy(self) -> None:
        self.assertIn("oq_boiler_commissioning::commissioning_target_temperature_c", BOILER_DISPATCH)
        commissioning_start = BOILER_DISPATCH.index("} else if (commissioning_task_active) {")
        commissioning_block = BOILER_DISPATCH[commissioning_start:]
        self.assertNotIn("max_c - 5.0f", commissioning_block)


if __name__ == "__main__":
    unittest.main()
