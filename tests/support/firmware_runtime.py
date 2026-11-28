"""Compile selected firmware runtime methods in native chain tests."""

import re
from pathlib import Path


def write_firmware_runtime_methods(root: Path, build_directory: Path) -> None:
    control_dir = Path("openquatt/includes/control")
    _write_methods(root, build_directory, control_dir / "oq_thermal_actuator_runtime.h", "oq_thermal_actuator", (
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
    _write_methods(root, build_directory, control_dir / "oq_thermal_request_runtime.h", "oq_thermal_request", (
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
    incident_dir = Path("components/openquatt_incident_manager")
    header = incident_dir / "OpenQuattIncidentManager.h"
    pattern = r"^  static constexpr uint32_t LINK_ROUND_TIMEOUT_MS\s*=[^;]+;"
    timeout_constant = _extract((root / header).read_text(), pattern, header, "LINK_ROUND_TIMEOUT_MS")
    _write_methods(
        root, build_directory, incident_dir / "OpenQuattIncidentManager.cpp", "oq_incident_manager", (
            "observe_transport",
            "observe_working_mode",
            "observe_compressor_frequency",
            "request_start",
            "request_stop",
            "minimum_off_remaining_ms",
            "invalidate_restart_credit",
            "invalidate_restart_credit_",
            "outputs_for_slot_",
        ),
        class_name="OpenQuattIncidentManager",
        declarations=timeout_constant,
    )


def _extract(source: str, pattern: str, path: Path, name: str) -> str:
    match = re.search(pattern, source, re.M | re.S)
    if match is None:
        raise RuntimeError(f"Definition {name} not found in {path}")
    line = source.count("\n", 0, match.start()) + 1
    return f'#line {line} "{path}"\n{match.group()}\n'


def _write_methods(
    root: Path,
    build_directory: Path,
    relative_path: Path,
    module: str,
    methods: tuple[str, ...],
    *,
    class_name: str = "",
    declarations: str = "",
) -> None:
    source = (root / relative_path).read_text()
    indent = "" if class_name else "  "
    qualifier = f"{class_name}::" if class_name else ""
    extracted = [declarations]
    for method in methods:
        pattern = rf"^{indent}[\w:]+ {qualifier}{method}\([^{{]*\{{(?:[^\n]*\}}$|.*?^{indent}\}}$)"
        definition = _extract(source, pattern, relative_path, method)
        if qualifier:
            definition = definition.replace(qualifier, "", 1)
        extracted.append(definition)
    (build_directory / f"{module}_methods.inc").write_text("\n".join(extracted))
