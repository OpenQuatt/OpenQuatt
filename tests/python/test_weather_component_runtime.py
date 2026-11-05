"""Execute the production weather worker/publication lifecycle with platform fault injection."""

from pathlib import Path
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/weather"
STUB_HEADERS = (
    "esphome/core/component.h",
    "esphome/core/helpers.h",
    "esphome/core/log.h",
    "esphome/components/binary_sensor/binary_sensor.h",
    "esphome/components/sensor/sensor.h",
    "esphome/components/switch/switch.h",
    "esphome/components/text/text.h",
    "esphome/components/text_sensor/text_sensor.h",
    "esphome/components/time/real_time_clock.h",
    "esphome/components/ota/ota_backend.h",
    "esphome/components/network/util.h",
    "lwip/ip_addr.h",
    "freertos/FreeRTOS.h",
    "freertos/task.h",
    "esp_heap_caps.h",
    "esp_timer.h",
    "nvs.h",
)


class WeatherComponentRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="oq-weather-component-")
        cls.addClassCleanup(cls.directory.cleanup)
        temporary = Path(cls.directory.name)
        for header in STUB_HEADERS:
            path = temporary / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#pragma once\n#include "weather_runtime_stubs.h"\n')
        cls.binary = temporary / "weather-component"
        command = compiler_command(
            "-I", str(temporary),
            "-I", str(FIXTURES),
            "-I", str(ROOT),
            "-I", str(ROOT / "openquatt"),
            "-I", str(ROOT / "components/openquatt_common"),
            str(FIXTURES / "component_runtime.cpp"), "-o", str(cls.binary),
        )
        result = run_process(command, timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True)
        if result.returncode != 0:
            raise AssertionError(result.stdout + result.stderr)

    def scenario(self, name):
        result = run_process(
            [str(self.binary), name], timeout=RUN_TIMEOUT_SECONDS,
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, f"{name}: {result.stdout}{result.stderr}")

    def test_task_allocation_failure_keeps_old_text_and_terminates_pending_save(self):
        self.scenario("task_failure_first")

    def test_second_save_after_task_failure_is_visible_and_never_retries_allocation(self):
        self.scenario("task_failure_second")

    def test_fresh_install_task_failure_does_not_publish_a_saved_location(self):
        self.scenario("fresh_task_failure")

    def test_psram_failure_does_not_use_internal_ram_or_start_worker(self):
        self.scenario("psram_failure")

    def test_enable_across_signed_and_unsigned_32bit_uptime_rearms_fetch_and_reuses_worker(self):
        self.scenario("long_disabled")

    def test_publication_follows_commit_readback_and_second_save_reuses_worker(self):
        self.scenario("verified_publication")

    def test_ota_cancels_queued_save_before_nvs_and_pending_save_resumes_after_hold(self):
        self.scenario("queued_ota")

    def test_started_nvs_transaction_completes_and_publishes_verified_value_during_ota(self):
        self.scenario("started_ota")

    def test_failed_storage_boundaries_never_publish_unverified_text(self):
        for boundary in ("open", "write", "commit", "read", "mismatch"):
            with self.subTest(boundary=boundary):
                self.scenario(f"storage_{boundary}")

    def test_new_location_supersedes_queued_old_save_without_publishing_old_result(self):
        self.scenario("latest_queued")

    def test_new_location_supersedes_started_old_save_without_publishing_old_result(self):
        self.scenario("latest_started")

    def test_committed_but_unverified_save_and_failed_old_value_retry_never_acknowledge_storage(self):
        self.scenario("receipt_verify_retry")

    def test_same_value_supersession_keeps_only_the_latest_request_receipt(self):
        for phase in ("queued", "started"):
            with self.subTest(phase=phase):
                self.scenario(f"receipt_latest_{phase}")

    def test_ota_preserves_pending_receipt_and_only_verified_completion_marks_it_stored(self):
        for phase in ("queued", "started"):
            with self.subTest(phase=phase):
                self.scenario(f"receipt_ota_{phase}")

    def test_storage_faults_return_failed_receipts_even_when_the_old_text_is_unchanged(self):
        for boundary in ("open", "write", "commit", "read", "mismatch"):
            with self.subTest(boundary=boundary):
                self.scenario(f"storage_receipt_{boundary}")

    def test_task_and_psram_allocation_failures_never_store_the_new_request_receipt(self):
        for allocation in ("task", "psram"):
            with self.subTest(allocation=allocation):
                self.scenario(f"receipt_{allocation}_failure")

    def test_restored_switch_keeps_weather_invalid_until_fresh_fetch_and_valid_clock(self):
        for state in ("on", "off", "on_ota"):
            with self.subTest(state=state):
                self.scenario(f"restore_{state}")


if __name__ == "__main__":
    unittest.main()
