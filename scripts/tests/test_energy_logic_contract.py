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
        self.assertEqual(HP_IO.count("oq_energy::hp_input_power("), 1)
        self.assertEqual(HP_IO.count("oq_energy::hp_heating_power("), 1)
        self.assertEqual(HP_IO.count("oq_energy::hp_cooling_power("), 1)
        self.assertEqual(HP_IO.count("oq_energy::instant_ratio_or_nan("), 2)
        self.assertIn("id(${hp_id}_pump_relay).has_state(),", HP_IO)
        self.assertIn("pump_relay_known && in.pump_relay_running", LOGIC)

    def test_calibration_coefficients_have_one_owner(self) -> None:
        for coefficient in (
            "5.150232354845286f",
            "1.1240096401010435f",
            "-0.04858859969715763f",
            "150.06430841218332f",
        ):
            self.assertIn(coefficient, LOGIC)
            self.assertNotIn(coefficient, HP_IO)

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
