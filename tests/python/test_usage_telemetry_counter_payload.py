"""Run the production JSON builder for retry snapshots and maximum Q payloads."""

import json
from pathlib import Path
import re
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components/openquatt_usage_telemetry"


class UsageTelemetryCounterPayloadTest(unittest.TestCase):
    def test_payload_with_and_without_optional_max31865(self):
        source = (COMPONENT / "OpenQuattUsageTelemetry.cpp").read_text()
        header = (COMPONENT / "OpenQuattUsageTelemetry.h").read_text()
        fields = header[header.index("  std::string broker_;"):header.index(
            "  openquatt_common::PsramBuffer<char> external_publish_topic_;")]
        helpers = source[source.index("void append_json_key_("):source.index("const char* reset_reason_name_(")]
        builder = source[source.index("bool OpenQuattUsageTelemetry::build_payload_()"):source.index(
            "std::string OpenQuattUsageTelemetry::read_hardware_revision_(")]
        capacity = re.search(r"static constexpr size_t TELEMETRY_PAYLOAD_CAPACITY = \d+U;", source).group()
        select_inputs = "\n".join(
            f"  value.{name} = &selection;" for name in re.findall(r"select::Select\* (\w+)", fields))
        sensor_inputs = "\n".join(
            f"  value.{name} = &sensor;" for name in re.findall(r"sensor::Sensor\* (\w+)", fields))
        switch_inputs = "\n".join(
            f"  value.{name} = &feature;" for name in re.findall(r"switch_::Switch\* (\w+)", fields))
        fixture = r'''
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include "components/openquatt_usage_telemetry/OpenQuattUsageTelemetryPolicy.h"
#define ESP_LOGE(...)
constexpr uint32_t MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_SPIRAM = 2;
uint32_t heap_caps_get_free_size(uint32_t) { return UINT32_MAX; }
uint32_t heap_caps_get_minimum_free_size(uint32_t) { return UINT32_MAX; }
uint32_t heap_caps_get_largest_free_block(uint32_t) { return UINT32_MAX; }
int64_t esp_timer_get_time() { return INT64_MAX; }
int esp_reset_reason() { return 0; }
using ESPPreferenceObject = int;
namespace esphome {
namespace openquatt_common {
template<class T> class PsramBuffer {
  std::unique_ptr<T[]> storage_;
  size_t size_{0};
 public:
  bool allocate_external(size_t size) { storage_.reset(new T[size]); size_ = size; return true; }
  void release() { storage_.reset(); size_ = 0; }
  explicit operator bool() const { return storage_ != nullptr; }
  T* data() const { return storage_.get(); }
  size_t size() const { return size_; }
};
}
namespace sensor { struct Sensor { float state = -123456789.5f; bool has_state() const { return true; } }; }
namespace text_sensor { struct TextSensor { std::string state = "WiFi"; bool has_state() const { return true; } }; }
namespace binary_sensor { struct BinarySensor {}; }
namespace switch_ { struct Switch { bool state = false; }; }
namespace select { struct Select {
  std::string option = "CIC or HA input";
  bool has_state() const { return true; }
  const std::string& current_option() const { return option; }
}; }
namespace time { struct RealTimeClock {
  struct Now { uint32_t timestamp = UINT32_MAX; bool is_valid() const { return true; } };
  Now now() { return {}; }
}; }
namespace openquatt_mqtt_config { struct OpenQuattMqttConfig { bool is_enabled() const { return false; } }; }
namespace openquatt_modbus { struct OpenQuattModbusClientHub {
  uint32_t partial_response_count() const { return UINT32_MAX; }
  uint32_t parse_failed_count() const { return UINT32_MAX; }
  uint32_t recovered_response_count() const { return UINT32_MAX; }
  uint32_t offline_count() const { return UINT32_MAX; }
}; }
namespace max31865 { struct MAX31865Sensor {
  uint32_t refin = 7, rtdin = 9;
  uint32_t refin_low_fault_count() const { return refin; }
  uint32_t rtdin_low_fault_count() const { return rtdin; }
}; }
namespace openquatt_usage_telemetry {
// CAPACITY
const char* reset_reason_name_(int) { return "interrupt_watchdog"; }
// HELPERS
class OpenQuattUsageTelemetry {
 public:
  bool build_payload_();
  void clear_payload_();
  std::string read_hardware_revision_(uint16_t& serial) const {
    serial = UINT16_MAX;
    return "255.255 (batch 65535)";
  }
  static std::string random_message_id_() { return "01234567-89ab-4cde-8fab-0123456789ab"; }
// FIELDS
};
// BUILDER
}
}
int main() {
  using namespace esphome;
  using namespace openquatt_usage_telemetry;
  OpenQuattUsageTelemetry value;
  value.installation_id_ = OpenQuattUsageTelemetry::random_message_id_();
  value.firmware_version_ = std::string(32, 'v');
  value.release_channel_ = std::string(16, 'd');
  value.hardware_profile_ = "heatpump_controller_q";
  value.topology_ = "single";
  value.connection_ = "wifi";
  select::Select selection, generation, heating, supply, local, flow, q_flow;
  generation.option = "V1.5";
  heating.option = "Water Temperature Control (heating curve)";
  supply.option = "HA input";
  local.option = "PT1000";
  flow.option = "Outdoor unit";
  q_flow.option = "Local";
  sensor::Sensor sensor;
  switch_::Switch feature;
  time::RealTimeClock clock;
  openquatt_modbus::OpenQuattModbusClientHub hub;
  value.clock_ = &clock;
  value.modbus_hub_ = &hub;
// INPUTS
  value.quatt_hybrid_generation_select_ = &generation;
  value.heating_strategy_select_ = &heating;
  value.water_supply_source_select_ = &supply;
  value.local_water_supply_temp_source_select_ = &local;
  value.flow_source_select_ = &flow;
  value.q_flow_source_select_ = &q_flow;
#ifdef USE_OPENQUATT_MAX31865_COUNTERS
  max31865::MAX31865Sensor max_sensor;
  value.max31865_sensor_ = &max_sensor;
#endif
  assert(value.build_payload_());
  std::cout << value.payload_.data() << '\n';
#ifdef USE_OPENQUATT_MAX31865_COUNTERS
  max_sensor.refin = 11; max_sensor.rtdin = 13;
#endif
  // Same logical ID after an unacknowledged attempt: the snapshot stays fixed.
  assert(value.build_payload_());
  std::cout << value.payload_.data() << '\n';
  value.counter_snapshot_.commit();
  value.payload_message_id_.clear();
  assert(value.build_payload_());
  std::cout << value.payload_.data() << '\n';
  value.counter_snapshot_.commit();
  value.payload_message_id_.clear();
#ifdef USE_OPENQUATT_MAX31865_COUNTERS
  max_sensor.refin = UINT32_MAX; max_sensor.rtdin = UINT32_MAX;
#endif
  assert(value.build_payload_());
  std::cout << value.payload_.data() << '\n';
  // All six counters at UINT32_MAX together, as on a first publication.
  value.counter_snapshot_ = {};
  value.payload_message_id_.clear();
  assert(value.build_payload_());
  std::cout << value.payload_.data() << '\n';
}
'''
        replacements = {
            "// CAPACITY": capacity, "// HELPERS": helpers, "// FIELDS": fields,
            "// BUILDER": builder, "// INPUTS": "\n".join((select_inputs, sensor_inputs, switch_inputs)),
        }
        for marker, production in replacements.items():
            fixture = fixture.replace(marker, production)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "payload.cpp"
            cpp.write_text(fixture)
            for optional in (False, True):
                with self.subTest(max31865=optional):
                    executable = Path(directory) / f"payload_{optional}"
                    flags = ["-DUSE_OPENQUATT_MAX31865_COUNTERS"] if optional else []
                    run_process(
                        [*compiler_command(*flags), f"-I{ROOT}", str(cpp), "-o", str(executable)],
                        check=True, timeout=COMPILE_TIMEOUT_SECONDS,
                    )
                    result = run_process(
                        [str(executable)], check=True, capture_output=True, text=True,
                        timeout=RUN_TIMEOUT_SECONDS,
                    )
                    lines = result.stdout.splitlines()
                    payloads = [json.loads(line) for line in lines]
                    self.assertEqual(len(payloads), 5)
                    for line in lines:
                        self.assertLessEqual(len(line), 2048)
                    for key in ("max31865_refin_low_fault_count", "max31865_rtdin_low_fault_count"):
                        if optional:
                            self.assertEqual(payloads[0][key], 7 if "refin" in key else 9)
                            self.assertEqual(payloads[1][key], payloads[0][key])
                            self.assertEqual(payloads[2][key], 4)
                            self.assertEqual(payloads[3][key], 2**32 - 1 - (11 if "refin" in key else 13))
                            self.assertEqual(payloads[4][key], 2**32 - 1)
                        else:
                            self.assertTrue(all(key not in payload for payload in payloads))
                    self.assertEqual(payloads[0]["modbus_parse_failed_count"], 2**32 - 1)
                    self.assertEqual(payloads[1]["modbus_parse_failed_count"], 2**32 - 1)
                    self.assertEqual(payloads[2]["modbus_parse_failed_count"], 0)
                    self.assertEqual(payloads[4]["modbus_parse_failed_count"], 2**32 - 1)


if __name__ == "__main__":
    unittest.main()
