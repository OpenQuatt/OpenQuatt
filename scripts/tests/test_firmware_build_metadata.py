from __future__ import annotations

import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts" / "firmware_build_metadata.py"
SPEC = importlib.util.spec_from_file_location("firmware_build_metadata", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def make_firmware(build_id: str) -> bytes:
    size = (
        MODULE.ESP_IMAGE_HEADER_SIZE
        + MODULE.ESP_IMAGE_SEGMENT_HEADER_SIZE
        + MODULE.ESP_APP_DESC_ELF_SHA_OFFSET
        + 32
    )
    data = bytearray(size)
    data[0] = MODULE.ESP_IMAGE_MAGIC
    app_desc = MODULE.ESP_IMAGE_HEADER_SIZE + MODULE.ESP_IMAGE_SEGMENT_HEADER_SIZE
    data[app_desc : app_desc + 4] = MODULE.ESP_APP_DESC_MAGIC.to_bytes(4, "little")
    sha_offset = app_desc + MODULE.ESP_APP_DESC_ELF_SHA_OFFSET
    data[sha_offset : sha_offset + 32] = bytes.fromhex(build_id)
    return bytes(data)


class FirmwareBuildMetadataTests(unittest.TestCase):
    def test_build_time_is_formatted_as_deterministic_utc(self) -> None:
        self.assertEqual(
            "2026-08-23 00:00:00 +0000",
            MODULE.format_build_time(1787443200),
        )

    def test_embedded_elf_sha_is_read_from_app_descriptor(self) -> None:
        expected = hashlib.sha256(b"elf").hexdigest()
        with tempfile.TemporaryDirectory() as temp_dir:
            firmware = Path(temp_dir) / "firmware.bin"
            firmware.write_bytes(make_firmware(expected))
            self.assertEqual(expected, MODULE.read_embedded_elf_sha256(firmware))

    def test_invalid_app_descriptor_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            firmware = Path(temp_dir) / "firmware.bin"
            firmware.write_bytes(bytes([MODULE.ESP_IMAGE_MAGIC]) + bytes(255))
            with self.assertRaises(MODULE.BuildMetadataError):
                MODULE.read_embedded_elf_sha256(firmware)

    def test_substitutions_are_validated_and_sorted(self) -> None:
        self.assertEqual(
            {"build_epoch": "123", "build_target": "configs/test.yaml"},
            MODULE.parse_substitutions(
                ["build_target=configs/test.yaml", "build_epoch=123"]
            ),
        )
        for substitutions in (["missing-separator"], ["UPPER=value"], ["a=1", "a=2"]):
            with self.subTest(substitutions=substitutions), self.assertRaises(
                MODULE.BuildMetadataError
            ):
                MODULE.parse_substitutions(substitutions)

    def test_source_commit_must_be_full_sha(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            with self.assertRaisesRegex(MODULE.BuildMetadataError, "full 40-character"):
                MODULE.validate_source_commit(Path(temp_dir), "1234567")

    def test_source_commit_requires_a_git_checkout(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir, self.assertRaisesRegex(
            MODULE.BuildMetadataError,
            "Cannot resolve checked-out",
        ):
            MODULE.validate_source_commit(Path(temp_dir), "1" * 40)

    def test_source_repository_must_be_owner_and_name(self) -> None:
        self.assertEqual(
            "contributor/OpenQuatt",
            MODULE.normalize_repository("contributor/OpenQuatt"),
        )
        for value in (
            "OpenQuatt",
            "https://github.com/OpenQuatt/OpenQuatt",
            "owner/repo/extra",
            "../repo",
            "owner/..",
        ):
            with self.subTest(value=value), self.assertRaises(MODULE.BuildMetadataError):
                MODULE.normalize_repository(value)

    def test_target_paths_and_artifact_names_are_restricted(self) -> None:
        self.assertEqual(
            "configs/heatpump_controller_q/duo_wifi.yaml",
            MODULE.normalize_target_config("configs/heatpump_controller_q/duo_wifi.yaml"),
        )
        self.assertEqual(
            "openquatt-q-duo-wifi",
            MODULE.normalize_identifier("openquatt-q-duo-wifi", "artifact_name"),
        )
        for value in ("../secrets.yaml", "/tmp/config.yaml", "configs/not-yaml.txt"):
            with self.subTest(value=value), self.assertRaises(MODULE.BuildMetadataError):
                MODULE.normalize_target_config(value)
        for value in ("../../result", "UPPER", "contains space"):
            with self.subTest(value=value), self.assertRaises(MODULE.BuildMetadataError):
                MODULE.normalize_identifier(value, "artifact_name")

    def test_metadata_filename_is_commit_and_build_id_addressed(self) -> None:
        commit = "1" * 40
        build_id = "2" * 64
        self.assertEqual(
            f"openquatt-q-duo-wifi.{commit}.{build_id}.build.json",
            MODULE.metadata_filename(
                {
                    "build_id": build_id,
                    "identity": {
                        "artifact_name": "openquatt-q-duo-wifi",
                        "source_commit": commit,
                    },
                }
            ),
        )

    def test_idf_metadata_replaces_workspace_paths_and_manifest_hash(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir).resolve()
            dependency_lock = root / "dependencies.lock"
            dependency_lock.write_text(
                f"source:\n  path: {root}/components/local\n"
                f"manifest_hash: {'3' * 64}\n",
                encoding="utf-8",
            )
            record = MODULE.portable_idf_file_metadata(
                dependency_lock,
                source_root=root,
                mask_manifest_hash=True,
            )
            self.assertNotIn(str(root), record["content"])
            self.assertIn(MODULE.PORTABLE_SOURCE_ROOT, record["content"])
            self.assertIn(MODULE.PORTABLE_IDF_MANIFEST_HASH, record["content"])
            self.assertEqual("3" * 64, record["manifest_hash"])


if __name__ == "__main__":
    unittest.main()
