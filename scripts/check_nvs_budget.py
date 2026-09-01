#!/usr/bin/env python3
"""Fail when a validated ESPHome config leaves too little usable NVS space.

ESP-IDF NVS stores ESPHome preferences as blobs. A blob consumes two metadata
entries plus one 32-byte entry per payload chunk. One page is kept available
for garbage collection, so it is excluded from the usable capacity.
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
REQUIRED_AVAILABLE_ENTRIES = 126

# Custom OpenQuatt preferences: MQTT config 11, web auth 6, crash state 4,
# usage telemetry 3, network preference 3 and incident reset journal 9.
CUSTOM_PREFERENCE_ENTRIES = 36
# Namespace records, ESPHome safe-mode/WiFi/PHY state and estimation margin.
ESP_SYSTEM_RESERVE_ENTRIES = 14

CPP_SCALAR_BYTES = {
    "bool": 1,
    "float": 4,
    "int": 4,
    "int32_t": 4,
    "uint32_t": 4,
}


def blob_entries(payload_bytes: int) -> int:
    if payload_bytes < 0:
        raise ValueError("NVS payload size cannot be negative")
    return 2 + math.ceil(payload_bytes / 32)


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
    usable_entries = (pages - NVS_GC_RESERVED_PAGES) * NVS_ENTRIES_PER_PAGE
    reserved_entries = CUSTOM_PREFERENCE_ENTRIES + ESP_SYSTEM_RESERVE_ENTRIES
    estimated_entries = sum(entity_entries.values()) + reserved_entries
    available_entries = usable_entries - estimated_entries

    for category in sorted(entity_entries):
        print(f"{category:18} keys={entity_keys[category]:3} entries={entity_entries[category]:3}")
    print(f"{'OpenQuatt/system':18} keys={'-':>3} entries={reserved_entries:3}")
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
