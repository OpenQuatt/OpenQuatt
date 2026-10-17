#!/usr/bin/env python3
"""Run OpenQuatt's hardware-independent tests with the same checks as CI."""

from __future__ import annotations

import argparse
import importlib.metadata
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tests.support.cpp import (  # noqa: E402
    COMPILE_TIMEOUT_SECONDS,
    RUN_TIMEOUT_SECONDS,
    compiler_command,
)
from tests.support.process import run_process  # noqa: E402


SUITES = ("host", "python", "hil", "web")


def test_files(suite: str, pattern: str | None = None) -> list[Path]:
    legacy = sorted((ROOT / "scripts/tests").glob("test_*.py"))
    if legacy:
        raise RuntimeError(
            "Tests still exist in scripts/tests; move them to tests/python so discovery cannot silently omit them."
        )
    directory, default_pattern = {
        "host": (ROOT / "tests/host", "*_test.cpp"),
        "python": (ROOT / "tests/python", "test_*.py"),
        "hil": (ROOT / "tests/hil", "*.test.mjs"),
        "web": (ROOT / "openquatt/web/tests", "*.test.mjs"),
    }[suite]
    files = sorted(path for path in directory.glob(pattern or default_pattern) if path.is_file())
    if not files:
        raise RuntimeError(f"No {suite} tests found ({directory / (pattern or default_pattern)}).")
    return files


def require_tool(name: str) -> str:
    executable = shutil.which(name)
    if executable is None:
        raise RuntimeError(f"Required test tool is missing: {name}.")
    return executable


def check_node() -> None:
    required_major = (ROOT / ".node-version").read_text().strip().split(".")[0]
    version = run_process(
        [require_tool("node"), "--version"], capture_output=True, text=True, check=True, timeout=10
    ).stdout.strip()
    if version.removeprefix("v").split(".")[0] != required_major:
        raise RuntimeError(f"Node.js {required_major}.x is required; found {version}. See .node-version.")


def check_python_environment() -> None:
    requirement = (ROOT / ".github/requirements-esphome.txt").read_text().strip()
    expected = requirement.removeprefix("esphome==")
    try:
        installed = importlib.metadata.version("esphome")
    except importlib.metadata.PackageNotFoundError:
        installed = "not installed"
    if installed != expected:
        raise RuntimeError(
            f"Python tests require {requirement}; found {installed} in {sys.executable}. "
            "Run python3 scripts/dev.py bootstrap, then activate .venv/bin/activate."
        )
    require_tool("jq")
    compiler_command()
    check_node()


def run_host(files: list[Path]) -> None:
    command = compiler_command("-I", str(ROOT), "-I", str(ROOT / "openquatt"))
    with tempfile.TemporaryDirectory(prefix="openquatt-host-tests-") as directory:
        for source in files:
            binary = Path(directory) / source.stem
            print(f"[build] {source.stem}", flush=True)
            run_process(
                [*command, str(source), "-o", str(binary)],
                cwd=ROOT,
                check=True,
                timeout=COMPILE_TIMEOUT_SECONDS,
            )
            print(f"[run] {source.stem}", flush=True)
            run_process([str(binary)], cwd=ROOT, check=True, timeout=RUN_TIMEOUT_SECONDS)
    print(f"Host regression tests passed ({len(files)}).", flush=True)


def run_python_suite(suite: unittest.TestSuite, stream=None) -> bool:
    if suite.countTestCases() == 0:
        raise RuntimeError("Python test discovery produced an empty suite.")
    result = unittest.TextTestRunner(verbosity=2, stream=stream).run(suite)
    if result.skipped:
        print("Required Python tests were skipped:", file=stream or sys.stderr)
        for test, reason in result.skipped:
            print(f"- {test.id()}: {reason}", file=stream or sys.stderr)
    return result.wasSuccessful() and not result.skipped


def run_python(files: list[Path]) -> bool:
    # Real ESPHome package composition validates the generated web asset paths.
    run_process([require_tool("npm"), "run", "build:web"], cwd=ROOT, check=True, timeout=120)
    names = [".".join(path.relative_to(ROOT).with_suffix("").parts) for path in files]
    return run_python_suite(unittest.defaultTestLoader.loadTestsFromNames(names))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suites", nargs="*", choices=(*SUITES, "all"), default=["all"])
    parser.add_argument("--list", action="store_true", help="List test files without running tools or tests.")
    parser.add_argument("--pattern", help="Filename glob for one host or Python suite; quote the glob.")
    parser.add_argument("--sanitize", action="store_true", help="Enable ASan and UBSan for all C++ test harnesses.")
    args = parser.parse_args(argv)
    suites = list(SUITES) if "all" in args.suites else list(dict.fromkeys(args.suites))
    if args.pattern and (len(suites) != 1 or suites[0] not in ("host", "python")):
        parser.error("--pattern requires exactly one suite: host or python")
    if args.sanitize:
        os.environ["OPENQUATT_TEST_SANITIZERS"] = "address,undefined"
    try:
        files_by_suite = {suite: test_files(suite, args.pattern) for suite in suites}
        if args.list:
            for suite, files in files_by_suite.items():
                print(f"{suite}: {len(files)} test files")
                for path in files:
                    print(f"  {path.relative_to(ROOT)}")
            return 0
        if "python" in suites:
            check_python_environment()
        else:
            if "host" in suites:
                compiler_command()
            if "hil" in suites or "web" in suites:
                check_node()
        if "web" in suites or "python" in suites:
            require_tool("npm")
        for suite in suites:
            print(f"=== {suite} tests ===", flush=True)
            if suite == "host":
                run_host(files_by_suite[suite])
            elif suite == "python":
                if not run_python(files_by_suite[suite]):
                    return 1
            elif suite == "hil":
                check_node()
                run_process(
                    [require_tool("node"), "--test", *map(str, files_by_suite[suite])],
                    cwd=ROOT, check=True, timeout=180,
                )
            elif suite == "web":
                check_node()
                run_process([require_tool("npm"), "run", "check:web"], cwd=ROOT, check=True, timeout=300)
        return 0
    except KeyboardInterrupt:
        print("Test run interrupted.", file=sys.stderr)
        return 130
    except (RuntimeError, ValueError, OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
        print(f"Test run failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
