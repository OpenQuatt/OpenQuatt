from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONTROL_YAML = (ROOT / "openquatt/oq_boiler_control.yaml").read_text()
DISPATCH_YAML = (ROOT / "openquatt/oq_boiler_dispatch.yaml").read_text()
OTB_YAML = (ROOT / "openquatt/oq_boiler_opentherm.yaml").read_text()
CONTROL_RUNTIME = (ROOT / "openquatt/includes/control/oq_boiler_runtime.h").read_text()
DISPATCH_RUNTIME = (ROOT / "openquatt/includes/control/oq_boiler_dispatch_runtime.h").read_text()
OTB_RUNTIME = (ROOT / "openquatt/includes/boiler/oq_boiler_otb_runtime.h").read_text()


class BoilerRuntimeContractTest(unittest.TestCase):
    def test_yaml_is_a_compact_runtime_contract(self) -> None:
        self.assertLessEqual(len(CONTROL_YAML.splitlines()), 345)
        self.assertLessEqual(len(DISPATCH_YAML.splitlines()), 60)
        self.assertLessEqual(len(OTB_YAML.splitlines()), 980)
        self.assertEqual(CONTROL_YAML.count("oq_boiler_runtime::runtime().tick("), 1)
        self.assertEqual(DISPATCH_YAML.count("oq_boiler_dispatch_runtime::tick("), 1)
        self.assertEqual(OTB_YAML.count("oq_boiler_otb_runtime::apply_command("), 1)
        self.assertEqual(OTB_YAML.count("oq_boiler_otb_runtime::link_watch("), 1)

    def test_stateful_decisions_live_in_cpp(self) -> None:
        for removed in (
            "static oq_boiler::BlockReason last_block_reason",
            "static oq_boiler_output::Controller output_controller",
            "static bool last_connected",
        ):
            self.assertNotIn(removed, CONTROL_YAML)
        self.assertNotIn("const bool ph_fresh", DISPATCH_YAML)
        self.assertNotIn("id(otb_ch_enable).turn_on()", OTB_YAML)
        self.assertIn("oq_boiler::evaluate(", CONTROL_RUNTIME)
        self.assertIn("oq_boiler_dispatch::dispatch(", DISPATCH_RUNTIME)
        self.assertIn("oq_boiler_transport::evaluate_command_adapter(", OTB_RUNTIME)

    def test_host_regressions_cover_failure_boundaries(self) -> None:
        dispatch_test = (ROOT / "tests/host/boiler_dispatch_logic_test.cpp").read_text()
        transport_test = (ROOT / "tests/host/boiler_transport_logic_test.cpp").read_text()
        controller_test = (ROOT / "tests/host/hp_fallback_logic_test.cpp").read_text()
        self.assertIn("test_power_house_rejects_stale_or_wrong_strategy_output", dispatch_test)
        self.assertIn("test_fallback_and_commissioning_keep_authorization_provenance", dispatch_test)
        self.assertIn("test_power_house_without_flow_keeps_r1_power_but_no_ot_target", dispatch_test)
        self.assertIn("test_otb_adapter_flow_loss_withdraws_central_request", transport_test)
        self.assertIn("test_otb_adapter_never_touches_r1_owned_transport", transport_test)
        self.assertIn("test_boiler_diagnostic_helpers_are_bounded", controller_test)

    def test_q_only_otb_binding_is_guarded(self) -> None:
        self.assertIn("#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q", OTB_RUNTIME)


if __name__ == "__main__":
    unittest.main()
