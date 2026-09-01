import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PROFILE = (ROOT / "configs/hil/input_sources_fast_duo_wifi.yaml").read_text()
RUNNER = (ROOT / "scripts/hil/run-input-sources.mjs").read_text()
REST_CLIENT = (ROOT / "scripts/hil/rest-client.mjs").read_text()
SUBSTITUTIONS = (ROOT / "openquatt/oq_substitutions_common.yaml").read_text()
TARGETS = (ROOT / "build_targets.yaml").read_text()
DOCS = (ROOT / "docs/hil-testing.md").read_text()
PACKAGE = (ROOT / "package.json").read_text()
WORKFLOW = (ROOT / ".github/workflows/ci-build.yml").read_text()


class HilHarnessContractTest(unittest.TestCase):
    def test_fast_profile_is_explicitly_test_only(self):
        self.assertIn("HIL TEST ONLY", PROFILE)
        self.assertIn(
            "!include ../heatpump_controller_q/duo_wifi.yaml", PROFILE
        )
        self.assertIn('name: "HIL Test Profile"', PROFILE)
        self.assertIn('return {"input-sources-fast-v1"};', PROFILE)
        self.assertNotIn("input_sources_fast_duo_wifi.yaml", TARGETS)

    def test_fast_profile_does_not_change_production_floors(self):
        for marker in (
            'api_input_room_temperature_stale_s: "45"',
            'api_input_heating_enable_stale_s: "45"',
            'oq_selected_input_stale_hold_s: "10"',
            'oq_hp_min_off_s: "10"',
        ):
            self.assertIn(marker, PROFILE)
        for marker in (
            'api_input_room_temperature_stale_s: "600"',
            'api_input_heating_enable_stale_s: "0"',
            'oq_selected_input_stale_hold_s: "300"',
            'oq_cooling_minimum_off_min_s: "240"',
            'oq_hp_min_off_s: "240"',
        ):
            self.assertIn(marker, SUBSTITUTIONS)
        self.assertNotIn("oq_cooling_minimum_off_min_s", PROFILE)

    def test_mutations_are_gated_and_targets_have_no_defaults(self):
        self.assertIn("mutating HIL runs require --apply", RUNNER)
        self.assertIn("--device and --restore-config", RUNNER)
        self.assertIn("openquatt-modbus-opentherm-v1", RUNNER)
        self.assertIn("simulator contract differs", RUNNER)
        self.assertIn("writeIntervalMs < 1000", REST_CLIENT)
        self.assertNotIn("192.168.", RUNNER)
        self.assertNotIn("192.168.", REST_CLIENT)

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


if __name__ == "__main__":
    unittest.main()
