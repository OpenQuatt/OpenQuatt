from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMMON_PACKAGE = (ROOT / "openquatt" / "oq_common.yaml").read_text()


class FirmwareDowngradeContractTest(unittest.TestCase):
    def test_forced_install_rechecks_and_validates_the_selected_manifest(self) -> None:
        start = COMMON_PACKAGE.index(
            "  - id: oq_install_firmware_update_target_deferred\n"
        )
        end = COMMON_PACKAGE.index("\n  - id:", start + 1)
        install_script = COMMON_PACKAGE[start:end]

        self.assertIn("- update.check:\n          id: oq_firmware_update", install_script)
        self.assertIn("&& id(oq_fw_check_target_ready)", install_script)
        self.assertIn("- update.perform:\n                id: oq_firmware_update", install_script)
        self.assertIn("force_update: true", install_script)
        self.assertLess(
            install_script.index("- update.check:"),
            install_script.index("- update.perform:"),
        )

    def test_internal_install_button_uses_the_forced_install_script(self) -> None:
        self.assertIn(
            "id: oq_install_firmware_update_target\n"
            "    name: Install Firmware Update Target",
            COMMON_PACKAGE,
        )
        self.assertIn(
            "- script.execute: oq_install_firmware_update_target_deferred",
            COMMON_PACKAGE,
        )


if __name__ == "__main__":
    unittest.main()
