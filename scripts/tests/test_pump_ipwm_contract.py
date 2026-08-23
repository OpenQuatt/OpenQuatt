from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HP_IO = (ROOT / "openquatt" / "oq_HP_io.yaml").read_text()
CIC_SERVER = (ROOT / "openquatt" / "oq_cic_compatibility_server.yaml").read_text()
MANAGER_CPP = (
    ROOT
    / "components"
    / "openquatt_incident_manager"
    / "OpenQuattIncidentManager.cpp"
).read_text()
PUMP_IPWM_HEADER = (
    ROOT
    / "openquatt"
    / "includes"
    / "diagnostics"
    / "oq_pump_ipwm_feedback.h"
).read_text()


def yaml_block(source: str, start_marker: str, end_marker: str) -> str:
    start = source.index(start_marker)
    end = source.index(end_marker, start)
    return source[start:end]


class PumpIpwmContractTest(unittest.TestCase):
    def test_r2137_is_preserved_raw_and_cic_passes_it_through(self) -> None:
        raw = yaml_block(
            HP_IO,
            "id: ${hp_id}_pump_ipwm_feedback_raw",
            "id: ${hp_id}_pump_power",
        )
        self.assertIn("address: 2137", raw)
        self.assertIn("value_type: U_WORD", raw)
        self.assertNotIn("multiply:", raw)

        cic = yaml_block(CIC_SERVER, "- address: 2137", "- address: 2138")
        self.assertIn("${cic_compat_hp_id}_pump_ipwm_feedback_raw", cic)
        self.assertNotIn("pump_power", cic)
        self.assertNotIn("* 10.0f", cic)

    def test_r2137_has_one_fixed_power_mapping(self) -> None:
        self.assertIn("raw >= 50U && raw <= 750U", PUMP_IPWM_HEADER)
        self.assertIn("result.power_valid = true", PUMP_IPWM_HEADER)
        self.assertIn("result.power_w = static_cast<float>(raw) * 0.1F", PUMP_IPWM_HEADER)
        self.assertNotIn("Profile", PUMP_IPWM_HEADER)

    def test_diagnostic_codes_cannot_enter_power_input(self) -> None:
        pump_power = yaml_block(
            HP_IO,
            "id: ${hp_id}_pump_power",
            "id: ${hp_id}_flow",
        )
        power_input = yaml_block(
            HP_IO,
            "id: ${hp_id}_power_input",
            "id: ${hp_id}_heat_power",
        )
        self.assertIn("oq_pump_ipwm::decode", pump_power)
        self.assertIn("feedback.power_valid ? feedback.power_w : NAN", pump_power)
        self.assertIn("oq_pump_ipwm::power_contribution_w", power_input)
        self.assertNotIn("id(${hp_id}_pump_power).state", power_input)

    def test_pump_fault_context_is_recent_and_invalidated_offline(self) -> None:
        flow = yaml_block(HP_IO, "id: ${hp_id}_flow", "id: ${hp_id}_runtime_hours")
        self.assertIn("observe_pump_context", flow)
        self.assertNotIn("raw_max_age_ms", flow)
        self.assertNotIn("oq_modbus_telemetry_skip}UL + 1UL", flow)
        self.assertNotIn("${hp_id}_set_pump_mode", flow)
        self.assertIn("observe_pump_register", HP_IO)
        self.assertIn("2010U", HP_IO)
        self.assertIn("2108U", HP_IO)
        self.assertIn("2115U", HP_IO)
        self.assertIn("2137U", HP_IO)
        self.assertIn("kDiagnosticContextFreshnessMs = 20000U", PUMP_IPWM_HEADER)
        self.assertIn("read_if_fresh", PUMP_IPWM_HEADER)
        flow_switch = yaml_block(
            HP_IO,
            "id: ${hp_id}_status_2115_raw",
            "id: ${hp_id}_status_2120_raw",
        )
        self.assertIn("address: 2113", flow_switch)
        self.assertIn("offset: 4", flow_switch)
        self.assertIn("register_count: 3", flow_switch)
        self.assertNotIn("register_count: 1", flow_switch)
        self.assertNotIn("address: 2115", flow_switch)
        self.assertIn("unit->pump_context = {};", MANAGER_CPP)
        self.assertIn(
            "register_address == 2121U && (word & (1U << 13U)) != 0U",
            MANAGER_CPP,
        )
        self.assertIn("pump_fault_active && !fault_snapshot_complete", MANAGER_CPP)
        self.assertIn("next.units[slot].pump_context =", MANAGER_CPP)


if __name__ == "__main__":
    unittest.main()
