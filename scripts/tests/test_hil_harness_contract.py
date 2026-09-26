import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PROFILE = (ROOT / "configs/hil/input_sources_fast_duo_wifi.yaml").read_text()
V2_PROFILE = (ROOT / "configs/hil/issue_667_v2_performance_duo_wifi.yaml").read_text()
CONTROL_PROFILE = (ROOT / "configs/hil/control_regression_duo_wifi.yaml").read_text()
HIL_CONTROLLER = (ROOT / "configs/heatpump_controller_q/duo_hil.yaml").read_text()
HIL_CONTROLLER_COMPAT = (ROOT / "configs/heatpump_controller_q/duo_wifi_hil.yaml").read_text()
RUNNER = (ROOT / "scripts/hil/run-input-sources.mjs").read_text()
SCENARIO = (ROOT / "tests/hil/scenarios/input-sources.mjs").read_text()
SESSION = (ROOT / "scripts/hil/session.mjs").read_text()
V2_RUNNER = (ROOT / "scripts/hil/run-v2-performance.mjs").read_text()
REST_CLIENT = (ROOT / "scripts/hil/rest-client.mjs").read_text()
SUBSTITUTIONS = (ROOT / "openquatt/oq_substitutions_common.yaml").read_text()
TARGETS = (ROOT / "build_targets.yaml").read_text()
DOCS = (ROOT / "docs/development/hil-testing.md").read_text()
PACKAGE = (ROOT / "package.json").read_text()
WORKFLOW = (ROOT / ".github/workflows/ci-build.yml").read_text()
ESPHOME_BUILD_WORKFLOW = (ROOT / ".github/workflows/esphome-build.yml").read_text()
SCENARIO_POLICY = (ROOT / "tests/hil/scenarios/README.md").read_text()


class HilHarnessContractTest(unittest.TestCase):
    def test_fast_profile_is_explicitly_test_only(self):
        self.assertIn("HIL TEST ONLY", PROFILE)
        self.assertIn(
            "!include ../heatpump_controller_q/duo_hil.yaml", PROFILE
        )
        self.assertIn('name: "HIL Test Profile"', PROFILE)
        self.assertIn('return {"input-sources-fast-v1"};', PROFILE)
        self.assertNotIn("input_sources_fast_duo_wifi.yaml", TARGETS)

    def test_hil_overlays_never_claim_the_production_identity(self):
        # duo.yaml carries the production device_name "openquatt". An overlay
        # that includes it directly republishes "openquatt.local" on the test
        # controller, which collides with a real production controller on the
        # same network. Both HIL overlays must build on the HIL testcontroller
        # entrypoint instead.
        for profile in (PROFILE, V2_PROFILE):
            self.assertNotIn("heatpump_controller_q/duo.yaml", profile)
        self.assertIn('device_name: "openquatt-test"', HIL_CONTROLLER)
        self.assertIn('project_name: "openquatt.test"', HIL_CONTROLLER)
        self.assertIn("openquatt-test.local", DOCS)

    def test_mono_control_overlay_keeps_test_identity_and_single_topology(self):
        overlay = (ROOT / "configs/hil/control_regression_mono_wifi.yaml").read_text()
        baseline = (ROOT / "configs/heatpump_controller_q/single_hil.yaml").read_text()
        self.assertIn("HIL TEST ONLY", overlay)
        self.assertIn("!include ../heatpump_controller_q/single_hil.yaml", overlay)
        self.assertIn('return {"control-regression-mono-v1"};', overlay)
        self.assertIn('device_name: "openquatt-test"', baseline)
        self.assertIn("!include single.yaml", baseline)
        self.assertNotIn("control_regression_mono_wifi.yaml", TARGETS)
        self.assertIn("esphome config configs/hil/control_regression_mono_wifi.yaml", ESPHOME_BUILD_WORKFLOW)

    def test_control_regression_overlay_reuses_test_only_observations(self):
        self.assertIn("HIL TEST ONLY", CONTROL_PROFILE)
        self.assertIn("!include issue_667_v2_performance_duo_wifi.yaml", CONTROL_PROFILE)
        self.assertIn('return {"control-regression-v1"};', CONTROL_PROFILE)
        self.assertNotIn("substitutions:", CONTROL_PROFILE)
        self.assertNotIn("control_regression_duo_wifi.yaml", TARGETS)
        self.assertIn("esphome config configs/hil/control_regression_duo_wifi.yaml", ESPHOME_BUILD_WORKFLOW)

    def test_fast_profile_does_not_change_production_floors(self):
        for marker in (
            'api_input_room_temperature_stale_s: "45"',
            'api_input_heating_enable_stale_s: "45"',
            'api_input_heating_supply_target_stale_s: "45"',
            'ha_room_temperature_stale_s: "90"',
            'ha_heating_supply_target_stale_s: "90"',
            'ha_ingress_fresh_s: "75"',
            'oq_selected_input_stale_hold_s: "10"',
            'oq_hp_min_off_s: "10"',
        ):
            self.assertIn(marker, PROFILE)
        for marker in (
            'api_input_room_temperature_stale_s: "600"',
            'api_input_heating_enable_stale_s: "0"',
            'ha_room_temperature_stale_s: "600"',
            'ha_heating_supply_target_stale_s: "900"',
            'ha_ingress_fresh_s: "300"',
            'oq_selected_input_stale_hold_s: "300"',
            'oq_cooling_minimum_off_min_s: "240"',
            'oq_hp_min_off_s: "240"',
        ):
            self.assertIn(marker, SUBSTITUTIONS)
        self.assertNotIn("oq_cooling_minimum_off_min_s", PROFILE)

    def test_mutations_are_gated_and_targets_have_no_defaults(self):
        self.assertIn("mutating HIL runs require --apply", RUNNER)
        self.assertIn("--device and --restore-config", RUNNER)
        self.assertIn("openquatt-modbus-opentherm-v2", RUNNER)
        self.assertIn("simulator contract differs", RUNNER)
        self.assertIn("writeIntervalMs < 1000", REST_CLIENT)
        self.assertNotIn("192.168.", RUNNER)
        self.assertNotIn("192.168.", REST_CLIENT)

    def test_room_setpoint_validity_has_end_to_end_hil_coverage(self):
        self.assertIn("'setpoint-validity'", RUNNER)
        self.assertIn("testRoomSetpointValidity", SCENARIO)
        self.assertIn("Thermostat room setpoint", SCENARIO)
        self.assertIn("OT - Room Setpoint", SCENARIO)
        self.assertIn("Room Setpoint (Selected)", SCENARIO)
        self.assertIn("selected room setpoint rejects 35.5", SCENARIO)
        self.assertIn("selected room temperature zero remains valid", SCENARIO)
        self.assertIn("Thermostat room setpoint", SESSION)
        self.assertIn("Thermostat room temperature", SESSION)
        self.assertIn("OpenTherm Enabled", SESSION)

    def test_setpoint_validity_stage_documents_its_simulator_precondition(self):
        # The stage can only inject the rejected values when the thermostat
        # simulator accepts 0..40 degrees. Simulator PR #2 widened the test
        # slider for exactly this; production keeps 5..35.
        self.assertIn("setpoint-validity", DOCS)
        self.assertIn("Thermostat room setpoint", DOCS)
        self.assertIn("OpenQuatt-Simulator/pull/2", DOCS)
        self.assertIn("5..35", DOCS)

    def test_issue_667_profile_and_runner_are_test_only(self):
        self.assertIn("HIL TEST ONLY", V2_PROFILE)
        self.assertIn("issue-667-v2-performance-v1", V2_PROFILE)
        self.assertIn("!include ../heatpump_controller_q/duo_hil.yaml", V2_PROFILE)
        self.assertIn("HIL HP1 Power Input quality", V2_PROFILE)
        self.assertIn("HIL Low-load Pmin", V2_PROFILE)
        self.assertIn("id(cic_component).stop_poller();", V2_PROFILE)
        self.assertIn("id(cic_component).start_poller();", V2_PROFILE)
        self.assertIn("flash_write_interval: 1s", HIL_CONTROLLER)
        self.assertIn("!include duo_hil.yaml", HIL_CONTROLLER_COMPAT)
        self.assertIn("openquatt-modbus-opentherm-v2", V2_RUNNER)
        self.assertIn("v2PerformanceScenario", V2_RUNNER)
        self.assertIn("configs/heatpump_controller_q/duo_hil.yaml", V2_RUNNER)
        self.assertNotIn("192.168.", V2_RUNNER)
        self.assertNotIn("issue_667_v2_performance_duo_wifi.yaml", TARGETS)

    def test_hil_scenarios_are_domain_oriented_and_selective(self):
        normalized_policy = " ".join(SCENARIO_POLICY.split())
        normalized_docs = " ".join(DOCS.split())
        self.assertIn("HIL is geen archief van issues of PR's", normalized_policy)
        self.assertIn("host- of simulator-only test", normalized_policy)
        self.assertIn("rooktest plus de relevante", normalized_policy)
        self.assertIn("releasekwalificatie", normalized_policy)
        self.assertIn("## Onderhoudsmodel", normalized_docs)
        self.assertIn("niet rond issues of pull requests", normalized_docs)
        self.assertIn("Het bestaan van een HIL-case betekent nadrukkelijk niet", normalized_docs)

        scenario_files = (ROOT / "tests/hil/scenarios").glob("*.mjs")
        forbidden = re.compile(r"^(?:issue|pr)[-_]?\d+", re.IGNORECASE)
        for scenario_file in scenario_files:
            self.assertIsNone(
                forbidden.match(scenario_file.stem),
                f"HIL scenario must describe a durable domain, not {scenario_file.name}",
            )

    def test_harness_is_documented_and_checked_in_ci(self):
        self.assertIn("snapshot.json", DOCS)
        self.assertIn("--restore-snapshot", DOCS)
        self.assertIn("OpenQuatt/OpenQuatt-Simulator", DOCS)
        self.assertIn('"check:hil"', PACKAGE)
        self.assertNotIn("hil-harness-tests:", WORKFLOW)
        host_job = WORKFLOW.split("  host-regression-tests:", 1)[1].split(
            "\n  validate-and-compile:", 1
        )[0]
        self.assertIn("npm run check:hil", host_job)
        self.assertIn("./scripts/run_host_regression_tests.sh", host_job)
        self.assertIn("Validate Duo HIL config", ESPHOME_BUILD_WORKFLOW)
        self.assertIn(
            "esphome config configs/heatpump_controller_q/duo_hil.yaml",
            ESPHOME_BUILD_WORKFLOW,
        )
        self.assertIn("Validate HIL test overlays", ESPHOME_BUILD_WORKFLOW)
        self.assertIn(
            "esphome config configs/hil/input_sources_fast_duo_wifi.yaml",
            ESPHOME_BUILD_WORKFLOW,
        )


if __name__ == "__main__":
    unittest.main()
