#!/usr/bin/env python3
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Iterable, Sequence

from build_targets import filter_targets, load_targets

MIN_BOOTSTRAP_PYTHON = (3, 12)

def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def enabled_targets() -> list[dict[str, str]]:
    return filter_targets(load_targets(), "enabled")


def default_configs() -> list[str]:
    return [target["config"] for target in enabled_targets()]


def factory_files() -> list[str]:
    return [f"{target['artifact_name']}.firmware.factory.bin" for target in enabled_targets()]


def build_path_by_config() -> dict[str, str]:
    return {target["config"]: target["build_path"] for target in load_targets()}


def config_log_stem(config: str) -> str:
    path = Path(config)
    return "_".join((*path.parent.parts, path.stem)) if path.parent.parts else path.stem


def resolve_path(path_value: str) -> Path:
    path = Path(path_value)
    if path.is_absolute():
        return path
    return repo_root() / path


def venv_bin_dir(venv_dir: Path) -> Path:
    return venv_dir / "bin"


def _existing_path(paths: Iterable[Path]) -> Path | None:
    for path in paths:
        if path.exists():
            return path
    return None


def venv_python_path(venv_dir: Path) -> Path | None:
    return _existing_path(
        (
            venv_bin_dir(venv_dir) / "python",
            venv_bin_dir(venv_dir) / "python3",
        )
    )


def resolve_helper_python(venv_dir: Path) -> list[str]:
    candidate = venv_python_path(venv_dir)
    if candidate is not None:
        return [str(candidate)]
    return [sys.executable]


def resolve_esphome_command(venv_dir: Path) -> list[str]:
    candidate = _existing_path(
        (
            venv_bin_dir(venv_dir) / "esphome",
        )
    )
    if candidate is not None:
        return [str(candidate)]

    in_path = shutil.which("esphome")
    if in_path:
        return [in_path]

    raise SystemExit(
        "ESPHome executable not found. Run the bootstrap command first or install "
        "'esphome' in PATH."
    )


def bootstrap_python_candidates(explicit_python: str) -> list[str]:
    if explicit_python:
        return [explicit_python]

    candidates: list[str] = []
    seen: set[str] = set()

    def add(candidate: str | None) -> None:
        if not candidate:
            return
        path = Path(candidate).expanduser()
        try:
            resolved = str(path.resolve())
        except OSError:
            resolved = str(path)
        if resolved in seen or not Path(resolved).exists():
            return
        seen.add(resolved)
        candidates.append(resolved)

    if sys.platform == "darwin":
        for candidate in (
            "/opt/homebrew/bin/python3",
            "/usr/local/bin/python3",
            "/Library/Frameworks/Python.framework/Versions/3.13/bin/python3",
            "/Library/Frameworks/Python.framework/Versions/3.12/bin/python3",
            "/usr/bin/python3",
        ):
            add(candidate)

    add(sys.executable)
    add(shutil.which("python3"))
    add(shutil.which("python"))

    return candidates


def can_create_bootstrap_venv(python_exe: str, root_dir: Path) -> tuple[bool, str]:
    minimum = ".".join(str(part) for part in MIN_BOOTSTRAP_PYTHON)
    version_check = subprocess.run(
        [
            python_exe,
            "-c",
            (
                "import sys; "
                "print('.'.join(str(part) for part in sys.version_info[:3])); "
                f"raise SystemExit(0 if sys.version_info >= {MIN_BOOTSTRAP_PYTHON!r} else 1)"
            ),
        ],
        cwd=root_dir,
        capture_output=True,
        text=True,
        check=False,
    )
    detected = version_check.stdout.strip() or "unknown"
    if version_check.returncode != 0:
        return False, f"Python {detected} is too old; ESPHome 2026.8 requires Python {minimum} or newer"

    with tempfile.TemporaryDirectory(prefix="openquatt-bootstrap-check-") as tmp_dir:
        probe_venv = Path(tmp_dir) / "venv"
        completed = subprocess.run(
            [python_exe, "-m", "venv", str(probe_venv)],
            cwd=root_dir,
            capture_output=True,
            text=True,
            check=False,
        )
        if completed.returncode != 0:
            detail = completed.stderr.strip() or completed.stdout.strip() or "venv creation failed"
            return False, detail

        probe_python = venv_python_path(probe_venv)
        if probe_python is None:
            return False, "venv created without a Python executable"

        pip_check = subprocess.run(
            [str(probe_python), "-m", "pip", "--version"],
            cwd=root_dir,
            capture_output=True,
            text=True,
            check=False,
        )
        if pip_check.returncode != 0:
            detail = pip_check.stderr.strip() or pip_check.stdout.strip() or "pip is unavailable inside the venv"
            return False, detail

    return True, ""


def resolve_bootstrap_python(explicit_python: str, root_dir: Path) -> str:
    failures: list[str] = []
    for candidate in bootstrap_python_candidates(explicit_python):
        ok, detail = can_create_bootstrap_venv(candidate, root_dir)
        if ok:
            return candidate
        failures.append(f"{candidate}: {detail}")

    if failures:
        joined = "\n".join(f"  - {failure}" for failure in failures)
        raise SystemExit(
            "No usable Python executable found for bootstrap.\n"
            "Tried:\n"
            f"{joined}\n"
            "Pass --python-exe /path/to/python3 to override."
        )

    raise SystemExit("No Python executable found for bootstrap.")


def ensure_factory_dir(factory_dir: Path) -> list[str]:
    if not factory_dir.is_dir():
        raise SystemExit(f"Factory firmware directory does not exist: {factory_dir}")

    files = sorted(path.name for path in factory_dir.glob("*.firmware.factory.bin") if path.is_file())
    if not files:
        raise SystemExit(f"Factory firmware directory contains no *.firmware.factory.bin files: {factory_dir}")
    return files


def tail_lines(path: Path, limit: int = 80) -> str:
    if not path.exists():
        return ""
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        lines = handle.readlines()
    return "".join(lines[-limit:])


def format_duration(total_seconds: float) -> str:
    seconds = int(total_seconds)
    minutes, seconds = divmod(seconds, 60)
    hours, minutes = divmod(minutes, 60)
    if hours > 0:
        return f"{hours}h{minutes:02d}m{seconds:02d}s"
    if minutes > 0:
        return f"{minutes}m{seconds:02d}s"
    return f"{seconds}s"


def run_command(
    command: Sequence[str],
    *,
    cwd: Path,
    env: dict[str, str] | None = None,
    log_path: Path | None = None,
    check: bool = True,
    heartbeat_label: str | None = None,
    heartbeat_interval_s: float = 20.0,
) -> int:
    if log_path is None:
        completed = subprocess.run(command, cwd=cwd, env=env, check=False)
        if check and completed.returncode != 0:
            raise subprocess.CalledProcessError(completed.returncode, command)
        return completed.returncode

    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8", errors="replace") as handle:
        started_at = time.monotonic()
        next_heartbeat_at = started_at + heartbeat_interval_s
        process = subprocess.Popen(
            command,
            cwd=cwd,
            env=env,
            stdout=handle,
            stderr=subprocess.STDOUT,
            text=True,
        )
        while True:
            exit_code = process.poll()
            if exit_code is not None:
                break
            if heartbeat_label is not None:
                now = time.monotonic()
                if now >= next_heartbeat_at:
                    print(f"[wait] {heartbeat_label} ({format_duration(now - started_at)})", flush=True)
                    next_heartbeat_at = now + heartbeat_interval_s
            time.sleep(1.0)

    if check and exit_code != 0:
        raise subprocess.CalledProcessError(exit_code, command)

    return exit_code


def run_logged(
    command: Sequence[str],
    *,
    cwd: Path,
    env: dict[str, str] | None,
    log_path: Path,
    label: str,
) -> None:
    print(f"[run] {label}", flush=True)
    exit_code = run_command(
        command,
        cwd=cwd,
        env=env,
        log_path=log_path,
        check=False,
        heartbeat_label=label,
    )
    if exit_code != 0:
        print(f"[FAIL] {label}", file=sys.stderr)
        tail = tail_lines(log_path)
        if tail:
            print(tail, file=sys.stderr, end="" if tail.endswith("\n") else "\n")
        raise SystemExit(f"{label} failed. Full log: {log_path}")

    print(f"[ok] {label}")


def default_native_idf_tools_dir() -> Path:
    if sys.platform == "darwin":
        cache_root = Path.home() / "Library" / "Caches"
    elif os.name == "nt":
        cache_root = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
    else:
        cache_root = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    return cache_root / "esphome" / "idf"


def configure_native_ccache(
    env: dict[str, str],
    native_idf_tools_dir: Path,
) -> tuple[bool, str]:
    explicit_setting = env.get("IDF_CCACHE_ENABLE")
    if explicit_setting is not None and explicit_setting.strip().lower() in {"", "0", "false", "no", "off"}:
        return False, "disabled by IDF_CCACHE_ENABLE"

    ccache_executable = shutil.which("ccache", path=env.get("PATH"))
    if ccache_executable is None:
        if explicit_setting is not None:
            raise SystemExit("IDF_CCACHE_ENABLE is set, but the ccache executable is not available in PATH.")
        return False, "not installed"

    env.setdefault("IDF_CCACHE_ENABLE", "1")
    env.setdefault("CCACHE_DIR", str(native_idf_tools_dir / "ccache"))
    env.setdefault("CCACHE_MAXSIZE", "2G")
    env.setdefault("CCACHE_NOHASHDIR", "true")
    env.setdefault("CCACHE_DEPEND", "1")
    return True, f"{ccache_executable} (cache: {env['CCACHE_DIR']})"


def build_pages_site(site_dir: Path, factory_dir: Path, helper_python: Sequence[str]) -> None:
    root_dir = repo_root()
    available_factory_files = ensure_factory_dir(factory_dir)

    run_command(["npm", "run", "build:web:preview"], cwd=root_dir)

    if site_dir.exists():
        shutil.rmtree(site_dir)

    (site_dir / "firmware" / "main").mkdir(parents=True, exist_ok=True)
    (site_dir / "css").mkdir(parents=True, exist_ok=True)
    (site_dir / "js").mkdir(parents=True, exist_ok=True)
    shutil.copytree(root_dir / "docs", site_dir, dirs_exist_ok=True)

    for stale_file in ("onderhoudsgids.md", "releaseproces.md"):
        stale_path = site_dir / stale_file
        if stale_path.exists():
            stale_path.unlink()

    run_command(
        [*helper_python, str(root_dir / "scripts" / "build_pages_docs.py"), str(site_dir)],
        cwd=root_dir,
    )

    for relative_path in (
        "css/openquatt-preview.css",
        "js/mock-scenarios.js",
        "js/mock-incident-scenarios.js",
        "js/mock-entity-defs.js",
        "js/mock-fixtures.js",
        "js/mock-device.js",
        "js/openquatt-preview.js",
    ):
        shutil.copy2(root_dir / "openquatt" / "web" / relative_path, site_dir / relative_path)

    demo_html = (root_dir / "openquatt" / "web" / "dev.html").read_text(encoding="utf-8")
    demo_html = demo_html.replace("<title>OpenQuatt UI Preview</title>", "<title>OpenQuatt web-app demo</title>")
    demo_html = demo_html.replace(
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        '<meta name="viewport" content="width=device-width, initial-scale=1">\n    <base href="../">',
    )
    demo_dir = site_dir / "demo"
    demo_dir.mkdir(parents=True, exist_ok=True)
    (demo_dir / "index.html").write_text(demo_html, encoding="utf-8")

    (site_dir / ".nojekyll").touch()

    for file_name in available_factory_files:
        shutil.copy2(factory_dir / file_name, site_dir / "firmware" / "main" / file_name)

    (site_dir / "firmware" / "main" / "factory_files.json").write_text(
        json.dumps({"factory_files": available_factory_files}, indent=2) + "\n",
        encoding="utf-8",
    )


def describe_version(root_dir: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(root_dir), "describe", "--tags", "--always", "--dirty"],
        capture_output=True,
        text=True,
        check=False,
    )
    label = result.stdout.strip()
    return label or "local-preview"


def default_jobs() -> int:
    raw_jobs = os.environ.get("JOBS", "2")
    try:
        value = int(raw_jobs)
    except ValueError as exc:
        raise SystemExit("JOBS must be a positive integer.") from exc
    if value < 1:
        raise SystemExit("JOBS must be a positive integer.")
    return value


def bootstrap_command(args: argparse.Namespace) -> int:
    root_dir = repo_root()
    venv_dir = resolve_path(args.venv_dir)
    requirements_file = root_dir / ".github" / "requirements-esphome.txt"
    python_exe = resolve_bootstrap_python(args.python_exe, root_dir)

    print(f"Using Python: {python_exe}")
    print(f"Virtual environment: {venv_dir}")

    run_command([python_exe, "-m", "venv", "--clear", str(venv_dir)], cwd=root_dir)

    venv_python = venv_python_path(venv_dir)
    if venv_python is None:
        raise SystemExit(f"Virtual environment Python not found under {venv_dir}")

    run_command([str(venv_python), "-m", "pip", "install", "--upgrade", "pip"], cwd=root_dir)
    run_command(
        [str(venv_python), "-m", "pip", "install", "-r", str(requirements_file)],
        cwd=root_dir,
    )
    run_command([str(venv_python), "-m", "esphome", "version"], cwd=root_dir)

    print()
    print("Local ESPHome environment is ready.")
    print("Bootstrap again: python3 scripts/dev.py bootstrap")
    print("Validate/compile: python3 scripts/dev.py validate")
    return 0


def validate_command(args: argparse.Namespace) -> int:
    root_dir = repo_root()
    venv_dir = resolve_path(args.venv_dir)
    command_root = root_dir
    log_dir = root_dir / ".tmp" / "validate_local_logs"
    helper_python = resolve_helper_python(venv_dir)
    esphome_command = resolve_esphome_command(venv_dir)
    target_build_paths = build_path_by_config()

    log_dir.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env.setdefault("ESPHOME_ESP_IDF_PREFIX", str(default_native_idf_tools_dir()))
    native_idf_tools_dir = Path(env["ESPHOME_ESP_IDF_PREFIX"]).expanduser()
    ccache_enabled, ccache_status = configure_native_ccache(env, native_idf_tools_dir)

    print(f"Workspace root: {root_dir}")
    print(f"ESP-IDF tools dir: {native_idf_tools_dir}")
    print(f"ccache: {ccache_status}")
    print(f"Log dir: {log_dir}")
    print(f"Parallel compile jobs: {args.jobs}")

    command_scripts_dir = command_root / "scripts"
    run_logged(
        [*helper_python, str(command_scripts_dir / "check_style_consistency.py")],
        cwd=command_root,
        env=env,
        log_path=log_dir / "style-consistency.log",
        label="style consistency",
    )
    run_logged(
        [*helper_python, str(command_scripts_dir / "check_docs_consistency.py")],
        cwd=command_root,
        env=env,
        log_path=log_dir / "docs-consistency.log",
        label="docs consistency",
    )
    run_logged(
        ["node", str(command_scripts_dir / "check_web_docs_sync.mjs")],
        cwd=command_root,
        env=env,
        log_path=log_dir / "web-docs-sync.log",
        label="web/docs contract",
    )
    run_logged(
        ["npm", "run", "build:web"],
        cwd=command_root,
        env=env,
        log_path=log_dir / "web-bundles.log",
        label="web bundles",
    )

    for config in args.configs:
        stem = config_log_stem(config)
        run_logged(
            [*esphome_command, "config", config],
            cwd=command_root,
            env=env,
            log_path=log_dir / f"{stem}.config.log",
            label=f"config {config}",
        )
        run_logged(
            [*helper_python, str(command_scripts_dir / "check_nvs_budget.py"), config],
            cwd=command_root,
            env=env,
            log_path=log_dir / f"{stem}.nvs-budget.log",
            label=f"NVS budget {config}",
        )

    if args.config_only:
        print()
        print("Validation complete.")
        return 0

    compile_queue = list(args.configs)
    frameworks_dir = native_idf_tools_dir / "frameworks"
    cold_native_idf_cache = not frameworks_dir.exists() or not any(frameworks_dir.iterdir())
    if args.jobs > 1 and cold_native_idf_cache:
        print(
            "Cold native ESP-IDF cache detected; compiling the first target separately "
            "before starting parallel target builds."
        )

    def compile_one(config: str) -> tuple[str, int, Path]:
        log_path = log_dir / f"{config_log_stem(config)}.compile.log"
        label = f"compile {config}"
        build_root = command_root / target_build_paths.get(config, f".esphome/build/{Path(config).stem}")
        compile_env = env.copy()
        if ccache_enabled:
            compile_env.setdefault("CCACHE_BASEDIR", str(build_root.resolve()))
        print(f"[run] {label}", flush=True)
        exit_code = run_command(
            [*esphome_command, "compile", config],
            cwd=command_root,
            env=compile_env,
            log_path=log_path,
            check=False,
            heartbeat_label=label,
        )
        if exit_code != 0:
            tail = tail_lines(log_path, limit=160)
            tail_lower = tail.lower()
            cmake_cache_mismatch = (
                "does not match the source" in tail_lower
                and "used to generate cache" in tail_lower
            )
            retryable_failure = any(
                marker in tail_lower
                for marker in (
                    "connection reset by peer",
                    "failed to download",
                    "temporary failure in name resolution",
                    "timed out while downloading",
                    "another process",
                )
            ) or ("idf_component_manager" in tail_lower and "lock" in tail_lower)
            retryable_failure = retryable_failure or (
                "downloaded component" in tail_lower and "corrupted" in tail_lower
            )
            retryable_failure = retryable_failure or "does not contain a component" in tail_lower
            if cmake_cache_mismatch:
                print(
                    f"[retry] compile {config}: resetting the native CMake build directory "
                    "after the ESP-IDF cache location changed."
                )
                shutil.rmtree(build_root / "build", ignore_errors=True)
            elif retryable_failure:
                print(
                    f"[retry] compile {config}: retrying after a transient native ESP-IDF failure."
                )
            if cmake_cache_mismatch or retryable_failure:
                exit_code = run_command(
                    [*esphome_command, "compile", config],
                    cwd=command_root,
                    env=compile_env,
                    log_path=log_path,
                    check=False,
                    heartbeat_label=label,
                )
        if exit_code == 0:
            build_dir = build_root / "build"
            artifact_log_path = log_dir / f"{config_log_stem(config)}.artifacts.log"
            exit_code = run_command(
                [
                    *helper_python,
                    str(command_scripts_dir / "repair_factory_bin.py"),
                    str(build_dir),
                    "--normalize-app",
                ],
                cwd=command_root,
                env=compile_env,
                log_path=artifact_log_path,
                check=False,
                heartbeat_label=f"validate artifacts {config}",
            )
            if exit_code != 0:
                log_path = artifact_log_path
        return config, exit_code, log_path

    results: list[tuple[str, int, Path]] = []
    if compile_queue and args.jobs > 1 and cold_native_idf_cache:
        results.append(compile_one(compile_queue.pop(0)))

    if compile_queue:
        if args.jobs == 1:
            results = [compile_one(config) for config in compile_queue]
        else:
            compile_groups: dict[Path, list[str]] = {}
            for config in compile_queue:
                compile_groups.setdefault(Path(config).parent, []).append(config)

            def compile_group(configs: list[str]) -> list[tuple[str, int, Path]]:
                return [compile_one(config) for config in configs]

            worker_count = min(args.jobs, len(compile_groups))
            with concurrent.futures.ThreadPoolExecutor(max_workers=worker_count) as executor:
                futures = [executor.submit(compile_group, configs) for configs in compile_groups.values()]
                for future in concurrent.futures.as_completed(futures):
                    results.extend(future.result())

    order = {config: index for index, config in enumerate(args.configs)}
    results.sort(key=lambda item: order[item[0]])

    failures = 0
    for config, exit_code, log_path in results:
        if exit_code != 0:
            failures += 1
            print(f"[FAIL] compile {config}", file=sys.stderr)
            tail = tail_lines(log_path)
            if tail:
                print(tail, file=sys.stderr, end="" if tail.endswith("\n") else "\n")
            continue
        print(f"[ok] compile {config}")

    if failures:
        raise SystemExit(f"Validation finished with {failures} compile failure(s).")

    print()
    print("Validation complete.")
    return 0


def prepare_pages_site_command(args: argparse.Namespace) -> int:
    venv_dir = resolve_path(args.venv_dir)
    helper_python = resolve_helper_python(venv_dir)
    build_pages_site(Path(args.site_dir).resolve(), Path(args.factory_bin_dir).resolve(), helper_python)
    return 0


def preview_pages_command(args: argparse.Namespace) -> int:
    root_dir = repo_root()
    venv_dir = resolve_path(args.venv_dir)
    helper_python = resolve_helper_python(venv_dir)

    temp_dir = Path(tempfile.mkdtemp(prefix="openquatt-pages-preview."))
    site_dir = temp_dir / "site"
    work_firmware_dir = temp_dir / "firmware"
    work_firmware_dir.mkdir(parents=True, exist_ok=True)

    try:
        if args.firmware_dir:
            source_dir = Path(args.firmware_dir).resolve()
            for file_name in ensure_factory_dir(source_dir):
                shutil.copy2(source_dir / file_name, work_firmware_dir / file_name)
        else:
            for file_name in factory_files():
                (work_firmware_dir / file_name).touch()

        build_pages_site(site_dir, work_firmware_dir, helper_python)
        version_path = site_dir / "firmware" / "main" / "version.json"
        version_path.write_text(
            json.dumps(
                {
                    "version": describe_version(root_dir),
                    "channel": "local",
                    "release_url": "https://github.com/OpenQuatt/OpenQuatt/releases/latest",
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )

        print("Local Pages preview ready.")
        print(f"Preview directory: {site_dir}")
        print("Open:")
        print(f"  http://{args.host}:{args.port}/")
        print(f"  http://{args.host}:{args.port}/verwarmen-en-koelen.html")
        print(f"  http://{args.host}:{args.port}/install/index.html")

        if not args.firmware_dir:
            print()
            print("Using placeholder firmware binaries.")
            print("Use --firmware-dir <dir> if you want to test with real factory images.")

        if args.no_serve:
            print()
            print("Build completed without starting the HTTP server because --no-serve was used.")
            if not args.keep:
                print("Use --keep if you want to inspect the generated preview directory after the command exits.")
            return 0

        print()
        print("Stop with Ctrl+C.")
        run_command(
            [
                *helper_python,
                "-m",
                "http.server",
                str(args.port),
                "--bind",
                args.host,
                "--directory",
                str(site_dir),
            ],
            cwd=root_dir,
            check=False,
        )
        return 0
    finally:
        if args.keep:
            print(f"Preview directory kept at: {temp_dir}")
        else:
            shutil.rmtree(temp_dir, ignore_errors=True)


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Unified local developer commands for OpenQuatt.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    bootstrap_parser = subparsers.add_parser(
        "bootstrap",
        help="Create or update the local ESPHome virtual environment.",
    )
    bootstrap_parser.add_argument("--python-exe", default="", help="Python executable to use for venv creation.")
    bootstrap_parser.add_argument("--venv-dir", default=".venv", help="Virtual environment directory.")
    bootstrap_parser.set_defaults(func=bootstrap_command)

    validate_parser = subparsers.add_parser(
        "validate",
        help="Run style/docs validation and validate or compile all firmware profiles.",
    )
    validate_parser.add_argument(
        "--config",
        dest="configs",
        action="append",
        default=[],
        help="Validate only the given config file. May be passed multiple times.",
    )
    validate_parser.add_argument(
        "--config-only",
        action="store_true",
        help="Skip firmware compilation after config validation.",
    )
    validate_parser.add_argument("--venv-dir", default=".venv", help="Virtual environment directory.")
    validate_parser.add_argument(
        "--jobs",
        type=int,
        default=default_jobs(),
        help="Maximum number of concurrent compile jobs.",
    )
    validate_parser.set_defaults(func=validate_command)

    prepare_parser = subparsers.add_parser(
        "prepare-pages-site",
        help="Assemble the local Pages site from docs and factory binaries.",
    )
    prepare_parser.add_argument("site_dir", help="Output site directory.")
    prepare_parser.add_argument("factory_bin_dir", help="Directory containing factory firmware binaries.")
    prepare_parser.add_argument("--venv-dir", default=".venv", help="Virtual environment directory.")
    prepare_parser.set_defaults(func=prepare_pages_site_command)

    preview_parser = subparsers.add_parser(
        "preview-pages",
        help="Build and optionally serve a local Pages preview.",
    )
    preview_parser.add_argument("--port", type=int, default=8000, help="HTTP port to use.")
    preview_parser.add_argument("--host", default="127.0.0.1", help="Bind host for the preview server.")
    preview_parser.add_argument(
        "--firmware-dir",
        default="",
        help="Directory containing real *.firmware.factory.bin files.",
    )
    preview_parser.add_argument("--keep", action="store_true", help="Keep the temporary preview directory.")
    preview_parser.add_argument(
        "--no-serve",
        action="store_true",
        help="Build the preview but do not start the HTTP server.",
    )
    preview_parser.add_argument("--venv-dir", default=".venv", help="Virtual environment directory.")
    preview_parser.set_defaults(func=preview_pages_command)

    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = create_parser()
    args = parser.parse_args(argv)
    if getattr(args, "command", None) == "validate" and not args.configs:
        args.configs = default_configs()
    if getattr(args, "command", None) == "validate" and args.jobs < 1:
        parser.error("--jobs must be a positive integer")
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
