"""Execute the real weather HTTP route, renderer and main-loop publication."""
from pathlib import Path
import json
import tempfile
import unittest

from tests.python.test_weather_component_runtime import STUB_HEADERS
from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/weather"


class WeatherEndpointRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="oq-weather-endpoint-")
        cls.addClassCleanup(cls.directory.cleanup)
        temporary = Path(cls.directory.name)
        for header in (*STUB_HEADERS, "OpenQuattHttpStatus.h", "lwip/sockets.h",
                       "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h",
                       "esphome/components/web_server_base/web_server_base.h"):
            path = temporary / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#pragma once\n#include "weather_endpoint_stubs.h"\n')
        cls.binary = temporary / "weather-endpoint"
        result = run_process(compiler_command(
            "-I", str(temporary), "-I", str(FIXTURES), "-I", str(ROOT),
            "-I", str(ROOT / "openquatt"), "-I", str(ROOT / "components/openquatt_common"),
            str(FIXTURES / "endpoint_runtime.cpp"), "-o", str(cls.binary),
        ), timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True)
        if result.returncode != 0:
            raise AssertionError(result.stdout + result.stderr)

    def scenario(self, name):
        result = run_process([str(self.binary), name], timeout=RUN_TIMEOUT_SECONDS,
                             capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_real_route_schema_authentication_no_store_escaping_and_exact_epochs(self):
        self.scenario("schema")

    def test_busy_and_failed_socket_release_the_only_request_copy(self):
        self.scenario("socket")

    def test_psram_failure_keeps_endpoint_and_dew_point_fail_closed_without_fallback(self):
        self.scenario("unavailable")

    def test_model_timestamp_cannot_be_refreshed_by_download_or_lifecycle_transitions(self):
        self.scenario("lifecycle")

    def test_http_cannot_rejuvenate_a_stale_main_loop_or_invalid_clock(self):
        self.scenario("stale")

    def test_concurrent_main_loop_publication_and_real_http_serialization(self):
        self.scenario("concurrency")

    def test_complete_strict_psram_layout_stays_below_twenty_one_kib(self):
        output = self.scenario("memory")
        self.assertIn("Storage=", output)

    def test_wind_research_is_atomic_optional_timestamped_and_never_revived(self):
        normal, partial, missing, no_dew_point, stale, expired, disabled, location, ota = [
            json.loads(line) for line in self.scenario("wind").splitlines()]
        wind = normal["current_wind"]
        self.assertEqual(wind, {"schema": 1, "source": "model", "height_m": 10,
                               "direction_convention": "from_true_north_clockwise", "used_by_model": False,
                               "valid": True, "direction_valid": True, "time_epoch": 1800000000,
                               "speed_mps": 2.5, "direction_deg": 359.5})
        self.assertEqual(normal["fetched_ms"], (1 << 40) + 1000)
        self.assertEqual(normal["published_ms"], normal["fetched_ms"])
        self.assertEqual(normal["wind_direction_deg"], [0] * 36)
        self.assertAlmostEqual(normal["latitude"], 52.09, places=4)
        self.assertAlmostEqual(normal["longitude"], 6.15, places=4)
        self.assertEqual(partial["wind_direction_deg"][:3], [360, None, 0])
        self.assertTrue(partial["current_wind"]["valid"])
        self.assertFalse(partial["current_wind"]["direction_valid"])
        self.assertIsNone(partial["current_wind"]["direction_deg"])
        self.assertEqual(missing["wind_direction_deg"], [None] * 36)
        self.assertFalse(missing["current_wind"]["valid"])
        self.assertTrue(missing["current"]["valid"])
        self.assertTrue(no_dew_point["current_wind"]["valid"])
        self.assertFalse(no_dew_point["current"]["valid"])
        self.assertTrue(expired["valid"])
        for frame in (stale, expired, disabled, location, ota):
            self.assertFalse(frame["current_wind"]["valid"])
            self.assertFalse(frame["current_wind"]["direction_valid"])
            self.assertIsNone(frame["current_wind"]["speed_mps"])
            self.assertIsNone(frame["current_wind"]["direction_deg"])
        for frame in (stale, disabled, location, ota):
            self.assertNotIn("wind_direction_deg", frame)

    def test_diagnostics_remain_readable_without_psram(self):
        self.scenario("diagnostics")

    def test_atomic_issue_preserves_all_horizons_and_original_context_across_a_new_fetch(self):
        before, after, revoked, live = [json.loads(line) for line in self.scenario("atomic").splitlines()]
        self.assertEqual(before["scenario"], after["scenario"])
        issue = after["scenario"]
        self.assertTrue(issue["valid"])
        self.assertEqual(issue["horizon_valid"], [True] * 3)
        self.assertEqual(issue["room_start_c"], 0)
        self.assertEqual(issue["held_heat_w"], 0)
        self.assertEqual(issue["weather_generation"], 7)
        self.assertEqual(after["generation"], 8)
        self.assertLess(issue["issued_epoch"], after["fetched_epoch"])
        self.assertEqual(issue["configuration"], after["configuration"])
        self.assertFalse(revoked["scenario"]["valid"])
        self.assertEqual(revoked["scenario"]["reason"], "model_unavailable")
        self.assertEqual(revoked["scenario"]["temperature_c"], [None] * 3)
        self.assertTrue(live["scenario"]["valid"])
        self.assertGreater(live["scenario"]["issued_epoch"], issue["issued_epoch"])

    def test_invalid_atomic_object_never_leaks_retained_scalars_through_lifecycle_and_model_gates(self):
        results = [json.loads(line) for line in self.scenario("atomic_gates").splitlines()]
        self.assertEqual(len(results), 8)
        for result in results:
            self.assertFalse(result["scenario"]["valid"])
            self.assertEqual(result["scenario"]["horizon_valid"], [False] * 3)
            self.assertEqual(result["scenario"]["temperature_c"], [None] * 3)

    def test_failed_receipt_publication_observes_an_already_invalid_atomic_scenario(self):
        result = json.loads(self.scenario("receipt_interleave"))
        self.assertFalse(result["scenario"]["valid"])
        self.assertEqual(result["scenario"]["reason"], "location_invalid")
        self.assertEqual(result["scenario"]["temperature_c"], [None] * 3)


if __name__ == "__main__":
    unittest.main()
