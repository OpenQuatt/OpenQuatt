#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


PATTERNS = ["*.c", "*.cc", "*.cpp", "*.cxx", "*.h", "*.hh", "*.hpp", "*.hxx"]
VERSION_FILE = ".clang-format-version"


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def resolve_clang_format_binary() -> str | None:
    env_override = os.environ.get("CLANG_FORMAT_BIN", "").strip()
    if env_override:
        candidate = Path(env_override).expanduser()
        if candidate.exists():
            return str(candidate)
        return None

    in_path = shutil.which("clang-format")
    if in_path:
        return in_path

    if os.name != "nt":
        return None

    windows_candidates = [
        Path("C:/Program Files/LLVM/bin/clang-format.exe"),
        Path("C:/Program Files (x86)/LLVM/bin/clang-format.exe"),
    ]
    local_app_data = os.environ.get("LOCALAPPDATA", "").strip()
    if local_app_data:
        windows_candidates.extend(
            [
                Path(local_app_data) / "Programs" / "LLVM" / "bin" / "clang-format.exe",
                Path(local_app_data) / "Microsoft" / "WinGet" / "Packages",
            ]
        )

    for candidate in windows_candidates:
        if candidate.is_file():
            return str(candidate)

    if local_app_data:
        winget_packages = Path(local_app_data) / "Microsoft" / "WinGet" / "Packages"
        if winget_packages.is_dir():
            matches = list(winget_packages.glob("**/clang-format.exe"))
            if matches:
                return str(matches[0])

    return None


def required_clang_format_version(root: Path) -> str:
    version = (root / VERSION_FILE).read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit(f"Invalid clang-format version in {VERSION_FILE}: {version!r}")
    return version


def parse_clang_format_version(output: str) -> str | None:
    match = re.search(r"clang-format version (\d+\.\d+\.\d+)", output)
    return match.group(1) if match else None


def installed_clang_format_version(clang_format_bin: str) -> str | None:
    completed = subprocess.run(
        [clang_format_bin, "--version"], capture_output=True, text=True, check=False
    )
    if completed.returncode != 0:
        return None
    return parse_clang_format_version(completed.stdout)


def list_cpp_files(root: Path) -> list[str]:
    command = ["git", "ls-files", "--cached", "--others", "--exclude-standard", "--", *PATTERNS]
    completed = subprocess.run(command, cwd=root, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip() or "git ls-files failed"
        raise SystemExit(message)
    files = {line.strip() for line in completed.stdout.splitlines() if line.strip()}
    return sorted(files)


def chunked(items: list[str], size: int) -> list[list[str]]:
    return [items[index : index + size] for index in range(0, len(items), size)]


def run_clang_format(clang_format_bin: str, root: Path, files: list[str], mode: str) -> int:
    if mode == "check":
        base_command = [clang_format_bin, "--style=file", "--dry-run", "--Werror"]
    else:
        base_command = [clang_format_bin, "--style=file", "-i"]

    max_batch_size = 100
    overall_return_code = 0
    for group in chunked(files, max_batch_size):
        command = [*base_command, *group]
        completed = subprocess.run(command, cwd=root, check=False)
        if completed.returncode != 0:
            overall_return_code = completed.returncode

    return overall_return_code


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check or apply clang-format on repository C/C++ files.")
    parser.add_argument("mode", choices=["check", "fix"], help="Use 'check' for verification and 'fix' to rewrite.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = repo_root()
    clang_format_bin = resolve_clang_format_binary()
    if not clang_format_bin:
        print("clang-format was not found in PATH.", file=sys.stderr)
        print("Install clang-format or set CLANG_FORMAT_BIN to its executable path.", file=sys.stderr)
        return 2

    required_version = required_clang_format_version(root)
    installed_version = installed_clang_format_version(clang_format_bin)
    if installed_version != required_version:
        found = installed_version or "unknown"
        print(
            f"clang-format {required_version} is required; found {found} at {clang_format_bin}.",
            file=sys.stderr,
        )
        print(
            f"Install it with: uv tool install --force clang-format=={required_version}",
            file=sys.stderr,
        )
        return 2

    files = list_cpp_files(root)
    if not files:
        print("No C/C++ source files found.")
        return 0

    verb = "Checking" if args.mode == "check" else "Formatting"
    print(f"{verb} {len(files)} C/C++ files with clang-format...")
    return_code = run_clang_format(clang_format_bin, root, files, args.mode)
    if return_code != 0:
        if args.mode == "check":
            print("Formatting differences detected.", file=sys.stderr)
            print("Run: pnpm run fix:cpp-format", file=sys.stderr)
            return 1
        return return_code

    if args.mode == "check":
        print("C/C++ formatting check passed.")
    else:
        print("C/C++ formatting updated.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
