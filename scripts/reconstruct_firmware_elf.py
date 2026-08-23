#!/usr/bin/env python3
"""Rebuild and verify a firmware ELF from an exact OpenQuatt checkout.

The script never accepts a merely similar ELF: it copies the reconstructed ELF
to the requested output only after both its SHA-256 and the OTA image's embedded
``esp_app_desc.app_elf_sha256`` equal the captured build ID.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Mapping, Sequence

from firmware_build_metadata import (
    BuildMetadataError,
    COMMIT_RE,
    find_sdkconfig,
    IDF_MANIFEST_HASH_RE,
    normalize_repository,
    normalize_target_config,
    PORTABLE_IDF_MANIFEST_HASH,
    PORTABLE_SOURCE_ROOT,
    SHA256_RE,
    parse_substitutions,
    read_embedded_elf_sha256,
    sha256_file,
)


UINT32_MAX = 0xFFFFFFFF


class ReconstructionError(RuntimeError):
    pass


@dataclass(frozen=True)
class CapturedTextInput:
    content: str
    sha256: str
    manifest_hash: str | None = None


@dataclass(frozen=True)
class RebuildRequest:
    source_repository: str
    source_commit: str
    build_epoch: int
    target_config: str
    expected_build_id: str
    firmware_version: str
    release_channel: str
    substitutions: dict[str, str]
    idf_dependencies_lock: CapturedTextInput
    idf_component_manifest: CapturedTextInput
    deterministic_wrapper_sha256: str
    esphome_requirements: CapturedTextInput
    npm_lock_sha256: str
    sdkconfig_sha256: str
    web_asset_sha256: dict[str, str]


def normalize_commit(value: str) -> str:
    value = value.strip().lower()
    if not COMMIT_RE.fullmatch(value):
        raise ReconstructionError("source_commit must be a full 40-character Git SHA")
    return value


def normalize_build_id(value: str) -> str:
    value = value.strip().lower()
    if not SHA256_RE.fullmatch(value) or value == "0" * 64:
        raise ReconstructionError("expected_build_id must be a non-zero 64-character SHA-256")
    return value


def normalize_epoch(value: int) -> int:
    if isinstance(value, bool) or value < 0 or value > UINT32_MAX:
        raise ReconstructionError("build_epoch must fit in an unsigned 32-bit timestamp")
    return value


def require_hash_input(
    inputs: Mapping[str, object],
    name: str,
    *,
    require_content: bool = False,
) -> tuple[str, str | None]:
    entry = inputs.get(name)
    if not isinstance(entry, dict):
        raise ReconstructionError(f"Build metadata input {name!r} is missing")
    digest = entry.get("sha256")
    if not isinstance(digest, str):
        raise ReconstructionError(f"Build metadata input {name!r} has no SHA-256")
    digest = normalize_build_id(digest)
    content = entry.get("content")
    if require_content:
        if not isinstance(content, str):
            raise ReconstructionError(
                f"Build metadata input {name!r} must include captured content"
            )
        actual = hashlib.sha256(content.encode("utf-8")).hexdigest()
        if actual != digest:
            raise ReconstructionError(
                f"Build metadata input {name!r} content does not match its SHA-256"
            )
    elif content is not None and not isinstance(content, str):
        raise ReconstructionError(f"Build metadata input {name!r} content is invalid")
    size = entry.get("size")
    if not isinstance(size, int) or isinstance(size, bool) or size < 0:
        raise ReconstructionError(f"Build metadata input {name!r} has an invalid size")
    if isinstance(content, str) and len(content.encode("utf-8")) != size:
        raise ReconstructionError(
            f"Build metadata input {name!r} content does not match its captured size"
        )
    return digest, content if isinstance(content, str) else None


def require_portable_idf_input(
    inputs: Mapping[str, object],
    name: str,
    *,
    require_manifest_hash: bool = False,
) -> CapturedTextInput:
    digest, content = require_hash_input(inputs, name, require_content=True)
    entry = inputs.get(name)
    if not isinstance(entry, dict):
        raise ReconstructionError(f"Build metadata input {name!r} is missing")
    if entry.get("path_root") != PORTABLE_SOURCE_ROOT or content is None:
        raise ReconstructionError(
            f"Build metadata input {name!r} is not source-root portable"
        )
    for line in content.splitlines():
        stripped = line.strip()
        if stripped.startswith(("path:", "override_path:")):
            value = stripped.partition(":")[2].strip().strip("'\"")
            if value.startswith("/"):
                raise ReconstructionError(
                    f"Build metadata input {name!r} contains an unportable absolute path"
                )
    manifest_hash = entry.get("manifest_hash")
    if require_manifest_hash:
        if not isinstance(manifest_hash, str) or not SHA256_RE.fullmatch(manifest_hash):
            raise ReconstructionError(
                f"Build metadata input {name!r} has no captured IDF manifest hash"
            )
        if content.count(PORTABLE_IDF_MANIFEST_HASH) != 1:
            raise ReconstructionError(
                f"Build metadata input {name!r} has an invalid manifest-hash placeholder"
            )
    elif manifest_hash is not None:
        raise ReconstructionError(
            f"Build metadata input {name!r} has an unexpected IDF manifest hash"
        )
    return CapturedTextInput(content, digest, manifest_hash)


def request_from_record(record: Mapping[str, object]) -> RebuildRequest:
    if record.get("schema_version") != 1:
        raise ReconstructionError("Unsupported firmware build metadata schema")
    identity = record.get("identity")
    substitutions = record.get("substitutions")
    if not isinstance(identity, dict) or not isinstance(substitutions, dict):
        raise ReconstructionError("Build metadata is missing identity or substitutions")
    if not all(isinstance(key, str) and isinstance(value, str) for key, value in substitutions.items()):
        raise ReconstructionError("Build metadata substitutions must contain strings")

    epoch = identity.get("build_epoch")
    target_config = identity.get("target_config")
    source_repository = identity.get("source_repository")
    source_commit = identity.get("source_commit")
    firmware_version = identity.get("firmware_version")
    release_channel = identity.get("release_channel")
    expected_build_id = record.get("build_id")
    if (
        not isinstance(epoch, int)
        or isinstance(epoch, bool)
        or not isinstance(target_config, str)
        or not isinstance(source_repository, str)
        or not isinstance(source_commit, str)
        or not isinstance(firmware_version, str)
        or not firmware_version
        or not isinstance(release_channel, str)
        or not release_channel
        or not isinstance(expected_build_id, str)
    ):
        raise ReconstructionError("Build metadata identity is incomplete")

    normalized_substitutions = parse_substitutions(
        [f"{key}={value}" for key, value in substitutions.items()]
    )
    inputs = record.get("inputs")
    if not isinstance(inputs, dict):
        raise ReconstructionError("Build metadata is missing captured build inputs")
    wrapper_sha256, _ = require_hash_input(inputs, "deterministic_wrapper")
    requirements_sha256, requirements_content = require_hash_input(
        inputs,
        "esphome_requirements",
        require_content=True,
    )
    npm_lock_sha256, _ = require_hash_input(inputs, "npm_lock")
    dependency_lock = require_portable_idf_input(
        inputs,
        "idf_dependencies_lock",
        require_manifest_hash=True,
    )
    component_manifest = require_portable_idf_input(inputs, "idf_component_manifest")
    sdkconfig_sha256, _ = require_hash_input(inputs, "sdkconfig")
    web_assets = inputs.get("web_assets")
    if not isinstance(web_assets, dict):
        raise ReconstructionError("Build metadata is missing captured web assets")
    expected_web_assets = (
        "openquatt/web/css/openquatt-app.css",
        "openquatt/web/js/openquatt-app.js",
    )
    web_asset_sha256: dict[str, str] = {}
    for relative in expected_web_assets:
        digest, _ = require_hash_input(web_assets, relative)
        web_asset_sha256[relative] = digest
    if set(web_assets) != set(expected_web_assets):
        raise ReconstructionError("Build metadata contains an unexpected web asset set")

    if requirements_content is None:
        raise ReconstructionError("Build metadata is missing required captured file content")

    request = RebuildRequest(
        source_repository=normalize_repository(source_repository),
        source_commit=normalize_commit(source_commit),
        build_epoch=normalize_epoch(epoch),
        target_config=normalize_target_config(target_config),
        expected_build_id=normalize_build_id(expected_build_id),
        firmware_version=firmware_version,
        release_channel=release_channel,
        substitutions=normalized_substitutions,
        idf_dependencies_lock=dependency_lock,
        idf_component_manifest=component_manifest,
        deterministic_wrapper_sha256=wrapper_sha256,
        esphome_requirements=CapturedTextInput(
            requirements_content,
            requirements_sha256,
        ),
        npm_lock_sha256=npm_lock_sha256,
        sdkconfig_sha256=sdkconfig_sha256,
        web_asset_sha256=web_asset_sha256,
    )
    required = {
        "build_source_repository": request.source_repository,
        "build_source_commit": request.source_commit,
        "build_target": request.target_config,
        "build_epoch": str(request.build_epoch),
        "project_version": request.firmware_version,
        "release_channel": request.release_channel,
    }
    for key, expected in required.items():
        if request.substitutions.get(key) != expected:
            raise ReconstructionError(
                f"Build metadata substitution {key!r} does not match its identity field"
            )
    return request


def load_record(path: Path) -> RebuildRequest:
    try:
        record = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as err:
        raise ReconstructionError(f"Cannot read build metadata: {err}") from err
    if not isinstance(record, dict):
        raise ReconstructionError("Build metadata must be a JSON object")
    return request_from_record(record)


def cross_check_captured_identity(
    request: RebuildRequest,
    *,
    build_id: str,
    source_repository: str,
    source_commit: str,
    build_epoch: int,
    target_config: str,
    firmware_version: str,
    release_channel: str,
) -> None:
    """Bind a downloaded record to every rebuild-relevant crash field."""

    captured = {
        "captured_build_id": normalize_build_id(build_id),
        "captured_source_repository": normalize_repository(source_repository),
        "captured_source_commit": normalize_commit(source_commit),
        "captured_build_epoch": normalize_epoch(build_epoch),
        "captured_build_target": normalize_target_config(target_config),
        "captured_firmware_version": firmware_version,
        "captured_release_channel": release_channel,
    }
    expected = {
        "captured_build_id": request.expected_build_id,
        "captured_source_repository": request.source_repository,
        "captured_source_commit": request.source_commit,
        "captured_build_epoch": request.build_epoch,
        "captured_build_target": request.target_config,
        "captured_firmware_version": request.firmware_version,
        "captured_release_channel": request.release_channel,
    }
    for field, value in captured.items():
        if value != expected[field]:
            raise ReconstructionError(
                f"Build metadata {field} does not match the captured crash value"
            )


def verify_file_sha256(path: Path, expected: str, description: str) -> None:
    if not path.is_file():
        raise ReconstructionError(f"Captured {description} is missing: {path}")
    actual = sha256_file(path)
    if actual != expected:
        raise ReconstructionError(
            f"Captured {description} does not match build metadata ({actual} != {expected})"
        )


def verify_source_inputs(source_root: Path, request: RebuildRequest) -> Path:
    """Verify captured source files before package installation or code execution."""

    source_root = source_root.resolve()
    actual_commit = checked_out_commit(source_root)
    if actual_commit != request.source_commit:
        raise ReconstructionError(
            f"Source checkout {actual_commit} does not match captured {request.source_commit}"
        )
    wrapper = source_root / "scripts" / "esphome_deterministic.py"
    verify_file_sha256(
        wrapper,
        request.deterministic_wrapper_sha256,
        "deterministic ESPHome wrapper",
    )
    requirements = source_root / ".github" / "requirements-esphome.txt"
    verify_file_sha256(
        requirements,
        request.esphome_requirements.sha256,
        "ESPHome requirements lock",
    )
    if requirements.read_text(encoding="utf-8") != request.esphome_requirements.content:
        raise ReconstructionError(
            "Captured ESPHome requirements content does not match build metadata"
        )
    verify_file_sha256(
        source_root / "package-lock.json",
        request.npm_lock_sha256,
        "npm lockfile",
    )
    return wrapper


def load_source_targets(source_root: Path) -> list[dict[str, str]]:
    module_path = source_root / "scripts" / "build_targets.py"
    spec = importlib.util.spec_from_file_location("openquatt_source_build_targets", module_path)
    if spec is None or spec.loader is None:
        raise ReconstructionError(f"Cannot load target matrix from {module_path}")
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
        targets = module.load_targets()
    except (OSError, SystemExit, SyntaxError) as err:
        raise ReconstructionError(f"Cannot load target matrix from source checkout: {err}") from err
    if not isinstance(targets, list):
        raise ReconstructionError("Source target matrix returned an invalid value")
    return targets


def resolve_target(source_root: Path, target_config: str) -> tuple[dict[str, str], Path, Path]:
    matches = [
        target
        for target in load_source_targets(source_root)
        if target.get("config") == target_config and target.get("status") == "enabled"
    ]
    if len(matches) != 1:
        raise ReconstructionError(
            f"target_config must identify one enabled source target: {target_config}"
        )
    target = matches[0]
    root = source_root.resolve()
    config_path = (root / target["config"]).resolve()
    build_root = (root / target["build_path"]).resolve()
    if (
        not config_path.is_relative_to(root)
        or not build_root.is_relative_to(root)
        or build_root == root
        or ".esphome" not in build_root.parts
    ):
        raise ReconstructionError("Target matrix resolves outside the source checkout")
    if not config_path.is_file():
        raise ReconstructionError(f"Target config is missing: {config_path}")
    return target, config_path, build_root


def checked_out_commit(source_root: Path) -> str:
    completed = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=source_root,
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        raise ReconstructionError(completed.stderr.strip() or "Cannot resolve source checkout")
    return normalize_commit(completed.stdout)


def compile_command(
    python_executable: str,
    wrapper: Path,
    config_path: str | Path,
    substitutions: Mapping[str, str],
) -> list[str]:
    command = [python_executable, str(wrapper)]
    for key, value in sorted(substitutions.items()):
        command.extend(("-s", key, value))
    command.extend(("compile", str(config_path)))
    return command


def run_checked(command: Sequence[str], *, cwd: Path, env: Mapping[str, str] | None = None) -> None:
    completed = subprocess.run(list(command), cwd=cwd, env=env, check=False)
    if completed.returncode != 0:
        raise ReconstructionError(
            f"Command failed with exit code {completed.returncode}: {' '.join(command)}"
        )


def verify_and_copy_elf(build_dir: Path, expected_build_id: str, output: Path) -> str:
    firmware_elf = build_dir / "firmware.elf"
    firmware_ota = build_dir / "firmware.ota.bin"
    if not firmware_elf.is_file() or not firmware_ota.is_file():
        raise ReconstructionError("Rebuild did not produce firmware.elf and firmware.ota.bin")
    elf_sha256 = sha256_file(firmware_elf)
    try:
        embedded_sha256 = read_embedded_elf_sha256(firmware_ota)
    except BuildMetadataError as err:
        raise ReconstructionError(str(err)) from err
    if elf_sha256 != embedded_sha256:
        raise ReconstructionError(
            "Rebuilt ELF and OTA image disagree about the ELF SHA-256; refusing symbolization"
        )
    if elf_sha256 != expected_build_id:
        raise ReconstructionError(
            "Rebuilt ELF SHA-256 does not match captured build_id; refusing symbolization "
            f"({elf_sha256} != {expected_build_id})"
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    shutil.copyfile(firmware_elf, temporary)
    temporary.replace(output)
    return elf_sha256


def render_portable_idf_input(captured: CapturedTextInput, source_root: Path) -> str:
    rendered = captured.content.replace(
        f"{PORTABLE_SOURCE_ROOT}/",
        f"{source_root.resolve().as_posix()}/",
    )
    if captured.manifest_hash is not None:
        rendered = rendered.replace(PORTABLE_IDF_MANIFEST_HASH, captured.manifest_hash)
    if PORTABLE_SOURCE_ROOT in rendered or PORTABLE_IDF_MANIFEST_HASH in rendered:
        raise ReconstructionError("Captured IDF input has an unresolved portable placeholder")
    return rendered


def verify_portable_idf_input(
    path: Path,
    captured: CapturedTextInput,
    source_root: Path,
    description: str,
) -> None:
    if not path.is_file():
        raise ReconstructionError(f"Captured {description} is missing: {path}")
    portable = path.read_text(encoding="utf-8").replace(
        f"{source_root.resolve().as_posix()}/",
        f"{PORTABLE_SOURCE_ROOT}/",
    )
    if captured.manifest_hash is not None:
        matches = IDF_MANIFEST_HASH_RE.findall(portable)
        if len(matches) != 1:
            raise ReconstructionError(
                f"Generated {description} has no unique IDF manifest hash"
            )
        portable = IDF_MANIFEST_HASH_RE.sub(
            f"manifest_hash: {PORTABLE_IDF_MANIFEST_HASH}",
            portable,
        )
    actual = hashlib.sha256(portable.encode("utf-8")).hexdigest()
    if actual != captured.sha256 or portable != captured.content:
        raise ReconstructionError(
            f"Generated {description} does not match captured portable build metadata"
        )


def seed_idf_dependency_state(
    build_root: Path,
    source_root: Path,
    request: RebuildRequest,
) -> None:
    """Restore the matching manifest with its lock so ESPHome preserves the lock."""

    component_manifest = build_root / "src" / "idf_component.yml"
    component_manifest.parent.mkdir(parents=True, exist_ok=True)
    component_manifest.write_text(
        render_portable_idf_input(request.idf_component_manifest, source_root),
        encoding="utf-8",
    )
    (build_root / "dependencies.lock").write_text(
        render_portable_idf_input(request.idf_dependencies_lock, source_root),
        encoding="utf-8",
    )
    verify_portable_idf_input(
        component_manifest,
        request.idf_component_manifest,
        source_root,
        "IDF component manifest",
    )
    verify_portable_idf_input(
        build_root / "dependencies.lock",
        request.idf_dependencies_lock,
        source_root,
        "IDF dependency lock",
    )


def verify_web_assets(source_root: Path, request: RebuildRequest) -> None:
    for relative, digest in request.web_asset_sha256.items():
        verify_file_sha256(source_root / relative, digest, f"generated web asset {relative}")


def verify_generated_build_inputs(
    build_root: Path,
    source_root: Path,
    request: RebuildRequest,
) -> None:
    verify_portable_idf_input(
        build_root / "dependencies.lock",
        request.idf_dependencies_lock,
        source_root,
        "generated IDF dependency lock",
    )
    verify_portable_idf_input(
        build_root / "src" / "idf_component.yml",
        request.idf_component_manifest,
        source_root,
        "generated IDF component manifest",
    )
    try:
        sdkconfig = find_sdkconfig(build_root)
    except BuildMetadataError as err:
        raise ReconstructionError(str(err)) from err
    verify_file_sha256(sdkconfig, request.sdkconfig_sha256, "generated sdkconfig")


def reconstruct(
    *,
    source_root: Path,
    request: RebuildRequest,
    output: Path,
    result_json: Path | None,
    skip_web_build: bool = False,
) -> dict[str, object]:
    source_root = source_root.resolve()
    wrapper = verify_source_inputs(source_root, request)
    _, config_path, build_root = resolve_target(source_root, request.target_config)
    if build_root.exists():
        shutil.rmtree(build_root)
    seed_idf_dependency_state(build_root, source_root, request)

    if not skip_web_build:
        run_checked(["npm", "ci"], cwd=source_root)
        run_checked(["npm", "run", "build:web"], cwd=source_root)
    verify_web_assets(source_root, request)

    environment = os.environ.copy()
    environment["SOURCE_DATE_EPOCH"] = str(request.build_epoch)
    environment["IDF_CCACHE_ENABLE"] = "0"
    command = compile_command(
        sys.executable,
        wrapper,
        request.target_config,
        request.substitutions,
    )
    run_checked(command, cwd=source_root, env=environment)
    verify_generated_build_inputs(build_root, source_root, request)

    build_dir = build_root / "build"
    verified_sha = verify_and_copy_elf(build_dir, request.expected_build_id, output)
    result: dict[str, object] = {
        "schema_version": 1,
        "build_id": verified_sha,
        "source_repository": request.source_repository,
        "source_commit": request.source_commit,
        "build_epoch": request.build_epoch,
        "target_config": request.target_config,
        "elf": str(output.resolve()),
        "cmake_cache": str((build_dir / "CMakeCache.txt").resolve()),
    }
    if result_json is not None:
        result_json.parent.mkdir(parents=True, exist_ok=True)
        result_json.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return result


def request_from_args(args: argparse.Namespace) -> RebuildRequest:
    request = load_record(args.metadata)
    captured_values = (
        args.captured_build_id,
        args.captured_source_repository,
        args.captured_source_commit,
        args.captured_build_epoch,
        args.captured_build_target,
        args.captured_firmware_version,
        args.captured_release_channel,
    )
    if any(value is not None for value in captured_values):
        if any(value is None for value in captured_values):
            raise ReconstructionError(
                "Supply every --captured-* build identity field when cross-checking a crash"
            )
        cross_check_captured_identity(
            request,
            build_id=args.captured_build_id,
            source_repository=args.captured_source_repository,
            source_commit=args.captured_source_commit,
            build_epoch=args.captured_build_epoch,
            target_config=args.captured_build_target,
            firmware_version=args.captured_firmware_version,
            release_channel=args.captured_release_channel,
        )
    return request


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--captured-build-id")
    parser.add_argument("--captured-source-repository")
    parser.add_argument("--captured-source-commit")
    parser.add_argument("--captured-build-epoch", type=int)
    parser.add_argument("--captured-build-target")
    parser.add_argument("--captured-firmware-version")
    parser.add_argument("--captured-release-channel")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--result-json", type=Path)
    validation = parser.add_mutually_exclusive_group()
    validation.add_argument("--validate-metadata-only", action="store_true")
    validation.add_argument("--validate-source-only", action="store_true")
    parser.add_argument("--skip-web-build", action="store_true", help=argparse.SUPPRESS)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = create_parser().parse_args(argv)
    try:
        request = request_from_args(args)
        if args.validate_metadata_only:
            print(json.dumps({"schema_version": 1, "build_id": request.expected_build_id}))
            return 0
        if args.validate_source_only:
            verify_source_inputs(args.source_root, request)
            print(json.dumps({"schema_version": 1, "source_inputs_verified": True}))
            return 0
        if args.output is None:
            raise ReconstructionError("--output is required for reconstruction")
        result = reconstruct(
            source_root=args.source_root,
            request=request,
            output=args.output.resolve(),
            result_json=args.result_json.resolve() if args.result_json else None,
            skip_web_build=args.skip_web_build,
        )
    except (BuildMetadataError, ReconstructionError) as err:
        raise SystemExit(str(err)) from err
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
