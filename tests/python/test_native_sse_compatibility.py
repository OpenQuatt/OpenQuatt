from __future__ import annotations

from importlib import resources, util
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[2]


class NativeSseCompatibilityTest(unittest.TestCase):
    def test_generated_patch_preserves_native_httpd_sse_and_reset_between_configs(self) -> None:
        from esphome.const import __version__
        from esphome.helpers import copy_file_if_changed

        expected = (ROOT / ".github/requirements-esphome.txt").read_text().strip().removeprefix("esphome==")
        self.assertEqual(__version__, expected)
        spec = util.spec_from_file_location(
            "http_request_update_source", ROOT / "components/web_server_base/http_request_update_source.py"
        )
        patch = util.module_from_spec(spec)
        spec.loader.exec_module(patch)
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
        installed = resources.files("esphome.components.http_request.update")
        sdk_paths.update({
            ("esphome", "components", "http_request", "update", name): Path(str(installed.joinpath(name)))
            for name in ["http_request_update.cpp", "http_request_update.h"]
        })
        ota_parts = ("esphome", "components", "http_request", "ota", "ota_http_request.cpp")
        sdk_paths[ota_parts] = Path(str(
            resources.files("esphome.components.http_request.ota").joinpath("ota_http_request.cpp")))
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
            patch.install_update_source_patch(core, writer)
            hook = writer.copy_src_tree
            for _ in range(2):
                self.assertEqual(writer.copy_src_tree(), "copied")
                expected_cpp = originals[cpp_parts].decode()
                self.assertEqual(core.relative_src_path(*cpp_parts).read_text(), expected_cpp)
                self.assertEqual(core.relative_src_path(*header_parts).read_bytes(), originals[header_parts])
                for name in ["helpers.cpp", "helpers.h"]:
                    parts = ("esphome", "core", name)
                    self.assertEqual(core.relative_src_path(*parts).read_bytes(), originals[parts])
                for name in ["http_request_update.cpp", "http_request_update.h"]:
                    parts = ("esphome", "components", "http_request", "update", name)
                    expected_source = patch._transform(name, originals[parts].decode())[0]
                    self.assertEqual(core.relative_src_path(*parts).read_text(), expected_source)
                expected_ota = patch._transform(
                    "ota_http_request.cpp", originals[ota_parts].decode())[0]
                self.assertEqual(core.relative_src_path(*ota_parts).read_text(), expected_ota)

            core.data = {}
            writer.copy_src_tree()
            for parts, source in originals.items():
                self.assertEqual(core.relative_src_path(*parts).read_bytes(), source)
            build = build / "next-config"
            patch.install_update_source_patch(core, writer)
            self.assertIs(writer.copy_src_tree, hook)
            writer.copy_src_tree()
            self.assertEqual(core.relative_src_path(*cpp_parts).read_text(), expected_cpp)
            self.assertEqual(core.relative_src_path(*ota_parts).read_text(), expected_ota)
            self.assertEqual({parts: path.read_bytes() for parts, path in sdk_copies.items()}, originals)
            self.assertEqual({parts: path.read_bytes() for parts, path in sdk_paths.items()}, originals)


if __name__ == "__main__":
    unittest.main()
