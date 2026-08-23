from __future__ import annotations

import hashlib
import importlib.util
import json
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))
MODULE_PATH = SCRIPTS / "reconstruct_firmware_elf.py"
SPEC = importlib.util.spec_from_file_location("reconstruct_firmware_elf", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

from firmware_build_metadata import (  # noqa: E402
    ESP_APP_DESC_ELF_SHA_OFFSET,
    ESP_APP_DESC_MAGIC,
    ESP_IMAGE_HEADER_SIZE,
    ESP_IMAGE_MAGIC,
    ESP_IMAGE_SEGMENT_HEADER_SIZE,
)


def firmware_with_build_id(build_id: str) -> bytes:
    size = ESP_IMAGE_HEADER_SIZE + ESP_IMAGE_SEGMENT_HEADER_SIZE + ESP_APP_DESC_ELF_SHA_OFFSET + 32
    data = bytearray(size)
    data[0] = ESP_IMAGE_MAGIC
    app_desc = ESP_IMAGE_HEADER_SIZE + ESP_IMAGE_SEGMENT_HEADER_SIZE
    data[app_desc : app_desc + 4] = ESP_APP_DESC_MAGIC.to_bytes(4, "little")
    offset = app_desc + ESP_APP_DESC_ELF_SHA_OFFSET
    data[offset : offset + 32] = bytes.fromhex(build_id)
    return bytes(data)


def input_record(content: str, *, include_content: bool = False) -> dict[str, object]:
    encoded = content.encode("utf-8")
    record: dict[str, object] = {
        "sha256": hashlib.sha256(encoded).hexdigest(),
        "size": len(encoded),
    }
    if include_content:
        record["content"] = content
    return record


def portable_idf_record(
    content: str,
    *,
    manifest_hash: str | None = None,
) -> dict[str, object]:
    record = input_record(content, include_content=True)
    record["path_root"] = MODULE.PORTABLE_SOURCE_ROOT
    if manifest_hash is not None:
        record["manifest_hash"] = manifest_hash
    return record


def build_record() -> dict[str, object]:
    commit = "1" * 40
    return {
        "schema_version": 1,
        "build_id": "2" * 64,
        "identity": {
            "source_repository": "OpenQuatt/OpenQuatt",
            "source_commit": commit,
            "build_epoch": 123,
            "target_config": "configs/test.yaml",
            "firmware_version": "v1.2.3",
            "release_channel": "dev",
        },
        "substitutions": {
            "build_source_repository": "OpenQuatt/OpenQuatt",
            "build_source_commit": commit,
            "build_target": "configs/test.yaml",
            "build_epoch": "123",
            "project_version": "v1.2.3",
            "release_channel": "dev",
        },
        "inputs": {
            "deterministic_wrapper": input_record("wrapper"),
            "esphome_requirements": input_record("esphome==1\n", include_content=True),
            "npm_lock": input_record("npm lock"),
            "idf_dependencies_lock": portable_idf_record(
                f"path: {MODULE.PORTABLE_SOURCE_ROOT}/component\n"
                f"manifest_hash: {MODULE.PORTABLE_IDF_MANIFEST_HASH}\n",
                manifest_hash="5" * 64,
            ),
            "idf_component_manifest": portable_idf_record(
                f"override_path: {MODULE.PORTABLE_SOURCE_ROOT}/component\n"
            ),
            "sdkconfig": input_record("sdkconfig"),
            "web_assets": {
                "openquatt/web/css/openquatt-app.css": input_record("css"),
                "openquatt/web/js/openquatt-app.js": input_record("js"),
            },
        },
    }


def rebuild_request(**overrides: object) -> MODULE.RebuildRequest:
    values: dict[str, object] = {
        "source_repository": "OpenQuatt/OpenQuatt",
        "source_commit": "1" * 40,
        "build_epoch": 123,
        "target_config": "configs/test.yaml",
        "expected_build_id": "2" * 64,
        "firmware_version": "v1.2.3",
        "release_channel": "dev",
        "substitutions": {},
        "idf_dependencies_lock": MODULE.CapturedTextInput(
            f"path: {MODULE.PORTABLE_SOURCE_ROOT}/component\n"
            f"manifest_hash: {MODULE.PORTABLE_IDF_MANIFEST_HASH}\n",
            hashlib.sha256(
                (
                    f"path: {MODULE.PORTABLE_SOURCE_ROOT}/component\n"
                    f"manifest_hash: {MODULE.PORTABLE_IDF_MANIFEST_HASH}\n"
                ).encode()
            ).hexdigest(),
            "5" * 64,
        ),
        "idf_component_manifest": MODULE.CapturedTextInput(
            f"override_path: {MODULE.PORTABLE_SOURCE_ROOT}/component\n",
            hashlib.sha256(
                f"override_path: {MODULE.PORTABLE_SOURCE_ROOT}/component\n".encode()
            ).hexdigest(),
        ),
        "deterministic_wrapper_sha256": hashlib.sha256(b"wrapper").hexdigest(),
        "esphome_requirements": MODULE.CapturedTextInput(
            "esphome==1\n", hashlib.sha256(b"esphome==1\n").hexdigest()
        ),
        "npm_lock_sha256": hashlib.sha256(b"npm lock").hexdigest(),
        "sdkconfig_sha256": hashlib.sha256(b"sdkconfig").hexdigest(),
        "web_asset_sha256": {
            "openquatt/web/css/openquatt-app.css": hashlib.sha256(b"css").hexdigest(),
            "openquatt/web/js/openquatt-app.js": hashlib.sha256(b"js").hexdigest(),
        },
    }
    values.update(overrides)
    return MODULE.RebuildRequest(**values)


class ReconstructFirmwareElfTests(unittest.TestCase):
    def test_record_identity_must_match_required_substitutions(self) -> None:
        record = build_record()
        request = MODULE.request_from_record(record)
        self.assertEqual("2" * 64, request.expected_build_id)
        self.assertEqual("OpenQuatt/OpenQuatt", request.source_repository)

        record["substitutions"]["build_epoch"] = "124"
        with self.assertRaisesRegex(MODULE.ReconstructionError, "does not match"):
            MODULE.request_from_record(record)

    def test_metadata_requires_complete_hashed_rebuild_inputs(self) -> None:
        record = build_record()
        del record["inputs"]["idf_dependencies_lock"]
        with self.assertRaisesRegex(MODULE.ReconstructionError, "idf_dependencies_lock"):
            MODULE.request_from_record(record)

        record = build_record()
        record["inputs"]["idf_component_manifest"]["content"] = "tampered"
        with self.assertRaisesRegex(MODULE.ReconstructionError, "does not match"):
            MODULE.request_from_record(record)

    def test_compile_command_passes_sorted_substitutions_as_arguments(self) -> None:
        command = MODULE.compile_command(
            "/python",
            Path("/wrapper.py"),
            Path("/source/config.yaml"),
            {"z_value": "last", "a_value": "first"},
        )
        self.assertEqual(
            [
                "/python", "/wrapper.py", "-s", "a_value", "first", "-s",
                "z_value", "last", "compile", "/source/config.yaml",
            ],
            command,
        )

    def test_elf_is_copied_only_after_both_hashes_match(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            build = root / "build"
            build.mkdir()
            elf = b"verified elf"
            build_id = hashlib.sha256(elf).hexdigest()
            (build / "firmware.elf").write_bytes(elf)
            (build / "firmware.ota.bin").write_bytes(firmware_with_build_id(build_id))
            output = root / "out" / "firmware.elf"
            self.assertEqual(build_id, MODULE.verify_and_copy_elf(build, build_id, output))
            self.assertEqual(elf, output.read_bytes())

    def test_mismatched_expected_hash_fails_closed_without_output(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            build = root / "build"
            build.mkdir()
            elf = b"wrong elf"
            actual = hashlib.sha256(elf).hexdigest()
            (build / "firmware.elf").write_bytes(elf)
            (build / "firmware.ota.bin").write_bytes(firmware_with_build_id(actual))
            output = root / "firmware.elf"
            with self.assertRaisesRegex(MODULE.ReconstructionError, "refusing symbolization"):
                MODULE.verify_and_copy_elf(build, "f" * 64, output)
            self.assertFalse(output.exists())

    def test_partial_captured_identity_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            metadata = Path(temp_dir) / "record.json"
            metadata.write_text(json.dumps(build_record()), encoding="utf-8")
            args = MODULE.create_parser().parse_args(
                [
                    "--source-root", "/source", "--metadata", str(metadata),
                    "--captured-source-commit", "1" * 40,
                ]
            )
            with self.assertRaisesRegex(MODULE.ReconstructionError, "every --captured"):
                MODULE.request_from_args(args)

    def test_every_captured_build_field_is_cross_checked(self) -> None:
        request = MODULE.request_from_record(build_record())
        captured: dict[str, object] = {
            "build_id": request.expected_build_id,
            "source_repository": request.source_repository,
            "source_commit": request.source_commit,
            "build_epoch": request.build_epoch,
            "target_config": request.target_config,
            "firmware_version": request.firmware_version,
            "release_channel": request.release_channel,
        }
        MODULE.cross_check_captured_identity(request, **captured)
        replacements: dict[str, object] = {
            "build_id": "3" * 64,
            "source_repository": "fork/OpenQuatt",
            "source_commit": "4" * 40,
            "build_epoch": 124,
            "target_config": "configs/other.yaml",
            "firmware_version": "v9.9.9",
            "release_channel": "main",
        }
        for field, replacement in replacements.items():
            changed = dict(captured)
            changed[field] = replacement
            with self.subTest(field=field), self.assertRaisesRegex(
                MODULE.ReconstructionError, "does not match"
            ):
                MODULE.cross_check_captured_identity(request, **changed)

    def test_source_wrapper_and_lockfiles_are_verified(self) -> None:
        request = rebuild_request()
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "scripts").mkdir()
            (root / ".github").mkdir()
            (root / "scripts" / "esphome_deterministic.py").write_text("wrapper")
            (root / ".github" / "requirements-esphome.txt").write_text("esphome==1\n")
            (root / "package-lock.json").write_text("npm lock")
            with mock.patch.object(
                MODULE, "checked_out_commit", return_value=request.source_commit
            ):
                self.assertEqual(
                    root.resolve() / "scripts" / "esphome_deterministic.py",
                    MODULE.verify_source_inputs(root, request),
                )
                (root / "package-lock.json").write_text("changed")
                with self.assertRaisesRegex(MODULE.ReconstructionError, "npm lockfile"):
                    MODULE.verify_source_inputs(root, request)

    def test_idf_dependency_lock_is_seeded_with_its_manifest(self) -> None:
        request = rebuild_request()
        with tempfile.TemporaryDirectory() as temp_dir:
            source_root = Path(temp_dir).resolve()
            build_root = source_root / ".esphome" / "build" / "target"
            MODULE.seed_idf_dependency_state(build_root, source_root, request)
            self.assertEqual(
                f"path: {source_root}/component\nmanifest_hash: {'5' * 64}\n",
                (build_root / "dependencies.lock").read_text(),
            )
            self.assertEqual(
                f"override_path: {source_root}/component\n",
                (build_root / "src" / "idf_component.yml").read_text(),
            )

    def test_generated_lock_manifest_and_sdkconfig_are_verified(self) -> None:
        request = rebuild_request()
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "src").mkdir()
            (root / "dependencies.lock").write_text("idf lock")
            (root / "src" / "idf_component.yml").write_text("idf manifest")
            (root / "sdkconfig.target").write_text("sdkconfig")
            (root / "dependencies.lock").write_text(
                f"path: {root.resolve()}/component\nmanifest_hash: {'8' * 64}\n"
            )
            (root / "src" / "idf_component.yml").write_text(
                f"override_path: {root.resolve()}/component\n"
            )
            MODULE.verify_generated_build_inputs(root, root, request)
            (root / "dependencies.lock").write_text("updated")
            with self.assertRaisesRegex(MODULE.ReconstructionError, "dependency lock"):
                MODULE.verify_generated_build_inputs(root, root, request)


if __name__ == "__main__":
    unittest.main()
