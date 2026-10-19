from __future__ import annotations

import contextlib
import hashlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock
from urllib.parse import urlparse

import yaml

from scripts import build_targets
from scripts import prepare_q_debug_symbols
from scripts import verify_release as verifier


ROOT = Path(__file__).resolve().parents[2]
TAG = "v1.2.3"
SHA = "1" * 40


class VerifyReleaseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = self.root / "assets"
        self.metadata = self.root / "metadata"
        self.symbols = self.root / "symbols"
        self.symbols_archive = self.root / "symbols.zip"
        self.assets.mkdir()
        self.metadata.mkdir()
        self.targets = build_targets.filter_targets(build_targets.load_targets(), "enabled")
        matrix_file = self.root / "build_targets.yaml"
        matrix_file.write_text((ROOT / "build_targets.yaml").read_text(encoding="utf-8"), encoding="utf-8")
        for field, value in (("REPO_ROOT", self.root), ("TARGETS_FILE", matrix_file)):
            patcher = mock.patch.object(build_targets, field, value)
            patcher.start()
            self.addCleanup(patcher.stop)

        staging = self.root / "symbol-staging"
        for target in self.targets:
            artifact_dir = self.root / "dist" / target["artifact_name"]
            artifact_dir.mkdir(parents=True)
            (artifact_dir / "firmware.ota.bin").write_bytes(f"ota-{target['id']}".encode())
            (artifact_dir / "firmware.factory.bin").write_bytes(f"factory-{target['id']}".encode())
            if target["hardware"] == "heatpump_controller_q":
                symbol_dir = staging / target["id"]
                symbol_dir.mkdir(parents=True)
                (symbol_dir / "firmware.elf").write_bytes(f"elf-{target['id']}".encode())
                (symbol_dir / "openquatt.map").write_text(f"map-{target['id']}", encoding="utf-8")
        self.release_url = f"https://github.com/OpenQuatt/OpenQuatt/releases/tag/{TAG}"
        with contextlib.redirect_stdout(io.StringIO()):
            build_targets.prepare_release_assets(
                TAG, f"https://github.com/OpenQuatt/OpenQuatt/releases/download/{TAG}", self.release_url, True
            )
            prepare_q_debug_symbols.prepare_q_debug_symbols(TAG, SHA, "2026.9.0", staging, self.symbols)
        for path in [*self.root.glob("*-ota.manifest.json"), *(self.root / "dist").glob("*.bin")]:
            (self.assets / path.name).write_bytes(path.read_bytes())
        self.release = {
            "tag_name": TAG, "html_url": self.release_url, "draft": False, "prerelease": False,
            "assets": [self.asset_record(p) for p in sorted(self.assets.iterdir())],
        }
        self.run = {
            "id": 123, "name": "Release Build", "head_branch": TAG, "head_sha": SHA,
            "status": "completed", "conclusion": "success", "updated_at": "2026-01-01T00:00:00Z",
        }
        self.artifact = {
            "name": f"openquatt-q-debug-symbols-{TAG}", "expired": False, "size_in_bytes": 100,
            "workflow_run": {"id": 123, "head_sha": SHA},
            "created_at": "2026-01-01T00:00:00Z", "expires_at": "2026-04-01T00:00:00Z",
        }
        self.pack_symbols()
        self.pages_run = {
            "name": "Pages Deploy", "head_sha": SHA, "status": "completed", "conclusion": "success",
            "event": "workflow_run", "created_at": "2026-01-01T00:00:01Z",
        }
        self.save_metadata()
        self.source = verifier.ReleaseSource(build_targets, TAG, "2026.9.0", 90)
        self.argv = [
            TAG, "--source-sha", SHA, "--source-tree", str(self.root),
            "--metadata-dir", str(self.metadata), "--assets-dir", str(self.assets),
            "--symbols-archive", str(self.symbols_archive),
        ]
        self.args = verifier.create_parser().parse_args(self.argv)
        self.payloads = {"version.json": json.dumps({"version": TAG, "release_url": self.release_url}).encode()}
        self.payloads.update({p.name: p.read_bytes() for p in self.assets.glob("*.factory.bin")})

    @staticmethod
    def write_json(path: Path, value) -> None:
        path.write_text(json.dumps(value), encoding="utf-8")

    @staticmethod
    def asset_record(path: Path) -> dict:
        return {"name": path.name, "size": path.stat().st_size,
                "digest": "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()}

    def save_metadata(self) -> None:
        self.write_json(self.metadata / "release.json", self.release)
        self.write_json(self.metadata / "release-run.json", self.run)
        self.write_json(self.metadata / "artifact-pages.json", [{"artifacts": []}, {"artifacts": [self.artifact]}])
        self.write_json(self.metadata / "pages-run.json", self.pages_run)

    def pack_symbols(self) -> None:
        with zipfile.ZipFile(self.symbols_archive, "w") as bundle:
            for path in sorted(self.symbols.rglob("*")):
                if path.is_file():
                    bundle.write(path, path.relative_to(self.symbols).as_posix())
        self.save_archive_digest()

    def save_archive_digest(self) -> None:
        self.artifact["size_in_bytes"] = self.symbols_archive.stat().st_size
        self.artifact["digest"] = "sha256:" + hashlib.sha256(self.symbols_archive.read_bytes()).hexdigest()

    def update_asset(self, name: str, value: dict) -> None:
        path = self.assets / name
        self.write_json(path, value)
        self.release["assets"] = [self.asset_record(path) if item["name"] == name else item
                                  for item in self.release["assets"]]
        self.save_metadata()

    def urlopen(self, request, timeout):
        self.assertEqual(60, timeout)
        self.assertEqual("https", urlparse(request.full_url).scheme)
        return io.BytesIO(self.payloads[Path(urlparse(request.full_url).path).name])

    def run_verifier(self, argv=None) -> tuple[int, str]:
        output = io.StringIO()
        with (mock.patch.object(verifier, "load_source", return_value=self.source),
              mock.patch.object(verifier.urllib.request, "urlopen", side_effect=self.urlopen),
              contextlib.redirect_stdout(output), contextlib.redirect_stderr(output)):
            result = verifier.main(argv or self.argv)
        return result, output.getvalue()

    def test_complete_release_with_aliases_eol_and_paginated_symbols(self) -> None:
        result, output = self.run_verifier()
        self.assertEqual(0, result, output)
        self.assertIn("byte-identical factory binaries", output)

    def test_missing_unexpected_and_duplicate_assets_fail(self) -> None:
        original = self.release["assets"].copy()
        for assets in (original[1:], [*original, {"name": "unexpected"}], [*original, original[0]]):
            with self.subTest(assets=len(assets)):
                self.release["assets"] = assets
                self.save_metadata()
                self.assertEqual(1, self.run_verifier()[0])

    def test_corrupt_or_missing_file_and_missing_github_digest_fail(self) -> None:
        path = self.assets / self.release["assets"][0]["name"]
        original = path.read_bytes()
        path.write_bytes(b"x" * len(original))
        self.assertIn("SHA256", self.run_verifier()[1])
        path.write_bytes(original)
        self.release["assets"][0].pop("digest")
        self.save_metadata()
        self.assertIn("SHA256", self.run_verifier()[1])
        path.unlink()
        self.assertEqual(1, self.run_verifier()[0])

    def test_manifest_semantics_checked_even_with_matching_asset_digest(self) -> None:
        target = self.targets[0]
        name = target["manifest_name"]
        original = json.loads((self.assets / name).read_text())
        for field, value in (("version", "v9.9.9"), ("name", "wrong profile")):
            with self.subTest(field=field):
                manifest = json.loads(json.dumps(original))
                manifest[field] = value
                self.update_asset(name, manifest)
                self.assertEqual(1, self.run_verifier()[0])
        for field in ("path", "release_url", "md5"):
            with self.subTest(ota=field):
                manifest = json.loads(json.dumps(original))
                manifest["builds"][0]["ota"][field] = "wrong"
                self.update_asset(name, manifest)
                self.assertEqual(1, self.run_verifier()[0])

    def test_alias_cannot_silently_point_to_canonical_url(self) -> None:
        target = self.targets[0]
        alias = build_targets.artifact_names(target)[1]
        name = build_targets.manifest_name_for_artifact(target, alias)
        manifest = json.loads((self.assets / name).read_text())
        manifest["builds"][0]["ota"]["path"] = manifest["builds"][0]["ota"]["path"].replace(alias, target["artifact_name"])
        self.update_asset(name, manifest)
        self.assertIn("OTA URL", self.run_verifier()[1])

    def test_legacy_version_or_firmware_cannot_be_upgraded_to_q(self) -> None:
        name = f"{build_targets.LEGACY_EOL_TARGETS[0]['artifact_name']}-ota.manifest.json"
        manifest = json.loads((self.assets / name).read_text())
        manifest["version"] = TAG
        self.update_asset(name, manifest)
        self.assertIn("Legacy EOL", self.run_verifier()[1])

    def test_draft_prerelease_or_wrong_repo_fails(self) -> None:
        for field, value in (("draft", True), ("prerelease", True), ("html_url", "https://github.com/other/repo")):
            with self.subTest(field=field):
                old = self.release[field]
                self.release[field] = value
                self.save_metadata()
                self.assertEqual(1, self.run_verifier()[0])
                self.release[field] = old

    def test_wrong_failed_or_unfinished_release_run_fails(self) -> None:
        for field, value in (("head_sha", "2" * 40), ("head_branch", "dev"),
                             ("conclusion", "failure"), ("status", "in_progress")):
            with self.subTest(field=field):
                old = self.run[field]
                self.run[field] = value
                self.save_metadata()
                self.assertEqual(1, self.run_verifier()[0])
                self.run[field] = old

    def test_symbol_expiry_run_and_retention_fail_closed(self) -> None:
        for field, value in (("expired", True), ("workflow_run", {"id": 456, "head_sha": SHA}),
                             ("expires_at", "2026-01-08T00:00:00Z")):
            with self.subTest(field=field):
                old = self.artifact[field]
                self.artifact[field] = value
                self.save_metadata()
                self.assertEqual(1, self.run_verifier()[0])
                self.artifact[field] = old

    def test_wrong_or_traversing_symbol_records_fail(self) -> None:
        path = self.symbols / "index.json"
        original = json.loads(path.read_text())
        for field, value in (("source_commit", "2" * 40), ("esphome_version", "2026.10.0"),
                             ("reporting_build_id", "wrong"), ("build_target", "wrong"),
                             ("files", {"elf": "../outside", "map": "../outside"})):
            with self.subTest(field=field):
                index = json.loads(json.dumps(original))
                index["targets"][0][field] = value
                self.write_json(path, index)
                self.pack_symbols()
                self.save_metadata()
                self.assertEqual(1, self.run_verifier()[0])

    def test_missing_map_and_symlinked_asset_fail(self) -> None:
        mapfile = next(self.symbols.glob("*/openquatt.map"))
        content = mapfile.read_bytes()
        mapfile.unlink()
        self.pack_symbols()
        self.save_metadata()
        self.assertEqual(1, self.run_verifier()[0])
        mapfile.write_bytes(content)
        self.pack_symbols()
        self.save_metadata()
        asset = self.assets / self.release["assets"][0]["name"]
        outside = self.root / "outside"
        outside.write_bytes(asset.read_bytes())
        asset.unlink()
        asset.symlink_to(outside)
        self.assertEqual(1, self.run_verifier()[0])

    def test_symbol_zip_digest_and_structure_are_checked(self) -> None:
        original = self.symbols_archive.read_bytes()
        self.symbols_archive.write_bytes(b"x" * len(original))
        self.assertIn("ZIP SHA256", self.run_verifier()[1])
        self.symbols_archive.write_bytes(original)
        self.artifact.pop("digest")
        self.save_metadata()
        self.assertIn("ZIP SHA256", self.run_verifier()[1])
        self.pack_symbols()
        with zipfile.ZipFile(self.symbols_archive, "a") as bundle:
            bundle.writestr("../outside", b"unexpected")
        self.artifact["size_in_bytes"] = self.symbols_archive.stat().st_size
        self.artifact["digest"] = "sha256:" + hashlib.sha256(self.symbols_archive.read_bytes()).hexdigest()
        self.save_metadata()
        self.assertIn("ZIP file set", self.run_verifier()[1])

    def test_duplicate_zip_members_fail(self) -> None:
        with zipfile.ZipFile(self.symbols_archive, "a") as bundle, self.assertWarns(UserWarning):
            bundle.writestr("index.json", b"{}")
        self.save_archive_digest()
        self.save_metadata()
        self.assertIn("Duplicate symbol ZIP", self.run_verifier()[1])

    def test_encrypted_zip_member_fails_closed(self) -> None:
        raw = bytearray(self.symbols_archive.read_bytes())
        central = raw.index(b"PK\x01\x02")
        raw[central + 8] |= 1  # Central-directory general-purpose encryption flag.
        self.symbols_archive.write_bytes(raw)
        self.save_archive_digest()
        self.save_metadata()
        self.assertIn("Encrypted symbol ZIP", self.run_verifier()[1])

    def test_zip_crc_corruption_fails_even_if_archive_digest_matches(self) -> None:
        with zipfile.ZipFile(self.symbols_archive) as bundle:
            info = bundle.infolist()[0]
            self.assertEqual(zipfile.ZIP_STORED, info.compress_type)
        raw = bytearray(self.symbols_archive.read_bytes())
        header = info.header_offset
        name_length = int.from_bytes(raw[header + 26:header + 28], "little")
        extra_length = int.from_bytes(raw[header + 28:header + 30], "little")
        raw[header + 30 + name_length + extra_length] ^= 1
        self.symbols_archive.write_bytes(raw)
        self.save_archive_digest()
        self.save_metadata()
        self.assertIn("CRC", self.run_verifier()[1])

    def test_old_push_pages_failed_pages_and_other_sha_fail(self) -> None:
        for field, value in (("event", "push"), ("created_at", "2025-12-31T23:59:59Z"),
                             ("head_sha", "2" * 40), ("conclusion", "failure")):
            with self.subTest(field=field):
                old = self.pages_run[field]
                self.pages_run[field] = value
                self.save_metadata()
                self.assertEqual(1, self.run_verifier()[0])
                self.pages_run[field] = old

    def test_live_installer_stale_metadata_or_corrupt_binary_fail(self) -> None:
        self.payloads["version.json"] = json.dumps({"version": "v0.0.1", "release_url": self.release_url}).encode()
        self.assertIn("installer version", self.run_verifier()[1])
        self.payloads["version.json"] = json.dumps({"version": TAG, "release_url": self.release_url}).encode()
        name = next(name for name in self.payloads if name.endswith(".bin"))
        original = self.payloads[name]
        for bad in (original[:-1], original + b"x", b"x" * len(original)):
            with self.subTest(length=len(bad)):
                self.payloads[name] = bad
                self.assertEqual(1, self.run_verifier()[0])

    def test_missing_or_malformed_metadata_and_network_error_fail(self) -> None:
        path = self.metadata / "pages-run.json"
        path.write_text("not json", encoding="utf-8")
        self.assertEqual(1, self.run_verifier()[0])
        path.unlink()
        self.assertEqual(1, self.run_verifier()[0])
        self.save_metadata()
        with mock.patch.object(self, "urlopen", side_effect=OSError("network down")):
            self.assertIn("network down", self.run_verifier()[1])

    def test_source_version_and_input_validation_fail(self) -> None:
        self.source.project_version = "v9.9.9"
        self.assertIn("project_version", self.run_verifier()[1])
        self.source.project_version = TAG
        for tag in ("dev-latest", "v1.2.3-rc1", "../v1.2.3"):
            with self.subTest(tag=tag):
                self.assertEqual(1, self.run_verifier([tag, *self.argv[1:]])[0])

    def test_source_loader_rejects_wrong_sha_or_dirty_checkout(self) -> None:
        for values in (["2" * 40], [SHA, " M build_targets.yaml"]):
            with self.subTest(values=values), mock.patch.object(verifier.subprocess, "check_output", side_effect=values):
                with self.assertRaises(verifier.VerificationError):
                    verifier.load_source(ROOT, SHA)

    def test_source_loader_reads_current_pin_and_retention(self) -> None:
        with mock.patch.object(verifier.subprocess, "check_output", side_effect=[SHA, ""]):
            source = verifier.load_source(ROOT, SHA)
        expected_pin = (ROOT / ".github/requirements-esphome.txt").read_text().strip().split("==")[1]
        workflow = yaml.safe_load((ROOT / ".github/workflows/release-build.yml").read_text())
        step = next(s for s in workflow["jobs"]["publish-release"]["steps"]
                    if s.get("name") == "Upload Q debug symbol artifact")
        substitutions = yaml.safe_load((ROOT / "openquatt/oq_substitutions_common.yaml").read_text())
        self.assertEqual(expected_pin, source.esphome_version)
        self.assertEqual(step["with"]["retention-days"], source.retention_days)
        self.assertEqual(substitutions["project_version"], source.project_version)

    def test_cli_remains_fail_closed_with_python_optimization(self) -> None:
        result = subprocess.run(
            [sys.executable, "-O", str(ROOT / "scripts/verify_release.py"), "dev-latest", *self.argv[1:]],
            capture_output=True, text=True, timeout=15, check=False,
        )
        self.assertEqual(1, result.returncode)
        self.assertIn("Expected stable", result.stderr)


if __name__ == "__main__":
    unittest.main()
