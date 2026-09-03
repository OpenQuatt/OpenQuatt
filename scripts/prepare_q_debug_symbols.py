#!/usr/bin/env python3
"""Package exact Q release ELF/map files for crash symbolication."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path
from typing import Sequence


REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from scripts import build_targets  # noqa: E402


Q_HARDWARE = "heatpump_controller_q"


def sha256sum(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def prepare_q_debug_symbols(
    version: str,
    source_commit: str,
    esphome_version: str,
    artifact_root: Path,
    output_dir: Path,
) -> None:
    """Build one indexed Q debug-symbol directory from release staging artifacts."""
    if artifact_root.is_symlink():
        raise SystemExit(f"Q debug symbol staging root must not be a symlink: {artifact_root}")
    if output_dir.is_symlink():
        raise SystemExit(f"Q debug symbol output must not be a symlink: {output_dir}")

    q_targets = [
        target
        for target in build_targets.filter_targets(build_targets.load_targets(), "enabled")
        if target.get("hardware") == Q_HARDWARE
    ]
    if not q_targets:
        raise SystemExit("No enabled Q build targets found")

    output_dir.mkdir(parents=True, exist_ok=True)
    records: list[dict[str, object]] = []

    for target in q_targets:
        target_id = target["id"]
        source_dir = artifact_root / target_id
        if source_dir.is_symlink() or not source_dir.is_dir():
            raise SystemExit(f"Missing Q debug symbol directory for target {target_id}: {source_dir}")

        elf_source = source_dir / "firmware.elf"
        map_source = source_dir / "openquatt.map"
        for required_path in (elf_source, map_source):
            if required_path.is_symlink() or not required_path.is_file():
                raise SystemExit(f"Missing Q debug symbol file for target {target_id}: {required_path}")

        target_output = output_dir / target_id
        target_output.mkdir(parents=True, exist_ok=True)
        elf_dest = target_output / "firmware.elf"
        map_dest = target_output / "openquatt.map"
        shutil.copy2(elf_source, elf_dest)
        shutil.copy2(map_source, map_dest)

        records.append(
            {
                "target_id": target_id,
                "build_target": target["config"],
                "source_commit": source_commit,
                "reporting_build_id": sha256sum(elf_dest),
                "esphome_version": esphome_version,
                "files": {
                    "elf": f"{target_id}/firmware.elf",
                    "map": f"{target_id}/openquatt.map",
                },
            }
        )

    index = {
        "schema_version": 1,
        "release": version,
        "targets": records,
    }
    (output_dir / "index.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Package Q release debug symbols.")
    parser.add_argument("version")
    parser.add_argument("source_commit")
    parser.add_argument("esphome_version")
    parser.add_argument("--artifact-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = create_parser().parse_args(argv)
    prepare_q_debug_symbols(
        args.version,
        args.source_commit,
        args.esphome_version,
        args.artifact_root,
        args.output_dir,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
