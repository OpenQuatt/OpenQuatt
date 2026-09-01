from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
WEB_AUTH_HEADER = (
    ROOT / "components" / "openquatt_web_auth" / "OpenQuattWebAuth.h"
).read_text()
WEB_AUTH_CPP = (
    ROOT / "components" / "openquatt_web_auth" / "OpenQuattWebAuth.cpp"
).read_text()
MODBUS_HUB = (ROOT / "openquatt" / "oq_common.yaml").read_text()
MODBUS_CONTROLLER = (ROOT / "openquatt" / "oq_HP_io.yaml").read_text()
ODU_EEPROM_HEADER = (
    ROOT
    / "components"
    / "openquatt_odu_eeprom_dump"
    / "OpenQuattOduEepromDump.h"
).read_text()
ODU_RUNTIME_TABLE = (
    ROOT
    / "openquatt"
    / "includes"
    / "experimental"
    / "oq_odu_runtime_frequency_table.h"
).read_text()
ODU_RUNTIME_TABLE_YAML = (
    ROOT / "openquatt" / "experimental" / "oq_odu_runtime_frequency_table_hp.yaml"
).read_text()
CAPTIVE_PORTAL_ROUTER_CPP = (
    ROOT
    / "components"
    / "openquatt_captive_portal_router"
    / "OpenQuattCaptivePortalRouter.cpp"
).read_text()
WIFI_PROFILE = (ROOT / "openquatt" / "connection" / "wifi.yaml").read_text()
WIFI_ETH_PROFILE = (
    ROOT / "openquatt" / "connection" / "wifi_eth.yaml"
).read_text()


class ESPHomeCompatibilityContractTest(unittest.TestCase):
    def test_captive_portal_router_registers_before_web_server(self) -> None:
        self.assertIn(
            "return setup_priority::WIFI + 0.5f;", CAPTIVE_PORTAL_ROUTER_CPP
        )
        self.assertIn(
            "add_handler_without_auth(this);", CAPTIVE_PORTAL_ROUTER_CPP
        )
        profiles = (
            (
                "wifi",
                WIFI_PROFILE,
                "components: [openquatt_captive_portal_router]",
            ),
            (
                "wifi_eth",
                WIFI_ETH_PROFILE,
                "components: [openquatt_network, openquatt_captive_portal_router]",
            ),
        )
        for profile_name, profile, component_declaration in profiles:
            with self.subTest(profile=profile_name):
                self.assertIn(component_declaration, profile)
                self.assertIn("openquatt_captive_portal_router:", profile)

    def test_captive_portal_router_delegates_existing_portal_routes(self) -> None:
        self.assertIn("portal->canHandle(request)", CAPTIVE_PORTAL_ROUTER_CPP)
        self.assertIn("portal->handleRequest(request)", CAPTIVE_PORTAL_ROUTER_CPP)

    def test_captive_portal_router_only_handles_soft_ap_requests(self) -> None:
        self.assertIn("wifi->wifi_soft_ap_ip()", CAPTIVE_PORTAL_ROUTER_CPP)
        self.assertIn("getsockname(httpd_req_to_sockfd", CAPTIVE_PORTAL_ROUTER_CPP)
        self.assertIn("local_address.ss_family != AF_INET", CAPTIVE_PORTAL_ROUTER_CPP)

    def test_web_auth_credentials_have_component_lifetime(self) -> None:
        self.assertIn("AuthStorage runtime_storage_{};", WEB_AUTH_HEADER)
        self.assertIn(
            "std::memcpy(&this->runtime_storage_, &storage, "
            "sizeof(this->runtime_storage_));",
            WEB_AUTH_CPP,
        )
        self.assertIn(
            "set_auth_username(this->runtime_storage_.username)",
            WEB_AUTH_CPP,
        )
        self.assertIn(
            "set_auth_password(this->runtime_storage_.password)",
            WEB_AUTH_CPP,
        )
        self.assertNotIn("set_auth_username(storage.username)", WEB_AUTH_CPP)
        self.assertNotIn("set_auth_password(storage.password)", WEB_AUTH_CPP)

    def test_modbus_spacing_is_owned_by_the_hub(self) -> None:
        self.assertIn(
            "turnaround_time: ${oq_modbus_command_throttle_ms}ms",
            MODBUS_HUB,
        )
        self.assertNotIn("command_throttle:", MODBUS_CONTROLLER)

    def test_custom_modbus_callbacks_use_span_payloads(self) -> None:
        custom_modbus = MODBUS_CONTROLLER + ODU_RUNTIME_TABLE
        self.assertIn("std::span<const uint8_t>", custom_modbus)
        self.assertIn("esphome::modbus::EntityType", custom_modbus)
        self.assertNotIn("ModbusRegisterType", custom_modbus)
        self.assertNotIn("const std::vector<uint8_t>&", custom_modbus)
        self.assertNotIn("const std::vector<uint8_t> &", custom_modbus)

    def test_runtime_table_uses_confirmed_single_register_writes(self) -> None:
        self.assertIn("create_write_single_command", ODU_RUNTIME_TABLE)
        self.assertIn(
            "queue_runtime_write_register(refs, tables, write_index + 1U, "
            "operation_token)",
            ODU_RUNTIME_TABLE,
        )
        self.assertIn("*refs.write_operation_token != operation_token", ODU_RUNTIME_TABLE)
        self.assertIn("runtime_frequency_write_timeout", ODU_RUNTIME_TABLE_YAML)
        self.assertIn("VERIFY_FAILED: write acknowledgement timeout", ODU_RUNTIME_TABLE_YAML)
        self.assertNotIn("create_write_multiple_command", ODU_RUNTIME_TABLE)

    def test_odu_eeprom_uses_maximum_fc03_read_size(self) -> None:
        self.assertIn(
            "EEPROM_BLOCK_SIZE = modbus::MAX_NUM_OF_REGISTERS_TO_READ",
            ODU_EEPROM_HEADER,
        )
        self.assertNotIn("EEPROM_BLOCK_SIZE = 22", ODU_EEPROM_HEADER)


if __name__ == "__main__":
    unittest.main()
