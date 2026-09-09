from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REUSABLE_BUILD_WORKFLOW = (ROOT / ".github/workflows/esphome-build.yml").read_text(encoding="utf-8")
RELEASE_WORKFLOW = (ROOT / ".github/workflows/release-build.yml").read_text(encoding="utf-8")
DEV_WORKFLOW = (ROOT / ".github/workflows/dev-build.yml").read_text(encoding="utf-8")
PAGES_WORKFLOW = (ROOT / ".github/workflows/pages-deploy.yml").read_text(encoding="utf-8")


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

    def test_release_is_published_only_after_expected_assets_are_verified(self) -> None:
        self.assertIn("needs: validate-release-state", RELEASE_WORKFLOW)
        self.assertIn("--draft --generate-notes", RELEASE_WORKFLOW)
        self.assertIn("name: Verify release assets", RELEASE_WORKFLOW)
        self.assertIn("scripts/build_targets.py release-files --status enabled", RELEASE_WORKFLOW)
        self.assertIn("name: Publish complete release", RELEASE_WORKFLOW)
        self.assertGreater(
            RELEASE_WORKFLOW.index("Publish complete release"),
            RELEASE_WORKFLOW.index("Verify release assets"),
        )

    def test_pages_uses_one_complete_stable_release_for_assets_and_metadata(self) -> None:
        self.assertIn("name: Resolve latest complete stable release", PAGES_WORKFLOW)
        self.assertIn("scripts/build_targets.py factory-files --status enabled", PAGES_WORKFLOW)
        self.assertIn("select(.draft == false and .prerelease == false)", PAGES_WORKFLOW)
        self.assertIn("needs: resolve-stable-release", PAGES_WORKFLOW)
        self.assertIn("if: ${{ needs.resolve-stable-release.outputs.tag != '' }}", PAGES_WORKFLOW)
        self.assertIn('gh release download "${RELEASE_TAG}"', PAGES_WORKFLOW)
        self.assertGreaterEqual(PAGES_WORKFLOW.count("needs.resolve-stable-release.outputs.tag"), 3)


if __name__ == "__main__":
    unittest.main()
