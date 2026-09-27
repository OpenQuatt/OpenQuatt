"""Run the production DNS repair against DHCP clearing and failover boundaries."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_ethernet_lifecycle import method

ROOT = Path(__file__).resolve().parents[2]


class DnsRecoveryTest(unittest.TestCase):
    def test_dns_recovery(self):
        production = method((ROOT / "components/openquatt_network/OpenQuattNetworkManager.cpp").read_text(),
                            "void OpenQuattNetworkManager::restore_default_dns_(uint32_t now)")
        fixture = (Path(__file__).parent / "fixtures/network_dns_recovery.cpp").read_text()
        with tempfile.TemporaryDirectory(prefix="oq-dns-test-") as directory:
            source = Path(directory) / "test.cpp"
            source.write_text(fixture.replace("// PRODUCTION_METHOD", production))
            binary = Path(directory) / "test"
            command = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror"]
            if sys.platform == "darwin":
                sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
                command.extend(["-isystem", str(Path(sdk) / "usr/include/c++/v1")])
            result = subprocess.run([*command, str(source), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
