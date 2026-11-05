"""Run the real weather TLS/DNS control flow against bounded platform fault injection.

Set OPENQUATT_WEATHER_ARDUINOJSON_INCLUDE to the directory containing ArduinoJson.h
to exercise the real JSON parsers as well. Without that optional local dependency,
the offline CI adapter stubs only the parse entrypoints; HTTP framing stays real.
"""

import os
from pathlib import Path
import tempfile
import unittest

from tests.python.test_weather_component_runtime import STUB_HEADERS
from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/fixtures/weather"
TRANSPORT = ROOT / "components/openquatt_weather/OpenQuattWeatherTransport.cpp"
PLATFORM_HEADERS = (*STUB_HEADERS, "esp_crt_bundle.h", "esp_tls.h", "lwip/dns.h", "lwip/ip4_addr.h", "lwip/tcpip.h")


class WeatherTransportRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="oq-weather-transport-")
        cls.addClassCleanup(cls.directory.cleanup)
        temporary = Path(cls.directory.name)
        for header in PLATFORM_HEADERS:
            path = temporary / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#pragma once\n#include "weather_transport_stubs.h"\n')
        production = TRANSPORT.read_text()
        include = '#include "OpenQuattWeatherJson.h"'
        if production.count(include) != 1:
            raise AssertionError("Transport JSON include changed; update the dependency adapter explicitly")
        (temporary / "production_weather_transport.cpp").write_text(
            production.replace(include, '#include "transport_json_adapter.h"')
        )
        parser_flags = []
        parser_directory = os.environ.get("OPENQUATT_WEATHER_ARDUINOJSON_INCLUDE", "").strip()
        if parser_directory:
            if not (Path(parser_directory) / "ArduinoJson.h").is_file():
                raise AssertionError("OPENQUATT_WEATHER_ARDUINOJSON_INCLUDE does not contain ArduinoJson.h")
            parser_flags = ["-DOQ_WEATHER_TEST_REAL_JSON=1", "-I", parser_directory]
        cls.binary = temporary / "weather-transport"
        result = run_process(
            compiler_command(
                "-I", str(temporary), "-I", str(FIXTURES), "-I", str(ROOT),
                "-I", str(ROOT / "openquatt"), "-I", str(ROOT / "components/openquatt_weather"),
                "-I", str(ROOT / "components/openquatt_common"), *parser_flags,
                str(FIXTURES / "transport_runtime.cpp"), "-o", str(cls.binary),
            ),
            timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
        )
        if result.returncode != 0:
            raise AssertionError(result.stdout + result.stderr)

    def scenario(self, name):
        result = run_process(
            [str(self.binary), name], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, f"{name}: {result.stdout}{result.stderr}")

    def test_successful_real_http_framing_and_fixed_certificate_identity(self):
        for framing in ("length", "chunked", "close"):
            with self.subTest(framing=framing):
                self.scenario(f"success_{framing}")

    def test_want_read_write_and_partial_writes_yield_without_duplicate_bytes(self):
        self.scenario("wants")

    def test_dutch_full_postcodes_use_the_four_digit_geocoding_query(self):
        self.scenario("postcode_geocoding")

    def test_header_and_body_drip_stop_at_the_absolute_deadline(self):
        for mode in ("header", "body"):
            with self.subTest(mode=mode):
                self.scenario(f"drip_{mode}")

    def test_continuously_available_oversize_headers_stop_at_fixed_caps(self):
        for mode in ("header", "metadata"):
            with self.subTest(mode=mode):
                self.scenario(f"oversize_{mode}")

    def test_cancel_during_dns_connect_and_read_releases_the_only_tls_owner(self):
        for stage in ("dns", "connect", "read"):
            with self.subTest(stage=stage):
                self.scenario(f"cancel_{stage}")

    def test_connect_polling_stops_after_the_deadline_and_one_bounded_poll(self):
        self.scenario("connect_deadline")

    def test_missing_and_late_dns_callbacks_do_not_overwrite_a_new_request(self):
        for mode in ("deadline", "cancel", "queued"):
            with self.subTest(mode=mode):
                self.scenario(f"late_dns_{mode}")

    def test_dns_faults_cleanly_recover_without_creating_tls_early(self):
        for mode in ("queue", "lookup", "ipv6", "negative"):
            with self.subTest(mode=mode):
                self.scenario(f"dns_{mode}")

    def test_tls_allocation_io_and_truncation_failures_cleanup_once(self):
        for mode in ("init", "connect", "write", "zero_write", "read", "truncated", "empty"):
            with self.subTest(mode=mode):
                self.scenario(f"tls_{mode}")


if __name__ == "__main__":
    unittest.main()
