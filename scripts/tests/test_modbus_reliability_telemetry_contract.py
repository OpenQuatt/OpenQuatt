from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMMON = (ROOT / "openquatt" / "oq_common.yaml").read_text()
TELEMETRY_YAML = (ROOT / "openquatt" / "oq_usage_telemetry.yaml").read_text()
HP_IO = (ROOT / "openquatt" / "oq_HP_io.yaml").read_text()
MODBUS_HEADER = (
    ROOT / "components" / "openquatt_modbus" / "OpenQuattModbusClientHub.h"
).read_text()
MODBUS_SOURCE = (
    ROOT / "components" / "openquatt_modbus" / "OpenQuattModbusClientHub.cpp"
).read_text()
TELEMETRY_CPP = (
    ROOT / "components" / "openquatt_usage_telemetry" / "OpenQuattUsageTelemetry.cpp"
).read_text()
TELEMETRY_HEADER = (
    ROOT / "components" / "openquatt_usage_telemetry" / "OpenQuattUsageTelemetry.h"
).read_text()
TELEMETRY_INIT = (
    ROOT / "components" / "openquatt_usage_telemetry" / "__init__.py"
).read_text()


class ModbusReliabilityTelemetryContractTest(unittest.TestCase):
    def test_primary_odu_bus_uses_instrumented_client_hub(self) -> None:
        self.assertIn("components: [openquatt_modbus]", COMMON)
        self.assertIn("openquatt_modbus:", COMMON)
        self.assertNotIn("\nmodbus:\n  - uart_id: uart_bus\n    id: mod_bus", COMMON)
        self.assertIn("class OpenQuattModbusClientHub : public modbus::ModbusClientHub", MODBUS_HEADER)
        self.assertNotIn("rx_full_threshold: 1", COMMON)
        self.assertIn("rx_timeout: 1", COMMON)

    def test_transport_counts_at_parser_source_not_logger_output(self) -> None:
        self.assertNotIn("logger:\n  on_message:", TELEMETRY_YAML)
        self.assertIn("parse_modbus_frames() override", MODBUS_HEADER)
        self.assertIn("increment_(this->parse_failed_count_)", MODBUS_SOURCE)
        self.assertIn("increment_(this->partial_response_count_)", MODBUS_SOURCE)
        self.assertIn('LOG_STR("parse failed")', MODBUS_SOURCE)
        self.assertIn('LOG_STR("timeout after partial response")', MODBUS_SOURCE)
        self.assertIn("try_resync_expected_response_()", MODBUS_SOURCE)
        self.assertIn("find_expected_response_offset(", MODBUS_SOURCE)
        self.assertIn("trailing bytes after expected response", MODBUS_SOURCE)
        self.assertIn("increment_(this->recovered_response_count_)", MODBUS_SOURCE)
        self.assertIn("Give the bounded request-aware resync one last chance", MODBUS_SOURCE)
        self.assertLess(
            MODBUS_SOURCE.index("Give the bounded request-aware resync one last chance"),
            MODBUS_SOURCE.index('increment_(this->partial_response_count_)', MODBUS_SOURCE.index("Give the bounded")),
        )

    def test_counters_are_exact_uint32_and_do_not_require_sensor_entities(self) -> None:
        self.assertIn("uint32_t partial_response_count_{0U};", MODBUS_HEADER)
        self.assertIn("uint32_t parse_failed_count_{0U};", MODBUS_HEADER)
        self.assertIn("uint32_t recovered_response_count_{0U};", MODBUS_HEADER)
        self.assertIn("uint32_t offline_count_{0U};", MODBUS_HEADER)
        self.assertNotIn("Modbus partial response count", TELEMETRY_YAML)
        self.assertNotIn("CONF_MODBUS_PARTIAL_RESPONSE_COUNT_SENSOR", TELEMETRY_INIT)
        self.assertIn("CONF_MODBUS_HUB", TELEMETRY_INIT)
        self.assertIn("OpenQuattModbusClientHub* modbus_hub_", TELEMETRY_HEADER)

    def test_offline_transitions_are_aggregated_on_same_primary_hub(self) -> None:
        self.assertIn("id(mod_bus).record_offline_transition()", HP_IO)
        self.assertIn("record_offline_transition()", MODBUS_HEADER)

    def test_usage_payload_reads_integer_counters_directly(self) -> None:
        self.assertIn('"modbus_partial_response_count"', TELEMETRY_CPP)
        self.assertIn("partial_response_count()", TELEMETRY_CPP)
        self.assertIn('"modbus_parse_failed_count"', TELEMETRY_CPP)
        self.assertIn("parse_failed_count()", TELEMETRY_CPP)
        self.assertIn('"modbus_recovered_response_count"', TELEMETRY_CPP)
        self.assertIn("recovered_response_count()", TELEMETRY_CPP)
        self.assertIn('"modbus_offline_count"', TELEMETRY_CPP)
        self.assertIn("offline_count()", TELEMETRY_CPP)


if __name__ == "__main__":
    unittest.main()
