from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts import check_style_consistency


class InternalEntityPresentationMetadataTests(unittest.TestCase):
    def run_check(self, yaml: str) -> list[check_style_consistency.Finding]:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            path = root / "entities.yaml"
            path.write_text(yaml, encoding="utf-8")
            findings: list[check_style_consistency.Finding] = []
            with mock.patch.object(check_style_consistency, "REPO_ROOT", root):
                check_style_consistency.check_no_internal_entity_presentation_metadata(path, findings)
            return findings

    def test_reports_presentation_metadata_before_and_after_internal_flag(self) -> None:
        findings = self.run_check(
            """binary_sensor:
  - platform: template
    id: clock_valid
    icon: mdi:clock-check
    internal: true
    disabled_by_default: true
    device_class: connectivity
    entity_category: diagnostic
"""
        )

        self.assertEqual(
            {"device_class", "disabled_by_default", "entity_category", "icon"},
            {finding.message.split(":", 1)[0].strip("`") for finding in findings},
        )

    def test_does_not_cross_into_the_next_entity_mapping(self) -> None:
        findings = self.run_check(
            """binary_sensor:
  - platform: template
    id: internal_helper
    internal: true

  - platform: template
    id: public_diagnostic
    icon: mdi:check
    device_class: connectivity
    entity_category: diagnostic
"""
        )

        self.assertEqual([], findings)

    def test_checks_nested_custom_component_entities(self) -> None:
        findings = self.run_check(
            """custom_component:
  installation_id:
    id: installation_id
    name: Installation ID
    internal: true
    entity_category: diagnostic
"""
        )

        self.assertEqual(1, len(findings))
        self.assertIn("`entity_category:`", findings[0].message)


if __name__ == "__main__":
    unittest.main()
