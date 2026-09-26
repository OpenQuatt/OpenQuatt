from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONTROL_YAML = (ROOT / "openquatt/oq_boiler_control.yaml").read_text()
DISPATCH_YAML = (ROOT / "openquatt/oq_boiler_dispatch.yaml").read_text()
OTB_YAML = (ROOT / "openquatt/oq_boiler_opentherm.yaml").read_text()
CONTROL_RUNTIME = (ROOT / "openquatt/includes/control/oq_boiler_runtime.h").read_text()
DISPATCH_RUNTIME = (ROOT / "openquatt/includes/control/oq_boiler_dispatch_runtime.h").read_text()
OTB_RUNTIME = (ROOT / "openquatt/includes/boiler/oq_boiler_otb_runtime.h").read_text()
BOILER_LOGIC = (ROOT / "openquatt/includes/boiler/oq_boiler_logic.h").read_text()
RELAY_TARGET_LOGIC = (ROOT / "openquatt/includes/boiler/oq_boiler_relay_target_logic.h").read_text()
DISPATCH_LOGIC = (ROOT / "openquatt/includes/control/oq_boiler_dispatch_logic.h").read_text()
SUBSTITUTIONS = (ROOT / "openquatt/oq_substitutions_common.yaml").read_text()


class BoilerRuntimeContractTest(unittest.TestCase):
    def test_yaml_is_a_compact_runtime_contract(self) -> None:
        self.assertLessEqual(len(CONTROL_YAML.splitlines()), 360)
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

    def test_relay_target_control_is_isolated_policy(self) -> None:
        # De hysterese is boiler-specifiek beleid en staat niet als losse
        # constanten verspreid over de keten.
        self.assertIn("struct RelayTargetConfig", RELAY_TARGET_LOGIC)
        self.assertIn("float start_delta_c = 2.0f;", RELAY_TARGET_LOGIC)
        self.assertIn("float stop_delta_c = 0.5f;", RELAY_TARGET_LOGIC)
        self.assertIn("bool relay_target_config_valid(", RELAY_TARGET_LOGIC)
        self.assertIn("RelayTargetConfig relay_target = {};", CONTROL_RUNTIME)
        # De doelregeling zit in de uitgangsadapter, niet in de ketelketen of
        # in een YAML-lambda.
        self.assertIn("oq_boiler::evaluate_relay_target(", CONTROL_RUNTIME)
        self.assertIn("oq_boiler::relay_target_control_applies(", CONTROL_RUNTIME)
        self.assertIn("bool relay_target_control_applies(uint8_t source, bool opentherm_selected)", BOILER_LOGIC)
        self.assertNotIn("relay_target", DISPATCH_LOGIC)
        self.assertNotIn("RelayTargetConfig{", DISPATCH_YAML)
        # De OpenTherm-transport blijft onaangeroerd: die ontvangt het doel via
        # de bus en moduleert zelf.
        self.assertNotIn("relay_target", OTB_RUNTIME)
        self.assertNotIn("relay_target", OTB_YAML)

    def test_satisfied_target_is_not_a_safety_failure(self) -> None:
        self.assertIn("BLOCK_TARGET_SATISFIED = 25", BOILER_LOGIC)
        self.assertIn("return \"requested boiler target temperature satisfied\";", BOILER_LOGIC)
        # Een bereikt doel is een normaal gevolg van de R1-doelregeling en
        # wordt daarom niet als blokkade of fout gerapporteerd.
        self.assertIn(
            "decision.blocked = decision.demand_present && !decision.output_active && !relay_target_satisfied;",
            BOILER_LOGIC,
        )

    def test_phase_one_keeps_existing_hp_defrost_tunables(self) -> None:
        # FASE 1 raakt het bestaande defrostgedrag van de warmtepomp niet: de
        # vermogensderating en de Duo-compensatie blijven op dezelfde waarden.
        for substitution in (
            'oq_defrost_power_factor: "0.764"',
            'oq_defrost_comp_min_f: "6"',
            'oq_defrost_comp_boost_steps: "1"',
        ):
            self.assertIn(substitution, SUBSTITUTIONS)


if __name__ == "__main__":
    unittest.main()
