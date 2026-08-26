from __future__ import annotations

import tempfile
import unittest
from unittest import mock
from pathlib import Path

from scripts import build_targets


ROOT = Path(__file__).resolve().parents[2]
BUILD_WORKFLOW = (ROOT / ".github/workflows/pr-test-firmware.yml").read_text(encoding="utf-8")
PUBLISH_WORKFLOW = (ROOT / ".github/workflows/pr-test-firmware-publish.yml").read_text(encoding="utf-8")
REUSABLE_BUILD_WORKFLOW = (ROOT / ".github/workflows/esphome-build.yml").read_text(encoding="utf-8")


class PrTestFirmwareWorkflowTests(unittest.TestCase):
    def test_untrusted_build_has_read_only_repository_access(self) -> None:
        self.assertIn("permissions:\n  contents: read", BUILD_WORKFLOW)
        self.assertNotIn("contents: write", BUILD_WORKFLOW)
        self.assertIn("persist-credentials: false", BUILD_WORKFLOW)
        self.assertNotIn("github.event.pull_request.head.repo.full_name == github.repository", BUILD_WORKFLOW)

    def test_pr_head_is_explicit_for_every_reusable_build_checkout(self) -> None:
        self.assertIn("checkout_ref: ${{ github.event.pull_request.head.sha }}", BUILD_WORKFLOW)
        self.assertIn(
            "checkout_repository: ${{ github.event.pull_request.head.repo.full_name }}",
            BUILD_WORKFLOW,
        )
        self.assertEqual(2, REUSABLE_BUILD_WORKFLOW.count("ref: ${{ inputs.checkout_ref }}"))
        self.assertEqual(
            2,
            REUSABLE_BUILD_WORKFLOW.count(
                "repository: ${{ inputs.checkout_repository || github.repository }}"
            ),
        )

    def test_release_channel_follows_supported_pr_base_branch(self) -> None:
        self.assertIn("types: [opened, reopened, labeled, synchronize, edited]", BUILD_WORKFLOW)
        self.assertIn("github.event.changes.base != null", BUILD_WORKFLOW)
        self.assertIn("BASE_REF: ${{ github.event.pull_request.base.ref }}", BUILD_WORKFLOW)
        self.assertIn('case "${BASE_REF}" in\n            main|dev)', BUILD_WORKFLOW)
        self.assertIn(
            "release_channel: ${{ steps.pr_meta.outputs.release_channel }}",
            BUILD_WORKFLOW,
        )
        self.assertIn(
            'echo "release_channel=${RELEASE_CHANNEL}" >> "${GITHUB_OUTPUT}"',
            BUILD_WORKFLOW,
        )
        self.assertIn(
            "release_channel: ${{ needs.compute-meta.outputs.release_channel }}",
            BUILD_WORKFLOW,
        )
        self.assertIn('--arg base_ref "${BASE_REF}"', BUILD_WORKFLOW)
        self.assertIn('--arg release_channel "${RELEASE_CHANNEL}"', BUILD_WORKFLOW)

    def test_release_channel_is_revalidated_before_publishing(self) -> None:
        self.assertIn('BASE_REF="$(jq -er \'.base_ref', PUBLISH_WORKFLOW)
        self.assertIn('RELEASE_CHANNEL="$(jq -er \'.release_channel', PUBLISH_WORKFLOW)
        self.assertIn('[[ "${RELEASE_CHANNEL}" != "${BASE_REF}" ]]', PUBLISH_WORKFLOW)
        self.assertIn("base branch changed since this firmware was built", PUBLISH_WORKFLOW)
        self.assertIn("base_ref: ${{ steps.gate.outputs.base_ref }}", PUBLISH_WORKFLOW)
        self.assertIn("BASE_REF: ${{ needs.validate-pr-test-build.outputs.base_ref }}", PUBLISH_WORKFLOW)
        self.assertGreaterEqual(PUBLISH_WORKFLOW.count(".base.ref"), 2)

    def test_privileged_workflow_revalidates_before_publishing(self) -> None:
        self.assertIn("workflow_run:", PUBLISH_WORKFLOW)
        self.assertIn("pull_request_target:", PUBLISH_WORKFLOW)
        self.assertIn("run-id: ${{ github.event.workflow_run.id }}", PUBLISH_WORKFLOW)
        self.assertIn("path: ${{ runner.temp }}/firmware", PUBLISH_WORKFLOW)
        self.assertIn("should_publish: ${{ steps.metadata.outputs.found }}", PUBLISH_WORKFLOW)
        self.assertGreaterEqual(PUBLISH_WORKFLOW.count(".head.sha"), 2)
        self.assertGreaterEqual(PUBLISH_WORKFLOW.count('any(.name == "test-firmware")'), 2)
        self.assertNotIn("github.event.pull_request.head.sha", PUBLISH_WORKFLOW)
        self.assertIn(
            "group: pr-test-firmware-${{ needs.validate-pr-test-build.outputs.pr_number }}\n"
            "      cancel-in-progress: false",
            PUBLISH_WORKFLOW,
        )

    def test_test_firmware_label_is_removed_only_after_merge_to_dev(self) -> None:
        self.assertIn("github.event.pull_request.merged == true", PUBLISH_WORKFLOW)
        self.assertIn("github.event.pull_request.base.ref == 'dev'", PUBLISH_WORKFLOW)
        self.assertIn("pull-requests: write", PUBLISH_WORKFLOW)
        self.assertIn(
            '"repos/${GITHUB_REPOSITORY}/issues/${PR_NUMBER}/labels/test-firmware"',
            PUBLISH_WORKFLOW,
        )
        self.assertIn("grep -Fxq 'test-firmware'", PUBLISH_WORKFLOW)

    def test_artifact_root_option_is_available(self) -> None:
        args = build_targets.create_parser().parse_args(
            [
                "prepare-pr-test-assets",
                "423",
                "v1.2.3-pr.423.1+1234567",
                "1234567890abcdef1234567890abcdef12345678",
                "https://example.invalid/download",
                "https://example.invalid/release",
                "--artifact-root",
                "/tmp/firmware",
            ]
        )

        self.assertEqual(Path("/tmp/firmware"), args.artifact_root)

    def test_symlinked_artifact_directory_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            real_artifact = root / "real"
            real_artifact.mkdir()
            (root / "openquatt-test").symlink_to(real_artifact, target_is_directory=True)

            with self.assertRaisesRegex(SystemExit, "must not be a symlink"):
                build_targets.find_artifact_dir(root, "openquatt-test")

    def test_pr_assets_are_normalized_from_external_artifact_root(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            repo_root = temp_root / "repo"
            artifact_root = temp_root / "artifacts"
            artifact_dir = artifact_root / "openquatt-test-pr-423"
            artifact_dir.mkdir(parents=True)
            repo_root.mkdir()
            (artifact_dir / "firmware.ota.bin").write_bytes(b"firmware")

            targets_file = repo_root / "build_targets.yaml"
            targets_file.write_text(
                """targets:
  - id: test
    status: enabled
    artifact_name: openquatt-test
    hardware: test-hardware
    topology: single
    connection: wifi
    display_name: Test target
""",
                encoding="utf-8",
            )

            with (
                mock.patch.object(build_targets, "REPO_ROOT", repo_root),
                mock.patch.object(build_targets, "TARGETS_FILE", targets_file),
            ):
                build_targets.prepare_pr_test_assets(
                    "423",
                    "v1.2.3-pr.423.1+1234567",
                    "1234567890abcdef1234567890abcdef12345678",
                    "https://example.invalid/download",
                    "https://example.invalid/release",
                    artifact_root=artifact_root,
                )

            self.assertEqual(b"firmware", (repo_root / "dist/openquatt-test.firmware.ota.bin").read_bytes())
            self.assertTrue((repo_root / "dist/openquatt-test.firmware.ota.bin.md5").is_file())
            self.assertTrue((repo_root / "dist/pr-firmware.json").is_file())


if __name__ == "__main__":
    unittest.main()
