from __future__ import annotations

import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

from scripts import check_docs_consistency


class DocsImpactExemptionTests(unittest.TestCase):
    def run_strict_check(
        self,
        pr_body: str,
        changed_file: str = "openquatt/oq_common.yaml",
    ) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as temp_dir:
            event_path = Path(temp_dir) / "event.json"
            event_path.write_text(
                json.dumps({"pull_request": {"body": pr_body}}),
                encoding="utf-8",
            )
            environment = {
                "GITHUB_EVENT_NAME": "pull_request",
                "GITHUB_EVENT_PATH": str(event_path),
            }
            arguments = [
                "check_docs_consistency.py",
                "--changed-only",
                "--strict",
                "--changed-file",
                changed_file,
            ]
            output = io.StringIO()
            with (
                mock.patch.dict(os.environ, environment, clear=False),
                mock.patch.object(sys, "argv", arguments),
                redirect_stdout(output),
            ):
                exit_code = check_docs_consistency.main()
        return exit_code, output.getvalue()

    def run_check(
        self,
        pr_body: str,
        changed_file: str = "openquatt/oq_common.yaml",
        strict: bool = False,
    ) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as temp_dir:
            event_path = Path(temp_dir) / "event.json"
            event_path.write_text(
                json.dumps({"pull_request": {"body": pr_body}}),
                encoding="utf-8",
            )
            environment = {
                "GITHUB_EVENT_NAME": "pull_request",
                "GITHUB_EVENT_PATH": str(event_path),
            }
            arguments = ["check_docs_consistency.py", "--changed-only"]
            if strict:
                arguments.append("--strict")
            arguments.extend(["--changed-file", changed_file])
            output = io.StringIO()
            with (
                mock.patch.dict(os.environ, environment, clear=False),
                mock.patch.object(sys, "argv", arguments),
                redirect_stdout(output),
            ):
                exit_code = check_docs_consistency.main()
        return exit_code, output.getvalue()

    def test_internal_oq_common_change_with_motivation_passes(self) -> None:
        exit_code, output = self.run_strict_check(
            """
## Documentatie

- [ ] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [x] Geen documentatiewijziging nodig

Docs-impact motivatie:

Alleen interne Modbus-timers zijn gewijzigd; entities en gebruikersconfiguratie blijven gelijk.
"""
        )

        self.assertEqual(0, exit_code, output)
        self.assertIn("Docs consistency checks passed.", output)

    def test_user_entity_change_without_docs_or_motivation_fails(self) -> None:
        exit_code, output = self.run_strict_check(
            """
## Documentatie

- [ ] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [ ] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
            changed_file="openquatt/oq_substitutions_common.yaml",
        )

        self.assertEqual(1, exit_code, output)
        self.assertIn("Gebruikersentiteiten en instellingen raakt gebruikersdocumentatie", output)
        self.assertNotIn("Service/debug entities changed", output)

    def test_companion_link_guard_fails_when_reference_is_removed(self) -> None:
        read_text = check_docs_consistency.read_text

        def text_without_companion_link(path: Path) -> str:
            text = read_text(path)
            if path == check_docs_consistency.REPO_ROOT / "docs/dashboard/README.md":
                return text.replace(
                    "https://github.com/OpenQuatt/home-assistant-openquatt",
                    "",
                )
            return text

        with mock.patch.object(
            check_docs_consistency,
            "read_text",
            side_effect=text_without_companion_link,
        ):
            exit_code, output = self.run_strict_check(
                """
## Documentatie

- [x] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [ ] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
                changed_file="docs/dashboard/README.md",
            )

        self.assertEqual(1, exit_code, output)
        self.assertIn("Missing companion repository reference", output)

    # --- New tests for #518: advisory vs blocking, narrowed patterns, exclusive checkboxes ---

    def test_hard_contract_flow_mismatch_still_fails_without_strict(self) -> None:
        """Echte contractbreuk (flow constants) moet blocking blijven, ook zonder --strict."""
        original_read = check_docs_consistency.read_text

        def text_without_flow_constants(path: Path) -> str:
            text = original_read(path)
            if path == check_docs_consistency.REPO_ROOT / "docs/instellingen-en-meetwaarden.md":
                return text.replace("oq_flow_mismatch_threshold_lph", "")
            return text

        with mock.patch.object(check_docs_consistency, "read_text", side_effect=text_without_flow_constants):
            exit_code, output = self.run_check(
                """
## Documentatie

- [x] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [ ] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
                changed_file="openquatt/oq_flow_control.yaml",
                strict=False,
            )

        self.assertEqual(1, exit_code, output)
        self.assertIn("Missing `oq_flow_mismatch_threshold_lph`", output)

    def test_heuristic_is_advisory_without_strict(self) -> None:
        """Heuristiek mag zonder --strict alleen warning geven, geen failure (doelbeeld #518)."""
        exit_code, output = self.run_check(
            """
## Documentatie

- [x] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [ ] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
            changed_file="openquatt/oq_common.yaml",
            strict=False,
        )
        # warning annotation, maar exit 0
        self.assertEqual(0, exit_code, output)
        self.assertIn("Gebruikersentiteiten en instellingen raakt gebruikersdocumentatie", output)
        self.assertIn("warning", output.lower())

        # zelfde wijziging mét --strict moet wel falen
        exit_code_strict, output_strict = self.run_check(
            """
## Documentatie

- [x] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [ ] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
            changed_file="openquatt/oq_common.yaml",
            strict=True,
        )
        self.assertEqual(1, exit_code_strict, output_strict)

    def test_narrowed_core_config_no_longer_triggers(self) -> None:
        """core/config.js was te breed en is uit Quick Start rule gehaald."""
        exit_code, output = self.run_check(
            """
## Documentatie

- [x] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [ ] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
            changed_file="openquatt/web/js/src/core/config.js",
            strict=True,
        )
        self.assertEqual(0, exit_code, output)
        self.assertIn("Docs consistency checks passed.", output)
        self.assertNotIn("::warning", output)

    def test_narrowed_settings_core_no_longer_triggers(self) -> None:
        """settings/core.js en service.js zijn interne infra en blijven buiten mapping; privacy/security/silent wel."""
        for path in [
            "openquatt/web/js/src/settings/core.js",
            "openquatt/web/js/src/settings/service.js",
        ]:
            with self.subTest(path=path):
                exit_code, output = self.run_check(
                    """
## Documentatie

- [x] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [ ] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
                    changed_file=path,
                    strict=True,
                )
                self.assertEqual(0, exit_code, output)
                self.assertIn("Docs consistency checks passed.", output)
                self.assertNotIn("::warning", output)

    def test_settings_privacy_security_silent_still_triggers(self) -> None:
        """privacy/security/silent zijn gebruikersgericht en moeten advisory hint blijven geven (fix #518 punt 5)."""
        for path in [
            "openquatt/web/js/src/settings/privacy.js",
            "openquatt/web/js/src/settings/security.js",
            "openquatt/web/js/src/settings/silent.js",
        ]:
            with self.subTest(path=path):
                # zonder strict: advisory warning, exit 0
                exit_code, output = self.run_check(
                    """
## Documentatie

- [x] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [ ] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
                    changed_file=path,
                    strict=False,
                )
                self.assertEqual(0, exit_code, output)
                self.assertIn("Web-appinstellingen raakt gebruikersdocumentatie", output)
                self.assertIn("warning", output.lower())
                # met strict: blocking
                exit_code_s, output_s = self.run_check(
                    """
## Documentatie

- [x] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [ ] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
                    changed_file=path,
                    strict=True,
                )
                self.assertEqual(1, exit_code_s, output_s)

    def test_narrowed_strategy_manager_no_longer_triggers(self) -> None:
        for path in [
            "openquatt/oq_strategy_manager.yaml",
            "openquatt/oq_supervisory_controlmode.yaml",
        ]:
            with self.subTest(path=path):
                exit_code, output = self.run_check(
                    """
## Documentatie

- [x] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [ ] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
                    changed_file=path,
                    strict=True,
                )
                self.assertEqual(0, exit_code, output)
                self.assertIn("Docs consistency checks passed.", output)
                self.assertNotIn("::warning", output)

    def test_docs_checkboxes_both_checked_fails(self) -> None:
        exit_code, output = self.run_check(
            """
## Documentatie

- [x] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [x] Geen documentatiewijziging nodig

Docs-impact motivatie:

Dubbel aangevinkt, moet exclusief zijn.
""",
            changed_file="docs/web-app.md",
            strict=False,
        )
        self.assertEqual(1, exit_code, output)
        self.assertIn("exact één documentatie-optie", output.lower())

    def test_docs_checkboxes_none_checked_fails(self) -> None:
        exit_code, output = self.run_check(
            """
## Documentatie

- [ ] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [ ] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
            changed_file="docs/web-app.md",
            strict=False,
        )
        self.assertEqual(1, exit_code, output)
        self.assertIn("Kies één documentatie-optie", output)

    def test_docs_checkboxes_one_checked_passes_without_heuristic(self) -> None:
        exit_code, output = self.run_check(
            """
## Documentatie

- [x] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [ ] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
            changed_file="docs/web-app.md",
            strict=False,
        )
        self.assertEqual(0, exit_code, output)
        self.assertIn("Docs consistency checks passed.", output)

    def test_no_docs_without_motivation_fails_without_heuristic(self) -> None:
        """'Geen documentatiewijziging nodig' zonder motivatie moet blocking falen, ook zonder heuristiek (fix #2)."""
        # ongemapt bestand, motivatie leeg -> moet falen
        exit_code, output = self.run_check(
            """
## Documentatie

- [ ] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [x] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
            changed_file="README.md",
            strict=False,
        )
        self.assertEqual(1, exit_code, output)
        self.assertIn("Docs-impact motivatie", output)

        # gemapte bron maar motivatie leeg -> ook blocking (niet alleen advisory)
        exit_code2, output2 = self.run_check(
            """
## Documentatie

- [ ] Documentatie bijgewerkt voor de gebruikersgerichte wijziging
- [x] Geen documentatiewijziging nodig

Docs-impact motivatie:
""",
            changed_file="openquatt/oq_common.yaml",
            strict=False,
        )
        self.assertEqual(1, exit_code2, output2)
        self.assertIn("Docs-impact motivatie", output2)


if __name__ == "__main__":
    unittest.main()
