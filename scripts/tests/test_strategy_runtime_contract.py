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
LOGIC = (ROOT / "openquatt/includes/control/oq_strategy_logic.h").read_text()
HEAT_INTENT_RUNTIME = (ROOT / "openquatt/includes/control/oq_heat_intent_runtime.h").read_text()
HOST_TESTS = (
    (ROOT / "tests/host/heating_curve_restart_logic_test.cpp").read_text()
    + (ROOT / "tests/host/heat_intent_logic_test.cpp").read_text()
    + (ROOT / "tests/host/strategy_logic_test.cpp").read_text()
)


class StrategyRuntimeContractTest(unittest.TestCase):
    def test_yaml_is_a_compact_runtime_contract(self) -> None:
        calls = {
            "curve": ("oq_heating_curve_runtime::runtime()", 7),
            "power_house": ("oq_power_house_runtime::runtime()", 3),
            "cooling": ("oq_cooling_runtime::runtime()", 3),
            "manager": ("oq_strategy_runtime::runtime()", 3),
        }
        for name, (marker, expected) in calls.items():
            self.assertEqual(YAMLS[name].count(marker), expected)
        self.assertLessEqual(sum(len(source.splitlines()) for source in YAMLS.values()), 1550)
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
            "power_house": ("set_response_profile", "decide_demand", "decide_dispatch", "void reset()"),
            "cooling": ("demand_tick", "dispatch_tick", "publish_limiter_event_", "minimum_off_remaining_s"),
            "strategy": ("switch_heating_mode", "reset_shared_", "local_outside_temperature"),
        }
        for name, markers in required.items():
            for marker in markers:
                self.assertIn(marker, RUNTIMES[name])
            self.assertIn("#if defined(OQ_TOPOLOGY_DUO)", RUNTIMES[name])

    def test_new_boundaries_have_host_regressions(self) -> None:
        for marker in (
            "update_outside_ema(",
            "supply_target(",
            "cadence_due(",
            "elapsed_window_active(",
            "aggregate_local_outside(",
            "trusted_local_outside(",
            "UINT32_MAX",
        ):
            self.assertIn(marker, HOST_TESTS)

    def test_runtime_sources_remain_bounded(self) -> None:
        self.assertLessEqual(sum(len(source.splitlines()) for source in RUNTIMES.values()), 1250)
        self.assertLessEqual(len(HEAT_INTENT_RUNTIME.splitlines()), 90)
        self.assertLessEqual(len(LOGIC.splitlines()), 60)


if __name__ == "__main__":
    unittest.main()
