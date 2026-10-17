"""Exercise the production eFuse reader, boot log and telemetry serial contract."""

from pathlib import Path
import tempfile
import textwrap
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]


class QHardwareSerialTest(unittest.TestCase):
    def test_serial_reading_logging_and_telemetry(self):
        source = (ROOT / "components/openquatt_usage_telemetry/OpenQuattUsageTelemetry.cpp").read_text()
        start = source.index("std::string OpenQuattUsageTelemetry::read_hardware_revision_(")
        end = source.index("\nbool OpenQuattUsageTelemetry::time_reached_", start)
        revision_reader = source[start:end]
        start = source.index('  append_json_key_(payload, "hardware_serial");')
        end = source.index('  payload += R"(,"topology":', start)
        serial_payload = source[start:end]
        profile = (ROOT / "openquatt/profiles/heatpump_controller_q/hardware_revision.yaml").read_text()
        boot = textwrap.dedent(profile.split("- lambda: |-\n", 1)[1].split("\ntext_sensor:", 1)[0])
        fixture = r'''
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include "openquatt/includes/hardware/heatpump_controller_q_hardware_revision.h"
#include "components/openquatt_usage_telemetry/OpenQuattUsageTelemetryPolicy.h"
using esphome::openquatt_usage_telemetry::FixedBufferWriter;
struct TextSensor {
  std::string value;
  void publish_state(const char* text) { value = text; }
} oq_hardware_revision_text;
struct Sensor {
  float value = NAN;
  void publish_state(float number) { value = number; }
} oq_hardware_serial;
static std::string last_log;
static void log(const char*, const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  last_log = buffer;
}
#define ESP_LOGI(...) log(__VA_ARGS__)
#define ESP_LOGW(...) log(__VA_ARGS__)
#define ESP_LOGE(...) log(__VA_ARGS__)
#define id(name) name
void boot() { /* BOOT */ }
class OpenQuattUsageTelemetry {
 public:
  std::string hardware_profile_ = "heatpump_controller_q";
  std::string read_hardware_revision_(uint16_t& serial) const;
};
/* READER */
std::string payload(uint16_t hardware_serial) {
  char buffer[64];
  FixedBufferWriter writer(buffer, sizeof(buffer));
  auto& payload = writer;
  payload += R"({"schema_version":1)";
  auto append_json_key_ = [](FixedBufferWriter& output, const char* key) {
    output += R"(,")";
    output += key;
    output += R"(":)";
  };
  /* PAYLOAD */
  payload += '}';
  assert(payload.ok());
  return buffer;
}
int main() {
  OpenQuattUsageTelemetry telemetry;
  uint16_t serial = 999;
  efuse_raw = 0;
  assert(!oq_hardware::read_hardware_revision_efuse().programmed);
  boot();
  assert(oq_hardware_revision_text.value == "Not programmed");
  assert(std::isnan(oq_hardware_serial.value));
  assert(telemetry.read_hardware_revision_(serial).empty() && serial == 0);
  for (uint16_t expected : {0, 1, 137, 1023}) {
    efuse_raw = (static_cast<uint32_t>(expected) << 14U) | 0x2A01U;
    const auto revision = oq_hardware::read_hardware_revision_efuse();
    assert(revision.error == ESP_OK && revision.programmed);
    assert(revision.major == 1 && revision.minor == 0 && revision.batch == 42);
    assert(revision.serial == expected && revision.raw == efuse_raw);
    boot();
    assert(oq_hardware_revision_text.value == "1.0 (batch 42)");
    const auto serial_text = expected == 0 ? "unknown" : std::to_string(expected);
    assert(last_log.find("batch 42, serial " + serial_text) != std::string::npos);
    assert(expected == 0 ? std::isnan(oq_hardware_serial.value) : oq_hardware_serial.value == expected);
#if defined(OPENQUATT_HAS_Q_HARDWARE_REVISION)
    assert(telemetry.read_hardware_revision_(serial) == "1.0 (batch 42)" && serial == expected);
#else
    assert(telemetry.read_hardware_revision_(serial).empty() && serial == 0);
#endif
    assert(payload(serial) == "{\"schema_version\":1,\"hardware_serial\":" +
           (serial == 0 ? "null" : std::to_string(serial)) + "}");
  }
  efuse_raw = 0xFFFFFFFFU;
  const auto maximum = oq_hardware::read_hardware_revision_efuse();
  assert(maximum.raw == 0xFFFFFFU);
  assert(maximum.major == 15 && maximum.minor == 15 && maximum.batch == 63 && maximum.serial == 1023);
  efuse_error = 1;
  assert(!oq_hardware::read_hardware_revision_efuse().programmed);
  boot();
  assert(oq_hardware_revision_text.value == "Read error");
  assert(std::isnan(oq_hardware_serial.value));
  assert(telemetry.read_hardware_revision_(serial).empty() && serial == 0);
  efuse_error = ESP_OK;
  telemetry.hardware_profile_ = "ci_c";
  serial = 999;
  assert(telemetry.read_hardware_revision_(serial).empty() && serial == 0);
}
'''
        fixture = fixture.replace("/* BOOT */", boot).replace("/* READER */", revision_reader)
        fixture = fixture.replace("/* PAYLOAD */", serial_payload)
        with tempfile.TemporaryDirectory(prefix="oq-hardware-serial-") as directory:
            temp = Path(directory)
            (temp / "esp_efuse.h").write_text(r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
using esp_err_t = int;
static constexpr esp_err_t ESP_OK = 0;
inline uint32_t efuse_raw = 0;
inline esp_err_t efuse_error = ESP_OK;
inline const char* esp_err_to_name(esp_err_t) { return "test error"; }
inline esp_err_t esp_efuse_read_field_blob(const void*, void* destination, size_t bits) {
  assert(bits == 24);
  std::memcpy(destination, &efuse_raw, 3);
  return efuse_error;
}
''')
            (temp / "esp_efuse_table.h").write_text("#pragma once\ninline const void* ESP_EFUSE_USER_DATA = nullptr;\n")
            cpp = temp / "test.cpp"
            cpp.write_text(fixture)
            command = compiler_command()
            for with_q_header in (True, False):
                with self.subTest(with_q_header=with_q_header):
                    binary = temp / ("with_q" if with_q_header else "without_q")
                    defines = ["-DOPENQUATT_HAS_Q_HARDWARE_REVISION"] if with_q_header else []
                    result = run_process(
                        [*command, *defines, "-I", str(temp), "-I", str(ROOT), str(cpp), "-o", str(binary)],
                        timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    run_process([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
