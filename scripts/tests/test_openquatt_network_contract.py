from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
NETWORK_CPP = (
    ROOT / "components" / "openquatt_network" / "OpenQuattNetworkManager.cpp"
).read_text()
NETWORK_HEADER = (
    ROOT / "components" / "openquatt_network" / "OpenQuattNetworkManager.h"
).read_text()
NETWORK_SCHEMA = (
    ROOT / "components" / "openquatt_network" / "__init__.py"
).read_text()
NETWORK_PACKAGE = (ROOT / "openquatt" / "connection" / "wifi_eth.yaml").read_text()
USAGE_TELEMETRY_CPP = (
    ROOT / "components" / "openquatt_usage_telemetry" / "OpenQuattUsageTelemetry.cpp"
).read_text()
INSTALLER = (ROOT / "docs" / "install" / "install.js").read_text()
INSTALLER_PAGE = (ROOT / "docs" / "install" / "index.html").read_text()


def function_body(name: str, next_name: str) -> str:
    return NETWORK_CPP.split(name, 1)[1].split(next_name, 1)[0]


class OpenQuattNetworkContractTest(unittest.TestCase):
    def test_automatic_is_default_and_ethernet_has_route_priority(self) -> None:
        self.assertIn("Preference preference_{Preference::AUTOMATIC};", NETWORK_HEADER)
        self.assertIn('options=["Automatic", "WiFi", "Ethernet"]', NETWORK_SCHEMA)
        self.assertIn("priority: [ethernet, wifi]", NETWORK_PACKAGE)

    def test_automatic_startup_checks_ethernet_before_wifi(self) -> None:
        startup = function_body("handle_startup_(uint32_t now)", "handle_steady_")
        ethernet = startup.index("is_stable_(Connection::ETHERNET, now)")
        wifi = startup.index("is_stable_(Connection::WIFI, now)")
        self.assertLess(ethernet, wifi)
        self.assertIn("detection_timeout_ms_", startup)

    def test_stable_wifi_powers_down_ethernet_without_hotplug_polling(self) -> None:
        steady = function_body("handle_steady_(uint32_t now)", "handle_recovery_")
        self.assertIn("disable_ethernet_();", steady)
        self.assertNotIn("ensure_ethernet_enabled_();", steady)
        self.assertIn("W5500 PHY powered down", NETWORK_CPP)

    def test_ip_sensor_uses_the_active_interface(self) -> None:
        self.assertIn(
            'id(oq_connection_text).state == "Ethernet"', NETWORK_PACKAGE
        )
        self.assertIn(
            "global_eth_component->get_ip_addresses()", NETWORK_PACKAGE
        )
        self.assertIn('id(oq_connection_text).state == "WiFi"', NETWORK_PACKAGE)
        self.assertIn(
            "global_wifi_component->get_ip_addresses()", NETWORK_PACKAGE
        )

    def test_recovery_enables_both_and_keeps_ethernet_first_in_auto(self) -> None:
        recovery = function_body("begin_recovery_(uint32_t now)", "begin_switch_")
        self.assertIn("ensure_wifi_enabled_();", recovery)
        self.assertIn("ensure_ethernet_enabled_();", recovery)

        handler = function_body("handle_recovery_(uint32_t now)", "handle_switching_")
        automatic = handler.split("Preference::AUTOMATIC", 1)[1]
        self.assertLess(
            automatic.index("is_stable_(Connection::ETHERNET, now)"),
            automatic.index("is_stable_(Connection::WIFI, now)"),
        )

    def test_usage_telemetry_reports_the_active_connection(self) -> None:
        self.assertIn("active_connection_sensor: oq_connection_text", NETWORK_PACKAGE)
        self.assertIn("connection_preference_select: oq_preferred_connection", NETWORK_PACKAGE)
        self.assertIn('active_connection == "WiFi"', USAGE_TELEMETRY_CPP)
        self.assertIn('active_connection == "Ethernet"', USAGE_TELEMETRY_CPP)
        self.assertIn('connection_preference == "Automatic"', USAGE_TELEMETRY_CPP)

    def test_preference_change_is_published_only_after_nvs_readback(self) -> None:
        save = function_body("save_preference_(Preference preference)", "publish_preference_()")
        self.assertLess(save.index("global_preferences->sync()"), save.index("preference_store_.load"))
        self.assertLess(save.index("preference_store_.load"), save.index("this->preference_ = preference"))
        self.assertIn('log_stats("network-preference-write-failed")', save)
        self.assertIn("sizeof(PreferenceStorage) == 8U", NETWORK_HEADER)

    def test_q_installer_uses_canonical_automatic_builds_with_wifi_provisioning(self) -> None:
        self.assertIn(
            'fileName: "openquatt-heatpump-controller-q-single.firmware.factory.bin"',
            INSTALLER,
        )
        self.assertIn(
            'fileName: "openquatt-heatpump-controller-q-duo.firmware.factory.bin"',
            INSTALLER,
        )
        self.assertNotIn("openquatt-heatpump-controller-q-single-wifi.firmware.factory.bin", INSTALLER)
        self.assertNotIn("openquatt-heatpump-controller-q-duo-eth.firmware.factory.bin", INSTALLER)
        self.assertIn('hardware === "heatpump_controller_q"', INSTALLER)
        self.assertIn("wifiProvisioning: true", INSTALLER)
        self.assertIn("zonder opgeslagen Wi-Fi-gegevens", INSTALLER_PAGE)


if __name__ == "__main__":
    unittest.main()
