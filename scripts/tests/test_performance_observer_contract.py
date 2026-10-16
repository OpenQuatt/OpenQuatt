from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PerformanceObserverContractTest(unittest.TestCase):
    def test_opt_in_is_volatile_and_duo_only(self):
        package = (ROOT / "openquatt/oq_performance_observer.yaml").read_text()
        self.assertIn("restore_mode: ALWAYS_OFF", package)
        self.assertNotIn("restore_value: true", package)
        self.assertIn("oq_performance_observer.yaml", (ROOT / "openquatt/topology/duo.yaml").read_text())
        self.assertNotIn("oq_performance_observer.yaml", (ROOT / "openquatt/topology/single.yaml").read_text())

    def test_long_report_stays_local_and_ha_only_gets_short_status(self):
        package = (ROOT / "openquatt/oq_performance_observer.yaml").read_text()
        full = package.split('name: "Performance Model Observer Report"', 1)[1].split('  - platform:', 1)[0]
        self.assertIn("internal: true", full)
        self.assertIn('name: "Performance Model Observer Status"', package)
        self.assertIn("runtime().summary()", package)
        runtime = (ROOT / "openquatt/includes/performance/oq_performance_observer_runtime.h").read_text()
        summary = runtime.split("std::string summary() const {", 1)[1].split("std::string report()", 1)[0]
        self.assertNotIn("thermal_w", summary)
        self.assertNotIn("context_ms", summary)
        # The explicit local bulk route deliberately supports internal diagnostics.
        self.assertIn("const bool include_internal = true;", (ROOT / "components/openquatt_entities/OpenQuattEntities.cpp").read_text())

    def test_psram_failure_and_main_loop_ownership_fail_closed(self):
        source = (ROOT / "openquatt/includes/performance/oq_performance_observer_runtime.h").read_text()
        self.assertIn("std::atomic<bool> requested_", source)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", source)
        self.assertIn("Status::ALLOCATION_FAILED", source)
        self.assertEqual(source.count("heap_caps_malloc("), 1)
        self.assertNotIn("MALLOC_CAP_INTERNAL", source)
        disable = source[source.index("if (!wanted) {"):source.index("} else {", source.index("if (!wanted) {"))]
        for marker in ("ph_hook = nullptr", "curve_hook = nullptr", "~RuntimeState()", "heap_caps_free(state_)", "state_ = nullptr"):
            self.assertIn(marker, disable)
        for marker in ("hp1_last_applied_level", "hp2_last_applied_level", "state_->stability.reset()", "state_->job.invalidate(why)"):
            self.assertIn(marker, source)

    def test_complete_matrix_is_bounded_and_reset_in_place(self):
        source = (ROOT / "openquatt/includes/performance/oq_performance_observer_logic.h").read_text()
        self.assertNotIn("cache_ = {}", source)
        self.assertIn("for (auto& row : cache_)", source)
        self.assertLess(source.index("if (!grid_.complete) return;"), source.index("result = next;"))
        self.assertIn("Status::BASELINE_MISMATCH", source)
        self.assertIn("policy.allowed[hp][level]", source)


if __name__ == "__main__":
    unittest.main()
