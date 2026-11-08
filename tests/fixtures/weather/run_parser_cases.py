"""Validate the production weather parser with ESPHome's exact ArduinoJson dependency."""
import copy
import json
import math
import os
from pathlib import Path
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[3]


def forecast():
    return {
        "latitude": 52.09, "longitude": 6.15, "utc_offset_seconds": 0,
        "hourly_units": {"time": "unixtime", "temperature_2m": "°C", "wind_speed_10m": "m/s"},
        "hourly": {"time": [1800000000 + i * 3600 for i in range(36)],
                   "temperature_2m": [10.5] * 36, "wind_speed_10m": [2.5] * 36},
        "current_units": {"time": "unixtime", "interval": "seconds", "temperature_2m": "°C",
                          "relative_humidity_2m": "%", "dew_point_2m": "°C"},
        "current": {"time": 1800000000, "interval": 900, "temperature_2m": 10.5,
                    "relative_humidity_2m": 85, "dew_point_2m": 8},
    }


def wind_forecast():
    payload = forecast()
    payload["hourly_units"]["wind_direction_10m"] = "°"
    payload["hourly"]["wind_direction_10m"] = [359.5, 0, 360] + [225.25] * 33
    payload["current_units"].update(wind_speed_10m="m/s", wind_direction_10m="°")
    payload["current"].update(wind_speed_10m=0, wind_direction_10m=0)
    return payload


class WeatherParserRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        include = Path(os.environ["OPENQUATT_WEATHER_ARDUINOJSON_INCLUDE"])
        if not (include / "ArduinoJson.h").is_file():
            raise RuntimeError("Point OPENQUATT_WEATHER_ARDUINOJSON_INCLUDE at ArduinoJson src")
        cls.directory = tempfile.TemporaryDirectory(prefix="oq-weather-parser-")
        cls.addClassCleanup(cls.directory.cleanup)
        cls.binary = Path(cls.directory.name) / "weather-parser"
        result = run_process(compiler_command(
            "-I", str(ROOT), "-I", str(include),
            str(ROOT / "tests/fixtures/weather/parser_runtime.cpp"), "-o", str(cls.binary),
        ), timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True)
        if result.returncode != 0:
            raise AssertionError(result.stdout + result.stderr)

    def parse(self, payload, epoch=1800000000, arena=8192):
        encoded = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
        result = run_process([str(self.binary), str(epoch), str(arena)], timeout=RUN_TIMEOUT_SECONDS,
                             input=encoded, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        parsed, current, stamp, wind = result.stdout.split()
        return bool(int(parsed)), bool(int(current)), int(stamp), float(wind)

    def test_hourly_wind_and_current_model_parse_within_existing_arena(self):
        self.assertEqual(self.parse(forecast()), (True, True, 1800000000, 2.5))
        self.assertFalse(self.parse(forecast(), arena=512)[0])

    def test_wind_units_length_types_and_physical_range_are_mandatory(self):
        for value in (None, "2.5", True, -0.1, 75.1):
            payload = forecast()
            payload["hourly"]["wind_speed_10m"][0] = value
            self.assertFalse(self.parse(payload)[0], value)
        for values in ([], [2.5] * 35, [2.5] * 37):
            payload = forecast()
            payload["hourly"]["wind_speed_10m"] = values
            self.assertFalse(self.parse(payload)[0])
        payload = forecast()
        payload["hourly_units"]["wind_speed_10m"] = "km/h"
        self.assertFalse(self.parse(payload)[0])

    def test_absent_or_malformed_current_clears_old_current_but_preserves_hourly(self):
        base = forecast()
        for key in ("current", "current_units"):
            for value in (None, {}, True, "bad"):
                payload = copy.deepcopy(base)
                payload[key] = value
                self.assertEqual(self.parse(payload)[:3], (True, False, 0))
            payload = copy.deepcopy(base)
            del payload[key]
            self.assertEqual(self.parse(payload)[:3], (True, False, 0))
        for key in base["current"]:
            payload = forecast()
            payload["current"][key] = None
            self.assertEqual(self.parse(payload)[:3], (True, False, 0))
        for key in base["current_units"]:
            payload = forecast()
            payload["current_units"][key] = "wrong"
            self.assertEqual(self.parse(payload)[:3], (True, False, 0))

    def test_current_time_alignment_age_and_units_fail_closed(self):
        for stamp in (0, 1800000001, 1800000900, 1799997300):
            payload = forecast()
            payload["current"]["time"] = stamp
            self.assertEqual(self.parse(payload)[:3], (True, False, 0), stamp)
        payload = forecast()
        payload["current"]["time"] -= 1800
        self.assertTrue(self.parse(payload)[1])
        self.assertFalse(self.parse(payload, epoch=1800000001)[1])
        for interval in (None, 60, 1800, "900"):
            payload = forecast()
            payload["current"]["interval"] = interval
            self.assertEqual(self.parse(payload)[:3], (True, False, 0))

    def test_current_dew_point_humidity_and_temperature_bounds(self):
        for key, values in {
            "temperature_2m": [-40.1, 55.1, "10.5", True],
            "relative_humidity_2m": [-1, 100.1, "85", True],
            "dew_point_2m": [-40.1, 35.1, 11.1, "8", True],
        }.items():
            for value in values:
                payload = forecast()
                payload["current"][key] = value
                self.assertEqual(self.parse(payload)[:3], (True, False, 0), (key, value))

    def wind(self, payload, epoch=1800000000):
        result = run_process([str(self.binary), str(epoch), "8192", "wind"], timeout=RUN_TIMEOUT_SECONDS,
                             input=json.dumps(payload, ensure_ascii=False, separators=(",", ":")),
                             capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        values = result.stdout.split()
        return ([int(x) for x in values[:6]], [float(x) for x in values[6:]])

    def test_optional_wind_preserves_knots_and_zero_is_a_valid_north_direction(self):
        flags, numbers = self.wind(wind_forecast())
        self.assertEqual(flags, [1, 1, (1 << 36) - 1, 1, 1, 1800000000])
        self.assertEqual(numbers, [0, 0, 359.5, 0, 360] + [225.25] * 33)
        self.assertTrue(self.parse(wind_forecast())[0])
        self.assertFalse(self.parse(wind_forecast(), arena=512)[0])

    def test_missing_optional_fields_clear_previous_wind_without_affecting_base_weather(self):
        flags, numbers = self.wind(forecast())
        self.assertEqual(flags, [1, 1, 0, 0, 0, 0])
        self.assertTrue(all(math.isnan(x) for x in numbers))

    def test_bad_hourly_direction_is_local_to_the_optional_sample(self):
        for bad in (None, "90", True, -0.01, 360.0000001, 360.01, 1e30):
            payload = wind_forecast()
            payload["hourly"]["wind_direction_10m"][1] = bad
            flags, numbers = self.wind(payload)
            self.assertEqual(flags, [1, 1, ((1 << 36) - 1) ^ 2, 1, 1, 1800000000], bad)
            self.assertTrue(math.isnan(numbers[3]), bad)
            self.assertEqual(numbers[2], 359.5)
        for malformed in (None, [], [90] * 35, [90] * 37, "north", {}):
            payload = wind_forecast()
            payload["hourly"]["wind_direction_10m"] = malformed
            self.assertEqual(self.wind(payload)[0], [1, 1, 0, 1, 1, 1800000000])
        payload = wind_forecast()
        payload["hourly_units"]["wind_direction_10m"] = "rad"
        self.assertEqual(self.wind(payload)[0], [1, 1, 0, 1, 1, 1800000000])

    def test_optional_current_wind_and_dew_point_have_independent_gates(self):
        for bad in (None, "90", True, -1, 360.0000001, 361, 1e30):
            payload = wind_forecast()
            payload["current"]["wind_direction_10m"] = bad
            self.assertEqual(self.wind(payload)[0][3:5], [1, 0], bad)
            self.assertTrue(self.parse(payload)[1])
        for field in ("wind_speed_10m", "wind_direction_10m"):
            payload = wind_forecast()
            payload["current_units"][field] = "wrong"
            flags = self.wind(payload)[0]
            self.assertEqual(flags[3:5], [0, 0] if field == "wind_speed_10m" else [1, 0])
            self.assertTrue(self.parse(payload)[1])
        for key, bad in (("wind_speed_10m", -0.1), ("wind_speed_10m", -1e-50),
                         ("wind_speed_10m", 75.0000001), ("wind_speed_10m", 75.1),
                         ("wind_speed_10m", True), ("wind_speed_10m", None)):
            payload = wind_forecast()
            payload["current"][key] = bad
            self.assertEqual(self.wind(payload)[0][3:5], [0, 0])
            self.assertTrue(self.parse(payload)[1])
        payload = wind_forecast()
        payload["current"]["relative_humidity_2m"] = None
        self.assertEqual(self.wind(payload)[0][0:2], [1, 0])
        self.assertEqual(self.wind(payload)[0][3:5], [1, 1])
        for stamp in (0, 1800000001, 1800000900, 1799997300):
            payload = wind_forecast()
            payload["current"]["time"] = stamp
            self.assertEqual(self.wind(payload)[0][3:6], [0, 0, 0])
        payload = wind_forecast()
        payload["current"]["time"] -= 1800
        self.assertEqual(self.wind(payload)[0][3:5], [1, 1])
        self.assertEqual(self.wind(payload, epoch=1800000001)[0][3:5], [0, 0])


if __name__ == "__main__":
    unittest.main()
