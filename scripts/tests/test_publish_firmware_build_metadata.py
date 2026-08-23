from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))
sys.path.insert(0, str(Path(__file__).resolve().parent))
SPEC = importlib.util.spec_from_file_location(
    "publish_firmware_build_metadata",
    SCRIPTS / "publish_firmware_build_metadata.py",
)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

from test_reconstruct_firmware_elf import build_record  # noqa: E402


EXPECTED_SOURCE_REPOSITORY = "OpenQuatt/OpenQuatt"
EXPECTED_SOURCE_COMMIT = "1" * 40


def write_record(root: Path) -> Path:
    record = build_record()
    record["identity"]["artifact_name"] = "openquatt-q-duo-wifi"
    filename = (
        f"openquatt-q-duo-wifi.{record['identity']['source_commit']}."
        f"{record['build_id']}.build.json"
    )
    path = root / filename
    path.write_text(json.dumps(record, sort_keys=True) + "\n", encoding="utf-8")
    return path


class PublishFirmwareBuildMetadataTests(unittest.TestCase):
    def test_destination_is_repository_commit_and_build_addressed(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = write_record(Path(temp_dir))
            _, destination = MODULE.load_record(
                path,
                expected_source_repository=EXPECTED_SOURCE_REPOSITORY,
                expected_source_commit=EXPECTED_SOURCE_COMMIT,
            )
            self.assertEqual(
                (
                    "records/OpenQuatt/OpenQuatt/"
                    f"{'1' * 40}/{'2' * 64}/{path.name}"
                ),
                destination,
            )

    def test_non_addressed_filename_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            original = write_record(Path(temp_dir))
            wrong = original.with_name("current.build.json")
            wrong.write_bytes(original.read_bytes())
            with self.assertRaisesRegex(MODULE.PublishError, "not build-addressed"):
                MODULE.load_record(
                    wrong,
                    expected_source_repository=EXPECTED_SOURCE_REPOSITORY,
                    expected_source_commit=EXPECTED_SOURCE_COMMIT,
                )

    def test_record_repository_must_match_trusted_workflow_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = write_record(Path(temp_dir))
            with self.assertRaisesRegex(
                MODULE.PublishError,
                "source repository does not match the trusted workflow identity",
            ):
                MODULE.load_record(
                    path,
                    expected_source_repository="fork/OpenQuatt",
                    expected_source_commit=EXPECTED_SOURCE_COMMIT,
                )

    def test_record_commit_must_match_trusted_workflow_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = write_record(Path(temp_dir))
            with self.assertRaisesRegex(
                MODULE.PublishError,
                "source commit does not match the trusted workflow identity",
            ):
                MODULE.load_record(
                    path,
                    expected_source_repository=EXPECTED_SOURCE_REPOSITORY,
                    expected_source_commit="3" * 40,
                )

    def test_all_records_are_validated_before_the_durable_branch_is_created(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = write_record(Path(temp_dir))
            with mock.patch.object(MODULE, "ensure_branch") as ensure_branch:
                with self.assertRaisesRegex(
                    SystemExit,
                    "source repository does not match the trusted workflow identity",
                ):
                    MODULE.main(
                        [
                            "--github-repository",
                            EXPECTED_SOURCE_REPOSITORY,
                            "--start-sha",
                            EXPECTED_SOURCE_COMMIT,
                            "--expected-source-repository",
                            "fork/OpenQuatt",
                            "--expected-source-commit",
                            EXPECTED_SOURCE_COMMIT,
                            str(path),
                        ]
                    )
            ensure_branch.assert_not_called()

    def test_identical_existing_record_is_an_idempotent_retry(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = write_record(Path(temp_dir))
            with mock.patch.object(
                MODULE,
                "fetch_existing",
                return_value=path.read_bytes(),
            ):
                url = MODULE.publish_record(
                    repository="OpenQuatt/OpenQuatt",
                    branch=MODULE.DEFAULT_BRANCH,
                    path=path,
                    expected_source_repository=EXPECTED_SOURCE_REPOSITORY,
                    expected_source_commit=EXPECTED_SOURCE_COMMIT,
                )
            self.assertIn("raw.githubusercontent.com/OpenQuatt/OpenQuatt", url)
            self.assertTrue(url.endswith(path.name))

    def test_existing_record_is_never_overwritten(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = write_record(Path(temp_dir))
            with mock.patch.object(MODULE, "fetch_existing", return_value=b"different"):
                with self.assertRaisesRegex(MODULE.PublishError, "different rebuild contract"):
                    MODULE.publish_record(
                        repository="OpenQuatt/OpenQuatt",
                        branch=MODULE.DEFAULT_BRANCH,
                        path=path,
                        expected_source_repository=EXPECTED_SOURCE_REPOSITORY,
                        expected_source_commit=EXPECTED_SOURCE_COMMIT,
                    )

    def test_rerun_diagnostics_do_not_replace_the_rebuild_record(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = write_record(Path(temp_dir))
            existing = json.loads(path.read_text(encoding="utf-8"))
            existing["github"] = {"run_id": "first"}
            path_record = json.loads(path.read_text(encoding="utf-8"))
            path_record["github"] = {"run_id": "retry"}
            path.write_text(json.dumps(path_record, sort_keys=True) + "\n", encoding="utf-8")
            with mock.patch.object(
                MODULE,
                "fetch_existing",
                return_value=(json.dumps(existing, sort_keys=True) + "\n").encode(),
            ), mock.patch.object(MODULE, "run_gh") as publish:
                MODULE.publish_record(
                    repository="OpenQuatt/OpenQuatt",
                    branch=MODULE.DEFAULT_BRANCH,
                    path=path,
                    expected_source_repository=EXPECTED_SOURCE_REPOSITORY,
                    expected_source_commit=EXPECTED_SOURCE_COMMIT,
                )
            publish.assert_not_called()

    def test_concurrent_create_conflict_rechecks_the_destination(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            path = write_record(Path(temp_dir))
            failed = subprocess.CompletedProcess(
                args=["gh"],
                returncode=1,
                stdout="",
                stderr="HTTP 409",
            )
            with (
                mock.patch.object(
                    MODULE,
                    "fetch_existing",
                    side_effect=[None, path.read_bytes()],
                ),
                mock.patch.object(MODULE, "run_gh", return_value=failed),
                mock.patch.object(MODULE.time, "sleep"),
            ):
                MODULE.publish_record(
                    repository="OpenQuatt/OpenQuatt",
                    branch=MODULE.DEFAULT_BRANCH,
                    path=path,
                    expected_source_repository=EXPECTED_SOURCE_REPOSITORY,
                    expected_source_commit=EXPECTED_SOURCE_COMMIT,
                )


if __name__ == "__main__":
    unittest.main()
