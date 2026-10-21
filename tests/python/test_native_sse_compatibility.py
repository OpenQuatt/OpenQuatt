from __future__ import annotations

from importlib import resources, util
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[2]


class NativeSseCompatibilityTest(unittest.TestCase):
    def test_formatter_preserves_native_httpd_sse_and_reset_between_configs(self) -> None:
        from esphome.const import __version__
        from esphome.helpers import copy_file_if_changed

        expected = (ROOT / ".github/requirements-esphome.txt").read_text().strip().removeprefix("esphome==")
        self.assertEqual(__version__, expected)
        modules = {}
        for name in ["core_four_decimals"]:
            spec = util.spec_from_file_location(name, ROOT / "components/web_server_base" / f"{name}.py")
            modules[name] = util.module_from_spec(spec)
            spec.loader.exec_module(modules[name])
        installed = resources.files("esphome.components.web_server_idf")
        sdk_paths = {
            ("esphome", "components", "web_server_idf", name): Path(str(installed.joinpath(name)))
            for name in ["web_server_idf.cpp", "web_server_idf.h"]
        }
        installed = resources.files("esphome.core")
        sdk_paths.update({
            ("esphome", "core", name): Path(str(installed.joinpath(name)))
            for name in ["helpers.cpp", "helpers.h"]
        })
        originals = {parts: path.read_bytes() for parts, path in sdk_paths.items()}
        cpp_parts = ("esphome", "components", "web_server_idf", "web_server_idf.cpp")
        header_parts = ("esphome", "components", "web_server_idf", "web_server_idf.h")
        self.assertIn(b"config.stack_size = 4512;", originals[cpp_parts])
        self.assertFalse((ROOT / "components/web_server_base/core_httpd_stack.py").exists())
        self.assertNotIn("install_httpd_stack_patch", (ROOT / "components/web_server_base/__init__.py").read_text())
        self.assertIn(b"prepare_json_", originals[cpp_parts])
        self.assertIn(b"prepare_json_", originals[header_parts])
        self.assertFalse((ROOT / "components/web_server_base/core_sse_json.py").exists())
        self.assertNotIn("install_sse_json_patch", (ROOT / "components/web_server_base/__init__.py").read_text())

        with tempfile.TemporaryDirectory() as temporary:
            sdk_copies = {parts: Path(temporary).joinpath("installed", *parts) for parts in sdk_paths}
            for parts, path in sdk_copies.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(originals[parts])
            build = Path(temporary) / "build"
            core = SimpleNamespace(data={}, relative_src_path=lambda *parts: build.joinpath(*parts))

            def copy_sources() -> str:
                for parts, source in sdk_copies.items():
                    destination = core.relative_src_path(*parts)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    if not destination.exists():
                        destination.hardlink_to(source)
                    else:
                        copy_file_if_changed(source, destination)
                return "copied"

            writer = SimpleNamespace(copy_src_tree=copy_sources)
            installers = [modules["core_four_decimals"].install_four_decimal_patch]
            for install in installers:
                install(core, writer)
            hook = writer.copy_src_tree
            for _ in range(2):
                self.assertEqual(writer.copy_src_tree(), "copied")
                expected_cpp = originals[cpp_parts].decode()
                self.assertEqual(core.relative_src_path(*cpp_parts).read_text(), expected_cpp)
                self.assertEqual(core.relative_src_path(*header_parts).read_bytes(), originals[header_parts])
                for name in ["helpers.cpp", "helpers.h"]:
                    source = core.relative_src_path("esphome", "core", name).read_text()
                    expected_source = modules["core_four_decimals"]._transform(
                        name, originals[("esphome", "core", name)].decode())[0]
                    self.assertEqual(source, expected_source)

            core.data = {}
            writer.copy_src_tree()
            for parts, source in originals.items():
                self.assertEqual(core.relative_src_path(*parts).read_bytes(), source)
            build = build / "next-config"
            for install in installers:
                install(core, writer)
            self.assertIs(writer.copy_src_tree, hook)
            writer.copy_src_tree()
            self.assertEqual(core.relative_src_path(*cpp_parts).read_text(), expected_cpp)
            self.assertEqual({parts: path.read_bytes() for parts, path in sdk_copies.items()}, originals)
            self.assertEqual({parts: path.read_bytes() for parts, path in sdk_paths.items()}, originals)


if __name__ == "__main__":
    unittest.main()
