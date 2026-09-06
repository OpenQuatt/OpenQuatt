from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
HP_IO = (ROOT / "openquatt/oq_HP_io.yaml").read_text()
Q_PROFILE = (ROOT / "openquatt/profiles/heatpump_controller_q.yaml").read_text()
RAW_RECEIPT = (ROOT / "openquatt/includes/sources/oq_raw_receipt.h").read_text()
RECEIPT_RUNTIME = (ROOT / "openquatt/includes/sources/oq_source_receipt_runtime.h").read_text()
COMMON_BASE = (ROOT / "openquatt/base/common.yaml").read_text()
OT_SLAVE_HEADER = (ROOT / "components/openquatt_ot_slave/OpenQuattOTSlave.h").read_text()
OT_SLAVE_CPP = (ROOT / "components/openquatt_ot_slave/OpenQuattOTSlave.cpp").read_text()
OTB_YAML = (ROOT / "openquatt/oq_boiler_opentherm.yaml").read_text()
OTB_TELEMETRY = (ROOT / "openquatt/includes/boiler/oq_otb_telemetry.h").read_text()


def entity_block(source: str, entity_id: str, next_section: str = "\n  - platform:") -> str:
    start = source.index(f"    id: {entity_id}")
    end = source.find(next_section, start + 1)
    return source[start:] if end < 0 else source[start:end]


class LearningReceiptContractTest(unittest.TestCase):
    def test_clamped_modbus_receipts_observe_before_rejection(self) -> None:
        contracts = {
            "${hp_id}_outside_temp": ("outside", "x >= -30.0f && x <= 100.0f"),
            "${hp_id}_water_in_temp_raw": ("water_in", "x >= -10.0f && x <= 100.0f"),
            "${hp_id}_water_out_temp_raw": ("water_out", "x >= -10.0f && x <= 100.0f"),
        }
        for entity_id, (receipt_field, range_check) in contracts.items():
            block = entity_block(HP_IO, entity_id)
            observe = block.index(f"oq_sources::${{hp_id}}.{receipt_field}.observe(")
            clamp = block.index("- clamp:")
            self.assertLess(observe, clamp)
            self.assertIn(f"isfinite(x) && {range_check}", block)
            self.assertIn("oq_sources::monotonic_ms()", block)

    def test_controller_flow_invalid_input_cannot_bypass_receipt(self) -> None:
        block = entity_block(Q_PROFILE, "flow_rate_controller", "\ninterval:")
        nan_branch = block.index("if (isnan(lpm)) {")
        invalid_observe = block.index(
            "oq_sources::controller_flow.observe(lpm, oq_sources::monotonic_ms(), false);"
        )
        nan_return = block.index("return NAN;", nan_branch)
        self.assertLess(nan_branch, invalid_observe)
        self.assertLess(invalid_observe, nan_return)
        self.assertIn(
            "oq_sources::controller_flow.observe(accepted_lph, oq_sources::monotonic_ms(), isfinite(lph));",
            block,
        )

    def test_live_receipts_use_64_bit_monotonic_time(self) -> None:
        self.assertIn("uint64_t received_ms = 0;", RAW_RECEIPT)
        self.assertIn("bool fresh(uint64_t now_ms, uint64_t max_age_ms) const", RAW_RECEIPT)
        self.assertIn("uint64_t received_ms = 0;", OT_SLAVE_HEADER)
        self.assertIn("uint64_t received_ms{0};", OTB_TELEMETRY)
        self.assertIn(
            "millis(), oq_sources::monotonic_ms(), response_id, response_type,",
            OTB_YAML,
        )
        observations = re.findall(r"oq_sources::\$\{hp_id\}\.[a-z0-9_]+\.observe\((.*?)\);", HP_IO, re.DOTALL)
        self.assertEqual(len(observations), 9)
        self.assertTrue(all("oq_sources::monotonic_ms()" in observation for observation in observations))

    def test_defrost_receipt_is_bound_to_r2118_parser(self) -> None:
        defrost = entity_block(HP_IO, "${hp_id}_defrost")
        four_way = entity_block(HP_IO, "${hp_id}_4_way_valve")
        self.assertIn("address: 2118", defrost)
        self.assertIn("oq_sources::${hp_id}.defrost.observe(", defrost)
        self.assertNotIn("oq_sources::${hp_id}.defrost", four_way)

    def test_modbus_disconnect_invalidates_every_hp_receipt_without_retimestamping(self) -> None:
        offline_start = HP_IO.index("    on_offline:")
        offline_end = HP_IO.index("    on_online:", offline_start)
        offline = HP_IO[offline_start:offline_end]
        receipt_names = (
            "working_mode",
            "compressor_frequency",
            "status_2108",
            "defrost",
            "status_2119",
            "water_in",
            "water_out",
            "flow",
            "outside",
        )
        for name in receipt_names:
            self.assertIn(f"oq_sources::${{hp_id}}.{name}.invalidate();", offline)
        self.assertNotIn("oq_sources::monotonic_ms()", offline)

        status = entity_block(HP_IO, "${hp_id}_status_2108_raw")
        offline_guard = status.index("if (!id(${hp_id}_is_online)) {")
        invalidation = status.index("oq_sources::${hp_id}.status_2108.invalidate();")
        physical_observe = status.index("oq_sources::${hp_id}.status_2108.observe(")
        self.assertLess(offline_guard, invalidation)
        self.assertLess(invalidation, physical_observe)

    def test_invalid_ot_room_frames_revoke_without_retimestamping(self) -> None:
        revoke_start = OT_SLAVE_CPP.index("void OpenQuattOTSlave::revoke_master_room_receipt_")
        revoke_end = OT_SLAVE_CPP.index("void OpenQuattOTSlave::parseRequest", revoke_start)
        revoke = OT_SLAVE_CPP[revoke_start:revoke_end]
        self.assertIn("m_masterRoomTemperatureReceived = false;", revoke)
        self.assertIn("m_masterRoomSetpointReceived = false;", revoke)
        self.assertNotIn("ReceiptMs =", revoke)
        self.assertNotIn("m_lastMasterRoomTemperatureMs =", revoke)
        self.assertNotIn("m_lastMasterRoomSetpointMs =", revoke)
        self.assertIn(
            "m_masterRoomTemperatureReceived && master_room_temperature_fresh()",
            OT_SLAVE_CPP,
        )
        self.assertIn(
            "m_masterRoomSetpointReceived && master_room_setpoint_fresh()",
            OT_SLAVE_CPP,
        )

    def test_receipt_storage_is_header_owned_before_esphome_codegen(self) -> None:
        self.assertIn("oq_source_receipt_runtime.h", COMMON_BASE)
        self.assertNotIn("type: oq_sources::RawFloatReceipt", HP_IO)
        self.assertNotIn("type: oq_sources::RawFloatReceipt", Q_PROFILE)
        self.assertIn("struct HeatPumpReceipts", RECEIPT_RUNTIME)
        self.assertIn("inline HeatPumpReceipts hp1{};", RECEIPT_RUNTIME)
        self.assertIn("#if OQ_TOPOLOGY_DUO", RECEIPT_RUNTIME)
        self.assertIn("inline HeatPumpReceipts hp2{};", RECEIPT_RUNTIME)
        self.assertIn("#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q", RECEIPT_RUNTIME)
        self.assertIn("inline RawFloatReceipt controller_flow{};", RECEIPT_RUNTIME)


if __name__ == "__main__":
    unittest.main()
