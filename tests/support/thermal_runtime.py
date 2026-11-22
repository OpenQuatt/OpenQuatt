"""Compile selected firmware thermal runtime methods in the native test fixture."""

import re
from pathlib import Path


def write_thermal_runtime_methods(root: Path, build_directory: Path) -> None:
    _write_methods(root, build_directory, "oq_thermal_actuator", (
        "requested_mode_code",
        "set_mode",
        "write_mode_option_",
        "write_level",
        "record_transition",
        "resume_boot_cycle_",
        "apply_level_",
        "defrost_safety_stop_",
        "previous_applied_",
        "measured_thermal_mode_",
        "set_last_start_ms_",
        "set_last_stop_ms_",
    ))
    _write_methods(root, build_directory, "oq_thermal_request", (
        "tick",
        "run_automatic_",
        "update_startup_inhibit_",
        "startup_blocked_",
        "track_measured_start_",
        "measured_thermal_",
        "last_applied_",
        "mode_is_cooling_",
        "target_is_cooling_",
        "dew_stop_blocks_retained_",
        "physical_limit_",
        "allowed_",
        "publish_request_",
        "apply_minimum_runtime_",
    ))


def _write_methods(root: Path, build_directory: Path, module: str, methods: tuple[str, ...]) -> None:
    relative_path = Path("openquatt/includes/control") / f"{module}_runtime.h"
    source = (root / relative_path).read_text()
    extracted = []
    for method in methods:
        pattern = rf"^  (?:int|void|bool) {method}\([^{{]*\{{(?:[^\n]*\}}$|.*?^  \}}$)"
        match = re.search(pattern, source, re.M | re.S)
        if match is None:
            raise RuntimeError(f"Method {method} not found in {relative_path}")
        line = source.count("\n", 0, match.start()) + 1
        extracted.append(f'#line {line} "{relative_path}"\n{match.group()}\n')
    (build_directory / f"{module}_methods.inc").write_text("\n".join(extracted))
