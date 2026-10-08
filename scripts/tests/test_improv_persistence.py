"""Execute the real Improv command/loop/timeout against Wi-Fi storage methods."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

from test_wifi_recovery import ROOT, method, production_fixture


class ImprovPersistenceTest(unittest.TestCase):
    def test_actual_provisioning_state_machine(self):
        cpp = (ROOT / "components/improv_serial/improv_serial_component.cpp").read_text()
        methods = "\n".join(method(signature, cpp) for signature in (
            "void ImprovSerialComponent::loop()",
            "bool ImprovSerialComponent::parse_improv_payload_",
            "void ImprovSerialComponent::on_wifi_connect_timeout_",
            "void ImprovSerialComponent::set_state_",
        ))
        wifi = production_fixture().split("int main() {", 1)[0]
        fixture = (Path(__file__).parent / "fixtures/improv_persistence.cpp").read_text()
        source = wifi + fixture.replace("// PRODUCTION_IMPROV_METHODS", methods)
        self.compile_and_run(source)

    def test_actual_saved_wifi_action(self):
        header = (ROOT / "components/wifi/automation.h").read_text()
        action = method("template <typename... Ts>\nclass WiFiConfigureAction", header) + ";"
        wifi = production_fixture().split("int main() {", 1)[0]
        fixture = (Path(__file__).parent / "fixtures/wifi_configure_persistence.cpp").read_text()
        self.compile_and_run(wifi + fixture.replace("// PRODUCTION_WIFI_ACTION", action))

    def compile_and_run(self, source):
        with tempfile.TemporaryDirectory(prefix="oq-improv-test-") as directory:
            path = Path(directory) / "test.cpp"
            path.write_text(source)
            command = [os.environ.get("CXX", "c++"), "-std=c++20", "-Wall", "-Wextra", "-Werror"]
            if sys.platform == "darwin":
                sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
                command.extend(["-isystem", str(Path(sdk) / "usr/include/c++/v1")])
            binary = Path(directory) / "test"
            result = subprocess.run([*command, str(path), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
