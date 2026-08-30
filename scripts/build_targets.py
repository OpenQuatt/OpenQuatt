#!/usr/bin/env python3
"""Helpers for the OpenQuatt build-target matrix."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path
from typing import Sequence


REPO_ROOT = Path(__file__).resolve().parents[1]
TARGETS_FILE = REPO_ROOT / "build_targets.yaml"


def _parse_scalar(value: str) -> str:
    value = value.strip()
    if (value.startswith('"') and value.endswith('"')) or (value.startswith("'") and value.endswith("'")):
        return value[1:-1]
    return value


def load_targets() -> list[dict[str, str]]:
    """Parse the intentionally simple build_targets.yaml without extra deps."""
    targets: list[dict[str, str]] = []
    current: dict[str, str] | None = None
    in_targets = False

    for line_number, raw_line in enumerate(TARGETS_FILE.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.rstrip()
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if stripped == "targets:":
            in_targets = True
            continue
        if not in_targets:
            raise SystemExit(f"{TARGETS_FILE}:{line_number}: expected 'targets:' before target entries")
        if line.startswith("  - "):
            if current is not None:
                targets.append(current)
            current = {}
            key_value = line[4:]
        elif line.startswith("    "):
            if current is None:
                raise SystemExit(f"{TARGETS_FILE}:{line_number}: key outside target entry")
            key_value = line[4:]
        else:
            raise SystemExit(f"{TARGETS_FILE}:{line_number}: unsupported matrix syntax: {raw_line}")

        if ":" not in key_value:
            raise SystemExit(f"{TARGETS_FILE}:{line_number}: expected key/value pair")
        key, value = key_value.split(":", 1)
        current[key.strip()] = _parse_scalar(value)

    if current is not None:
        targets.append(current)
    if not targets:
        raise SystemExit(f"No targets found in {TARGETS_FILE}")
    return targets


def filter_targets(targets: list[dict[str, str]], status: str) -> list[dict[str, str]]:
    if status == "all":
        return targets
    return [target for target in targets if target.get("status") == status]


def artifact_names(target: dict[str, str]) -> list[str]:
    """Return the canonical artifact name followed by compatibility aliases."""
    names = [target["artifact_name"]]
    aliases = target.get("artifact_aliases", "")
    names.extend(alias.strip() for alias in aliases.split(",") if alias.strip())
    if len(names) != len(set(names)):
        raise SystemExit(f"Duplicate artifact name or alias for target {target['id']}")
    return names


def manifest_name_for_artifact(target: dict[str, str], artifact_name: str) -> str:
    if artifact_name == target["artifact_name"]:
        return target["manifest_name"]
    return f"{artifact_name}-ota.manifest.json"


def connection_for_artifact(target: dict[str, str], artifact_name: str) -> str:
    if artifact_name.endswith("-wifi"):
        return "wifi"
    if artifact_name.endswith("-eth"):
        return "eth"
    return target["connection"]


def display_name_for_artifact(target: dict[str, str], artifact_name: str) -> str:
    connection = connection_for_artifact(target, artifact_name)
    if target["connection"] == "auto" and connection == "wifi":
        return f"{target['display_name']} Wi-Fi"
    if target["connection"] == "auto" and connection == "eth":
        return f"{target['display_name']} Ethernet"
    return target["display_name"]


def release_asset_names(target: dict[str, str]) -> list[str]:
    """Return every file name that should exist on a release for this target."""
    names: list[str] = []
    for published_name in artifact_names(target):
        names.extend(
            [
                f"{published_name}.firmware.ota.bin",
                f"{published_name}.firmware.factory.bin",
            ]
        )
    names.extend(manifest_name_for_artifact(target, published_name) for published_name in artifact_names(target))
    return names


def md5sum(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_artifact_dir(
    dist_dir: Path,
    artifact_name: str,
    excluded_artifact_names: Sequence[str] = (),
) -> Path:
    direct = dist_dir / artifact_name
    if direct.is_dir():
        if direct.is_symlink():
            raise SystemExit(f"Artifact directory must not be a symlink: {direct}")
        return direct

    matches = sorted(
        path
        for path in dist_dir.glob(f"{artifact_name}-*")
        if path.is_dir()
        and not path.is_symlink()
        and not any(
            path.name == excluded_name or path.name.startswith(f"{excluded_name}-")
            for excluded_name in excluded_artifact_names
        )
    )
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise SystemExit(f"Missing downloaded artifact directory for {artifact_name} under {dist_dir}")
    names = ", ".join(str(path) for path in matches)
    raise SystemExit(f"Ambiguous artifact directories for {artifact_name}: {names}")


def prepare_release_assets(version: str, base_url: str, release_url: str) -> None:
    dist_dir = REPO_ROOT / "dist"
    dist_dir.mkdir(parents=True, exist_ok=True)

    for target in filter_targets(load_targets(), "enabled"):
        artifact_name = target["artifact_name"]
        artifact_dir = find_artifact_dir(dist_dir, artifact_name, artifact_names(target)[1:])

        ota_source = artifact_dir / "firmware.ota.bin"
        factory_source = artifact_dir / "firmware.factory.bin"
        if not ota_source.is_file() or not factory_source.is_file():
            raise SystemExit(f"Artifact {artifact_name} is missing firmware.ota.bin or firmware.factory.bin")

        for published_name in artifact_names(target):
            published_display_name = display_name_for_artifact(target, published_name)
            ota_name = f"{published_name}.firmware.ota.bin"
            factory_name = f"{published_name}.firmware.factory.bin"
            ota_dest = dist_dir / ota_name
            factory_dest = dist_dir / factory_name
            shutil.copy2(ota_source, ota_dest)
            shutil.copy2(factory_source, factory_dest)

            manifest = {
                "name": published_display_name,
                "version": version,
                "builds": [
                    {
                        "chipFamily": target["chip_family"],
                        "ota": {
                            "path": f"{base_url}/{ota_name}",
                            "md5": md5sum(ota_dest),
                            "release_url": release_url,
                            "summary": f"{published_display_name} firmware {version}",
                        },
                    },
                ],
            }
            manifest_path = REPO_ROOT / manifest_name_for_artifact(target, published_name)
            manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def prepare_pr_test_assets(
    pr_number: str,
    version: str,
    head_sha: str,
    base_url: str,
    release_url: str,
    artifact_root: Path | None = None,
) -> None:
    dist_dir = REPO_ROOT / "dist"
    dist_dir.mkdir(parents=True, exist_ok=True)
    artifact_root = artifact_root or dist_dir
    short_sha = head_sha[:7] if head_sha else ""
    assets: list[dict[str, str]] = []

    for target in filter_targets(load_targets(), "enabled"):
        artifact_name = target["artifact_name"]
        artifact_dir = find_artifact_dir(artifact_root, artifact_name, artifact_names(target)[1:])
        ota_source = artifact_dir / "firmware.ota.bin"
        if ota_source.is_symlink() or not ota_source.is_file():
            raise SystemExit(f"Artifact {artifact_name} is missing firmware.ota.bin")

        for published_name in artifact_names(target):
            published_display_name = display_name_for_artifact(target, published_name)
            ota_name = f"{published_name}.firmware.ota.bin"
            ota_dest = dist_dir / ota_name
            shutil.copy2(ota_source, ota_dest)
            digest = md5sum(ota_dest)
            md5_name = f"{ota_name}.md5"
            (dist_dir / md5_name).write_text(f"{digest}\n", encoding="utf-8")

            assets.append(
                {
                    "target": target["id"],
                    "hardware": target["hardware"],
                    "topology": target["topology"],
                    "connection": connection_for_artifact(target, published_name),
                    "display_name": published_display_name,
                    "ota_file": ota_name,
                    "ota_url": f"{base_url}/{ota_name}",
                    "md5_file": md5_name,
                    "md5_url": f"{base_url}/{md5_name}",
                    "md5": digest,
                }
            )

    catalog = {
        "pr": str(pr_number),
        "version": version,
        "head_sha": head_sha,
        "short_sha": short_sha,
        "release_url": release_url,
        "assets": assets,
    }
    (dist_dir / "pr-firmware.json").write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")


def command_list_configs(args: argparse.Namespace) -> int:
    for target in filter_targets(load_targets(), args.status):
        print(target["config"])
    return 0


def command_factory_files(args: argparse.Namespace) -> int:
    for target in filter_targets(load_targets(), args.status):
        for artifact_name in artifact_names(target):
            print(f"{artifact_name}.firmware.factory.bin")
    return 0


def command_release_files(args: argparse.Namespace) -> int:
    names: list[str] = []
    for target in filter_targets(load_targets(), args.status):
        names.extend(release_asset_names(target))
    for name in sorted(set(names)):
        print(name)
    return 0


def command_github_matrix(args: argparse.Namespace) -> int:
    targets = filter_targets(load_targets(), args.status)
    print(json.dumps({"target": targets}, separators=(",", ":")))
    return 0


def command_prepare_release_assets(args: argparse.Namespace) -> int:
    prepare_release_assets(args.version, args.base_url, args.release_url)
    return 0


def command_prepare_pr_test_assets(args: argparse.Namespace) -> int:
    prepare_pr_test_assets(
        args.pr_number,
        args.version,
        args.head_sha,
        args.base_url,
        args.release_url,
        artifact_root=args.artifact_root,
    )
    return 0


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Work with the OpenQuatt build-target matrix.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    def add_status_argument(subparser: argparse.ArgumentParser) -> None:
        subparser.add_argument(
            "--status",
            choices=("enabled", "planned", "all"),
            default="enabled",
            help="Target status filter.",
        )

    list_configs_parser = subparsers.add_parser("list-configs", help="Print target config paths.")
    add_status_argument(list_configs_parser)
    list_configs_parser.set_defaults(func=command_list_configs)

    factory_files_parser = subparsers.add_parser("factory-files", help="Print expected factory asset filenames.")
    add_status_argument(factory_files_parser)
    factory_files_parser.set_defaults(func=command_factory_files)

    release_files_parser = subparsers.add_parser("release-files", help="Print expected release asset filenames.")
    add_status_argument(release_files_parser)
    release_files_parser.set_defaults(func=command_release_files)

    github_matrix_parser = subparsers.add_parser("github-matrix", help="Print a GitHub Actions matrix JSON.")
    add_status_argument(github_matrix_parser)
    github_matrix_parser.set_defaults(func=command_github_matrix)

    prepare_parser = subparsers.add_parser("prepare-release-assets", help="Prepare release assets and OTA manifests.")
    prepare_parser.add_argument("version")
    prepare_parser.add_argument("base_url")
    prepare_parser.add_argument("release_url")
    prepare_parser.set_defaults(func=command_prepare_release_assets)

    pr_prepare_parser = subparsers.add_parser("prepare-pr-test-assets", help="Prepare PR test OTA assets.")
    pr_prepare_parser.add_argument("pr_number")
    pr_prepare_parser.add_argument("version")
    pr_prepare_parser.add_argument("head_sha")
    pr_prepare_parser.add_argument("base_url")
    pr_prepare_parser.add_argument("release_url")
    pr_prepare_parser.add_argument("--artifact-root", type=Path)
    pr_prepare_parser.set_defaults(func=command_prepare_pr_test_assets)

    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = create_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
