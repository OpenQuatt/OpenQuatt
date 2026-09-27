"""Exercise production MQTT diagnostics with missing and typed error events."""

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "components/openquatt_usage_telemetry/OpenQuattUsageTelemetry.cpp"


class UsageTelemetryErrorDiagnosticsTest(unittest.TestCase):
    def test_error_details_are_typed_and_bounded(self):
        source = SOURCE.read_text()
        start = source.index("void log_mqtt_failure_(")
        end = source.index("\nbool uuid_is_present_", start)
        production = source[start:end]
        fixture = r'''
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
static const char* TAG = "test";
static constexpr uint32_t MALLOC_CAP_INTERNAL = 1;
static constexpr uint32_t MALLOC_CAP_8BIT = 2;
static constexpr uint32_t MALLOC_CAP_SPIRAM = 4;
static std::vector<std::string> logs;
static void warn(const char*, const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  int length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  assert(length > 0 && length < 180);  // Leave room for the stored log prefix.
  logs.emplace_back(buffer);
}
#define ESP_LOGW(...) warn(__VA_ARGS__)
static unsigned heap_caps_get_free_size(uint32_t caps) {
  assert(caps == 3 || caps == MALLOC_CAP_SPIRAM);
  return caps == 3 ? 12345 : 1234567;
}
static unsigned heap_caps_get_minimum_free_size(uint32_t caps) {
  assert(caps == 3);
  return 2345;
}
static unsigned heap_caps_get_largest_free_block(uint32_t caps) {
  assert(caps == 3);
  return 6789;
}
enum { MQTT_ERROR_TYPE_TCP_TRANSPORT = 1, MQTT_ERROR_TYPE_CONNECTION_REFUSED = 2 };
struct esp_mqtt_error_codes_t {
  int error_type;
  int esp_transport_sock_errno;
  int esp_tls_last_esp_err;
  int esp_tls_stack_err;
  int esp_tls_cert_verify_flags;
  int connect_return_code;
};
// PRODUCTION
int main() {
  log_mqtt_failure_(nullptr);
  assert(logs[0].find("without transport error details") != std::string::npos);
  assert(logs[1] == "MQTT failure resources: internal_free=12345, internal_min=2345, internal_largest=6789, PSRAM_free=1234567");
  logs.clear();
  esp_mqtt_error_codes_t refused;  // Transport fields deliberately uninitialized.
  refused.error_type = MQTT_ERROR_TYPE_CONNECTION_REFUSED;
  refused.connect_return_code = 5;
  log_mqtt_failure_(&refused);
  assert(logs[0] == "MQTT connection refused: broker_code=5");
  logs.clear();
  esp_mqtt_error_codes_t transport;  // Broker field deliberately uninitialized.
  transport.error_type = MQTT_ERROR_TYPE_TCP_TRANSPORT;
  transport.esp_transport_sock_errno = 23;
  transport.esp_tls_last_esp_err = 0x8001;
  transport.esp_tls_stack_err = -0x1234;
  transport.esp_tls_cert_verify_flags = 8;
  log_mqtt_failure_(&transport);
  assert(logs[0].find("errno=23, esp_err=0x8001") != std::string::npos);
  assert(logs[0].find("cert_flags=0x8") != std::string::npos);
  logs.clear();
  esp_mqtt_error_codes_t unknown;
  unknown.error_type = 99;
  log_mqtt_failure_(&unknown);
  assert(logs[0] == "MQTT failed: error_type=99");
}
'''
        with tempfile.TemporaryDirectory(prefix="oq-mqtt-diagnostics-") as directory:
            cpp = Path(directory) / "test.cpp"
            cpp.write_text(fixture.replace("// PRODUCTION", production))
            binary = Path(directory) / "test"
            command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror"]
            if sys.platform == "darwin":
                sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
                command.extend(["-isystem", str(Path(sdk) / "usr/include/c++/v1")])
            result = subprocess.run([*command, str(cpp), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
