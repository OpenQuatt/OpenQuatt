from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REUSABLE_BUILD_WORKFLOW = (ROOT / ".github/workflows/esphome-build.yml").read_text(encoding="utf-8")
RELEASE_WORKFLOW = (ROOT / ".github/workflows/release-build.yml").read_text(encoding="utf-8")
DEV_WORKFLOW = (ROOT / ".github/workflows/dev-build.yml").read_text(encoding="utf-8")


class ReleaseBuildWorkflowTests(unittest.TestCase):
    def test_ccache_is_enabled_by_default_for_reusable_builds(self) -> None:
        self.assertIn(
            """      enable_ccache:
        required: false
        type: boolean
        default: true
""",
            REUSABLE_BUILD_WORKFLOW,
        )
        self.assertNotIn("enable_ccache: false", DEV_WORKFLOW)

    def test_release_uses_cache_free_enabled_target_matrix_as_publish_gate(self) -> None:
        self.assertIn(
            'MATRIX="$(python3 scripts/build_targets.py github-matrix --status enabled)"',
            REUSABLE_BUILD_WORKFLOW,
        )
        self.assertIn("enable_ccache: false", RELEASE_WORKFLOW)
        self.assertIn("needs: compile-profiles", RELEASE_WORKFLOW)
        self.assertIn(
            'if [[ "${{ inputs.enable_ccache }}" == "true" ]]; then',
            REUSABLE_BUILD_WORKFLOW,
        )
        self.assertIn("unset IDF_CCACHE_ENABLE", REUSABLE_BUILD_WORKFLOW)
        self.assertIn("export CCACHE_DISABLE=1", REUSABLE_BUILD_WORKFLOW)

    def test_disabled_ccache_skips_setup_restore_and_statistics(self) -> None:
        self.assertIn(
            "name: Make ccache available\n        if: ${{ inputs.enable_ccache }}",
            REUSABLE_BUILD_WORKFLOW,
        )
        self.assertIn(
            "inputs.enable_ccache && github.event_name != 'pull_request'",
            REUSABLE_BUILD_WORKFLOW,
        )
        self.assertIn(
            "inputs.enable_ccache && github.event_name == 'pull_request'",
            REUSABLE_BUILD_WORKFLOW,
        )
        self.assertIn(
            "name: Show ccache statistics\n        if: ${{ always() && inputs.enable_ccache }}",
            REUSABLE_BUILD_WORKFLOW,
        )


if __name__ == "__main__":
    unittest.main()
