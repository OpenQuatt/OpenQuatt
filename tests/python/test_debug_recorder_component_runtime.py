"""Compile the complete recorder with peripheral adapters, never source extraction.

The real header, translation unit, default schema and PsramBuffer run unchanged.
The fixture injects IDF allocation/HTTP failures and uses a host mutex; physical
PSRAM margins, FreeRTOS scheduling and network timing remain HIL responsibilities.
"""

import json
from pathlib import Path
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/fixtures/debug_recorder"


class RecorderComponentRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        directory = tempfile.TemporaryDirectory(prefix="oq-recorder-component-")
        cls.addClassCleanup(directory.cleanup)
        cls.binaries = {}
        for duo in [0, 1]:
            binary = Path(directory.name) / f"recorder-{duo}"
            result = run_process(
                compiler_command(
                    "-pthread", f"-DOQ_TOPOLOGY_DUO={duo}",
                    "-DUSE_SENSOR", "-DUSE_NUMBER", "-DUSE_BINARY_SENSOR",
                    "-DUSE_SWITCH", "-DUSE_TEXT_SENSOR", "-DUSE_SELECT",
                    "-I", str(FIXTURE), "-I", str(ROOT),
                    "-I", str(ROOT / "components/openquatt_common"),
                    str(ROOT / "components/openquatt_debug_recorder/OpenQuattDebugRecorder.cpp"),
                    str(FIXTURE / "component_test.cpp"), "-o", str(binary),
                ),
                timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
            )
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            cls.binaries[duo] = binary

    def scenario(self, name, duo):
        result = run_process(
            [str(self.binaries[duo]), name], timeout=RUN_TIMEOUT_SECONDS,
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout[:1000])
        return dict(line.split("\t", 1) for line in result.stdout.splitlines())

    @staticmethod
    def initial(payload):
        return {payload["columns"][index]: value for index, value in payload["initial"]}

    def test_setup_psram_failure_at_each_allocation_and_unavailable_peripherals(self):
        for duo in self.binaries:
            with self.subTest(duo=duo):
                output = self.scenario("setup", duo)
                for failure in range(1, 10):
                    status = json.loads(output[f"allocation-{failure}"])
                    self.assertFalse(status["available"])
                    self.assertFalse(status["active"])
                    self.assertEqual(status["sample_count"], 0)
                    self.assertEqual(status["storage"], "unavailable")
                ready = json.loads(output["ready"])
                self.assertTrue(ready["available"] and ready["active"])
                self.assertEqual(ready["storage"], "psram")
                self.assertGreaterEqual(ready["retention_capacity_s"], 6 * 3600)

    def test_deferred_capture_real_lock_contention_persistence_and_clock_wrap(self):
        for duo in self.binaries:
            with self.subTest(duo=duo):
                status = json.loads(self.scenario("lifecycle", duo)["lifecycle"])
                self.assertTrue(status["available"] and status["enabled"] and status["active"])
                self.assertGreater(status["elapsed_s"], 7 * 3600)
                self.assertEqual(status["sample_count"], status["sample_capacity"])

    def test_configuration_ownership_missing_values_real_pool_pressure_and_eviction(self):
        for duo in self.binaries:
            with self.subTest(duo=duo):
                self.scenario("configuration", duo)

    def test_real_default_schema_long_retention_snapshot_interleavings_and_http_failures(self):
        for duo in self.binaries:
            with self.subTest(duo=duo):
                output = self.scenario("retention", duo)
                status = json.loads(output["retention-status"])
                capacity = status["sample_capacity"]
                self.assertEqual(status["sample_count"], capacity)
                self.assertGreaterEqual(status["retention_capacity_s"], 6 * 3600)
                self.assertFalse(status["string_overflow"])
                for minutes in [0, 15, 120, 360, 4294967295]:
                    count = capacity if minutes in [0, 4294967295] else min(capacity, minutes * 6 + 1)
                    span = (count - 1) * 10
                    payload = json.loads(output[str(minutes)])
                    self.assertEqual(payload["recording"]["sample_count"], count)
                    self.assertEqual(payload["recording"]["duration_s"], span)
                    self.assertEqual(payload["samples"][0], [0, []])
                    self.assertEqual(payload["samples"][-1][0], span)
                    self.assertEqual(payload["recording"]["event_count"], count - 1)
                    self.assertEqual(payload["recording"]["mode"], "rolling")
                    initial = self.initial(payload)
                    self.assertEqual(initial["timeNowHhmm"], "12:34")
                    self.assertEqual(
                        json.loads(initial["configurationSnapshot"])["values"]["phRunExtension"],
                        minutes != 15,
                    )
                jitter = json.loads(output["jitter"])
                self.assertEqual([sample[0] for sample in jitter["samples"]], [0, 15, 47, 60])
                self.assertEqual(jitter["recording"]["event_count"], 3)
                self.assertEqual(json.loads(output["immutable"])["recording"]["sample_count"], 91)
                with self.assertRaises(json.JSONDecodeError):
                    json.loads(output["aborted"])
                empty = json.loads(output["empty"])
                self.assertEqual(empty["initial"], [])
                self.assertEqual(empty["samples"], [])
                self.assertEqual(empty["recording"]["duration_s"], 0)
                for minutes, expected in [(0, "00:00"), (545, "09:05"), (754, "12:34"),
                                          (1439, "23:59"), (1440, None), (65534, None)]:
                    self.assertEqual(self.initial(json.loads(output[f"clock-{minutes}"]))["timeNowHhmm"], expected)

    def test_partial_sync_and_failed_compensation_require_durable_retries(self):
        for duo in self.binaries:
            with self.subTest(duo=duo):
                output = self.scenario("preference-failures", duo)
                for previous in ["off", "on"]:
                    for failed_key in ["own", "other"]:
                        status = json.loads(output[f"preference-{previous}-{failed_key}"])
                        self.assertFalse(status["enabled"] or status["active"])
                        self.assertEqual(status["sample_count"], 3)
                for failure in ["save", "sync"]:
                    status = json.loads(output[f"schema-compensation-{failure}"])
                    self.assertFalse(status["enabled"] or status["active"])
                    self.assertEqual(status["sample_count"], 0)
                    self.assertEqual(status["recording_id"], 0)

    def test_registered_handler_methods_same_origin_csrf_and_failed_persistence(self):
        for duo in self.binaries:
            with self.subTest(duo=duo):
                output = self.scenario("http", duo)
                full = json.loads(output["http-full-download"])
                one_minute = json.loads(output["http-range-1"])
                overflow = json.loads(output["http-range-4294967297"])
                self.assertEqual(full["recording"]["sample_count"], 13)
                self.assertEqual(full["recording"]["duration_s"], 120)
                self.assertEqual(one_minute["recording"]["sample_count"], 7)
                self.assertEqual(one_minute["recording"]["duration_s"], 60)
                self.assertEqual(overflow, full)
                self.assertNotEqual(overflow["samples"], one_minute["samples"])
                self.assertFalse(overflow["recording"]["active"])


if __name__ == "__main__":
    unittest.main()
