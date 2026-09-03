from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts import build_targets
from scripts import prepare_q_debug_symbols as debug_symbols


ROOT = Path(__file__).resolve().parents[2]
REUSABLE_BUILD_WORKFLOW = (ROOT / ".github/workflows/esphome-build.yml").read_text(encoding="utf-8")
RELEASE_WORKFLOW = (ROOT / ".github/workflows/release-build.yml").read_text(encoding="utf-8")


class ReleaseDebugSymbolsTests(unittest.TestCase):
    def test_release_only_enables_q_debug_symbols(self) -> None:
        self.assertIn("include_debug_symbols:", REUSABLE_BUILD_WORKFLOW)
        self.assertIn("include_debug_symbols: true", RELEASE_WORKFLOW)
        self.assertIn(
            "inputs.include_debug_symbols && matrix.target.hardware == 'heatpump_controller_q'",
            REUSABLE_BUILD_WORKFLOW,
        )
        self.assertIn("retention-days: 1", REUSABLE_BUILD_WORKFLOW)
        self.assertIn("merge-multiple: true", RELEASE_WORKFLOW)
        self.assertIn("name: openquatt-q-debug-symbols-${{ github.ref_name }}", RELEASE_WORKFLOW)
        self.assertIn("retention-days: 90", RELEASE_WORKFLOW)

    def test_prepare_q_debug_symbols_indexes_only_q_targets(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            artifact_root = root / "staging"
            output_dir = root / "output"
            targets_file = root / "build_targets.yaml"
            targets_file.write_text(
                """targets:
  - id: q_single
    status: enabled
    hardware: heatpump_controller_q
    config: configs/heatpump_controller_q/single.yaml
  - id: q_duo
    status: enabled
    hardware: heatpump_controller_q
    config: configs/heatpump_controller_q/duo.yaml
  - id: waveshare_single
    status: enabled
    hardware: waveshare
    config: configs/waveshare/single_wifi.yaml
""",
                encoding="utf-8",
            )

            expected_hashes: dict[str, str] = {}
            for target_id in ("q_single", "q_duo"):
                target_dir = artifact_root / target_id
                target_dir.mkdir(parents=True)
                elf_bytes = f"elf-{target_id}".encode()
                (target_dir / "firmware.elf").write_bytes(elf_bytes)
                (target_dir / "openquatt.map").write_text(f"map-{target_id}\n", encoding="utf-8")
                expected_hashes[target_id] = hashlib.sha256(elf_bytes).hexdigest()

            with mock.patch.object(build_targets, "TARGETS_FILE", targets_file):
                debug_symbols.prepare_q_debug_symbols(
                    "v1.2.3",
                    "1234567890abcdef1234567890abcdef12345678",
                    "2026.8.2",
                    artifact_root,
                    output_dir,
                )

            index = json.loads((output_dir / "index.json").read_text(encoding="utf-8"))
            self.assertEqual(1, index["schema_version"])
            self.assertEqual("v1.2.3", index["release"])
            self.assertEqual(["q_single", "q_duo"], [record["target_id"] for record in index["targets"]])

            for record in index["targets"]:
                target_id = record["target_id"]
                self.assertEqual(expected_hashes[target_id], record["reporting_build_id"])
                self.assertEqual(
                    "1234567890abcdef1234567890abcdef12345678",
                    record["source_commit"],
                )
                self.assertEqual("2026.8.2", record["esphome_version"])
                self.assertEqual(f"{target_id}/firmware.elf", record["files"]["elf"])
                self.assertEqual(f"{target_id}/openquatt.map", record["files"]["map"])
                self.assertTrue((output_dir / record["files"]["elf"]).is_file())
                self.assertTrue((output_dir / record["files"]["map"]).is_file())

            self.assertFalse((output_dir / "waveshare_single").exists())

    def test_missing_q_mapfile_fails_packaging(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            artifact_root = root / "staging"
            target_dir = artifact_root / "q_single"
            target_dir.mkdir(parents=True)
            (target_dir / "firmware.elf").write_bytes(b"elf")

            targets_file = root / "build_targets.yaml"
            targets_file.write_text(
                """targets:
  - id: q_single
    status: enabled
    hardware: heatpump_controller_q
    config: configs/heatpump_controller_q/single.yaml
""",
                encoding="utf-8",
            )

            with mock.patch.object(build_targets, "TARGETS_FILE", targets_file):
                with self.assertRaisesRegex(SystemExit, "openquatt.map"):
                    debug_symbols.prepare_q_debug_symbols(
                        "v1.2.3",
                        "1234567890abcdef1234567890abcdef12345678",
                        "2026.8.2",
                        artifact_root,
                        root / "output",
                    )


if __name__ == "__main__":
    unittest.main()
