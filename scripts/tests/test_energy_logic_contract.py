from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HP_IO = (ROOT / "openquatt" / "oq_HP_io.yaml").read_text()
ENERGY = (ROOT / "openquatt" / "oq_energy.yaml").read_text()
REQUEST = (ROOT / "openquatt" / "oq_thermal_request_control.yaml").read_text()
LOGIC = (
    ROOT / "openquatt" / "includes" / "performance" / "oq_energy_logic.h"
).read_text()
RUNTIME = (
    ROOT / "openquatt" / "includes" / "performance" / "oq_energy_runtime.h"
).read_text()


class EnergyLogicContractTest(unittest.TestCase):
    def test_per_hp_yaml_keeps_only_inputs_and_formula_contracts(self) -> None:
        self.assertEqual(HP_IO.count("oq_energy::hp_input_power_for_variant("), 1)
        self.assertEqual(HP_IO.count("oq_energy::hp_heating_power("), 1)
        self.assertEqual(HP_IO.count("oq_energy::hp_cooling_power("), 1)
        self.assertEqual(HP_IO.count("oq_energy::instant_ratio_or_nan("), 2)
        self.assertIn("id(${hp_id}_pump_relay).has_state(),", HP_IO)
        self.assertIn("pump_relay_known && in.pump_relay_running", LOGIC)
        self.assertIn("id(${hp_id}_odu_generation_detection_complete)", HP_IO)
        self.assertIn("id(${hp_id}_power_input_status_code) = static_cast<int>(estimate.status)", HP_IO)
        self.assertIn("hp_input_power_status_name", LOGIC)
        self.assertIn(
            "const float pump_feedback_raw = id(${hp_id}_pump_ipwm_feedback_raw).state;",
            HP_IO,
        )
        self.assertIn("oq_pump_ipwm::decode", HP_IO)
        self.assertIn("pump_power_w,", HP_IO)
        self.assertNotIn("id(${hp_id}_pump_power).state,", HP_IO)
        for freshness_id in (
            "voltage_last_update_ms",
            "current_last_update_ms",
            "fan_speed_last_update_ms",
            "status_2108_last_update_ms",
            "pump_feedback_last_update_ms",
        ):
            self.assertIn(f"id(${{hp_id}}_{freshness_id})", HP_IO)

    def test_calibration_coefficients_have_one_owner(self) -> None:
        for coefficient in (
            "5.150232354845286f",
            "1.1240096401010435f",
            "-0.04858859969715763f",
            "150.06430841218332f",
        ):
            self.assertIn(coefficient, LOGIC)
            self.assertNotIn(coefficient, HP_IO)
        for coefficient in ("5.93f", "1.02579f", "-0.0133119f", "140.0f", "33.62f"):
            self.assertIn(coefficient, LOGIC)
            self.assertNotIn(coefficient, HP_IO)

    def test_v2_unavailable_power_is_not_silently_summed_as_zero(self) -> None:
        self.assertIn("v2_power_quality_contract_active()", RUNTIME)
        self.assertIn("nonnegative_sum_required", RUNTIME)

    def test_system_power_sensors_use_the_runtime_adapter(self) -> None:
        for function in (
            "total_power_input",
            "heating_power_input",
            "cooling_power_input",
            "total_heat_power",
            "total_cooling_power",
        ):
            self.assertEqual(REQUEST.count(f"oq_energy_runtime::{function}();"), 1)
            self.assertIn(f"inline float {function}()", RUNTIME)
        self.assertIn("#if OQ_TOPOLOGY_DUO", RUNTIME)
        self.assertIn("id(hp2_power_input).state", RUNTIME)

    def test_energy_ratios_share_the_tested_guards(self) -> None:
        self.assertEqual(ENERGY.count("oq_energy::ratio_or_nan("), 4)
        self.assertEqual(REQUEST.count("oq_energy::instant_ratio_or_nan("), 2)
        self.assertIn("input < minimum_input", LOGIC)
        self.assertIn("fabsf(input) < minimum_abs_input", LOGIC)


if __name__ == "__main__":
    unittest.main()
