"""Build consistent, bounded C++ host regressions without invoking a shell."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

from tests.support.process import run_process


COMPILE_TIMEOUT_SECONDS = 120
RUN_TIMEOUT_SECONDS = 30


def compiler_command(*extra_flags: str) -> list[str]:
    """Return the configured compiler, warning policy and optional sanitizers.

    CXX supports quoted executable paths and wrapper arguments, such as
    ``ccache clang++``. Assertions stay enabled even if CXX defines NDEBUG.
    OPENQUATT_TEST_SANITIZERS is empty/none or address,undefined; invalid values
    fail instead of accidentally running an unsanitized regression.
    """
    sanitizer = os.environ.get("OPENQUATT_TEST_SANITIZERS", "").strip()
    if sanitizer not in ("", "none", "address,undefined"):
        raise ValueError(
            "OPENQUATT_TEST_SANITIZERS must be empty, 'none', or "
            f"'address,undefined' (got {sanitizer!r})"
        )
    try:
        command = shlex.split(os.environ.get("CXX", "c++"))
    except ValueError as error:
        raise ValueError(f"Invalid CXX command: {error}") from error
    if not command or shutil.which(command[0]) is None:
        executable = command[0] if command else "<empty>"
        raise RuntimeError(f"C++ compiler executable not found: {executable!r}; set CXX or install a C++ compiler")

    command.extend(["-std=c++17", "-Wall", "-Wextra", "-Werror"])
    if sys.platform == "darwin":
        sdk = os.environ.get("SDKROOT", "").strip()
        if not sdk:
            if shutil.which("xcrun") is None:
                raise RuntimeError("macOS SDK lookup requires xcrun; install Command Line Tools or set SDKROOT")
            try:
                sdk = run_process(
                    ["xcrun", "--sdk", "macosx", "--show-sdk-path"],
                    text=True, capture_output=True, check=True, timeout=10,
                ).stdout.strip()
            except (OSError, subprocess.SubprocessError) as error:
                raise RuntimeError("Unable to locate macOS SDK; check xcrun or set SDKROOT") from error
            if not sdk:
                raise RuntimeError("xcrun returned no macOS SDK; check Command Line Tools or set SDKROOT")
        headers = Path(sdk) / "usr/include/c++/v1"
        if headers.is_dir():
            command.extend(["-isystem", str(headers)])
    if sanitizer == "address,undefined":
        command.extend([
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer", "-g",
        ])
    command.extend(extra_flags)
    command.append("-UNDEBUG")
    return command
