from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
YAMLS = {
    "curve": (ROOT / "openquatt/oq_heating_curve_strategy.yaml").read_text(),
    "power_house": (ROOT / "openquatt/oq_power_house_strategy.yaml").read_text(),
    "cooling": (ROOT / "openquatt/oq_cooling_strategy.yaml").read_text(),
    "manager": (ROOT / "openquatt/oq_strategy_manager.yaml").read_text(),
}
RUNTIMES = {
    name: (ROOT / f"openquatt/includes/control/oq_{name}_runtime.h").read_text()
    for name in ("heating_curve", "power_house", "cooling", "strategy")
}


class StrategyRuntimeContractTest(unittest.TestCase):
    def test_yaml_delegates_to_runtime(self) -> None:
        calls = {
            "curve": ("oq_heating_curve_runtime::runtime()", 8),
            "power_house": ("oq_power_house_runtime::runtime()", 6),
            "cooling": ("oq_cooling_runtime::runtime()", 3),
            "manager": ("oq_strategy_runtime::runtime()", 3),
        }
        for name, (marker, expected) in calls.items():
            self.assertEqual(YAMLS[name].count(marker), expected)
        for implementation_marker in (
            "DispatchState dispatch_state",
            "publish_cooling_limiter_event",
            "max_allowed_level_for_hp",
            "trusted_local_outside",
            "decide_dispatch(",
        ):
            self.assertNotIn(implementation_marker, "\n".join(YAMLS.values()))

    def test_runtime_owns_lifecycle_and_side_effects(self) -> None:
        required = {
            "heating_curve": ("write_pid_output", "strategy_tick", "dispatch_tick", "integral_reset_required"),
            "power_house": ("set_response_profile", "decide_startup_demand", "decide_dispatch", "void reset()"),
            "cooling": ("demand_tick", "dispatch_tick", "publish_limiter_event_", "minimum_off_remaining_s"),
            "strategy": ("switch_heating_mode", "reset_shared_", "local_outside_temperature"),
        }
        for name, markers in required.items():
            for marker in markers:
                self.assertIn(marker, RUNTIMES[name])
            self.assertIn("#if defined(OQ_TOPOLOGY_DUO)", RUNTIMES[name])


    def test_missing_model_data_is_held_per_heat_pump(self) -> None:
        power_house = RUNTIMES["power_house"]
        curve = RUNTIMES["heating_curve"]
        for marker in ("hp1_model_available", "hp2_model_available", "active_model_missing"):
            self.assertIn(marker, power_house)
            self.assertIn(marker, curve)
        self.assertIn("any_servable_candidate", power_house)
        self.assertIn("idle_model_missing", curve)
        self.assertIn("preserve_active_topology_without_model", power_house + curve)



if __name__ == "__main__":
    unittest.main()
