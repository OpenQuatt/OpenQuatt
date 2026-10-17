"""Execute OpenQuatt PHY guards with explicit upstream Ethernet lifecycle states."""
from pathlib import Path
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]
NETWORK = (ROOT / "components/openquatt_network/OpenQuattNetworkManager.cpp").read_text()


def method(source, signature):
    start = source.index(signature)
    end = source.index("{", start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == "{") - (source[end] == "}")
    return source[start:end + 1]


class EthernetLifecycleTest(unittest.TestCase):
    def test_actual_lifecycle_with_injected_failures(self):
        fixture = (ROOT / "tests/fixtures/ethernet_lifecycle.cpp").read_text()
        methods = "\n".join(method(NETWORK, signature) for signature in (
            "void OpenQuattNetworkManager::loop()",
            "bool OpenQuattNetworkManager::ensure_ethernet_enabled_()",
            "bool OpenQuattNetworkManager::disable_ethernet_()",
            "bool OpenQuattNetworkManager::prepare_ethernet_after_setup_()",
            "bool OpenQuattNetworkManager::power_down_w5500_()",
            "bool OpenQuattNetworkManager::wake_w5500_()",
        ))
        with tempfile.TemporaryDirectory(prefix="oq-ethernet-test-") as directory:
            source = Path(directory) / "test.cpp"
            source.write_text(fixture.replace("// PRODUCTION_METHODS", methods))
            binary = Path(directory) / "test"
            command = compiler_command("-Wno-unused-parameter", "-Wno-unused-but-set-variable")
            result = run_process(
                [*command, str(source), "-o", str(binary)],
                timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            run_process([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
