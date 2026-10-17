from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
PERFORMANCE_YAML = (ROOT / "openquatt/oq_performance_telemetry.yaml").read_text()
USAGE_SOURCE = (
    ROOT / "components/openquatt_usage_telemetry/OpenQuattUsageTelemetry.cpp"
).read_text()
PERFORMANCE_SOURCE = (
    ROOT / "components/openquatt_performance_telemetry/OpenQuattPerformanceTelemetry.cpp"
).read_text()


class PerformanceTelemetryContractTest(unittest.TestCase):
    def test_excludes_global_supply_and_power_model_context(self) -> None:
        for removed in (
            "strategy_select:",
            "supply_target_sensor:",
            "system_supply_sensor:",
        ):
            self.assertNotIn(removed, PERFORMANCE_YAML)
        for removed in (
            'R"(,"pem":',
            'R"(,"mk":',
            'R"(,"s":)',
            "strategy_code_",
            "supply_kind_",
        ):
            self.assertNotIn(removed, PERFORMANCE_SOURCE)
        self.assertIn('R"({"v":1,"iid":")"', PERFORMANCE_SOURCE)

    def test_external_id_creation_does_not_reapply_usage_consent(self) -> None:
        match = re.search(
            r"bool OpenQuattUsageTelemetry::ensure_installation_id_for_external\(\) \{(?P<body>.*?)\n\}",
            USAGE_SOURCE,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertIn("apply_external_installation_id_(storage)", body)
        self.assertNotIn("apply_storage_(storage)", body)

    def test_publication_slots_are_boot_monotonic_and_not_utc_anchored(self) -> None:
        self.assertIn("this->next_publish_us_ = PERFORMANCE_PUBLISH_INTERVAL_US;", PERFORMANCE_SOURCE)
        self.assertIn("const int64_t monotonic_now_us = esp_timer_get_time();", PERFORMANCE_SOURCE)
        self.assertIn("advance_performance_publish_deadline(this->next_publish_us_, monotonic_now_us)", PERFORMANCE_SOURCE)
        self.assertNotIn("minute_s % WINDOW_SECONDS", PERFORMANCE_SOURCE)

    def test_partial_and_semantic_batches_wait_for_the_next_slot(self) -> None:
        self.assertIn("if (publish_due) {", PERFORMANCE_SOURCE)
        self.assertIn("this->pending_publish_allowed_ = true;", PERFORMANCE_SOURCE)
        self.assertIn("this->close_window_(false);", PERFORMANCE_SOURCE)
        self.assertIn("!this->pending_publish_allowed_", PERFORMANCE_SOURCE)
        self.assertIn("this->minute_ = {};", PERFORMANCE_SOURCE)

    def test_transport_cooldown_retries_at_subsecond_boundary(self) -> None:
        self.assertIn("external_publish_next_allowed_us()", PERFORMANCE_SOURCE)
        self.assertIn("(allowed_us - now_us + 999) / 1000", PERFORMANCE_SOURCE)
        self.assertNotIn("this->schedule_retry_();\n  }\n}\n\nvoid OpenQuattPerformanceTelemetry::handle_transport_result_", PERFORMANCE_SOURCE)


if __name__ == "__main__":
    unittest.main()
