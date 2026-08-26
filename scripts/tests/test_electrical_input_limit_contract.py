from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SUPERVISOR = (ROOT / "openquatt" / "oq_supervisory_controlmode.yaml").read_text()
POWER_HOUSE = (ROOT / "openquatt" / "oq_power_house_strategy.yaml").read_text()
HEATING_CURVE = (ROOT / "openquatt" / "oq_heating_curve_strategy.yaml").read_text()
COOLING = (ROOT / "openquatt" / "oq_cooling_strategy.yaml").read_text()
HP_IO = (ROOT / "openquatt" / "oq_HP_io.yaml").read_text()


class ElectricalInputLimitContractTest(unittest.TestCase):
    def test_setting_is_persistent_and_keeps_generation_defaults(self) -> None:
        self.assertIn("id: oq_electrical_current_limit_configured_a", SUPERVISOR)
        self.assertIn("restore_value: true\n    initial_value: 'NAN'", SUPERVISOR)
        self.assertIn("id: oq_electrical_current_limit_a", SUPERVISOR)
        self.assertIn("id(hp_generation).current_option() == \"V2\"", SUPERVISOR)
        self.assertIn("max_current_a = ${oq_duo_current_limit_v2_a}", SUPERVISOR)
        self.assertIn("fmaxf(${oq_electrical_current_limit_min_a}, x)", SUPERVISOR)

    def test_single_and_duo_share_measured_feedback_limiter(self) -> None:
        limiter = SUPERVISOR.split(
            "// Electrical input limiter - measured safety net for Single and Duo",
            maxsplit=1,
        )[1].split("const uint32_t prepost_ms", maxsplit=1)[0]
        self.assertIn("id(hp1_is_online)", limiter)
        self.assertIn("id(${secondary_hp_id}_is_online)", limiter)
        self.assertIn("if (!measurement_valid)", limiter)
        self.assertNotIn("id(oq_power_cap_f) = (int) ${oq_power_cap_max_f}", limiter)

    def test_missing_or_stale_measurements_fail_conservatively(self) -> None:
        self.assertIn("${hp_id}_voltage_last_update_ms", HP_IO)
        self.assertIn("${hp_id}_current_last_update_ms", HP_IO)
        self.assertIn("${oq_power_measurement_stale_s}", SUPERVISOR)
        self.assertIn("id(oq_power_cap_f) = ${oq_power_cap_nan_f}", SUPERVISOR)
        self.assertIn("id(oq_power_cap_f) = ${oq_power_cap_nan_f} / 2", SUPERVISOR)

    def test_power_house_alone_uses_predictive_thresholds(self) -> None:
        self.assertEqual(POWER_HOUSE.count("const float P_EL_SOFT_W = id(oq_power_limit_soft_w);"), 2)
        self.assertEqual(POWER_HOUSE.count("const float P_EL_PEAK_W = id(oq_power_limit_peak_w);"), 2)
        self.assertIn("id(oq_power_cap_f)", HEATING_CURVE)
        self.assertIn("id(oq_power_cap_f)", COOLING)
        self.assertNotIn("oq_power_limit_peak_w", HEATING_CURVE)
        self.assertNotIn("oq_power_limit_peak_w", COOLING)


if __name__ == "__main__":
    unittest.main()
