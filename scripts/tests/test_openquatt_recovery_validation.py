import ast
from pathlib import Path
import types
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "components/openquatt_recovery/__init__.py"
CAPTIVE_PORTAL_HEADER = ROOT / "components/captive_portal/captive_portal.h"


class Invalid(ValueError):
    pass


class RecoveryValidationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        module = ast.parse(SOURCE.read_text())
        function = next(node for node in module.body if isinstance(node, ast.FunctionDef) and node.name == "validate_runtime_api_key")
        cls.full_config = {}
        namespace = {
            "cv": types.SimpleNamespace(Invalid=Invalid),
            "fv": types.SimpleNamespace(full_config=types.SimpleNamespace(get=lambda: cls.full_config)),
        }
        exec(compile(ast.Module(body=[function], type_ignores=[]), str(SOURCE), "exec"), namespace)
        cls.validate = staticmethod(namespace["validate_runtime_api_key"])

    def validate_config(self, full_config):
        type(self).full_config = full_config
        return self.validate({"id": "recovery"})

    def test_wifi_recovery_requires_access_point_and_portal(self):
        self.assertEqual(self.validate_config({"api": {"encryption": {}}}), {"id": "recovery"})
        self.assertEqual(
            self.validate_config({"api": {"encryption": {}}, "wifi": {"ap": {}}, "captive_portal": {}}),
            {"id": "recovery"},
        )
        with self.assertRaisesRegex(Invalid, "wifi.ap and captive_portal"):
            self.validate_config({"api": {"encryption": {}}, "wifi": {}, "captive_portal": {}})
        with self.assertRaisesRegex(Invalid, "wifi.ap and captive_portal"):
            self.validate_config({"api": {"encryption": {}}, "wifi": {"ap": {}}})

    def test_runtime_only_credentials_remain_required(self):
        with self.assertRaisesRegex(Invalid, "runtime-provisioned Wi-Fi"):
            self.validate_config({"api": {"encryption": {}}, "wifi": {"ap": {}, "networks": [{}]}, "captive_portal": {}})
        with self.assertRaisesRegex(Invalid, "runtime-provisioned API key"):
            self.validate_config({"api": {"encryption": {"key": "compiled"}}, "wifi": {"ap": {}}, "captive_portal": {}})

    def test_captive_portal_does_not_claim_recovery_routes(self):
        header = CAPTIVE_PORTAL_HEADER.read_text()
        self.assertIn('url != "/recovery" && url != "/recovery/status"', header)


if __name__ == "__main__":
    unittest.main()
