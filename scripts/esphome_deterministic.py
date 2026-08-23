#!/usr/bin/env python3
"""Run ESPHome with reproducible build-info timestamps when requested."""

from __future__ import annotations

import os
import sys
from datetime import datetime, timezone
from typing import Mapping


SOURCE_DATE_EPOCH = "SOURCE_DATE_EPOCH"


def parse_source_date_epoch(environment: Mapping[str, str] | None = None) -> int | None:
    environment = os.environ if environment is None else environment
    raw = environment.get(SOURCE_DATE_EPOCH, "").strip()
    if not raw:
        return None
    try:
        epoch = int(raw, 10)
    except ValueError as err:
        raise SystemExit(f"{SOURCE_DATE_EPOCH} must be an integer Unix timestamp") from err
    if epoch < 0 or epoch > 0xFFFFFFFF:
        raise SystemExit(f"{SOURCE_DATE_EPOCH} must fit in an unsigned 32-bit timestamp")
    return epoch


def format_build_time(epoch: int) -> str:
    return datetime.fromtimestamp(epoch, timezone.utc).strftime("%Y-%m-%d %H:%M:%S +0000")


def install_deterministic_build_info(epoch: int) -> None:
    """Replace ESPHome's wall-clock build-info provider for this process.

    ESPHome 2026.8 enables ESP-IDF's reproducible-build mode, but its own
    ``build_info_data.cpp`` still uses ``time.time()``. Keeping the override in
    this small wrapper lets OpenQuatt rebuild the exact ELF later without
    patching the installed ESPHome package.
    """

    from esphome import writer
    from esphome.core import CORE

    def get_build_info() -> tuple[int, int, str, str]:
        return CORE.config_hash, epoch, format_build_time(epoch), CORE.comment or ""

    writer.get_build_info = get_build_info


def main() -> int:
    epoch = parse_source_date_epoch()
    if epoch is not None:
        install_deterministic_build_info(epoch)

    from esphome.__main__ import main as esphome_main

    return esphome_main()


if __name__ == "__main__":
    sys.exit(main())
