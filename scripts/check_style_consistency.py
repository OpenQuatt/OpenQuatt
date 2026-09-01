#!/usr/bin/env python3
"""Repo-specific style consistency checks for OpenQuatt."""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]

TEXT_PATTERNS = (
    "configs/**/*.yaml",
    "openquatt/*.yaml",
    "openquatt/base/*.yaml",
    "openquatt/connection/*.yaml",
    "openquatt/packages/*.yaml",
    "openquatt/profiles/*.yaml",
    "openquatt/topology/*.yaml",
    "scripts/*.py",
    "scripts/*.sh",
)

YAML_BANNER_PATTERNS = (
    "configs/**/*.yaml",
    "openquatt/*.yaml",
    "openquatt/base/*.yaml",
    "openquatt/connection/*.yaml",
    "openquatt/packages/*.yaml",
    "openquatt/profiles/*.yaml",
    "openquatt/topology/*.yaml",
)

LAMBDA_PATTERNS = (
    "configs/**/*.yaml",
    "openquatt/*.yaml",
    "openquatt/base/*.yaml",
    "openquatt/connection/*.yaml",
    "openquatt/packages/*.yaml",
    "openquatt/profiles/*.yaml",
    "openquatt/topology/*.yaml",
)

PACKAGE_ORDER_PATTERNS = (
    "openquatt/*.yaml",
)

BUILTIN_YAML_PATTERNS = (
    "configs/**/*.yaml",
    "docs/templates/**/*.yaml",
    "openquatt/**/*.yaml",
)
ENTITY_YAML_PATTERNS = (
    "components/**/*.yaml",
    "configs/**/*.yaml",
    "openquatt/**/*.yaml",
)
BUILTIN_WEB_SORTING_KEY_RE = re.compile(
    r"""(?:^|[,{]\s*)["']?(?:sorting_groups|sorting_group_id|sorting_weight)["']?\s*:"""
)
INTERNAL_TRUE_RE = re.compile(r"^(?P<indent>\s*)internal:\s*true\s*(?:#.*)?$")
YAML_MAPPING_KEY_RE = re.compile(r"^(?P<indent>\s*)(?P<key>[A-Za-z_][\w]*):")
INTERNAL_ENTITY_PRESENTATION_KEYS = {
    "device_class",
    "disabled_by_default",
    "entity_category",
    "icon",
}

STRICT_TOP_LEVEL_ORDER_RULES = {
    "configs/waveshare/single_wifi.yaml": (
        "substitutions",
        "esphome",
        "packages",
    ),
    "configs/waveshare/duo_wifi.yaml": (
        "substitutions",
        "esphome",
        "packages",
    ),
    "configs/heatpump_listener/single_wifi.yaml": (
        "substitutions",
        "esphome",
        "packages",
    ),
    "configs/heatpump_listener/duo_wifi.yaml": (
        "substitutions",
        "esphome",
        "packages",
    ),
    "configs/heatpump_controller_q/single.yaml": (
        "substitutions",
        "esphome",
        "packages",
    ),
    "configs/heatpump_controller_q/duo.yaml": (
        "substitutions",
        "esphome",
        "packages",
    ),
    "openquatt/base/common.yaml": (
        "esphome",
        "esp32",
        "sensor",
    ),
    "openquatt/oq_packages_common.yaml": (
        "packages",
    ),
    "openquatt/oq_substitutions_common_package.yaml": (
        "substitutions",
    ),
    "openquatt/packages/10_runtime.yaml": (
        "packages",
    ),
    "openquatt/packages/20_strategies.yaml": (
        "packages",
    ),
    "openquatt/packages/30_thermal_pipeline.yaml": (
        "packages",
    ),
    "openquatt/packages/40_service_tasks.yaml": (
        "packages",
    ),
    "openquatt/packages/50_integrations.yaml": (
        "packages",
    ),
    "openquatt/packages/60_ui_runtime.yaml": (
        "packages",
    ),
    "openquatt/packages/90_heatpump1.yaml": (
        "packages",
    ),
    "openquatt/topology/duo.yaml": (
        "secondary_hp_id",
        "oq_topology",
        "secondary_outside_is_distinct",
        "flow_secondary_enabled",
        "flow_mismatch_internal",
        "hc_dual_tuning_internal",
        "hc_hp2_diag_internal",
        "hc_runtime_reset_button_name",
        "cic_secondary_enabled",
        "cic_hp2_sensor_internal",
        "sensor_sources_duo_flow_internal",
        "ot_secondary_present",
        "monitoring_hp2_internal",
    ),
    "openquatt/topology/duo_packages.yaml": (
        "packages",
    ),
    "openquatt/topology/duo_package.yaml": (
        "substitutions",
    ),
    "openquatt/topology/single.yaml": (
        "secondary_hp_id",
        "oq_topology",
        "secondary_outside_is_distinct",
        "flow_secondary_enabled",
        "flow_mismatch_internal",
        "oq_flow_kp_default",
        "oq_flow_ki_default",
        "hc_dual_tuning_internal",
        "hc_hp2_diag_internal",
        "hc_runtime_reset_button_name",
        "cic_secondary_enabled",
        "cic_hp2_sensor_internal",
        "sensor_sources_duo_flow_internal",
        "ot_secondary_present",
        "monitoring_hp2_internal",
    ),
    "openquatt/topology/single_package.yaml": (
        "substitutions",
    ),
    "openquatt/profiles/heatpump_controller_q_cic_compatibility.yaml": (
        "substitutions",
        "uart",
        "modbus",
        "packages",
    ),
    "openquatt/profiles/heatpump_controller_q_cic_compatibility_duo.yaml": (
        "packages",
    ),
    "openquatt/profiles/heatpump_controller_q.yaml": (
        "substitutions",
        "packages",
        "openquatt_usage_telemetry",
        "psram",
        "select",
        "spi",
        "sensor",
        "interval",
    ),
}

NESTED_KEY_ORDER_RULES = {
    ("configs/waveshare/single_wifi.yaml", "packages"): (
        "openquatt_substitutions_common",
        "openquatt_topology_single_substitutions",
        "openquatt_profile_waveshare",
        "openquatt_base_common",
        "openquatt_connection_wifi",
        "openquatt_packages_common",
    ),
    ("configs/waveshare/duo_wifi.yaml", "packages"): (
        "openquatt_substitutions_common",
        "openquatt_topology_duo_substitutions",
        "openquatt_profile_waveshare",
        "openquatt_base_common",
        "openquatt_connection_wifi",
        "openquatt_packages_common",
        "openquatt_topology_duo_packages",
    ),
    ("configs/heatpump_listener/single_wifi.yaml", "packages"): (
        "openquatt_substitutions_common",
        "openquatt_topology_single_substitutions",
        "openquatt_profile_heatpump_listener",
        "openquatt_base_common",
        "openquatt_connection_wifi",
        "oq_heatpump_listener_extras",
        "openquatt_packages_common",
    ),
    ("configs/heatpump_listener/duo_wifi.yaml", "packages"): (
        "openquatt_substitutions_common",
        "openquatt_topology_duo_substitutions",
        "openquatt_profile_heatpump_listener",
        "openquatt_base_common",
        "openquatt_connection_wifi",
        "oq_heatpump_listener_extras",
        "openquatt_packages_common",
        "openquatt_topology_duo_packages",
    ),
    ("configs/heatpump_controller_q/single.yaml", "packages"): (
        "openquatt_substitutions_common",
        "openquatt_topology_single_substitutions",
        "openquatt_profile_heatpump_controller_q",
        "openquatt_base_common",
        "openquatt_connection_wifi_eth",
        "openquatt_packages_common",
    ),
    ("configs/heatpump_controller_q/duo.yaml", "packages"): (
        "openquatt_substitutions_common",
        "openquatt_topology_duo_substitutions",
        "openquatt_profile_heatpump_controller_q",
        "openquatt_profile_heatpump_controller_q_cic_compatibility_duo",
        "openquatt_base_common",
        "openquatt_connection_wifi_eth",
        "openquatt_packages_common",
        "openquatt_topology_duo_packages",
    ),
    ("openquatt/oq_packages_common.yaml", "packages"): (
        "oq_runtime_packages",
        "oq_strategy_packages",
        "oq_thermal_pipeline_packages",
        "oq_service_task_packages",
        "oq_integration_packages",
        "oq_ui_runtime_packages",
        "oq_heatpump1_package",
    ),
    ("openquatt/topology/duo_packages.yaml", "packages"): (
        "heatpump2",
        "hp2_odu_runtime_frequency_table",
    ),
    ("openquatt/packages/10_runtime.yaml", "packages"): (
        "oq_common",
        "oq_supervisory_controlmode",
        "oq_commissioning",
        "oq_thermal_limits",
        "oq_installation_monitoring",
    ),
    ("openquatt/packages/20_strategies.yaml", "packages"): (
        "oq_strategy_manager",
        "oq_cooling_strategy",
        "oq_heating_curve_strategy",
        "oq_power_house_strategy",
    ),
    ("openquatt/packages/30_thermal_pipeline.yaml", "packages"): (
        "oq_thermal_request_control",
        "oq_thermal_actuator",
        "oq_flow_control",
    ),
    ("openquatt/packages/40_service_tasks.yaml", "packages"): (
        "oq_flow_autotune",
        "oq_air_purge",
        "oq_hp_water_calibration",
        "oq_manual_flow",
        "oq_manual_hp",
        "oq_boiler_test",
    ),
    ("openquatt/packages/50_integrations.yaml", "packages"): (
        "oq_boiler_dispatch",
        "oq_boiler_control",
        "oq_energy",
        "oq_cic",
        "oq_ha_inputs",
        "oq_api_ingress",
        "oq_mqtt_ingress",
        "oq_usage_telemetry",
        "oq_local_sensors",
        "oq_sensor_sources",
    ),
    ("openquatt/packages/60_ui_runtime.yaml", "packages"): (
        "oq_setup_status",
        "oq_webserver",
        "oq_web_access",
    ),
    ("openquatt/packages/90_heatpump1.yaml", "packages"): (
        "heatpump1",
        "hp1_odu_runtime_frequency_table",
    ),
    ("openquatt/profiles/heatpump_controller_q.yaml", "packages"): (
        "oq_hardware_revision",
        "oq_status_leds",
        "oq_cic_compatibility",
        "oq_aux_relay_control",
        "oq_sensor_source_selects_opentherm",
        "oq_ot_slave",
        "oq_boiler_opentherm",
    ),
}

SUBSTITUTION_SECTION_ORDER_RULES = {
    "openquatt/oq_substitutions_common.yaml": (
        "CORE PROJECT",
        "HARDWARE",
        "TOPOLOGY CONTRACT",
        "SUPERVISORY CONTROL MODE",
        "HEATING STRATEGY",
        "HEAT CONTROL",
        "FLOW CONTROL",
        "FLOW AUTOTUNE",
        "BOILER CONTROL",
        "CIC FEED",
        "HA INPUT ENTITY MAPPING",
        "HP IO (MODBUS POLLING)",
    ),
}

TOP_LEVEL_KEY_RE = re.compile(r"^([a-z_][a-z0-9_]*):(?:\s|$)")
NESTED_KEY_RE = re.compile(r"^(?P<indent>\s+)(?P<key>[a-z_][a-z0-9_]*):(?:\s|$)")
LAMBDA_LINE_RE = re.compile(r"^(?P<indent>\s*)(?:-\s+)?lambda:(?P<tail>.*)$")
LAMBDA_BLOCK_RE = re.compile(r"^(?P<indent>\s*)(?:-\s+)?lambda:\s*\|-\s*$")
PACKAGE_GROUP_BY_KEY = {
    "logger": 0,
    "api": 0,
    "ota": 0,
    "web_server": 0,
    "wifi": 0,
    "captive_portal": 0,
    "improv_serial": 0,
    "esp32_improv": 0,
    "http_request": 0,
    "uart": 0,
    "modbus": 0,
    "modbus_controller": 0,
    "modbus_server": 0,
    "one_wire": 0,
    "time": 0,
    "globals": 1,
    "script": 2,
    "output": 2,
    "climate": 2,
    "switch": 2,
    "select": 2,
    "number": 2,
    "datetime": 2,
    "text": 2,
    "button": 2,
    "binary_sensor": 3,
    "sensor": 3,
    "text_sensor": 3,
    "update": 3,
    "interval": 4,
}
PACKAGE_GROUP_LABELS = {
    0: "infrastructure",
    1: "globals",
    2: "control/helper",
    3: "published entities",
    4: "interval loop",
}


@dataclass
class Finding:
    file: str
    line: int
    message: str


def add(findings: list[Finding], file: str, line: int, message: str) -> None:
    findings.append(Finding(file=file, line=line, message=message))


def expand_patterns(patterns: tuple[str, ...]) -> list[Path]:
    paths: set[Path] = set()
    for pattern in patterns:
        paths.update(REPO_ROOT.glob(pattern))
    ignored_dirs = {".cache", ".esphome", ".git", ".tmp", ".venv", "__pycache__"}
    return sorted(path for path in paths if path.is_file() and not ignored_dirs.intersection(path.parts))


def read_lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8").splitlines()


def check_whitespace(path: Path, findings: list[Finding]) -> None:
    rel = path.relative_to(REPO_ROOT).as_posix()
    for idx, line in enumerate(read_lines(path), start=1):
        if "\t" in line:
            add(findings, rel, idx, "Tab character found; use spaces only.")
        if line.rstrip(" ") != line:
            add(findings, rel, idx, "Trailing whitespace found.")


def check_no_builtin_web_sorting(path: Path, findings: list[Finding]) -> None:
    rel = path.relative_to(REPO_ROOT).as_posix()
    for idx, line in enumerate(read_lines(path), start=1):
        stripped = line.strip()
        if stripped.startswith("#"):
            continue
        if BUILTIN_WEB_SORTING_KEY_RE.search(stripped):
            add(
                findings,
                rel,
                idx,
                "ESPHome web sorting metadata allocates internal heap; keep layout in the OpenQuatt SPA.",
            )


def check_no_internal_entity_presentation_metadata(path: Path, findings: list[Finding]) -> None:
    rel = path.relative_to(REPO_ROOT).as_posix()
    lines = read_lines(path)
    reported_lines: set[int] = set()

    for marker_index, line in enumerate(lines):
        internal_match = INTERNAL_TRUE_RE.match(line)
        if not internal_match:
            continue

        property_indent = len(internal_match.group("indent"))
        mapping_start = marker_index
        while mapping_start > 0:
            candidate = lines[mapping_start - 1]
            if not candidate.strip():
                mapping_start -= 1
                continue
            candidate_indent = len(candidate) - len(candidate.lstrip())
            if candidate_indent < property_indent or (
                candidate_indent == property_indent and candidate.lstrip().startswith("- ")
            ):
                break
            mapping_start -= 1

        mapping_end = marker_index + 1
        while mapping_end < len(lines):
            candidate = lines[mapping_end]
            if not candidate.strip():
                mapping_end += 1
                continue
            candidate_indent = len(candidate) - len(candidate.lstrip())
            if candidate_indent < property_indent or (
                candidate_indent == property_indent and candidate.lstrip().startswith("- ")
            ):
                break
            mapping_end += 1

        for sibling_index in range(mapping_start, mapping_end):
            sibling_match = YAML_MAPPING_KEY_RE.match(lines[sibling_index])
            if not sibling_match or len(sibling_match.group("indent")) != property_indent:
                continue
            key = sibling_match.group("key")
            line_number = sibling_index + 1
            if key not in INTERNAL_ENTITY_PRESENTATION_KEYS or line_number in reported_lines:
                continue
            reported_lines.add(line_number)
            add(
                findings,
                rel,
                line_number,
                f"`{key}:` is unused presentation metadata on a literal `internal: true` entity.",
            )


def check_yaml_banner(path: Path, findings: list[Finding]) -> None:
    rel = path.relative_to(REPO_ROOT).as_posix()
    nonempty = [(idx, line) for idx, line in enumerate(read_lines(path), start=1) if line.strip()]
    if len(nonempty) < 3:
        add(findings, rel, 1, "Expected a three-line OpenQuatt banner header at the top of the file.")
        return

    first, second, third = nonempty[:3]
    if not first[1].startswith("# ==="):
        add(findings, rel, first[0], "Banner header should start with a separator line.")
    if not second[1].startswith("# OpenQuatt"):
        add(findings, rel, second[0], "Banner header should contain an OpenQuatt title line.")
    if not third[1].startswith("# ==="):
        add(findings, rel, third[0], "Banner header should end with a separator line.")


def top_level_positions(path: Path) -> dict[str, int]:
    positions: dict[str, int] = {}
    for idx, line in enumerate(read_lines(path), start=1):
        if line.startswith((" ", "\t", "#")) or not line.strip():
            continue
        match = TOP_LEVEL_KEY_RE.match(line)
        if not match:
            continue
        positions.setdefault(match.group(1), idx)
    return positions


def top_level_keys(path: Path) -> list[tuple[int, str]]:
    keys: list[tuple[int, str]] = []
    for idx, line in enumerate(read_lines(path), start=1):
        if line.startswith((" ", "\t", "#")) or not line.strip():
            continue
        match = TOP_LEVEL_KEY_RE.match(line)
        if match:
            keys.append((idx, match.group(1)))
    return keys


def nested_mapping_keys(path: Path, parent_key: str) -> list[tuple[int, str]]:
    lines = read_lines(path)
    parent_line = None
    for idx, line in enumerate(lines, start=1):
        if line.startswith((" ", "\t", "#")) or not line.strip():
            continue
        match = TOP_LEVEL_KEY_RE.match(line)
        if match and match.group(1) == parent_key:
            parent_line = idx
            break

    if parent_line is None:
        return []

    keys: list[tuple[int, str]] = []
    cursor = parent_line
    while cursor < len(lines):
        line = lines[cursor]
        if line.strip() and not line.startswith((" ", "\t")):
            break
        match = NESTED_KEY_RE.match(line)
        if match and len(match.group("indent")) == 2:
            keys.append((cursor + 1, match.group("key")))
        cursor += 1
    return keys


def check_exact_key_order(
    path: Path,
    keys: list[tuple[int, str]],
    expected_keys: tuple[str, ...],
    findings: list[Finding],
    *,
    scope_label: str,
) -> None:
    rel = path.relative_to(REPO_ROOT).as_posix()
    actual_keys = [key for _, key in keys]
    line_by_key = {key: line for line, key in keys}
    expected_index = {key: idx for idx, key in enumerate(expected_keys)}

    previous_index = -1
    previous_key = None
    for line, key in keys:
        if key not in expected_index:
            add(findings, rel, line, f"Unexpected {scope_label} key `{key}:`.")
            continue
        current_index = expected_index[key]
        if current_index < previous_index:
            add(
                findings,
                rel,
                line,
                f"`{key}:` should not appear after `{previous_key}:` in the {scope_label} order.",
            )
        previous_index = max(previous_index, current_index)
        previous_key = key

    for key in expected_keys:
        if key not in actual_keys:
            add(findings, rel, 1, f"Missing expected {scope_label} key `{key}:`.")

    for key in set(actual_keys):
        if actual_keys.count(key) > 1:
            add(findings, rel, line_by_key[key], f"Duplicate {scope_label} key `{key}:`.")


def check_strict_top_level_order(path: Path, findings: list[Finding]) -> None:
    rel = path.relative_to(REPO_ROOT).as_posix()
    expected_keys = STRICT_TOP_LEVEL_ORDER_RULES.get(rel)
    if not expected_keys:
        return
    check_exact_key_order(
        path,
        top_level_keys(path),
        expected_keys,
        findings,
        scope_label="top-level",
    )


def check_nested_key_order(path: Path, findings: list[Finding]) -> None:
    rel = path.relative_to(REPO_ROOT).as_posix()
    for (rule_path, parent_key), expected_keys in NESTED_KEY_ORDER_RULES.items():
        if rel != rule_path:
            continue
        nested_keys = nested_mapping_keys(path, parent_key)
        if not nested_keys:
            add(findings, rel, 1, f"Missing expected `{parent_key}:` mapping.")
            continue
        check_exact_key_order(
            path,
            nested_keys,
            expected_keys,
            findings,
            scope_label=f"`{parent_key}` child",
        )


def check_substitution_section_order(path: Path, findings: list[Finding]) -> None:
    rel = path.relative_to(REPO_ROOT).as_posix()
    expected_titles = SUBSTITUTION_SECTION_ORDER_RULES.get(rel)
    if not expected_titles:
        return

    title_lines: list[tuple[int, str]] = []
    expected_title_set = set(expected_titles)
    for idx, line in enumerate(read_lines(path), start=1):
        if not line.startswith("# "):
            continue
        title = line[2:].strip()
        if title in expected_title_set:
            title_lines.append((idx, title))

    check_exact_key_order(
        path,
        title_lines,
        expected_titles,
        findings,
        scope_label="section",
    )


def check_package_group_order(path: Path, findings: list[Finding]) -> None:
    rel = path.relative_to(REPO_ROOT).as_posix()
    positions = top_level_positions(path)
    ordered_keys = sorted(
        ((line, key) for key, line in positions.items() if key in PACKAGE_GROUP_BY_KEY),
        key=lambda item: item[0],
    )
    previous_group = None
    previous_key = None
    previous_line = None
    for current_line, key in ordered_keys:
        current_group = PACKAGE_GROUP_BY_KEY[key]
        if previous_group is not None and current_group < previous_group:
            add(
                findings,
                rel,
                current_line,
                f"`{key}:` belongs to the {PACKAGE_GROUP_LABELS[current_group]} group and should not appear after `{previous_key}:` ({PACKAGE_GROUP_LABELS[previous_group]}).",
            )
        previous_group = current_group
        previous_key = key
        previous_line = current_line


def iter_lambda_blocks(path: Path) -> list[tuple[int, int, int, list[tuple[int, str]]]]:
    lines = read_lines(path)
    blocks: list[tuple[int, int, int, list[tuple[int, str]]]] = []
    idx = 0
    while idx < len(lines):
        line = lines[idx]
        block_match = LAMBDA_BLOCK_RE.match(line)
        if not block_match:
            idx += 1
            continue

        parent_indent = len(block_match.group("indent"))
        body: list[tuple[int, str]] = []
        cursor = idx + 1
        while cursor < len(lines):
            body_line = lines[cursor]
            indent = len(body_line) - len(body_line.lstrip(" "))
            if body_line.strip() and indent <= parent_indent:
                break
            if not body_line.strip() and cursor + 1 < len(lines):
                next_line = lines[cursor + 1]
                next_indent = len(next_line) - len(next_line.lstrip(" "))
                if next_line.strip() and next_indent <= parent_indent:
                    break
            body.append((cursor + 1, body_line))
            cursor += 1

        blocks.append((idx + 1, cursor, parent_indent, body))
        idx = cursor

    return blocks


def check_lambda_style(path: Path, findings: list[Finding]) -> None:
    rel = path.relative_to(REPO_ROOT).as_posix()
    lines = read_lines(path)

    for idx, line in enumerate(lines, start=1):
        line_match = LAMBDA_LINE_RE.match(line)
        if not line_match:
            continue
        if line_match.group("tail").strip() != "|-":
            add(findings, rel, idx, "Use `lambda: |-` for ESPHome lambdas.")

    for start_line, _, parent_indent, body in iter_lambda_blocks(path):
        nonblank_body = [(line_no, text) for line_no, text in body if text.strip()]
        if not nonblank_body:
            add(findings, rel, start_line, "Lambda block must contain code.")
            continue

        body_indents = [len(text) - len(text.lstrip(" ")) for _, text in nonblank_body]
        base_indent = min(body_indents)
        indent_delta = base_indent - parent_indent
        if indent_delta not in {2, 4}:
            add(
                findings,
                rel,
                start_line,
                "Lambda body should start 2 or 4 spaces deeper than the `lambda:` line.",
            )

        blank_run = 0
        for line_no, text in body:
            if not text.strip():
                blank_run += 1
                if blank_run >= 2:
                    add(findings, rel, line_no, "Avoid multiple consecutive blank lines inside a lambda block.")
                    break
                continue
            blank_run = 0

        if len(nonblank_body) >= 12:
            has_phase_comment = any(text.lstrip().startswith("//") for _, text in nonblank_body)
            if not has_phase_comment:
                add(
                    findings,
                    rel,
                    start_line,
                    "Lambda blocks with 12+ non-empty lines need at least one `//` phase or intent comment.",
                )


def main() -> int:
    findings: list[Finding] = []

    for path in expand_patterns(TEXT_PATTERNS):
        check_whitespace(path, findings)

    for path in expand_patterns(BUILTIN_YAML_PATTERNS):
        check_no_builtin_web_sorting(path, findings)

    for path in expand_patterns(ENTITY_YAML_PATTERNS):
        check_no_internal_entity_presentation_metadata(path, findings)

    for path in expand_patterns(YAML_BANNER_PATTERNS):
        check_yaml_banner(path, findings)

    for path in expand_patterns(LAMBDA_PATTERNS):
        check_lambda_style(path, findings)

    for path in expand_patterns(PACKAGE_ORDER_PATTERNS):
        check_package_group_order(path, findings)

    for rel_path in sorted(set(STRICT_TOP_LEVEL_ORDER_RULES)):
        check_strict_top_level_order(REPO_ROOT / rel_path, findings)

    for rel_path, _ in sorted(set(NESTED_KEY_ORDER_RULES)):
        check_nested_key_order(REPO_ROOT / rel_path, findings)

    for rel_path in sorted(set(SUBSTITUTION_SECTION_ORDER_RULES)):
        check_substitution_section_order(REPO_ROOT / rel_path, findings)

    if findings:
        for finding in sorted(findings, key=lambda item: (item.file, item.line, item.message)):
            print(f"{finding.file}:{finding.line}: {finding.message}")
        return 1

    print("Style consistency checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
