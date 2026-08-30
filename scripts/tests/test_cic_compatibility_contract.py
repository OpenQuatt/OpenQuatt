import hashlib
import re
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SERVER = ROOT / "openquatt" / "oq_cic_compatibility_server.yaml"
MODE = ROOT / "openquatt" / "oq_cic_compatibility.yaml"
RUNTIME = ROOT / "openquatt" / "includes" / "protocol" / "oq_cic_compatibility_runtime.h"
MODULE_FILES = (
    SERVER,
    ROOT / "openquatt" / "includes" / "protocol" / "oq_cic_register_logic.h",
    RUNTIME,
    ROOT / "tests" / "host" / "cic_register_logic_test.cpp",
    Path(__file__),
)
ORIGINAL_YAML_LINES = 468
REGISTER_CONTRACT_SHA256 = "08bb09cd60102bad0b79c3e94b95606c5d3e15abecf365983dd85019dfff9a58"
FIXED_ADDRESSES = (
    1999, 2006, 2010, 2015, 3999, 2099, 2100, 2101, 2102, 2103, 2104, 2105,
    2106, 2107, 2108, 2109, 2110, 2111, 2112, 2113, 2114, 2116, 2117, 2118,
    2122, 2131, 2132, 2133, 2134, 2135, 2136, 2137, 2138, 11006,
)
EXPECTED_ADDRESSES = FIXED_ADDRESSES + tuple(range(11160, 11180)) + tuple(range(11219, 11239))


class CicCompatibilityContractTest(unittest.TestCase):
    def setUp(self) -> None:
        self.server = SERVER.read_text(encoding="utf-8")

    def test_register_map_and_lambda_shape_are_stable(self) -> None:
        addresses = tuple(int(value) for value in re.findall(r"^\s+- address: (\d+)$", self.server, re.MULTILINE))
        self.assertEqual(addresses, EXPECTED_ADDRESSES)
        self.assertEqual(len(addresses), len(set(addresses)))
        self.assertEqual(self.server.count("value_type: U_WORD"), 74)
        self.assertEqual(self.server.count("read_lambda: return "), 74)
        self.assertEqual(self.server.count("write_lambda: return true;"), 5)
        self.assertEqual(self.server.count("read_lambda: return oq_cic::read("), 28)
        self.assertEqual(self.server.count("read_lambda: return oq_cic::read_${cic_compat_hp_id}_status_flags();"), 1)
        self.assertEqual(self.server.count("read_lambda: return oq_cic::constant("), 45)

    def test_register_contract_hash_is_stable(self) -> None:
        contract_keys = ("- address:", "value_type:", "read_lambda:", "write_lambda:")
        contract = "\n".join(
            line.strip() for line in self.server.splitlines() if line.strip().startswith(contract_keys)
        )
        self.assertEqual(hashlib.sha256(contract.encode()).hexdigest(), REGISTER_CONTRACT_SHA256)

    def test_runtime_preserves_current_dev_sources_and_timestamp(self) -> None:
        self.assertIn("id: cic_compatibility_last_request_ms", MODE.read_text(encoding="utf-8"))
        self.assertIn("id(${cic_compat_hp_id}_pump_ipwm_feedback_raw).state", self.server)
        self.assertEqual(
            RUNTIME.read_text(encoding="utf-8").count("id(cic_compatibility_last_request_ms) = millis();"), 2
        )

    def test_module_including_regression_tests_is_smaller_than_original_yaml(self) -> None:
        line_count = sum(len(path.read_text(encoding="utf-8").splitlines()) for path in MODULE_FILES)
        self.assertLess(line_count, ORIGINAL_YAML_LINES)


if __name__ == "__main__":
    unittest.main()
