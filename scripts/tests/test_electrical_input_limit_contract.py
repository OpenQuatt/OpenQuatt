from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SUPERVISOR = (ROOT / "openquatt" / "oq_supervisory_controlmode.yaml").read_text()
SUPERVISOR_RUNTIME = (ROOT / "openquatt/includes/control/oq_supervisory_state_runtime.h").read_text()
LIMITER_LOGIC = (ROOT / "openquatt" / "includes" / "control" / "oq_supervisory_power_limiter_logic.h").read_text()
LIMITER_RUNTIME = (ROOT / "openquatt" / "includes" / "control" / "oq_supervisory_power_limiter_runtime.h").read_text()
LIMITER_TEST = (ROOT / "tests" / "host" / "supervisory_power_limiter_logic_test.cpp").read_text()
POWER_HOUSE = (ROOT / "openquatt" / "oq_power_house_strategy.yaml").read_text()
POWER_HOUSE_RUNTIME = (ROOT / "openquatt/includes/control/oq_power_house_runtime.h").read_text()
DISPATCH = (ROOT / "openquatt" / "includes" / "control" / "oq_power_house_dispatch_logic.h").read_text()
HEATING_CURVE = (ROOT / "openquatt" / "oq_heating_curve_strategy.yaml").read_text()
HEATING_CURVE_RUNTIME = (ROOT / "openquatt/includes/control/oq_heating_curve_runtime.h").read_text()
COOLING = (ROOT / "openquatt" / "oq_cooling_strategy.yaml").read_text()
COOLING_RUNTIME = (ROOT / "openquatt/includes/control/oq_cooling_runtime.h").read_text()
HP_IO = (ROOT / "openquatt" / "oq_HP_io.yaml").read_text()
SUBSTITUTIONS = (ROOT / "openquatt" / "oq_substitutions_common.yaml").read_text()
WEB_LIMIT = (ROOT / "openquatt" / "web" / "js" / "src" / "settings" / "electrical-limit.js").read_text()

class ElectricalInputLimitContractTest(unittest.TestCase):
    def test_setting_is_persistent_and_keeps_generation_defaults(self) -> None:
        self.assertIn("id: oq_electrical_current_limit_configured_a", SUPERVISOR)
        self.assertIn("restore_value: true\n    initial_value: 'NAN'", SUPERVISOR)
        self.assertIn("id: oq_electrical_current_limit_a", SUPERVISOR)
        self.assertIn("id(hp_generation).current_option() == \"V2\"", LIMITER_RUNTIME)
        self.assertIn("standard_current_a(", LIMITER_LOGIC)
        self.assertIn("absolute_maximum_current_a(", LIMITER_LOGIC)
        self.assertIn("generation_known()", LIMITER_RUNTIME)
        self.assertIn("effective_current_a(", LIMITER_LOGIC)
        self.assertGreaterEqual(SUPERVISOR.count("traits.set_max_value"), 3)

    def test_firmware_and_web_share_absolute_limits(self) -> None:
        self.assertIn('oq_duo_current_limit_v2_max_a: "26.0"', SUBSTITUTIONS)
        self.assertIn("ELECTRICAL_LIMIT_V2_MAX_A = 26", WEB_LIMIT)
        self.assertIn('oq_electrical_current_limit_min_a: "10.0"', SUBSTITUTIONS)
        self.assertIn("ELECTRICAL_LIMIT_MIN_A = 10", WEB_LIMIT)
        self.assertIn("oq_duo_current_limit_v2_max_a", SUPERVISOR)

    def test_single_and_duo_share_measured_feedback_limiter(self) -> None:
        self.assertEqual(SUPERVISOR_RUNTIME.count("oq_supervisory_power_runtime::runtime().tick"), 1)
        self.assertNotIn("if (!measurement_valid)", SUPERVISOR)
        self.assertIn("id(hp1_is_online)", LIMITER_RUNTIME)
        self.assertIn("OQ_POWER_SECONDARY_ID(is_online)", LIMITER_RUNTIME)
        self.assertIn("!config.duo || valid(input.hp2", LIMITER_LOGIC)

    def test_missing_or_stale_measurements_fail_conservatively(self) -> None:
        self.assertIn("${hp_id}_voltage_last_update_ms", HP_IO)
        self.assertIn("${hp_id}_current_last_update_ms", HP_IO)
        self.assertIn("${oq_power_measurement_stale_s}", SUPERVISOR)
        for marker in ("fresh(", "fallback_cap(", "saturated_add("):
            self.assertIn(marker, LIMITER_LOGIC)
            self.assertIn(marker, LIMITER_TEST)
        self.assertIn("std::isfinite(input.total_power_w)", LIMITER_LOGIC)
        self.assertIn("std::numeric_limits<float>::infinity()", LIMITER_TEST)

    def test_yaml_is_a_compact_contract_and_extracted_sources_stay_bounded(self) -> None:
        self.assertLessEqual(len(SUPERVISOR.splitlines()), 2050)
        total = sum(len(source.splitlines()) for source in (SUPERVISOR, LIMITER_LOGIC, LIMITER_RUNTIME, LIMITER_TEST))
        self.assertLessEqual(total, 2360)

    def test_power_house_alone_uses_predictive_thresholds(self) -> None:
        self.assertEqual(POWER_HOUSE_RUNTIME.count("id(oq_power_limit_soft_w)"), 1)
        self.assertEqual(POWER_HOUSE_RUNTIME.count("id(oq_power_limit_peak_w)"), 1)
        self.assertIn("electrical_limits_valid", DISPATCH)
        self.assertIn("result.electrical_w > tuning.peak_limit_w", DISPATCH)
        self.assertIn("id(oq_power_cap_f)", HEATING_CURVE_RUNTIME)
        self.assertIn("id(oq_power_cap_f)", COOLING_RUNTIME)
        self.assertNotIn("oq_power_limit_peak_w", HEATING_CURVE + HEATING_CURVE_RUNTIME)
        self.assertNotIn("oq_power_limit_peak_w", COOLING + COOLING_RUNTIME)

    def test_heating_curve_dispatch_uses_power_capped_continuous_demand(self) -> None:
        for marker in ("oq_curve::power_capped_demand_u(id(oq_curve_demand_continuous), capped, config.demand_max_f)", "phase_target_power_w(heat_phase, demand_u, owner_capacity_w, duo_capacity_w)", "std::lround(demand_u * level_cap)", "const float dispatch_u = config.demand_max_f > 0 ? static_cast<float>(capped) / config.demand_max_f : 0.0f;", "dispatch_u >= 0.95f", "dispatch_u >= duo_enable_min_u", "dispatch_u <= duo_disable_max_u", "const bool demand_active = demand_u > 0.0f;", "hp1_candidate, hp2_candidate, demand_active"):
            self.assertIn(marker, HEATING_CURVE_RUNTIME)
        self.assertNotIn("if (isnan(demand_continuous))", HEATING_CURVE_RUNTIME)
        paths = ("openquatt/oq_heating_curve_strategy.yaml", "openquatt/includes/control/oq_heating_curve_logic.h", "openquatt/includes/control/oq_heating_curve_runtime.h", "tests/host/heating_curve_restart_logic_test.cpp", "scripts/tests/test_electrical_input_limit_contract.py")
        self.assertLessEqual(sum(len((ROOT / path).read_text().splitlines()) for path in paths), 1879)

if __name__ == "__main__":
    unittest.main()
