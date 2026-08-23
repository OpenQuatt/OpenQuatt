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
        self.assertIn("internal: true", raw)
        self.assertNotIn("multiply:", raw)
        self.assertNotIn("pump_ipwm_status", HP_IO)

        cic = yaml_block(CIC_SERVER, "- address: 2137", "- address: 2138")
        self.assertIn("${cic_compat_hp_id}_pump_ipwm_feedback_raw", cic)
        self.assertNotIn("pump_power", cic)
        self.assertNotIn("* 10.0f", cic)

    def test_r2137_has_one_fixed_power_mapping(self) -> None:
        self.assertIn("raw >= 50U && raw <= 750U", PUMP_IPWM_HEADER)
        self.assertIn("result.power_valid = true", PUMP_IPWM_HEADER)
        self.assertIn("result.power_w = static_cast<float>(raw) * 0.1F", PUMP_IPWM_HEADER)
        self.assertNotIn("Profile", PUMP_IPWM_HEADER)
        self.assertNotIn("ContextRawObservation", PUMP_IPWM_HEADER)
        self.assertNotIn("kDiagnosticContextFreshnessMs", PUMP_IPWM_HEADER)

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
        self.assertIn("id(${hp_id}_pump_power).state", power_input)
        self.assertNotIn("power_contribution_w", power_input)

    def test_pump_context_is_captured_only_when_r2121_b13_is_active(self) -> None:
        self.assertNotIn("pump_request_raw", HP_IO)
        self.assertNotIn("observe_pump_register", HP_IO)
        self.assertNotIn("observe_pump_register", MANAGER_CPP)

        flow = yaml_block(HP_IO, "id: ${hp_id}_flow", "id: ${hp_id}_runtime_hours")
        self.assertNotIn("observe_pump_context", flow)

        fault = yaml_block(
            HP_IO,
            "id: ${hp_id}_status_2121_raw",
            "# ----------------------------\n  # BINARY SENSOR",
        )
        self.assertIn("(fault_word & 0x2000u) != 0u", fault)
        self.assertIn("observe_pump_context", fault)
        self.assertIn("${hp_id}_set_pump_mode", fault)
        self.assertIn("${hp_id}_status_2108_raw", fault)
        self.assertIn("${hp_id}_status_2115_raw", fault)
        self.assertIn("${hp_id}_pump_ipwm_feedback_raw", fault)
        self.assertIn("${hp_id}_flow", fault)

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
        self.assertNotIn("water_flow_switch", HP_IO)
        self.assertIn("next.units[slot].pump_context =", MANAGER_CPP)


if __name__ == "__main__":
    unittest.main()
