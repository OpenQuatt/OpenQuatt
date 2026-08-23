from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class CrashSymbolicationWorkflowTests(unittest.TestCase):
    def test_build_uses_resolved_checkout_identity_and_deterministic_wrapper(self) -> None:
        workflow = (ROOT / ".github/workflows/esphome-build.yml").read_text(encoding="utf-8")

        self.assertIn('SOURCE_COMMIT="$(git rev-parse HEAD)"', workflow)
        self.assertIn("git show -s --format=%ct HEAD", workflow)
        self.assertIn('export SOURCE_DATE_EPOCH="${BUILD_EPOCH}"', workflow)
        self.assertIn("python3 scripts/esphome_deterministic.py", workflow)
        for name in (
            "build_source_repository",
            "build_source_commit",
            "build_target",
            "build_epoch",
        ):
            self.assertIn(f'-s {name} "', workflow)
            self.assertIn(f'--substitution "{name}=', workflow)
        self.assertIn('-s project_version "${PROJECT_VERSION}"', workflow)
        self.assertIn('-s release_channel "${RELEASE_CHANNEL}"', workflow)
        self.assertIn('--substitution "project_version=${PROJECT_VERSION}"', workflow)
        self.assertIn('--substitution "release_channel=${RELEASE_CHANNEL}"', workflow)

    def test_build_artifacts_contain_metadata_but_not_elf(self) -> None:
        workflow = (ROOT / ".github/workflows/esphome-build.yml").read_text(encoding="utf-8")

        self.assertIn(".build.json", workflow)
        self.assertIn(".*.*.build.json", workflow)
        self.assertNotIn("firmware.elf", workflow)

    def test_reconstruction_workflow_never_uploads_the_elf(self) -> None:
        workflow = (ROOT / ".github/workflows/reconstruct-crash.yml").read_text(encoding="utf-8")

        self.assertIn("inputs.build_metadata_url", workflow)
        self.assertIn("raw.githubusercontent.com", workflow)
        self.assertIn("--metadata", workflow)
        for name in (
            "build-id",
            "source-repository",
            "source-commit",
            "build-epoch",
            "build-target",
            "firmware-version",
            "release-channel",
        ):
            self.assertIn(f"--captured-{name}", workflow)
        self.assertIn("reconstruct_firmware_elf.py", workflow)
        self.assertIn("symbolize_firmware_crash.py", workflow)
        self.assertNotIn("actions/upload-artifact", workflow)
        self.assertIn("Deliberately no artifact upload", workflow)

    def test_reconstruction_validates_before_checkout_and_install(self) -> None:
        workflow = (ROOT / ".github/workflows/reconstruct-crash.yml").read_text(encoding="utf-8")

        validation = workflow.index("Validate captured identity and source repository policy")
        trusted_checkout = workflow.index("Checkout trusted reconstruction tooling")
        source_checkout = workflow.index("Checkout exact firmware source")
        verify_head = workflow.index("Verify exact source checkout before setup or installation")
        setup_node = workflow.index("Set up Node.js")
        install = workflow.index("Install captured source build dependencies")
        self.assertLess(validation, trusted_checkout)
        self.assertLess(validation, source_checkout)
        self.assertLess(source_checkout, verify_head)
        self.assertLess(verify_head, setup_node)
        self.assertLess(verify_head, install)
        self.assertIn("^[0-9a-f]{40}$", workflow)
        self.assertIn("^[0-9a-f]{64}$", workflow)
        self.assertIn("4294967295", workflow)
        self.assertIn("^configs/", workflow)

    def test_all_published_builds_persist_small_metadata_outside_mutable_releases(self) -> None:
        for filename in (
            "dev-build.yml",
            "release-build.yml",
            "pr-test-firmware-publish.yml",
        ):
            with self.subTest(filename=filename):
                workflow = (ROOT / ".github" / "workflows" / filename).read_text(
                    encoding="utf-8"
                )
                self.assertIn("publish_firmware_build_metadata.py", workflow)
                self.assertIn("*.build.json", workflow)

        dev = (ROOT / ".github/workflows/dev-build.yml").read_text(encoding="utf-8")
        release = (ROOT / ".github/workflows/release-build.yml").read_text(encoding="utf-8")
        self.assertLess(
            dev.index("Publish immutable firmware build metadata"),
            dev.index("Move dev release tag"),
        )
        self.assertLess(
            release.index("Publish immutable firmware build metadata"),
            release.index("Create release if needed"),
        )

        publisher = (ROOT / "scripts" / "publish_firmware_build_metadata.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('DEFAULT_BRANCH = "firmware-build-metadata"', publisher)
        self.assertIn('parser.add_argument("--expected-source-repository", required=True)', publisher)
        self.assertIn('parser.add_argument("--expected-source-commit", required=True)', publisher)
        self.assertIn("Immutable build metadata path already has a different rebuild contract", publisher)
        self.assertNotIn("firmware.elf", publisher)

    def test_publishers_bind_records_to_trusted_source_identity(self) -> None:
        dev = (ROOT / ".github/workflows/dev-build.yml").read_text(encoding="utf-8")
        release = (ROOT / ".github/workflows/release-build.yml").read_text(
            encoding="utf-8"
        )
        pr = (ROOT / ".github/workflows/pr-test-firmware-publish.yml").read_text(
            encoding="utf-8"
        )
        for workflow in (dev, release, pr):
            self.assertIn("--expected-source-repository", workflow)
            self.assertIn("--expected-source-commit", workflow)
        self.assertIn(
            '${{ needs.validate-pr-test-build.outputs.head_repository }}', pr
        )
        self.assertIn('${{ needs.validate-pr-test-build.outputs.head_sha }}', pr)

    def test_pr_metadata_publish_revalidates_current_pr_state(self) -> None:
        workflow = (
            ROOT / ".github/workflows/pr-test-firmware-publish.yml"
        ).read_text(encoding="utf-8")
        revalidation = workflow.index(
            "Revalidate PR before durable metadata publication"
        )
        metadata_publish = workflow.index(
            "Publish immutable firmware build metadata", revalidation
        )
        release_publish = workflow.index(
            "Publish current PR test release", metadata_publish
        )
        self.assertLess(revalidation, metadata_publish)
        self.assertLess(metadata_publish, release_publish)

        revalidation_block = workflow[revalidation:metadata_publish]
        for token in (
            'gh api "repos/${GITHUB_REPOSITORY}/pulls/${PR_NUMBER}"',
            "'.state'",
            'any(.name == "test-firmware")',
            "'.head.sha'",
            "'.head.repo.full_name'",
            "'.base.repo.full_name'",
            "'.base.ref'",
            '!= "${HEAD_SHA}"',
            '!= "${HEAD_REPOSITORY}"',
            '!= "${GITHUB_REPOSITORY}"',
            '!= "${BASE_REF}"',
        ):
            self.assertIn(token, revalidation_block)

        release_block = workflow[release_publish:]
        self.assertIn(
            'gh api "repos/${GITHUB_REPOSITORY}/pulls/${PR_NUMBER}"',
            release_block,
        )
        self.assertIn('any(.name == "test-firmware")', release_block)
        self.assertIn("'.head.sha'", release_block)

    def test_build_jobs_do_not_inherit_release_write_credentials(self) -> None:
        shared = (ROOT / ".github/workflows/esphome-build.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("default: false", shared)
        self.assertGreaterEqual(shared.count("contents: read"), 2)

        for filename in ("dev-build.yml", "release-build.yml"):
            with self.subTest(filename=filename):
                workflow = (ROOT / ".github/workflows" / filename).read_text(
                    encoding="utf-8"
                )
                self.assertIn("permissions:\n  contents: read", workflow)
                self.assertIn("checkout_persist_credentials: false", workflow)
                self.assertIn("persist-credentials: false", workflow)
                self.assertIn("contents: write", workflow)


if __name__ == "__main__":
    unittest.main()
