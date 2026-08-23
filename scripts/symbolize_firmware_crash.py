#!/usr/bin/env python3
"""Resolve crash PCs only after verifying the exact firmware ELF build ID."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Sequence

from firmware_build_metadata import SHA256_RE, parse_cmake_cache, sha256_file


ADDRESS_RE = re.compile(r"^0x[0-9a-fA-F]{1,16}$")
MAX_ADDRESSES = 64


class SymbolizationError(RuntimeError):
    pass


def normalize_build_id(value: str) -> str:
    value = value.strip().lower()
    if not SHA256_RE.fullmatch(value) or value == "0" * 64:
        raise SymbolizationError("expected_build_id must be a non-zero 64-character SHA-256")
    return value


def parse_addresses(values: Sequence[str]) -> list[str]:
    addresses: list[str] = []
    for value in values:
        for candidate in re.split(r"[\s,]+", value.strip()):
            if not candidate:
                continue
            if not ADDRESS_RE.fullmatch(candidate):
                raise SymbolizationError(f"Invalid crash address: {candidate!r}")
            addresses.append(f"0x{int(candidate, 16):x}")
    if not addresses:
        raise SymbolizationError("At least one crash address is required")
    if len(addresses) > MAX_ADDRESSES:
        raise SymbolizationError(f"At most {MAX_ADDRESSES} crash addresses are accepted")
    return addresses


def resolve_addr2line(cmake_cache: Path, explicit: Path | None = None) -> Path:
    if explicit is not None:
        executable = explicit.resolve()
    else:
        executable_value = parse_cmake_cache(cmake_cache).get("CMAKE_ADDR2LINE")
        if not executable_value:
            raise SymbolizationError(f"CMAKE_ADDR2LINE is missing from {cmake_cache}")
        executable = Path(executable_value).resolve()
    if not executable.is_file():
        raise SymbolizationError(f"addr2line executable is missing: {executable}")
    return executable


def symbolize(
    *,
    elf: Path,
    expected_build_id: str,
    addresses: Sequence[str],
    cmake_cache: Path,
    addr2line: Path | None = None,
) -> str:
    expected_build_id = normalize_build_id(expected_build_id)
    if not elf.is_file():
        raise SymbolizationError(f"Firmware ELF is missing: {elf}")
    actual_build_id = sha256_file(elf)
    if actual_build_id != expected_build_id:
        raise SymbolizationError(
            "ELF SHA-256 does not match captured build_id; refusing symbolization "
            f"({actual_build_id} != {expected_build_id})"
        )
    normalized_addresses = parse_addresses(addresses)
    executable = resolve_addr2line(cmake_cache, addr2line)
    completed = subprocess.run(
        [
            str(executable),
            "-a",
            "-f",
            "-C",
            "-i",
            "-e",
            str(elf.resolve()),
            *normalized_addresses,
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise SymbolizationError(f"addr2line failed: {detail}")
    return completed.stdout


def load_rebuild_result(path: Path) -> tuple[Path, Path, str]:
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
        elf = Path(result["elf"])
        cmake_cache = Path(result["cmake_cache"])
        build_id = str(result["build_id"])
    except (OSError, json.JSONDecodeError, KeyError, TypeError) as err:
        raise SymbolizationError(f"Cannot read reconstruction result: {err}") from err
    return elf, cmake_cache, normalize_build_id(build_id)


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rebuild-result", type=Path)
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--cmake-cache", type=Path)
    parser.add_argument("--addr2line", type=Path)
    parser.add_argument("--expected-build-id", required=True)
    parser.add_argument("addresses", nargs="+")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = create_parser().parse_args(argv)
    try:
        expected_build_id = normalize_build_id(args.expected_build_id)
        if args.rebuild_result:
            if args.elf or args.cmake_cache:
                raise SymbolizationError(
                    "Do not combine --rebuild-result with --elf or --cmake-cache"
                )
            elf, cmake_cache, rebuilt_id = load_rebuild_result(args.rebuild_result)
            if rebuilt_id != expected_build_id:
                raise SymbolizationError(
                    "Reconstruction result build_id does not match captured build_id"
                )
        else:
            if args.elf is None or args.cmake_cache is None:
                raise SymbolizationError(
                    "Provide --rebuild-result or both --elf and --cmake-cache"
                )
            elf, cmake_cache = args.elf, args.cmake_cache
        output = symbolize(
            elf=elf,
            expected_build_id=expected_build_id,
            addresses=args.addresses,
            cmake_cache=cmake_cache,
            addr2line=args.addr2line,
        )
    except SymbolizationError as err:
        raise SystemExit(str(err)) from err
    print(output, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
