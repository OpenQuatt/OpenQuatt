#!/usr/bin/env python3
"""Read-only checks of release downloads, exact symbols and the live installer."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
import urllib.request
import zipfile
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from types import ModuleType
from typing import Sequence

import yaml


class VerificationError(ValueError):
    """A release invariant was not met."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise VerificationError(message)


def load_json(path: Path) -> dict | list:
    value = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(value, (dict, list)), f"Expected JSON object or array: {path}")
    return value


def digest(path: Path, algorithm: str = "sha256") -> str:
    result = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def local_file(root: Path, name: str) -> Path:
    path = root / name
    require(not path.is_symlink() and path.resolve().is_relative_to(root.resolve()), f"Unsafe file path: {name}")
    require(path.is_file() and path.stat().st_size > 0, f"Missing or empty file: {path}")
    return path


@dataclass
class ReleaseSource:
    matrix: ModuleType
    project_version: str
    esphome_version: str
    retention_days: int


def load_source(source_tree: Path, source_sha: str) -> ReleaseSource:
    def git(*args: str) -> str:
        return subprocess.check_output(["git", *args], cwd=source_tree, text=True, timeout=15).strip()

    require(git("rev-parse", "HEAD") == source_sha, "Source tree HEAD differs from --source-sha")
    require(not git("status", "--porcelain", "--untracked-files=no"), "Source tree has tracked modifications")
    spec = importlib.util.spec_from_file_location("release_source_matrix", source_tree / "scripts/build_targets.py")
    require(spec is not None and spec.loader is not None, "Cannot load source target matrix")
    matrix = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(matrix)
    pin = (source_tree / ".github/requirements-esphome.txt").read_text(encoding="utf-8").strip()
    require(pin.startswith("esphome==") and len(pin.splitlines()) == 1, "Expected one pinned ESPHome version")
    workflow = yaml.safe_load((source_tree / ".github/workflows/release-build.yml").read_text(encoding="utf-8"))
    symbol_step = next(step for step in workflow["jobs"]["publish-release"]["steps"]
                       if step.get("name") == "Upload Q debug symbol artifact")
    retention = int(symbol_step["with"]["retention-days"])
    require(retention > 0, "Missing explicit debug-symbol retention")
    substitutions = yaml.safe_load((source_tree / "openquatt/oq_substitutions_common.yaml").read_text(encoding="utf-8"))
    return ReleaseSource(matrix, substitutions["project_version"], pin.split("==", 1)[1], retention)


def successful_run(run: dict, name: str, sha: str) -> None:
    require(run["name"] == name, f"Expected {name} workflow")
    require(run["head_sha"] == sha, f"{name} belongs to a different commit")
    require(run["status"] == "completed" and run["conclusion"] == "success", f"{name} is not successful")


def verify_assets(args: argparse.Namespace, source: ReleaseSource) -> tuple[list[dict], int, int]:
    matrix = source.matrix
    targets = matrix.filter_targets(matrix.load_targets(), "enabled")
    require(bool(targets), "No enabled release targets")
    expected = {name for target in targets for name in matrix.release_asset_names(target)}
    expected.update(f"{target['artifact_name']}-ota.manifest.json" for target in matrix.LEGACY_EOL_TARGETS)
    release = load_json(args.metadata_dir / "release.json")
    require(release["tag_name"] == args.tag, "Release tag mismatch")
    require(release["draft"] is False and release["prerelease"] is False, "Release is not public and stable")
    release_url = f"https://github.com/{args.repo}/releases/tag/{args.tag}"
    require(release["html_url"] == release_url, "Release repository/URL mismatch")
    assets = {asset["name"]: asset for asset in release["assets"]}
    require(len(assets) == len(release["assets"]), "Duplicate release asset names")
    require(set(assets) == expected, f"Asset set mismatch: missing={expected - set(assets)}, extra={set(assets) - expected}")
    for name, asset in assets.items():
        path = local_file(args.assets_dir, name)
        require(path.stat().st_size == asset["size"], f"Asset size mismatch: {name}")
        require(asset.get("digest") == f"sha256:{digest(path)}", f"Asset SHA256 mismatch/missing: {name}")

    base = f"https://github.com/{args.repo}/releases/download/{args.tag}"
    manifests = 0
    for target in targets:
        canonical = local_file(args.assets_dir, f"{target['artifact_name']}.firmware.ota.bin")
        for alias in matrix.artifact_names(target):
            name = matrix.manifest_name_for_artifact(target, alias)
            manifest = load_json(local_file(args.assets_dir, name))
            require(manifest["version"] == args.tag, f"Manifest version mismatch: {name}")
            require(manifest["name"] == matrix.display_name_for_artifact(target, alias), f"Manifest profile mismatch: {name}")
            require(len(manifest["builds"]) == 1, f"Expected one manifest build: {name}")
            build = manifest["builds"][0]
            require(build["chipFamily"] == target.get("chip_family", "ESP32-S3"), f"Manifest chip mismatch: {name}")
            ota = build["ota"]
            require(ota["path"] == f"{base}/{alias}.firmware.ota.bin", f"OTA URL mismatch: {name}")
            require(ota["release_url"] == release_url, f"OTA release URL mismatch: {name}")
            require(ota["md5"] == digest(canonical, "md5"), f"OTA MD5 mismatch: {name}")
            require(digest(local_file(args.assets_dir, f"{alias}.firmware.ota.bin")) == digest(canonical),
                    f"Alias binary differs: {alias}")
            manifests += 1

    for legacy in matrix.LEGACY_EOL_TARGETS:
        name = f"{legacy['artifact_name']}-ota.manifest.json"
        manifest = load_json(local_file(args.assets_dir, name))
        old_tag = matrix.LEGACY_EOL_RELEASE_TAG
        require(manifest["version"] == old_tag, f"Legacy EOL version changed: {name}")
        require(manifest["name"] == legacy["display_name"], f"Legacy profile changed: {name}")
        require(len(manifest["builds"]) == 1, f"Expected one legacy build: {name}")
        build = manifest["builds"][0]
        require(build["chipFamily"] == legacy["chip_family"], f"Legacy chip changed: {name}")
        require(build["ota"]["path"] == (
            f"https://github.com/{args.repo}/releases/download/{old_tag}/{legacy['artifact_name']}.firmware.ota.bin"
        ), f"Legacy OTA URL changed: {name}")
        require(build["ota"]["md5"] == legacy["md5"], f"Legacy MD5 changed: {name}")
        require(build["ota"]["release_url"] == f"https://github.com/{args.repo}/releases/tag/{old_tag}",
                f"Legacy release URL changed: {name}")
        manifests += 1
    return targets, len(assets), manifests


def verify_symbols(args: argparse.Namespace, source: ReleaseSource, targets: list[dict], run: dict) -> int:
    pages = load_json(args.metadata_dir / "artifact-pages.json")
    require(isinstance(pages, list) and bool(pages), "Expected paginated artifact JSON (--paginate --slurp)")
    artifacts = [artifact for page in pages for artifact in page["artifacts"]]
    matches = [a for a in artifacts if a["name"] == f"openquatt-q-debug-symbols-{args.tag}"]
    require(len(matches) == 1, "Expected one exact-release symbol artifact")
    artifact = matches[0]
    require(artifact["expired"] is False and artifact["size_in_bytes"] > 0, "Symbol artifact expired/empty")
    require(artifact["workflow_run"]["id"] == run["id"] and artifact["workflow_run"]["head_sha"] == args.source_sha,
            "Symbol artifact belongs to another run/commit")
    created = datetime.fromisoformat(artifact["created_at"].replace("Z", "+00:00"))
    expires = datetime.fromisoformat(artifact["expires_at"].replace("Z", "+00:00"))
    require(abs((expires - created).total_seconds() / 86400 - source.retention_days) < 0.1,
            "Symbol artifact retention differs from release workflow")
    archive = local_file(args.symbols_archive.parent, args.symbols_archive.name)
    require(archive.stat().st_size == artifact["size_in_bytes"], "Symbol ZIP size mismatch")
    require(artifact.get("digest") == f"sha256:{digest(archive)}", "Symbol ZIP SHA256 mismatch/missing")
    q_targets = {t["id"]: t for t in targets if t["hardware"] == "heatpump_controller_q"}
    with zipfile.ZipFile(archive) as bundle:
        names = bundle.namelist()
        require(len(names) == len(set(names)), "Duplicate symbol ZIP members")
        require(not any(info.flag_bits & 1 for info in bundle.infolist()), "Encrypted symbol ZIP")
        expected = {"index.json"} | {f"{target_id}/{name}" for target_id in q_targets
                                     for name in ("firmware.elf", "openquatt.map")}
        require({info.filename for info in bundle.infolist() if not info.is_dir()} == expected,
                "Symbol ZIP file set mismatch")
        require(bundle.getinfo("index.json").file_size <= 1024 * 1024, "Symbol index too large")
        index = json.loads(bundle.read("index.json"))
        require(index["schema_version"] == 1 and index["release"] == args.tag, "Symbol index release/schema mismatch")
        require(len(index["targets"]) == len(q_targets), "Symbol target count mismatch")
        require({record["target_id"] for record in index["targets"]} == set(q_targets), "Symbol targets mismatch")
        for record in index["targets"]:
            target_id = record["target_id"]
            require(record["source_commit"] == args.source_sha, f"Symbol commit mismatch: {target_id}")
            require(record["esphome_version"] == source.esphome_version, f"Symbol ESPHome pin mismatch: {target_id}")
            require(record["build_target"] == q_targets[target_id]["config"], f"Symbol config mismatch: {target_id}")
            for kind, filename in (("elf", "firmware.elf"), ("map", "openquatt.map")):
                name = f"{target_id}/{filename}"
                require(record["files"][kind] == name, f"Unexpected {kind} path: {target_id}")
                info = bundle.getinfo(name)
                require(info.file_size > 0 and not info.flag_bits & 1, f"Empty/encrypted symbol file: {name}")
                result = hashlib.sha256()
                with bundle.open(name) as stream:
                    for block in iter(lambda: stream.read(1024 * 1024), b""):
                        result.update(block)
                if kind == "elf":
                    require(record["reporting_build_id"] == result.hexdigest(), f"ELF build-ID mismatch: {target_id}")
    return len(q_targets)


def verify_pages(args: argparse.Namespace, targets: list[dict], release_run: dict) -> None:
    pages_run = load_json(args.metadata_dir / "pages-run.json")
    successful_run(pages_run, "Pages Deploy", args.source_sha)
    require(pages_run["event"] == "workflow_run", "Pages is not a post-Release-Build deployment")
    require(pages_run["created_at"] >= release_run["updated_at"], "Pages predates completion of Release Build")
    base = args.pages_base_url or f"https://{args.repo.split('/')[0].lower()}.github.io/{args.repo.split('/')[1]}/firmware/main"
    require(base.startswith("https://"), "Pages URL must use HTTPS")

    def request(name: str):
        return urllib.request.urlopen(urllib.request.Request(
            f"{base.rstrip('/')}/{name}?release={args.tag}", headers={"Cache-Control": "no-cache"}
        ), timeout=60)

    with request("version.json") as response:
        raw = response.read(1024 * 1024 + 1)
    require(len(raw) <= 1024 * 1024, "Pages metadata too large")
    metadata = json.loads(raw)
    require(metadata["version"] == args.tag, "Live installer version mismatch")
    require(metadata["release_url"] == f"https://github.com/{args.repo}/releases/tag/{args.tag}",
            "Live installer release URL mismatch")
    for target in targets:
        name = f"{target['artifact_name']}.firmware.factory.bin"
        expected = local_file(args.assets_dir, name)
        remaining = expected.stat().st_size
        result = hashlib.sha256()
        with request(name) as response:
            while block := response.read(min(1024 * 1024, remaining + 1)):
                remaining -= len(block)
                require(remaining >= 0, f"Live installer binary too large: {name}")
                result.update(block)
        require(remaining == 0 and result.hexdigest() == digest(expected), f"Live installer binary differs: {name}")


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag", help="Stable tag, e.g. v0.56.0")
    parser.add_argument("--source-sha", required=True, help="Expected full release commit SHA")
    parser.add_argument("--source-tree", required=True, type=Path, help="Clean checkout at that exact commit")
    parser.add_argument("--metadata-dir", required=True, type=Path)
    parser.add_argument("--assets-dir", required=True, type=Path)
    parser.add_argument("--symbols-archive", required=True, type=Path, help="Original GitHub Actions artifact ZIP")
    parser.add_argument("--repo", default="OpenQuatt/OpenQuatt")
    parser.add_argument("--pages-base-url", help="Override the public firmware/main URL")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = create_parser().parse_args(argv)
    try:
        require(re.fullmatch(r"v\d+\.\d+\.\d+", args.tag) is not None, "Expected stable vMAJOR.MINOR.PATCH tag")
        require(re.fullmatch(r"[0-9a-f]{40}", args.source_sha) is not None, "Expected full lowercase source SHA")
        require(re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repo) is not None, "Invalid repository")
        source = load_source(args.source_tree, args.source_sha)
        require(source.project_version == args.tag, "Source project_version differs from release tag")
        run = load_json(args.metadata_dir / "release-run.json")
        successful_run(run, "Release Build", args.source_sha)
        require(run["head_branch"] == args.tag, "Release Build belongs to a different tag")
        targets, asset_count, manifest_count = verify_assets(args, source)
        symbol_count = verify_symbols(args, source, targets, run)
        verify_pages(args, targets, run)
    except (OSError, ValueError, KeyError, TypeError, StopIteration, zipfile.BadZipFile, subprocess.SubprocessError) as exc:
        print(f"Release verification: FAIL: {exc}", file=sys.stderr)
        return 1
    print(f"PASS: {asset_count} assets (sizes/SHA256), {manifest_count} manifests, aliases and legacy EOL")
    print(f"PASS: {symbol_count} exact-SHA ELF/map sets, ESPHome {source.esphome_version}, {source.retention_days}-day retention")
    print(f"PASS: post-release Pages, live {args.tag}, {len(targets)} byte-identical factory binaries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
