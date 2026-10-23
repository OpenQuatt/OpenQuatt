"""Temporary, build-local backport of ESPHome PR 20800 for the reviewed ESPHome override."""

from __future__ import annotations

from functools import wraps
import logging
from pathlib import Path
import tempfile
from typing import Any


_LOGGER = logging.getLogger(__name__)
_ENABLED_KEY = "openquatt_four_decimal_formatter"
_HOOK_MARKER = "_openquatt_four_decimal_formatter"
_PATCHES = {
    "helpers.cpp": (
        (
            "// Fast float-to-string for accuracy_decimals 0-3 (covers virtually all sensor usage).",
            "// Fast float-to-string for accuracy_decimals 0-4 (covers sensors and small PID coefficients).",
        ),
        (
            "// Fast path for accuracy 0-3, finite values whose scaled magnitude fits in uint32_t.",
            "// Fast path for accuracy 0-4, finite values whose scaled magnitude fits in uint32_t.",
        ),
        (
            "// For 3 decimals that's |value| < ~4.29e6; larger totals fall through to snprintf.",
            "// For 4 decimals that's |value| < ~4.29e5; larger totals fall through to snprintf.",
        ),
        (
            "if (accuracy_decimals <= 3 && std::isfinite(value)) {",
            "if (accuracy_decimals <= 4 && std::isfinite(value)) {",
        ),
    ),
    "helpers.h": (
        (
            "/// Return 10^n for small non-negative n (0-3) as uint32_t, avoiding float.",
            "/// Return 10^n for small non-negative n (0-4) as uint32_t, avoiding float.",
        ),
        (
            "inline uint32_t small_pow10(int8_t n) { return n == 3 ? 1000 : n == 2 ? 100 : n == 1 ? 10 : 1; }",
            "inline uint32_t small_pow10(int8_t n) { return n == 4 ? 10000 : n == 3 ? 1000 : n == 2 ? 100 : n == 1 ? 10 : 1; }",
        ),
    ),
}


def _transform(name: str, source: str) -> tuple[str, bool]:
    replacements = _PATCHES[name]
    original = all(source.count(old) == 1 and new not in source for old, new in replacements)
    patched = all(source.count(new) == 1 and old not in source for old, new in replacements)
    if patched:
        return source, False
    if not original:
        raise RuntimeError(
            f"OpenQuatt four-decimal backport: unexpected {name}; review ESPHome PR 20800 "
            "and remove this backport after upgrading to a validated ESPHome release containing it."
        )
    for old, new in replacements:
        source = source.replace(old, new, 1)
    return source, True


def _patch_generated_sources(core: Any) -> None:
    prepared = []
    for name in _PATCHES:
        path = Path(core.relative_src_path("esphome", "core", name))
        source = path.read_bytes().decode("utf-8")
        patched, changed = _transform(name, source)
        prepared.append((path, patched, changed))
    if len({changed for _, _, changed in prepared}) != 1:
        raise RuntimeError("OpenQuatt four-decimal backport: partially patched ESPHome core; review both helper files.")
    for path, source, changed in prepared:
        if not changed:
            continue
        # Replace the build copy, so even a hard-linked source cannot modify the installed package.
        temporary_path = None
        try:
            with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as temporary:
                temporary_path = Path(temporary.name)
                temporary.write(source.encode("utf-8"))
            temporary_path.replace(path)
        finally:
            if temporary_path is not None:
                temporary_path.unlink(missing_ok=True)
    _LOGGER.info("OpenQuatt: four-decimal formatter backport active (ESPHome PR 20800)")


def install_four_decimal_patch(core: Any, writer: Any) -> None:
    """Patch each generated build, only while this config enables the local override."""
    core.data[_ENABLED_KEY] = True
    if getattr(writer.copy_src_tree, _HOOK_MARKER, False):
        return
    original = writer.copy_src_tree

    @wraps(original)
    def copy_src_tree(*args: Any, **kwargs: Any) -> Any:
        result = original(*args, **kwargs)
        if core.data.get(_ENABLED_KEY, False):
            _patch_generated_sources(core)
        return result

    setattr(copy_src_tree, _HOOK_MARKER, True)
    writer.copy_src_tree = copy_src_tree
