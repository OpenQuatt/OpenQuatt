from __future__ import annotations

import gzip
import importlib.util
import os
import sys
import types
import unittest
from unittest import mock
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts" / "esphome_deterministic.py"
SPEC = importlib.util.spec_from_file_location("esphome_deterministic", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class DeterministicEsphomeTests(unittest.TestCase):
    def test_missing_epoch_keeps_esphome_default(self) -> None:
        self.assertIsNone(MODULE.parse_source_date_epoch({}))

    def test_explicit_empty_environment_does_not_fall_back_to_process(self) -> None:
        with mock.patch.dict(os.environ, {"SOURCE_DATE_EPOCH": "123"}):
            self.assertIsNone(MODULE.parse_source_date_epoch({}))

    def test_epoch_is_formatted_in_utc(self) -> None:
        epoch = MODULE.parse_source_date_epoch({"SOURCE_DATE_EPOCH": "1787443200"})
        self.assertEqual(1787443200, epoch)
        self.assertEqual("2026-08-23 00:00:00 +0000", MODULE.format_build_time(epoch))

    def test_invalid_epoch_is_rejected(self) -> None:
        for value in ("invalid", "-1", str(0x100000000)):
            with self.subTest(value=value), self.assertRaises(SystemExit):
                MODULE.parse_source_date_epoch({"SOURCE_DATE_EPOCH": value})

    def test_source_document_paths_are_checkout_independent(self) -> None:
        source_root = Path("/tmp/checkout-a")
        self.assertEqual(
            "/openquatt-source/openquatt/oq_common.yaml",
            MODULE.canonical_document_path(
                "/tmp/checkout-a/configs/../openquatt/oq_common.yaml", source_root
            ),
        )
        self.assertEqual(
            "/external/config.yaml",
            MODULE.canonical_document_path("/external/config.yaml", source_root),
        )
        self.assertEqual(
            "include: /openquatt-source/components/header.h\n",
            MODULE.canonicalize_source_paths(
                "include: /tmp/checkout-a/components/header.h\n", source_root
            ),
        )

    def test_writer_override_uses_utc_epoch_and_current_config(self) -> None:
        writer = types.ModuleType("esphome.writer")
        writer.get_build_info = lambda: None
        core = types.ModuleType("esphome.core")

        class FakeCore:
            def __init__(self) -> None:
                self._config_hash = None
                self.comment = "test"
                self.config = {
                    "esphome": {"build_path": "/machine-local/build"},
                    "include": str(ROOT / "components" / "header.h"),
                }
                self.config_dir = ROOT / "configs"

        core.CORE = FakeCore()
        core.EsphomeCore = FakeCore
        core.DocumentLocation = type(
            "DocumentLocation",
            (),
            {"document": "", "line": 0, "as_line_directive": property(lambda self: "")},
        )
        esphome = types.ModuleType("esphome")
        esphome.writer = writer
        yaml_util = types.ModuleType("esphome.yaml_util")

        def dump(config, **_kwargs) -> str:
            self.assertNotIn("build_path", config["esphome"])
            return f"include: {config['include']}\n"

        yaml_util.dump = dump
        esphome.yaml_util = yaml_util
        constants = types.ModuleType("esphome.const")
        constants.CONF_BUILD_PATH = "build_path"
        constants.CONF_ESPHOME = "esphome"
        helpers = types.ModuleType("esphome.helpers")
        helpers.fnv1a_32bit_hash = (
            lambda value: (
                0x1234ABCD
                if "/openquatt-source/components/header.h" in value
                else 0
            )
        )
        original_gzip_compress = gzip.compress
        self.addCleanup(setattr, gzip, "compress", original_gzip_compress)

        with mock.patch.dict(
            sys.modules,
            {
                "esphome": esphome,
                "esphome.writer": writer,
                "esphome.core": core,
                "esphome.yaml_util": yaml_util,
                "esphome.const": constants,
                "esphome.helpers": helpers,
            },
        ):
            MODULE.install_deterministic_build_info(1787443200, ROOT)
            self.assertEqual(
                (
                    0x1234ABCD,
                    1787443200,
                    "2026-08-23 00:00:00 +0000",
                    "test",
                ),
                writer.get_build_info(),
            )
        compressed = gzip.compress(b"web asset")
        self.assertEqual(1787443200, int.from_bytes(compressed[4:8], "little"))


if __name__ == "__main__":
    unittest.main()
