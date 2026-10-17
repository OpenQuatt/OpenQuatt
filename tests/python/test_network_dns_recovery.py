"""Run the production DNS repair against DHCP clearing and failover boundaries."""
from pathlib import Path
import tempfile
import unittest

from tests.python.test_ethernet_lifecycle import method

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]


class DnsRecoveryTest(unittest.TestCase):
    def test_dns_recovery(self):
        production = method((ROOT / "components/openquatt_network/OpenQuattNetworkManager.cpp").read_text(),
                            "void OpenQuattNetworkManager::restore_default_dns_(uint32_t now)")
        fixture = (ROOT / "tests/fixtures/network_dns_recovery.cpp").read_text()
        with tempfile.TemporaryDirectory(prefix="oq-dns-test-") as directory:
            source = Path(directory) / "test.cpp"
            source.write_text(fixture.replace("// PRODUCTION_METHOD", production))
            binary = Path(directory) / "test"
            command = compiler_command()
            result = run_process(
                [*command, str(source), "-o", str(binary)],
                timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            run_process([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
