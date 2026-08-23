#!/usr/bin/env python3
"""Create the small provenance record needed to rebuild a firmware ELF.

The ELF itself is deliberately not published. The record binds an official
source revision and deterministic build inputs to ESP-IDF's ELF SHA-256 that is
also embedded in the OTA image and reported by crash telemetry.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import Sequence


SCHEMA_VERSION = 1
ESP_IMAGE_MAGIC = 0xE9
ESP_IMAGE_HEADER_SIZE = 24
ESP_IMAGE_SEGMENT_HEADER_SIZE = 8
ESP_APP_DESC_MAGIC = 0xABCD5432
ESP_APP_DESC_ELF_SHA_OFFSET = 144
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
SUBSTITUTION_KEY_RE = re.compile(r"^[a-z][a-z0-9_]*$")
REPOSITORY_RE = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
IDENTIFIER_RE = re.compile(r"^[a-z0-9][a-z0-9_-]*$")
IDF_MANIFEST_HASH_RE = re.compile(r"(?m)^manifest_hash: ([0-9a-f]{64})$")
PORTABLE_IDF_MANIFEST_HASH = "${OPENQUATT_IDF_MANIFEST_HASH}"
PORTABLE_SOURCE_ROOT = "${OPENQUATT_SOURCE_ROOT}"


class BuildMetadataError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_metadata(path: Path, *, include_text: bool = False) -> dict[str, object]:
    if not path.is_file():
        raise BuildMetadataError(f"Required build input is missing: {path}")
    metadata: dict[str, object] = {
        "sha256": sha256_file(path),
        "size": path.stat().st_size,
    }
    if include_text:
        metadata["content"] = path.read_text(encoding="utf-8")
    return metadata


def portable_idf_file_metadata(
    path: Path,
    *,
    source_root: Path,
    mask_manifest_hash: bool = False,
) -> dict[str, object]:
    """Capture generated IDF YAML without runner-specific absolute paths."""

    if not path.is_file():
        raise BuildMetadataError(f"Required build input is missing: {path}")
    content = path.read_text(encoding="utf-8")
    source_root_text = source_root.resolve().as_posix()
    if PORTABLE_SOURCE_ROOT in content or PORTABLE_IDF_MANIFEST_HASH in content:
        raise BuildMetadataError(f"Generated IDF input contains a reserved placeholder: {path}")
    portable = content.replace(
        f"{source_root_text}/",
        f"{PORTABLE_SOURCE_ROOT}/",
    )
    metadata: dict[str, object] = {"path_root": PORTABLE_SOURCE_ROOT}
    if mask_manifest_hash:
        matches = IDF_MANIFEST_HASH_RE.findall(portable)
        if len(matches) != 1:
            raise BuildMetadataError(
                f"Expected one IDF manifest_hash in dependency lock, found {len(matches)}"
            )
        metadata["manifest_hash"] = matches[0]
        portable = IDF_MANIFEST_HASH_RE.sub(
            f"manifest_hash: {PORTABLE_IDF_MANIFEST_HASH}",
            portable,
        )
    for line in portable.splitlines():
        stripped = line.strip()
        if stripped.startswith(("path:", "override_path:")):
            value = stripped.partition(":")[2].strip().strip("'\"")
            if value.startswith("/"):
                raise BuildMetadataError(
                    f"Generated IDF input has an absolute path outside source_root: {path}"
                )
    encoded = portable.encode("utf-8")
    metadata.update(
        {
            "sha256": hashlib.sha256(encoded).hexdigest(),
            "size": len(encoded),
            "content": portable,
        }
    )
    return metadata


def read_embedded_elf_sha256(firmware: Path) -> str:
    """Read ``esp_app_desc.app_elf_sha256`` from the first image segment."""

    minimum_size = (
        ESP_IMAGE_HEADER_SIZE
        + ESP_IMAGE_SEGMENT_HEADER_SIZE
        + ESP_APP_DESC_ELF_SHA_OFFSET
        + 32
    )
    with firmware.open("rb") as handle:
        prefix = handle.read(minimum_size)
    if len(prefix) < minimum_size:
        raise BuildMetadataError(f"Firmware image is too short: {firmware}")
    if prefix[0] != ESP_IMAGE_MAGIC:
        raise BuildMetadataError(f"Firmware image has invalid ESP image magic: {firmware}")

    app_desc_offset = ESP_IMAGE_HEADER_SIZE + ESP_IMAGE_SEGMENT_HEADER_SIZE
    magic = int.from_bytes(prefix[app_desc_offset : app_desc_offset + 4], "little")
    if magic != ESP_APP_DESC_MAGIC:
        raise BuildMetadataError(
            f"Firmware image has no ESP application descriptor in its first segment: {firmware}"
        )
    sha_offset = app_desc_offset + ESP_APP_DESC_ELF_SHA_OFFSET
    build_id = prefix[sha_offset : sha_offset + 32].hex()
    if not SHA256_RE.fullmatch(build_id) or build_id == "0" * 64:
        raise BuildMetadataError(f"Firmware image has no valid ELF SHA-256: {firmware}")
    return build_id


def parse_substitutions(values: Sequence[str]) -> dict[str, str]:
    substitutions: dict[str, str] = {}
    for value in values:
        key, separator, replacement = value.partition("=")
        if not separator or not SUBSTITUTION_KEY_RE.fullmatch(key):
            raise BuildMetadataError(f"Invalid substitution: {value!r}")
        if key in substitutions:
            raise BuildMetadataError(f"Duplicate substitution: {key}")
        substitutions[key] = replacement
    return dict(sorted(substitutions.items()))


def normalize_repository(value: str) -> str:
    value = value.strip()
    if not REPOSITORY_RE.fullmatch(value) or any(
        part in (".", "..") for part in value.split("/", 1)
    ):
        raise BuildMetadataError("source_repository must be a GitHub owner/repository name")
    return value


def normalize_identifier(value: str, field: str) -> str:
    if not IDENTIFIER_RE.fullmatch(value):
        raise BuildMetadataError(f"{field} must be a lowercase build identifier")
    return value


def normalize_target_config(value: str) -> str:
    path = PurePosixPath(value)
    if (
        path.is_absolute()
        or path.suffix not in (".yaml", ".yml")
        or not path.parts
        or any(part in ("", ".", "..") for part in path.parts)
    ):
        raise BuildMetadataError("target_config must be a repo-relative YAML path")
    return str(path)


def metadata_filename(record: dict[str, object]) -> str:
    """Return the immutable, build-addressed release asset name."""

    identity = record.get("identity")
    build_id = record.get("build_id")
    if not isinstance(identity, dict) or not isinstance(build_id, str):
        raise BuildMetadataError("Build metadata is missing identity or build_id")
    artifact_name = identity.get("artifact_name")
    source_commit = identity.get("source_commit")
    if not isinstance(artifact_name, str) or not isinstance(source_commit, str):
        raise BuildMetadataError("Build metadata identity is incomplete")
    artifact_name = normalize_identifier(artifact_name, "artifact_name")
    if not COMMIT_RE.fullmatch(source_commit) or not SHA256_RE.fullmatch(build_id):
        raise BuildMetadataError("Build metadata has an invalid commit or build_id")
    return f"{artifact_name}.{source_commit}.{build_id}.build.json"


def command_output(command: Sequence[str], cwd: Path | None = None) -> str | None:
    try:
        completed = subprocess.run(
            list(command),
            cwd=cwd,
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError:
        return None
    if completed.returncode != 0:
        return None
    output = completed.stdout.strip() or completed.stderr.strip()
    return output or None


def first_line(value: str | None) -> str | None:
    return value.splitlines()[0] if value else None


def format_build_time(build_epoch: int) -> str:
    return datetime.fromtimestamp(build_epoch, timezone.utc).strftime("%Y-%m-%d %H:%M:%S +0000")


def parse_cmake_cache(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(("#", "//")) or "=" not in line or ":" not in line:
            continue
        key_type, value = line.split("=", 1)
        key, _ = key_type.split(":", 1)
        values[key] = value
    return values


def collect_tool_versions(build_dir: Path) -> dict[str, object]:
    cache = parse_cmake_cache(build_dir / "CMakeCache.txt")
    versions: dict[str, object] = {
        "python": sys.version.splitlines()[0],
        "node": first_line(command_output(["node", "--version"])),
        "npm": first_line(command_output(["npm", "--version"])),
        "ccache": first_line(command_output(["ccache", "--version"])),
        "host_pip_freeze": command_output(
            [sys.executable, "-m", "pip", "freeze", "--all"]
        ),
    }
    try:
        from esphome.const import __version__ as esphome_version
    except ImportError:
        esphome_version = None
    versions["esphome"] = esphome_version

    cmake_tools: dict[str, object] = {}
    for key in ("CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_COMMAND", "CMAKE_MAKE_PROGRAM"):
        executable = cache.get(key)
        if executable:
            cmake_tools[key.lower()] = {
                "name": Path(executable).name,
                "version": first_line(command_output([executable, "--version"])),
            }
    idf_python = cache.get("PYTHON")
    if idf_python:
        cmake_tools["idf_python"] = {
            "version": first_line(command_output([idf_python, "--version"])),
            "pip_freeze": command_output([idf_python, "-m", "pip", "freeze", "--all"]),
        }
    versions["cmake_tools"] = cmake_tools
    idf_path_value = cache.get("IDF_PATH")
    if idf_path_value:
        idf_path = Path(idf_path_value)
        versions["esp_idf"] = {
            "directory_name": idf_path.name,
            "git_commit": command_output(["git", "rev-parse", "HEAD"], cwd=idf_path),
            "git_describe": command_output(
                ["git", "describe", "--always", "--dirty"],
                cwd=idf_path,
            ),
        }
    return versions


def validate_source_commit(source_root: Path, source_commit: str) -> str:
    normalized = source_commit.lower()
    if not COMMIT_RE.fullmatch(normalized):
        raise BuildMetadataError("source_commit must be a full 40-character Git SHA")
    actual = command_output(["git", "rev-parse", "HEAD"], cwd=source_root)
    if actual is None:
        raise BuildMetadataError(f"Cannot resolve checked-out source commit in {source_root}")
    if actual.lower() != normalized:
        raise BuildMetadataError(
            f"Checked-out source commit {actual} does not match requested {normalized}"
        )
    return normalized


def find_sdkconfig(build_root: Path) -> Path:
    candidates = sorted(
        path
        for path in build_root.glob("sdkconfig.*")
        if path.is_file() and not path.name.endswith(".esphomeinternal")
    )
    if len(candidates) != 1:
        raise BuildMetadataError(
            f"Expected one generated sdkconfig in {build_root}, found {len(candidates)}"
        )
    return candidates[0]


def create_record(
    *,
    source_root: Path,
    build_dir: Path,
    source_repository: str,
    source_commit: str,
    build_epoch: int,
    target_id: str,
    target_config: str,
    artifact_name: str,
    substitutions: dict[str, str],
    release_manifest_tag: str | None = None,
) -> dict[str, object]:
    if build_epoch < 0 or build_epoch > 0xFFFFFFFF:
        raise BuildMetadataError("build_epoch must fit in an unsigned 32-bit timestamp")
    source_root = source_root.resolve()
    source_repository = normalize_repository(source_repository)
    source_commit = validate_source_commit(source_root, source_commit)
    target_id = normalize_identifier(target_id, "target_id")
    artifact_name = normalize_identifier(artifact_name, "artifact_name")
    target_config = normalize_target_config(target_config)
    config_path = (source_root / target_config).resolve()
    if not config_path.is_relative_to(source_root) or not config_path.is_file():
        raise BuildMetadataError(f"Target config is missing or outside the source tree: {target_config}")
    required_substitutions = {
        "build_source_repository": source_repository,
        "build_source_commit": source_commit,
        "build_target": target_config,
        "build_epoch": str(build_epoch),
    }
    for key, expected in required_substitutions.items():
        if substitutions.get(key) != expected:
            raise BuildMetadataError(
                f"Build substitution {key!r} does not match the resolved build identity"
            )
    firmware_version = substitutions.get("project_version")
    release_channel = substitutions.get("release_channel")
    if not firmware_version or not release_channel:
        raise BuildMetadataError(
            "Build substitutions must include the effective project_version and release_channel"
        )
    build_dir = (
        build_dir.resolve()
        if build_dir.is_absolute()
        else (source_root / build_dir).resolve()
    )
    if (
        not build_dir.is_relative_to(source_root)
        or build_dir.name != "build"
        or ".esphome" not in build_dir.relative_to(source_root).parts
    ):
        raise BuildMetadataError("build_dir must be an ESPHome build directory inside source_root")
    build_root = build_dir.parent
    firmware_elf = build_dir / "firmware.elf"
    firmware_ota = build_dir / "firmware.ota.bin"
    if not firmware_elf.is_file() or not firmware_ota.is_file():
        raise BuildMetadataError(f"Build directory is missing firmware.elf or firmware.ota.bin: {build_dir}")

    elf_sha256 = sha256_file(firmware_elf)
    embedded_sha256 = read_embedded_elf_sha256(firmware_ota)
    if elf_sha256 != embedded_sha256:
        raise BuildMetadataError(
            "firmware.elf SHA-256 does not match esp_app_desc.app_elf_sha256 "
            f"({elf_sha256} != {embedded_sha256})"
        )

    build_info_path = build_root / "build_info.json"
    try:
        build_info = json.loads(build_info_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as err:
        raise BuildMetadataError(f"Cannot read ESPHome build_info.json: {err}") from err
    if build_info.get("build_time") != build_epoch:
        raise BuildMetadataError(
            "ESPHome build timestamp does not match deterministic build_epoch "
            f"({build_info.get('build_time')!r} != {build_epoch})"
        )
    expected_build_time = format_build_time(build_epoch)
    if build_info.get("build_time_str") != expected_build_time:
        raise BuildMetadataError(
            "ESPHome build-time string is not the deterministic UTC value "
            f"({build_info.get('build_time_str')!r} != {expected_build_time!r})"
        )

    requirements = source_root / ".github" / "requirements-esphome.txt"
    package_lock = source_root / "package-lock.json"
    dependencies_lock = build_root / "dependencies.lock"
    idf_component_manifest = build_root / "src" / "idf_component.yml"
    sdkconfig = find_sdkconfig(build_root)
    web_assets = {
        relative: file_metadata(source_root / relative)
        for relative in (
            "openquatt/web/css/openquatt-app.css",
            "openquatt/web/js/openquatt-app.js",
        )
    }

    return {
        "schema_version": SCHEMA_VERSION,
        "build_id": elf_sha256,
        "identity": {
            "source_repository": source_repository,
            "source_commit": source_commit,
            "build_epoch": build_epoch,
            "target_id": target_id,
            "target_config": target_config,
            "artifact_name": artifact_name,
            "firmware_version": firmware_version,
            "release_channel": release_channel,
        },
        "substitutions": substitutions,
        "artifacts": {
            "firmware_elf": {
                "sha256": elf_sha256,
                "size": firmware_elf.stat().st_size,
                "published": False,
            },
            "firmware_ota": file_metadata(firmware_ota),
        },
        "inputs": {
            "build_overrides": {
                "project_version": substitutions.get("project_version"),
                "release_channel": substitutions.get("release_channel"),
                "release_manifest_tag": release_manifest_tag,
                "release_manifest_url": substitutions.get("release_manifest_url"),
            },
            "deterministic_wrapper": file_metadata(
                source_root / "scripts" / "esphome_deterministic.py"
            ),
            "esphome_requirements": file_metadata(requirements, include_text=True),
            "npm_lock": file_metadata(package_lock),
            "idf_dependencies_lock": portable_idf_file_metadata(
                dependencies_lock,
                source_root=source_root,
                mask_manifest_hash=True,
            ),
            "idf_component_manifest": portable_idf_file_metadata(
                idf_component_manifest,
                source_root=source_root,
            ),
            "sdkconfig": file_metadata(sdkconfig),
            "web_assets": web_assets,
            "esphome_build_info": build_info,
        },
        "tools": collect_tool_versions(build_dir),
        "github": {
            "run_id": os.getenv("GITHUB_RUN_ID"),
            "run_attempt": os.getenv("GITHUB_RUN_ATTEMPT"),
            "runner_arch": os.getenv("RUNNER_ARCH"),
            "runner_image_os": os.getenv("ImageOS"),
            "runner_image_version": os.getenv("ImageVersion"),
        },
    }


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--source-repository", default="OpenQuatt/OpenQuatt")
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--build-epoch", type=int, required=True)
    parser.add_argument("--target-id", required=True)
    parser.add_argument("--target-config", required=True)
    parser.add_argument("--artifact-name", required=True)
    parser.add_argument("--release-manifest-tag")
    parser.add_argument("--substitution", action="append", default=[])
    parser.add_argument("--output", type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = create_parser().parse_args(argv)
    try:
        substitutions = parse_substitutions(args.substitution)
        record = create_record(
            source_root=args.source_root.resolve(),
            build_dir=args.build_dir,
            source_repository=args.source_repository,
            source_commit=args.source_commit,
            build_epoch=args.build_epoch,
            target_id=args.target_id,
            target_config=args.target_config,
            artifact_name=args.artifact_name,
            substitutions=substitutions,
            release_manifest_tag=args.release_manifest_tag,
        )
    except BuildMetadataError as err:
        raise SystemExit(str(err)) from err

    output = args.output or (
        args.source_root.resolve() / args.build_dir / metadata_filename(record)
    )
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(output)
    print(output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
