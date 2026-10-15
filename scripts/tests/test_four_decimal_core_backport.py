from __future__ import annotations

from importlib import resources, util
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = util.spec_from_file_location(
    "core_four_decimals", ROOT / "components/web_server_base/core_four_decimals.py"
)
backport = util.module_from_spec(SPEC)
SPEC.loader.exec_module(backport)


class FourDecimalCoreBackportTest(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.build = Path(self.directory.name)
        self.core = SimpleNamespace(
            data={}, relative_src_path=lambda *parts: self.build.joinpath(*parts)
        )
        self.originals = {
            name: "// Unrelated source stays unchanged.\n" + "\n".join(old for old, _ in changes) + "\n"
            for name, changes in backport._PATCHES.items()
        }

    def seed(self) -> None:
        for name, source in self.originals.items():
            path = self.core.relative_src_path("esphome", "core", name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8")

    def read(self, name: str) -> str:
        return self.core.relative_src_path("esphome", "core", name).read_text(encoding="utf-8")

    def test_patches_pair_and_accepts_second_application_without_rewriting(self) -> None:
        self.seed()
        backport._patch_generated_sources(self.core)
        timestamps = {}
        for name, changes in backport._PATCHES.items():
            self.assertTrue(self.read(name).startswith("// Unrelated source stays unchanged.\n"))
            for old, new in changes:
                self.assertNotIn(old, self.read(name))
                self.assertIn(new, self.read(name))
            timestamps[name] = self.core.relative_src_path("esphome", "core", name).stat().st_mtime_ns
        backport._patch_generated_sources(self.core)
        for name in backport._PATCHES:
            self.assertEqual(timestamps[name], self.core.relative_src_path("esphome", "core", name).stat().st_mtime_ns)

    def test_unknown_second_file_does_not_modify_first_file(self) -> None:
        self.seed()
        self.core.relative_src_path("esphome", "core", "helpers.h").write_text("unknown", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "unexpected helpers.h"):
            backport._patch_generated_sources(self.core)
        self.assertEqual(self.read("helpers.cpp"), self.originals["helpers.cpp"])

    def test_partial_pair_is_rejected(self) -> None:
        self.seed()
        source, _ = backport._transform("helpers.cpp", self.read("helpers.cpp"))
        self.core.relative_src_path("esphome", "core", "helpers.cpp").write_text(source, encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "partially patched"):
            backport._patch_generated_sources(self.core)
        self.assertEqual(self.read("helpers.h"), self.originals["helpers.h"])

    def test_duplicate_patch_anchor_is_rejected(self) -> None:
        source = self.originals["helpers.cpp"] + backport._PATCHES["helpers.cpp"][-1][0]
        with self.assertRaisesRegex(RuntimeError, "unexpected helpers.cpp"):
            backport._transform("helpers.cpp", source)

    def test_atomic_replace_keeps_hardlinked_installed_sources_unchanged(self) -> None:
        self.seed()
        installed = self.build / "installed"
        installed.mkdir()
        for name in self.originals:
            (installed / name).hardlink_to(self.core.relative_src_path("esphome", "core", name))
        backport._patch_generated_sources(self.core)
        for name in self.originals:
            self.assertEqual((installed / name).read_text(encoding="utf-8"), self.originals[name])
            self.assertNotEqual(self.read(name), self.originals[name])

    def test_write_failure_cleans_temporary_file_and_preserves_originals(self) -> None:
        self.seed()
        original_temporary_file = backport.tempfile.NamedTemporaryFile

        def failing_temporary_file(*args, **kwargs):
            temporary = original_temporary_file(*args, **kwargs)
            temporary.write = mock.Mock(side_effect=OSError("write failed"))
            return temporary

        with mock.patch.object(backport.tempfile, "NamedTemporaryFile", side_effect=failing_temporary_file):
            with self.assertRaisesRegex(OSError, "write failed"):
                backport._patch_generated_sources(self.core)
        self.assertEqual(set(self.core.relative_src_path("esphome", "core").iterdir()), {
            self.core.relative_src_path("esphome", "core", name) for name in self.originals
        })
        for name in self.originals:
            self.assertEqual(self.read(name), self.originals[name])

    def test_replace_failure_cleans_temporary_file_and_preserves_originals(self) -> None:
        self.seed()
        with mock.patch.object(Path, "replace", side_effect=OSError("replace failed")):
            with self.assertRaisesRegex(OSError, "replace failed"):
                backport._patch_generated_sources(self.core)
        self.assertEqual(set(self.core.relative_src_path("esphome", "core").iterdir()), {
            self.core.relative_src_path("esphome", "core", name) for name in self.originals
        })
        for name in self.originals:
            self.assertEqual(self.read(name), self.originals[name])

    def test_hook_runs_after_copy_and_installs_once(self) -> None:
        copies = []

        def original() -> str:
            copies.append(True)
            self.seed()
            return "result"

        writer = SimpleNamespace(copy_src_tree=original)
        backport.install_four_decimal_patch(self.core, writer)
        first_hook = writer.copy_src_tree
        backport.install_four_decimal_patch(self.core, writer)
        self.assertIs(writer.copy_src_tree, first_hook)
        self.assertEqual(writer.copy_src_tree(), "result")
        self.assertIn("accuracy_decimals <= 4", self.read("helpers.cpp"))
        self.assertEqual(writer.copy_src_tree(), "result")
        self.assertEqual(len(copies), 2)
        self.assertIn("n == 4 ? 10000", self.read("helpers.h"))

    def test_config_reset_disables_hook_and_next_enabled_config_uses_its_own_path(self) -> None:
        writer = SimpleNamespace(copy_src_tree=self.seed)
        backport.install_four_decimal_patch(self.core, writer)
        writer.copy_src_tree()
        self.core.data = {}
        writer.copy_src_tree()
        self.assertEqual(self.read("helpers.cpp"), self.originals["helpers.cpp"])
        old_build = self.build
        self.build = old_build / "next-config"
        backport.install_four_decimal_patch(self.core, writer)
        writer.copy_src_tree()
        self.assertIn("accuracy_decimals <= 4", self.read("helpers.cpp"))
        self.assertEqual((old_build / "esphome/core/helpers.cpp").read_text(encoding="utf-8"), self.originals["helpers.cpp"])

    def test_installed_2026_9_sources_are_supported_and_not_modified(self) -> None:
        try:
            from esphome.const import __version__
        except ImportError:
            self.skipTest("ESPHome 2026.9.0 is not installed")
        if __version__ != "2026.9.0":
            self.skipTest("Requires the pinned ESPHome 2026.9.0 source")
        installed = resources.files("esphome.core")
        self.originals = {name: installed.joinpath(name).read_text(encoding="utf-8") for name in backport._PATCHES}
        self.seed()
        backport._patch_generated_sources(self.core)
        for name in self.originals:
            self.assertEqual(installed.joinpath(name).read_text(encoding="utf-8"), self.originals[name])
        self.assertIn("accuracy_decimals <= 4", self.read("helpers.cpp"))
        self.assertIn("n == 4 ? 10000", self.read("helpers.h"))


if __name__ == "__main__":
    unittest.main()
