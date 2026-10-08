"""Run the production HTTP queue/router with the production Wi-Fi methods."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
from test_wifi_recovery import ROOT, production_fixture

class CaptivePortalProvisioningTest(unittest.TestCase):
    def test_router_requests_and_persist(self):
        root = ROOT / "components/openquatt_captive_portal_router"
        header = (root / "OpenQuattCaptivePortalRouter.h").read_text()
        source = (root / "OpenQuattCaptivePortalRouter.cpp").read_text()
        strip = lambda text: "\n".join(line for line in text.splitlines() if not line.startswith(("#include", "#pragma")))
        header = strip(header).replace(" protected:", " public:")
        source = strip(source)
        fixture = (Path(__file__).parent / "fixtures/captive_portal_provisioning.cpp").read_text()
        wifi = production_fixture().split("int main() {", 1)[0]
        content = wifi + fixture.replace("// PRODUCTION_ROUTER", header + "\n" + source)
        with tempfile.TemporaryDirectory(prefix="oq-portal-test-") as directory:
            path = Path(directory) / "test.cpp"
            path.write_text(content)
            command = [os.environ.get("CXX", "c++"), "-std=c++20", "-pthread", "-Wall", "-Wextra", "-Werror"]
            if sys.platform == "darwin":
                sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
                command += ["-isystem", str(Path(sdk) / "usr/include/c++/v1")]
            binary = Path(directory) / "test"
            result = subprocess.run([*command, str(path), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=15)

if __name__ == "__main__":
    unittest.main()
