from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from unittest import mock
from pathlib import Path

from scripts import build_targets


ROOT = Path(__file__).resolve().parents[2]
BUILD_WORKFLOW = (ROOT / ".github/workflows/pr-test-firmware.yml").read_text(encoding="utf-8")
PUBLISH_WORKFLOW = (ROOT / ".github/workflows/pr-test-firmware-publish.yml").read_text(encoding="utf-8")
REUSABLE_BUILD_WORKFLOW = (ROOT / ".github/workflows/esphome-build.yml").read_text(encoding="utf-8")


def evaluate_build_expression(expression: str, context: dict[str, object]) -> object:
    """Evaluate the boolean/string subset used by the build gate and group.

    All comparison operands in these fixtures have matching types. Python's
    short-circuit and/or therefore match the GitHub expressions used here.
    """
    for path in sorted(context, key=len, reverse=True):
        expression = expression.replace(path, repr(context[path]))
    expression = expression.replace("&&", " and ").replace("||", " or ")
    expression = re.sub(r"\bnull\b", "None", expression)
    return eval(
        " ".join(expression.split()),
        {"__builtins__": {}},
        {"contains": lambda values, value: value in values, "format": str.format},
    )


class PrTestFirmwareWorkflowTests(unittest.TestCase):
    def test_only_build_events_can_replace_the_pr_concurrency_group(self) -> None:
        group_block = BUILD_WORKFLOW.split("concurrency:\n", 1)[1].split("\njobs:", 1)[0]
        job_block = BUILD_WORKFLOW.split("  compute-meta:\n", 1)[1].split("    outputs:", 1)[0]
        group_expression = re.search(r"\$\{\{(.*?)\}\}", group_block, re.DOTALL).group(1)
        gate_expression = re.search(r"\$\{\{(.*?)\}\}", job_block, re.DOTALL).group(1)
        for action, changed_base, label, expected_build in (
            ("opened", None, None, True),
            ("reopened", None, None, True),
            ("synchronize", None, None, True),
            ("labeled", None, "test-firmware", True),
            ("labeled", None, "bug", False),
            ("edited", None, None, False),  # Title/body edits.
            ("edited", {"ref": {"from": "main"}}, None, True),
        ):
            for labels in ([], ["test-firmware"], ["bug", "test-firmware"]):
                for run_id in (1001, 1002):
                    with self.subTest(action=action, base=changed_base, labels=labels, run_id=run_id):
                        context = {
                            "github.event.pull_request.labels.*.name": labels,
                            "github.event.pull_request.number": 786,
                            "github.event.action": action,
                            "github.event.label.name": label,
                            "github.event.changes.base": changed_base,
                            "github.run_id": run_id,
                        }
                        should_build = expected_build and "test-firmware" in labels
                        self.assertEqual(should_build, evaluate_build_expression(gate_expression, context))
                        self.assertEqual(
                            "pr-test-firmware-786" if should_build else f"pr-test-firmware-noop-{run_id}",
                            evaluate_build_expression(group_expression, context),
                        )
        self.assertIn("cancel-in-progress: true", group_block)

    def test_reruns_replace_named_artifacts(self) -> None:
        metadata_step = BUILD_WORKFLOW.split("      - name: Upload build metadata\n", 1)[1]
        self.assertIn("name: pr-test-firmware-meta\n          overwrite: true", metadata_step)
        upload_steps = [
            step for step in REUSABLE_BUILD_WORKFLOW.split("      - name: ")[1:]
            if "uses: actions/upload-artifact@" in step
        ]
        self.assertEqual(4, len(upload_steps))
        for step in upload_steps:
            with self.subTest(step=step.splitlines()[0]):
                self.assertIn("overwrite: true", step)
        self.assertIn("artifact-ids: ${{ steps.metadata.outputs.artifact_id }}", PUBLISH_WORKFLOW)
        self.assertIn("artifact-ids: ${{ steps.firmware.outputs.artifact_ids }}", PUBLISH_WORKFLOW)

    def run_artifact_selection(
        self, step_name: str, pages: list[dict], source_run: dict, *, api_failure: bool = False,
    ) -> tuple[subprocess.CompletedProcess, dict[str, str]]:
        step = PUBLISH_WORKFLOW.split(f"      - name: {step_name}\n", 1)[1]
        shell = textwrap.dedent(step.split("        run: |\n", 1)[1].split("\n      - name:", 1)[0])
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "artifacts.json").write_text(json.dumps(pages))
            (root / "run.json").write_text(json.dumps(source_run))
            (root / "outputs").write_text("")
            gh = root / "gh"
            gh.write_text(
                '#!/bin/bash\n'
                'if [[ "$*" == *"--slurp"* && "$*" == *"--jq"* ]]; then\n'
                '  echo "--slurp is not supported with --jq" >&2; exit 1\n'
                'fi\n'
                'if [[ "$*" == *"/artifacts?"* ]]; then\n'
                '  if [[ "${API_FAILURE}" == "true" ]]; then exit 1; fi\n'
                '  exec cat "${ARTIFACT_FIXTURE}"\n'
                'else\n'
                '  exec cat "${RUN_FIXTURE}"\n'
                'fi\n'
            )
            gh.chmod(0o755)
            result = subprocess.run(
                ["bash", "-e", "-c", shell],
                env={
                    **os.environ,
                    "PATH": f"{root}{os.pathsep}{os.environ['PATH']}",
                    "ARTIFACT_FIXTURE": str(root / "artifacts.json"),
                    "API_FAILURE": "true" if api_failure else "false",
                    "RUN_FIXTURE": str(root / "run.json"),
                    "GITHUB_OUTPUT": str(root / "outputs"),
                    "GITHUB_REPOSITORY": "OpenQuatt/OpenQuatt",
                    "SOURCE_RUN_ID": "12345",
                    "PR_NUMBER": "786",
                },
                capture_output=True, text=True,
            )
            outputs = dict(line.split("=", 1) for line in (root / "outputs").read_text().splitlines())
            return result, outputs

    @unittest.skipUnless(shutil.which("jq"), "workflow shell tests require jq")
    def test_metadata_selection_pins_latest_nonexpired_id_across_pages(self) -> None:
        pages = [
            {"artifacts": [
                {"id": 300, "name": "pr-test-firmware-meta", "expired": True},
                {"id": 200, "name": "pr-test-firmware-meta", "expired": False},
                {"id": 900, "name": "unrelated", "expired": False},
            ]},
            {"artifacts": [{"id": 100, "name": "pr-test-firmware-meta", "expired": False}]},
        ]
        result, outputs = self.run_artifact_selection("Find build metadata artifact", pages, {})
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual({"artifact_id": "200", "found": "true"}, outputs)
        for pages in ([{"artifacts": []}], [{"artifacts": [
            {"id": 300, "name": "pr-test-firmware-meta", "expired": True},
        ]}]):
            result, outputs = self.run_artifact_selection("Find build metadata artifact", pages, {})
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertEqual({"found": "false"}, outputs)

    @unittest.skipUnless(shutil.which("jq"), "workflow shell tests require jq")
    def test_firmware_selection_uses_latest_per_name_and_rejects_unsuccessful_reruns(self) -> None:
        duo = "openquatt-heatpump-controller-q-duo-pr-786"
        single = "openquatt-heatpump-controller-q-single-pr-786"
        pages = [
            {"artifacts": [
                {"id": 500, "name": duo, "expired": True},
                {"id": 400, "name": "openquatt-other-pr-787", "expired": False},
                {"id": 300, "name": single, "expired": False},
                {"id": 200, "name": duo, "expired": False},
            ]},
            {"artifacts": [
                {"id": 100, "name": duo, "expired": False},
                {"id": 99, "name": "pr-test-firmware-meta", "expired": False},
            ]},
        ]
        result, outputs = self.run_artifact_selection(
            "Select current firmware artifacts", pages, {"status": "completed", "conclusion": "success"},
        )
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual({"artifact_ids": "200,300"}, outputs)
        for status, conclusion in (("completed", "failure"), ("completed", "cancelled"), ("in_progress", None), ("queued", None)):
            with self.subTest(status=status, conclusion=conclusion):
                result, outputs = self.run_artifact_selection(
                    "Select current firmware artifacts", pages, {"status": status, "conclusion": conclusion},
                )
                self.assertNotEqual(0, result.returncode)
                self.assertEqual({}, outputs)
                self.assertIn("Latest source build attempt is not successful", result.stderr)
        result, outputs = self.run_artifact_selection(
            "Select current firmware artifacts", [{"artifacts": []}], {"status": "completed", "conclusion": "success"},
        )
        self.assertNotEqual(0, result.returncode)
        self.assertEqual({}, outputs)
        self.assertIn("Missing or invalid firmware artifact IDs", result.stderr)

    @unittest.skipUnless(shutil.which("jq"), "workflow shell tests require jq")
    def test_artifact_api_failure_does_not_publish_or_skip_as_missing_metadata(self) -> None:
        for step in ("Find build metadata artifact", "Select current firmware artifacts"):
            with self.subTest(step=step):
                result, outputs = self.run_artifact_selection(
                    step, [{"artifacts": []}], {"status": "completed", "conclusion": "success"},
                    api_failure=True,
                )
                self.assertNotEqual(0, result.returncode)
                self.assertEqual({}, outputs)

    def test_latest_attempt_is_checked_inside_publish_lock_and_before_release_mutation(self) -> None:
        publish_job = PUBLISH_WORKFLOW.split("  publish-pr-test-release:\n", 1)[1].split("  delete-pr-test-release:", 1)[0]
        self.assertLess(publish_job.index("concurrency:"), publish_job.index("Select current firmware artifacts"))
        release_step = publish_job.split("      - name: Publish current PR test release\n", 1)[1]
        self.assertLess(release_step.index(".conclusion == \"success\""), release_step.index("gh release delete"))

    def run_pr_publication_gate(
        self, step_name: str, *, state: str = "open", labels: list | None = None,
        head_sha: str = "1234567890abcdef1234567890abcdef12345678", base_ref: str = "dev",
        metadata_changes: dict | None = None, api_failure: bool = False,
        fetched_sha: str = "1234567890abcdef1234567890abcdef12345678",
        source_run: dict | None = None, malformed_pr: bool = False, upload_failure: bool = False,
    ) -> tuple[subprocess.CompletedProcess, dict[str, str], str]:
        built_sha = "1234567890abcdef1234567890abcdef12345678"
        version = "v0.55.0-pr.786.1+1234567"
        metadata = {
            "pr_number": 786, "head_sha": built_sha, "head_repository": "OpenQuatt/OpenQuatt",
            "base_ref": "dev", "base_version": "v0.55.0", "release_channel": "dev",
            "run_number": 1, "version": version,
        }
        metadata.update(metadata_changes or {})
        pr = {
            "state": state, "labels": [{"name": "test-firmware"}] if labels is None else labels,
            "head": {"sha": head_sha, "repo": {"full_name": "OpenQuatt/OpenQuatt"}},
            "base": {"sha": "a" * 40, "ref": base_ref, "repo": {"full_name": "OpenQuatt/OpenQuatt"}},
        }
        step = PUBLISH_WORKFLOW.split(f"      - name: {step_name}\n", 1)[1]
        shell = textwrap.dedent(re.split(
            r"\n(?:      - name: |  [a-z][a-z0-9-]*:\n)",
            step.split("        run: |\n", 1)[1], maxsplit=1,
        )[0])
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            for name, content in (
                ("metadata.json", json.dumps(metadata)),
                ("pr.json", "{" if malformed_pr else json.dumps(pr)),
                ("run.json", json.dumps(source_run or {"status": "completed", "conclusion": "success"})),
                ("outputs", ""), ("calls", ""),
            ):
                (root / name).write_text(content)
            (root / "dist").mkdir()
            (root / "dist/test.firmware.ota.bin").write_bytes(b"test firmware")
            gh = root / "gh"
            gh.write_text(
                '#!/bin/bash\n'
                'printf "gh %s\\n" "$*" >> "${CALLS}"\n'
                'if [[ "$1" == "api" ]]; then\n'
                '  if [[ "${API_FAILURE}" == "true" ]]; then exit 1; fi\n'
                '  if [[ "$*" == *"/pulls/"* ]]; then exec cat "${PR_FIXTURE}"; fi\n'
                '  exec cat "${RUN_FIXTURE}"\n'
                'fi\n'
                'if [[ "$1 $2" == "release upload" && "${UPLOAD_FAILURE}" == "true" ]]; then exit 1; fi\n'
                'if [[ "$1 $2" == "release view" && "$*" == *"--json assets"* ]]; then echo 1; fi\n'
            )
            gh.chmod(0o755)
            git = root / "git"
            git.write_text(
                '#!/bin/bash\n'
                'printf "git %s\\n" "$*" >> "${CALLS}"\n'
                'if [[ "$1" == "rev-parse" ]]; then echo "${FETCHED_SHA}"; fi\n'
            )
            git.chmod(0o755)
            result = subprocess.run(
                ["bash", "-e", "-c", shell], cwd=root,
                env={
                    **os.environ, "PATH": f"{root}{os.pathsep}{os.environ['PATH']}",
                    "META_FILE": str(root / "metadata.json"), "PR_FIXTURE": str(root / "pr.json"),
                    "RUN_FIXTURE": str(root / "run.json"), "CALLS": str(root / "calls"),
                    "GITHUB_OUTPUT": str(root / "outputs"), "GITHUB_REPOSITORY": "OpenQuatt/OpenQuatt",
                    "SOURCE_HEAD_REPOSITORY": "OpenQuatt/OpenQuatt", "SOURCE_HEAD_SHA": built_sha,
                    "SOURCE_RUN_NUMBER": "1", "SOURCE_WORKFLOW_PATH": ".github/workflows/pr-test-firmware.yml",
                    "SOURCE_RUN_ID": "12345", "PR_NUMBER": "786", "HEAD_SHA": built_sha,
                    "BASE_REF": "dev", "SHORT_SHA": built_sha[:7], "VERSION": version,
                    "API_FAILURE": "true" if api_failure else "false", "FETCHED_SHA": fetched_sha,
                    "UPLOAD_FAILURE": "true" if upload_failure else "false",
                }, capture_output=True, text=True,
            )
            outputs = dict(line.split("=", 1) for line in (root / "outputs").read_text().splitlines())
            return result, outputs, (root / "calls").read_text()

    def assert_no_publication_mutations(self, calls: str) -> None:
        for command in ("gh release delete", "gh release create", "gh release upload", "git tag", "git push"):
            self.assertNotIn(command, calls)

    @unittest.skipUnless(shutil.which("jq"), "workflow shell tests require jq")
    def test_current_pr_gate_explicitly_authorizes_publication(self) -> None:
        result, outputs, calls = self.run_pr_publication_gate("Validate build and current PR state")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual("true", outputs.get("should_publish"))
        self.assertEqual("786", outputs["pr_number"])
        self.assert_no_publication_mutations(calls)
        self.assertIn("should_publish: ${{ steps.gate.outputs.should_publish }}", PUBLISH_WORKFLOW)

    @unittest.skipUnless(shutil.which("jq"), "workflow shell tests require jq")
    def test_obsolete_prs_skip_at_both_publication_gates_without_writes(self) -> None:
        for step in ("Validate build and current PR state", "Publish current PR test release"):
            for change in ({"state": "closed"}, {"labels": []}, {"head_sha": "b" * 40}, {"base_ref": "main"}):
                with self.subTest(step=step, change=change):
                    result, outputs, calls = self.run_pr_publication_gate(step, **change)
                    self.assertEqual(0, result.returncode, result.stderr)
                    if step == "Validate build and current PR state":
                        self.assertEqual({"should_publish": "false"}, outputs)
                    self.assert_no_publication_mutations(calls)

    @unittest.skipUnless(shutil.which("jq"), "workflow shell tests require jq")
    def test_metadata_errors_still_fail_even_after_pr_closed(self) -> None:
        for changes in (
            {"head_sha": "b" * 40}, {"head_repository": "other/repo"},
            {"base_version": "invalid"}, {"base_ref": "feature"}, {"release_channel": "main"},
            {"run_number": 2}, {"version": "v0.55.0"},
        ):
            with self.subTest(changes=changes):
                result, outputs, calls = self.run_pr_publication_gate(
                    "Validate build and current PR state", state="closed", metadata_changes=changes,
                )
                self.assertNotEqual(0, result.returncode)
                self.assertNotEqual("true", outputs.get("should_publish"))
                self.assert_no_publication_mutations(calls)

    @unittest.skipUnless(shutil.which("jq"), "workflow shell tests require jq")
    def test_invalid_pr_responses_and_api_errors_are_not_benign_skips(self) -> None:
        for step in ("Validate build and current PR state", "Publish current PR test release"):
            for change in (
                {"api_failure": True}, {"malformed_pr": True}, {"state": "unknown"},
                {"labels": [{"wrong": "label"}]}, {"head_sha": "invalid"},
            ):
                with self.subTest(step=step, change=change):
                    result, outputs, calls = self.run_pr_publication_gate(step, **change)
                    self.assertNotEqual(0, result.returncode)
                    self.assertNotEqual("true", outputs.get("should_publish"))
                    self.assert_no_publication_mutations(calls)

    @unittest.skipUnless(shutil.which("jq"), "workflow shell tests require jq")
    def test_head_moving_during_fetch_skips_but_invalid_fetch_fails(self) -> None:
        result, _, calls = self.run_pr_publication_gate("Publish current PR test release", fetched_sha="b" * 40)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assert_no_publication_mutations(calls)
        result, _, calls = self.run_pr_publication_gate("Publish current PR test release", fetched_sha="invalid")
        self.assertNotEqual(0, result.returncode)
        self.assert_no_publication_mutations(calls)

    @unittest.skipUnless(shutil.which("jq"), "workflow shell tests require jq")
    def test_current_publication_and_partial_upload_cleanup_still_work(self) -> None:
        result, _, calls = self.run_pr_publication_gate("Publish current PR test release")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("gh release create pr-786", calls)
        self.assertIn("gh release upload pr-786", calls)
        self.assertEqual(1, calls.count("gh release delete pr-786"))
        result, _, calls = self.run_pr_publication_gate("Publish current PR test release", upload_failure=True)
        self.assertNotEqual(0, result.returncode)
        self.assertEqual(2, calls.count("gh release delete pr-786"))

    @unittest.skipUnless(shutil.which("jq"), "workflow shell tests require jq")
    def test_unsuccessful_latest_build_still_fails_before_any_release_write(self) -> None:
        for status, conclusion in (("completed", "failure"), ("completed", "cancelled"), ("in_progress", None)):
            with self.subTest(status=status, conclusion=conclusion):
                result, _, calls = self.run_pr_publication_gate(
                    "Publish current PR test release", state="closed",
                    source_run={"status": status, "conclusion": conclusion},
                )
                self.assertNotEqual(0, result.returncode)
                self.assert_no_publication_mutations(calls)

    def test_close_and_unlabel_cleanup_jobs_remain_independent_of_publish_gate(self) -> None:
        for job in ("delete-pr-test-release", "remove-merged-pr-test-label"):
            block = PUBLISH_WORKFLOW.split(f"  {job}:\n", 1)[1]
            header = block.split("    steps:", 1)[0]
            self.assertNotIn("needs:", header)
            self.assertIn("github.event_name == 'pull_request_target'", header)
            self.assertIn("github.event.action == 'closed'", header)
        delete_job = PUBLISH_WORKFLOW.split("  delete-pr-test-release:\n", 1)[1]
        self.assertIn("github.event.action == 'unlabeled'", delete_job)
        self.assertIn("group: pr-test-firmware-${{ github.event.pull_request.number }}", delete_job)

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
        self.assertIn("base branch changed; skipping test publication", PUBLISH_WORKFLOW)
        self.assertIn("base_ref: ${{ steps.gate.outputs.base_ref }}", PUBLISH_WORKFLOW)
        self.assertIn("BASE_REF: ${{ needs.validate-pr-test-build.outputs.base_ref }}", PUBLISH_WORKFLOW)
        self.assertGreaterEqual(PUBLISH_WORKFLOW.count(".base.ref"), 2)

    def test_privileged_workflow_revalidates_before_publishing(self) -> None:
        self.assertIn("workflow_run:", PUBLISH_WORKFLOW)
        self.assertIn("pull_request_target:", PUBLISH_WORKFLOW)
        self.assertIn("run-id: ${{ github.event.workflow_run.id }}", PUBLISH_WORKFLOW)
        self.assertIn("path: ${{ runner.temp }}/firmware", PUBLISH_WORKFLOW)
        self.assertIn("should_publish: ${{ steps.gate.outputs.should_publish }}", PUBLISH_WORKFLOW)
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

    def test_unified_q_has_no_compatibility_artifact_uploads(self) -> None:
        self.assertNotIn("Upload WiFi compatibility OTA artifact", REUSABLE_BUILD_WORKFLOW)
        self.assertNotIn("Upload Ethernet compatibility OTA artifact", REUSABLE_BUILD_WORKFLOW)
        self.assertEqual(
            0,
            REUSABLE_BUILD_WORKFLOW.count("matrix.target.connection == 'auto'"),
        )
        self.assertIn("dist/*-ota.manifest.json", PUBLISH_WORKFLOW)

    def test_symlinked_artifact_directory_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            real_artifact = root / "real"
            real_artifact.mkdir()
            (root / "openquatt-test").symlink_to(real_artifact, target_is_directory=True)

            with self.assertRaisesRegex(SystemExit, "must not be a symlink"):
                build_targets.find_artifact_dir(root, "openquatt-test")

    def test_canonical_artifact_lookup_ignores_compatibility_alias_directories(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            canonical = root / "openquatt-test-pr-476"
            canonical.mkdir()
            (root / "openquatt-test-wifi-pr-476").mkdir()
            (root / "openquatt-test-eth-pr-476").mkdir()

            self.assertEqual(
                canonical,
                build_targets.find_artifact_dir(
                    root,
                    "openquatt-test",
                    ["openquatt-test-wifi", "openquatt-test-eth"],
                ),
            )

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
            manifest = json.loads((repo_root / "dist/openquatt-test-ota.manifest.json").read_text(encoding="utf-8"))
            self.assertEqual("v1.2.3-pr.423.1+1234567", manifest["version"])
            self.assertEqual(
                "https://example.invalid/download/openquatt-test.firmware.ota.bin",
                manifest["builds"][0]["ota"]["path"],
            )

    def test_release_aliases_are_byte_identical_and_get_matching_manifests(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            artifact_dir = repo_root / "dist/openquatt-test"
            artifact_dir.mkdir(parents=True)
            (artifact_dir / "firmware.ota.bin").write_bytes(b"ota-firmware")
            (artifact_dir / "firmware.factory.bin").write_bytes(b"factory-firmware")

            targets_file = repo_root / "build_targets.yaml"
            targets_file.write_text(
                """targets:
  - id: test
    status: enabled
    artifact_name: openquatt-test
    artifact_aliases: openquatt-test-wifi,openquatt-test-eth
    manifest_name: openquatt-test-ota.manifest.json
    chip_family: ESP32-S3
    connection: auto
    display_name: Test target
""",
                encoding="utf-8",
            )

            with (
                mock.patch.object(build_targets, "REPO_ROOT", repo_root),
                mock.patch.object(build_targets, "TARGETS_FILE", targets_file),
            ):
                target = build_targets.load_targets()[0]
                self.assertEqual(
                    [
                        "openquatt-test.firmware.ota.bin",
                        "openquatt-test.firmware.factory.bin",
                        "openquatt-test-ota.manifest.json",
                        "openquatt-test-wifi.firmware.ota.bin",
                        "openquatt-test-wifi-ota.manifest.json",
                        "openquatt-test-eth.firmware.ota.bin",
                        "openquatt-test-eth-ota.manifest.json",
                    ],
                    build_targets.release_asset_names(target),
                )
                build_targets.prepare_release_assets(
                    "v1.2.3",
                    "https://example.invalid/download",
                    "https://example.invalid/release",
                )

            canonical_ota = repo_root / "dist/openquatt-test.firmware.ota.bin"
            canonical_factory = repo_root / "dist/openquatt-test.firmware.factory.bin"
            self.assertEqual(b"ota-firmware", canonical_ota.read_bytes())
            self.assertEqual(b"factory-firmware", canonical_factory.read_bytes())

            expected_ota_md5 = build_targets.md5sum(canonical_ota)
            for published_name in ("openquatt-test", "openquatt-test-wifi", "openquatt-test-eth"):
                published_ota = repo_root / f"dist/{published_name}.firmware.ota.bin"
                self.assertEqual(canonical_ota.read_bytes(), published_ota.read_bytes())
                manifest = json.loads(
                    (repo_root / f"{published_name}-ota.manifest.json").read_text(encoding="utf-8")
                )
                self.assertEqual(
                    f"https://example.invalid/download/{published_name}.firmware.ota.bin",
                    manifest["builds"][0]["ota"]["path"],
                )
                self.assertEqual(expected_ota_md5, manifest["builds"][0]["ota"]["md5"])

            for alias in ("openquatt-test-wifi", "openquatt-test-eth"):
                self.assertFalse((repo_root / f"dist/{alias}.firmware.factory.bin").exists())
                expected_connection = "Wi-Fi" if alias.endswith("-wifi") else "Ethernet"
                manifest = json.loads(
                    (repo_root / f"{alias}-ota.manifest.json").read_text(encoding="utf-8")
                )
                self.assertEqual(
                    f"Test target {expected_connection}",
                    manifest["name"],
                )

    def test_pr_aliases_publish_canonical_bin_with_compatibility_manifests(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            artifact_dir = repo_root / "dist/openquatt-test"
            artifact_dir.mkdir(parents=True)
            (artifact_dir / "firmware.ota.bin").write_bytes(b"ota-firmware")
            targets_file = repo_root / "build_targets.yaml"
            targets_file.write_text(
                """targets:
  - id: test
    status: enabled
    artifact_name: openquatt-test
    artifact_aliases: openquatt-test-wifi,openquatt-test-eth
    manifest_name: openquatt-test-ota.manifest.json
    chip_family: ESP32-S3
    hardware: heatpump_controller_q
    topology: duo
    connection: auto
    display_name: Test target
""",
                encoding="utf-8",
            )

            with (
                mock.patch.object(build_targets, "REPO_ROOT", repo_root),
                mock.patch.object(build_targets, "TARGETS_FILE", targets_file),
            ):
                build_targets.prepare_pr_test_assets(
                    "476",
                    "v1.2.3-pr.476.1+1234567",
                    "1234567890abcdef1234567890abcdef12345678",
                    "https://example.invalid/download",
                    "https://example.invalid/release",
                )

            catalog = json.loads(
                (repo_root / "dist/pr-firmware.json").read_text(encoding="utf-8")
            )
            # Legacy-firmware zonder manifest-capability gebruikt de
            # verbindingsspecifieke bins; daarom bestaan de alias-copies
            # naast de canonieke binary.
            self.assertEqual(
                ["auto", "wifi", "eth"],
                [asset["connection"] for asset in catalog["assets"]],
            )
            self.assertEqual(
                ["Test target", "Test target Wi-Fi", "Test target Ethernet"],
                [asset["display_name"] for asset in catalog["assets"]],
            )
            self.assertEqual(
                "openquatt-test.firmware.ota.bin",
                catalog["assets"][0]["ota_file"],
            )
            self.assertEqual(
                "openquatt-test-ota.manifest.json",
                catalog["assets"][0]["manifest_file"],
            )
            for alias in ("openquatt-test-wifi", "openquatt-test-eth"):
                self.assertEqual(
                    b"ota-firmware",
                    (repo_root / f"dist/{alias}.firmware.ota.bin").read_bytes(),
                )
                self.assertTrue((repo_root / f"dist/{alias}.firmware.ota.bin.md5").is_file())

            manifests = {
                name: json.loads((repo_root / f"dist/{name}").read_text(encoding="utf-8"))
                for name in (
                    "openquatt-test-ota.manifest.json",
                    "openquatt-test-wifi-ota.manifest.json",
                    "openquatt-test-eth-ota.manifest.json",
                )
            }
            expected_ota_path = "https://example.invalid/download/openquatt-test.firmware.ota.bin"
            for manifest in manifests.values():
                self.assertEqual("v1.2.3-pr.476.1+1234567", manifest["version"])
                self.assertEqual(expected_ota_path, manifest["builds"][0]["ota"]["path"])


if __name__ == "__main__":
    unittest.main()
