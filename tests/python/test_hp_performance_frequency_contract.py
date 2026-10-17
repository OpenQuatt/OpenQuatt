from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
FREQUENCY_HEADER = (
    ROOT / "openquatt" / "includes" / "performance" / "hp_perf_frequency.h"
).read_text()
STRATEGIES = "\n".join(
    (ROOT / "openquatt" / name).read_text()
    for name in (
        "oq_heating_curve_strategy.yaml",
        "oq_power_house_strategy.yaml",
        "oq_supervisory_controlmode.yaml",
    )
)
STRATEGIES += "\n" + "\n".join(
    (ROOT / f"openquatt/includes/control/oq_{name}_runtime.h").read_text()
    for name in ("heating_curve", "power_house", "supervisory_state")
)


class HPPerformanceFrequencyContractTest(unittest.TestCase):
    def test_reference_axes_are_explicit_and_version_specific(self) -> None:
        self.assertIn("V1_HEATING_FREQUENCIES_HZ", FREQUENCY_HEADER)
        self.assertIn("30.0f, 39.0f, 49.0f, 55.0f, 61.0f", FREQUENCY_HEADER)
        self.assertIn("V2_HEATING_FREQUENCIES_HZ", FREQUENCY_HEADER)
        self.assertIn("20.0f, 26.0f, 30.0f, 48.0f, 55.0f", FREQUENCY_HEADER)

    def test_frequency_interpolation_does_not_extrapolate(self) -> None:
        self.assertIn("frequency_hz < frequencies_hz.front()", FREQUENCY_HEADER)
        self.assertIn("frequency_hz > frequencies_hz.back()", FREQUENCY_HEADER)
        self.assertIn("return NAN", FREQUENCY_HEADER)

    def test_control_strategies_enter_performance_maps_via_frequency(self) -> None:
        self.assertIn("oq_perf::predict_candidate(", STRATEGIES)
        self.assertNotIn("oq_perf::model_frequency_hz(", STRATEGIES)
        self.assertNotIn("oq_perf::interp_power_th_w(", STRATEGIES)
        self.assertNotIn("oq_perf::interp_power_el_w(", STRATEGIES)

    def test_v2_frequency_path_uses_one_joint_prediction(self) -> None:
        self.assertIn("oq_v2_model::predict_heating_for_control(frequency_hz", FREQUENCY_HEADER)
        self.assertIn("prediction.pth_w, prediction.cop, prediction.pel_w", FREQUENCY_HEADER)

    def test_low_supply_boundary_requires_existing_cold_start_guard(self) -> None:
        self.assertIn("allow_low_supply_boundary_estimate", FREQUENCY_HEADER)
        self.assertGreaterEqual(STRATEGIES.count("id(oq_cold_start_session_active)"), 3)
        self.assertGreaterEqual(STRATEGIES.count("!id(oq_cold_start_hp_blocked)"), 3)


if __name__ == "__main__":
    unittest.main()
