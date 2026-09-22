"""Compile the actual patched storage methods with a failure-injecting backend.

Radio/IDF integration remains a firmware/HIL check, not a host simulation claim.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CPP = (ROOT / "components/wifi/wifi_component.cpp").read_text()
HEADER = (ROOT / "components/wifi/wifi_component.h").read_text()


def method(signature):
    start = CPP.index(signature)
    end = CPP.index("{", start)
    depth = 1
    while depth:
        end += 1
        depth += (CPP[end] == "{") - (CPP[end] == "}")
    return CPP[start:end + 1]


class WifiRecoveryTest(unittest.TestCase):
    def test_actual_storage_methods(self):
        declarations = HEADER.split("struct SavedWifiSettings", 1)[1].split("enum WiFiComponentState", 1)[0]
        methods = "\n".join(method(signature) for signature in (
            "void WiFiComponent::init_preferences_()",
            "bool WiFiComponent::clear_saved_sta_checked()",
            "void WiFiComponent::save_wifi_sta(const char",
            "void WiFiComponent::persist_pending_credentials_()",
            "void WiFiComponent::stop_provisioning_ap_()",
        ))
        fixture = (Path(__file__).parent / "fixtures/wifi_recovery.cpp").read_text()
        source = fixture.replace("// PRODUCTION_RECORDS", "struct SavedWifiSettings" + declarations)
        source = source.replace("// PRODUCTION_METHODS", methods)
        with tempfile.TemporaryDirectory(prefix="oq-wifi-test-") as directory:
            source_path = Path(directory) / "test.cpp"
            source_path.write_text(source)
            for flags in ([], ["-DUSE_WIFI_FAST_CONNECT"],
                          ["-DUSE_WIFI_FAST_CONNECT", "-DUSE_WIFI_FAST_CONNECT_IN_FLASH"]):
                with self.subTest(flags=flags):
                    command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags]
                    if sys.platform == "darwin":
                        sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
                        command.extend(["-isystem", str(Path(sdk) / "usr/include/c++/v1")])
                    binary = Path(directory) / "test"
                    result = subprocess.run([*command, str(source_path), "-o", str(binary)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    subprocess.run([str(binary)], check=True, timeout=15)

    def test_native_integration_boundaries(self):
        start = method("void WiFiComponent::start()")
        self.assertNotIn("make_preference", start)
        self.assertIn("save.ssid[0] != '\\0'", start)
        fast = method("void WiFiComponent::save_fast_connect_settings_")
        self.assertIn("if (this->provisioning_required_) return;", fast)
        portal = (ROOT / "components/captive_portal/captive_portal.cpp").read_text()
        for source in (CPP, portal):
            self.assertNotIn("add_on_closed_callback", source)
            self.assertNotIn("is_provisioning_closed()", source)
        network = (ROOT / "components/openquatt_network/OpenQuattNetworkManager.cpp").read_text()
        self.assertIn("Preference::ETHERNET && !this->provisioning_override_", network)
        override = network.split("if (this->provisioning_override_) {", 1)[1].split("switch (this->phase_)", 1)[0]
        self.assertIn("ensure_wifi_enabled_()", override)
        self.assertIn("requires_provisioning()", override)
        self.assertNotIn("save_preference_", override)
        self.assertIn('DEFAULT_AP_TIMEOUT = "90s"', (ROOT / "components/wifi/__init__.py").read_text())


if __name__ == "__main__":
    unittest.main()
