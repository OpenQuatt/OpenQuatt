from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMMON_PACKAGE = (ROOT / "openquatt" / "oq_common.yaml").read_text()
WEB_UPDATE = (ROOT / "openquatt" / "web" / "js" / "src" / "features" / "firmware-update.js").read_text()
WEB_ACTIONS = (ROOT / "openquatt" / "web" / "js" / "src" / "features" / "firmware-actions.js").read_text()


def _script_block(script_id: str) -> str:
    start = COMMON_PACKAGE.index(f"  - id: {script_id}\n")
    end = COMMON_PACKAGE.index("\n  - id:", start + 1)
    return COMMON_PACKAGE[start:end]


class PrManifestInstallContractTest(unittest.TestCase):
    def test_manifest_button_validates_exact_pr_manifest(self) -> None:
        self.assertIn("id: oq_install_firmware_test_manifest", COMMON_PACKAGE)
        self.assertIn('name: "Install Firmware Test Manifest"', COMMON_PACKAGE)
        self.assertIn(
            "https://github.com/OpenQuatt/OpenQuatt/releases/download/pr-",
            COMMON_PACKAGE,
        )
        # Canonical HCQ manifest without connection suffix via substitution.
        self.assertIn(
            'artifact_base + "${oq_artifact_connection_suffix}-ota.manifest.json"',
            COMMON_PACKAGE,
        )
        # PR number 1-6 digits, no leading zero, exact manifest filename.
        self.assertIn("pr.size() <= 6", COMMON_PACKAGE)
        self.assertIn('pr[0] == \'0\'', COMMON_PACKAGE)
        self.assertIn("file == expected_manifest", COMMON_PACKAGE)

    def test_manifest_install_rejects_stale_checks_with_exact_url(self) -> None:
        install_script = _script_block("oq_install_firmware_test_manifest_deferred")

        # Claims install early so periodic main/dev checks cannot overwrite source.
        self.assertLess(
            install_script.index("id(oq_firmware_target_install_active) = true;"),
            install_script.index("- update.check:"),
        )
        # Exact firmware URL derived from manifest, not generic suffix match.
        self.assertIn("expected_firmware_url", install_script)
        self.assertIn("actual_firmware_url", install_script)
        self.assertIn(
            "id(oq_firmware_update).update_info.firmware_url",
            install_script,
        )
        self.assertIn("stale", install_script)
        # Source re-asserted before retry and before perform.
        self.assertGreaterEqual(
            install_script.count('id(oq_firmware_update).set_source_url(url.c_str());'),
            3,
        )
        self.assertIn("- update.perform:", install_script)
        self.assertIn("id: oq_firmware_update", install_script)
        self.assertIn("force_update: true", install_script)
        self.assertLess(
            install_script.index("- update.check:"),
            install_script.index("- update.perform:"),
        )

    def test_update_lifecycle_is_exclusive_during_install(self) -> None:
        self.assertIn(
            "Firmware source change ignored during active install.",
            COMMON_PACKAGE,
        )
        self.assertIn(
            "Firmware update check skipped: install active.",
            COMMON_PACKAGE,
        )
        self.assertIn(
            "Firmware update check ignored because an install is active.",
            COMMON_PACKAGE,
        )
        self.assertIn(
            "firmware info no longer matches this PR",
            COMMON_PACKAGE,
        )

    def test_webapp_uniform_selection_uses_preferred_connection(self) -> None:
        self.assertIn(
            'hardware === "heatpump_controller_q" && hasEntity("preferredConnection")',
            WEB_UPDATE,
        )
        self.assertIn("hasFirmwareTestManifestCapability", WEB_UPDATE)
        self.assertIn("hasFirmwareTestLegacyCapability", WEB_UPDATE)

    def test_webapp_keeps_legacy_fallback(self) -> None:
        self.assertIn("useLegacy", WEB_ACTIONS)
        self.assertIn('setFirmwareTestTextEntity("firmwareTestOtaUrl"', WEB_ACTIONS)
        self.assertIn('setFirmwareTestTextEntity("firmwareTestManifestUrl"', WEB_ACTIONS)


if __name__ == "__main__":
    unittest.main()
