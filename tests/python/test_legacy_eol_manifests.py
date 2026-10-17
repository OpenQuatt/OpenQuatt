from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts import build_targets


class LegacyEolManifestTests(unittest.TestCase):
    def test_release_keeps_legacy_manifests_on_v050_assets(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            artifact_dir = repo_root / "dist" / "openquatt-heatpump-controller-q-single"
            artifact_dir.mkdir(parents=True)
            (artifact_dir / "firmware.ota.bin").write_bytes(b"q-ota")
            (artifact_dir / "firmware.factory.bin").write_bytes(b"q-factory")
            targets_file = repo_root / "build_targets.yaml"
            targets_file.write_text(
                """targets:
  - id: heatpump_controller_q_single
    status: enabled
    artifact_name: openquatt-heatpump-controller-q-single
    hardware: heatpump_controller_q
    topology: single
    connection: auto
    display_name: OpenQuatt Heatpump Controller Q Single
""",
                encoding="utf-8",
            )

            with (
                mock.patch.object(build_targets, "REPO_ROOT", repo_root),
                mock.patch.object(build_targets, "TARGETS_FILE", targets_file),
            ):
                build_targets.prepare_release_assets(
                    "v0.51.0",
                    "https://github.com/OpenQuatt/OpenQuatt/releases/download/v0.51.0",
                    "https://github.com/OpenQuatt/OpenQuatt/releases/tag/v0.51.0",
                    include_legacy_eol_manifests=True,
                )
                release_files = list(build_targets.filter_targets(build_targets.load_targets(), "enabled"))

            self.assertEqual(1, len(release_files))
            manifest_path = repo_root / "openquatt-waveshare-single-wifi-ota.manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            ota = manifest["builds"][0]["ota"]
            self.assertEqual("v0.50.0", manifest["version"])
            self.assertEqual(
                "https://github.com/OpenQuatt/OpenQuatt/releases/download/v0.50.0/"
                "openquatt-waveshare-single-wifi.firmware.ota.bin",
                ota["path"],
            )
            self.assertEqual("b454437984d1c0725ee1b1127ae2f22c", ota["md5"])
            self.assertNotIn("heatpump-controller-q", ota["path"])
            with mock.patch.object(build_targets, "TARGETS_FILE", targets_file):
                args = build_targets.create_parser().parse_args(
                    ["release-files", "--include-legacy-eol-manifests"]
                )
                with mock.patch("sys.stdout") as stdout:
                    args.func(args)
            self.assertTrue(
                any(
                    "openquatt-waveshare-single-wifi-ota.manifest.json" in call.args[0]
                    for call in stdout.write.call_args_list
                )
            )


if __name__ == "__main__":
    unittest.main()
