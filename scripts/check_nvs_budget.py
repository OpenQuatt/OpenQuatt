#!/usr/bin/env python3
"""Fail when a validated ESPHome config leaves too little usable NVS space.

ESP-IDF NVS stores ESPHome preferences as blobs. A blob consumes an index,
one header per chunk and one 32-byte entry per payload block. One page is
kept available for garbage collection. Legacy/vendor records are not inferred
from YAML; validate the estimate against runtime NVS statistics as well.
"""

from __future__ import annotations

import argparse
import csv
import math
import re
from collections import defaultdict
from pathlib import Path
from typing import Any


NVS_PAGE_SIZE = 4096
NVS_ENTRIES_PER_PAGE = 126
NVS_GC_RESERVED_PAGES = 1
REQUIRED_AVAILABLE_ENTRIES = 100

# (component, bytes per blob, blobs per configured instance). Sizes correspond
# to the current component storage structs, not their runtime buffers.
CUSTOM_PREFERENCE_LAYOUTS = (
    ("openquatt_mqtt_config", 272, 1),
    ("openquatt_web_auth", 104, 1),
    ("openquatt_crash_telemetry", 56, 1),
    ("openquatt_usage_telemetry", 28, 1),
    ("openquatt_performance_telemetry", 8, 1),
    ("openquatt_network", 8, 1),
    ("openquatt_incident_manager", 8, 3),
    ("openquatt_debug_recorder", 2, 1),
    ("openquatt_odu_settings", 16, 1),
    ("openquatt_odu_defrost", 16, 2),
)
PHY_CALIBRATION_BYTES = 1904  # esp_phy_calibration_data_t, ESP-IDF 5.5.5.

CPP_SCALAR_BYTES = {
    "bool": 1,
    "float": 4,
    "int": 4,
    "int32_t": 4,
    "uint32_t": 4,
}


def blob_entries(payload_bytes: int, chunk_count: int = 1) -> int:
    if payload_bytes < 0:
        raise ValueError("NVS payload size cannot be negative")
    if chunk_count < 1:
        raise ValueError("NVS blob must have at least one chunk")
    return 1 + chunk_count + math.ceil(payload_bytes / 32)


def cpp_type_bytes(type_name: str) -> int:
    type_name = type_name.strip()
    if type_name in CPP_SCALAR_BYTES:
        return CPP_SCALAR_BYTES[type_name]
    array_match = re.fullmatch(r"(.+?)\[(\d+)]", type_name)
    if array_match:
        return cpp_type_bytes(array_match.group(1)) * int(array_match.group(2))
    raise ValueError(f"Unknown restored global type {type_name!r}; extend the NVS estimator")


def parse_nvs_partition_size(partition_path: Path) -> int:
    with partition_path.open(encoding="utf-8", newline="") as partition_file:
        for row in csv.reader(partition_file):
            fields = [field.strip() for field in row]
            if not fields or fields[0].startswith("#") or fields[0] != "nvs":
                continue
            if len(fields) < 5:
                raise ValueError(f"Malformed NVS partition row in {partition_path}")
            return int(fields[4], 0)
    raise ValueError(f"No NVS partition found in {partition_path}")


def _add(category_entries: dict[str, int], category_keys: dict[str, int], category: str, payload_bytes: int) -> None:
    category_entries[category] += blob_entries(payload_bytes)
    category_keys[category] += 1


def estimate_entity_preferences(config: Any) -> tuple[dict[str, int], dict[str, int]]:
    entries: dict[str, int] = defaultdict(int)
    keys: dict[str, int] = defaultdict(int)

    for item in config.get("globals", []):
        if item.get("restore_value") is True:
            _add(entries, keys, "globals", cpp_type_bytes(str(item["type"])))

    for item in config.get("number", []):
        if item.get("restore_value") is True:
            _add(entries, keys, "numbers", 4)

    for item in config.get("select", []):
        if item.get("restore_value") is True:
            _add(entries, keys, "selects", 4)

    for item in config.get("switch", []):
        if str(item.get("restore_mode", "")).startswith("RESTORE"):
            _add(entries, keys, "switches", 1)

    for item in config.get("datetime", []):
        if item.get("restore_value") is True:
            _add(entries, keys, "datetimes", 16)

    for item in config.get("text", []):
        if item.get("restore_value") is True:
            _add(entries, keys, "texts", int(item["max_length"]) + 1)

    for item in config.get("sensor", []):
        if item.get("restore") is True:
            _add(entries, keys, "restored sensors", 4)

    for item in config.get("climate", []):
        if item.get("platform") == "pid":
            _add(entries, keys, "PID climates", 16)

    return dict(entries), dict(keys)


def component_instances(config: Any, component: str) -> int:
    if component not in config:
        return 0
    value = config[component]
    return len(value) if isinstance(value, list) else 1


def estimate_custom_preferences(config: Any) -> dict[str, int]:
    entries = {}
    for component, payload_bytes, blobs_per_instance in CUSTOM_PREFERENCE_LAYOUTS:
        count = component_instances(config, component)
        if count:
            entries[component] = count * blobs_per_instance * blob_entries(payload_bytes)
    if component_instances(config, "openquatt_incident_manager"):
        # Restart handoff lives in its own namespace, separate from the three
        # incident reset records in the ESPHome namespace.
        entries["restart handoff"] = blob_entries(64)
    return entries


def estimate_system_preferences(config: Any) -> dict[str, int]:
    entries = {"namespace esphome": 1}
    if component_instances(config, "openquatt_incident_manager"):
        entries["namespace openquatt"] = 1
    if "wifi" in config:
        entries["WiFi settings"] = blob_entries(98)
        entries["WiFi fast connect"] = blob_entries(8)
        framework = config.get("esp32", {}).get("framework", {})
        phy_storage = framework.get("sdkconfig_options", {}).get("CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE", True)
        ignore_mac_crc = framework.get("advanced", {}).get("ignore_efuse_mac_crc", False)
        if str(phy_storage).lower() not in ("false", "n", "0") and not ignore_mac_crc:
            # Plan for two chunks rather than best-case page alignment. This
            # is an estimate, not an upper bound under arbitrary fragmentation.
            entries["PHY calibration"] = blob_entries(PHY_CALIBRATION_BYTES, chunk_count=2)
            entries["PHY MAC"] = blob_entries(6)
            entries["PHY version"] = 1  # Native nvs_set_u32, not an ESPHome blob.
            entries["namespace phy"] = 1
    if "api" in config and "encryption" in config["api"]:
        entries["API Noise PSK"] = blob_entries(32)
    if "safe_mode" in config and not config["safe_mode"].get("disabled", False):
        # Count the flash-backed fallback even when RTC storage is requested.
        entries["safe mode"] = blob_entries(4)
    if config.get("factory_reset", {}).get("resets_required", 0):
        entries["factory reset"] = blob_entries(1)
    return entries


def load_validated_config(config_path: Path) -> Any:
    from esphome.config import read_config
    from esphome.core import CORE

    CORE.config_path = config_path
    return read_config({}, skip_external_update=True)


def check_config(config_path: Path) -> int:
    config = load_validated_config(config_path)
    partition_path = Path(config["esp32"]["partitions"])
    partition_size = parse_nvs_partition_size(partition_path)
    pages = partition_size // NVS_PAGE_SIZE
    if partition_size % NVS_PAGE_SIZE or pages <= NVS_GC_RESERVED_PAGES:
        raise ValueError(f"Unsupported NVS partition size: 0x{partition_size:X}")

    entity_entries, entity_keys = estimate_entity_preferences(config)
    custom_entries = estimate_custom_preferences(config)
    system_entries = estimate_system_preferences(config)
    usable_entries = (pages - NVS_GC_RESERVED_PAGES) * NVS_ENTRIES_PER_PAGE
    reserved_entries = sum(custom_entries.values()) + sum(system_entries.values())
    estimated_entries = sum(entity_entries.values()) + reserved_entries
    available_entries = usable_entries - estimated_entries

    for category in sorted(entity_entries):
        print(f"{category:18} keys={entity_keys[category]:3} entries={entity_entries[category]:3}")
    for category, entries in custom_entries.items():
        print(f"custom/{category}: entries={entries}")
    for category, entries in system_entries.items():
        print(f"system/{category}: entries={entries}")
    print(f"{'OpenQuatt/system':18} keys={'-':>3} entries={reserved_entries:3}")
    print("NVS estimate: two PHY chunks assumed; legacy/vendor records and fragmentation need runtime measurement")
    print(
        f"NVS budget: partition=0x{partition_size:X} usable={usable_entries} "
        f"estimated={estimated_entries} available={available_entries} "
        f"required={REQUIRED_AVAILABLE_ENTRIES}"
    )
    if available_entries < REQUIRED_AVAILABLE_ENTRIES:
        print("NVS budget: FAIL")
        return 1
    print("NVS budget: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path, help="Validated ESPHome config to inspect")
    args = parser.parse_args()
    return check_config(args.config.resolve())


if __name__ == "__main__":
    raise SystemExit(main())
