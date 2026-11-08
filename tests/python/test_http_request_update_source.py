from __future__ import annotations

from importlib import resources, util
from pathlib import Path
import re
import tempfile
from types import SimpleNamespace
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]
SPEC = util.spec_from_file_location("update_source", ROOT / "components/web_server_base/http_request_update_source.py")
PATCH = util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


def upstream_sources() -> dict[str, str]:
    from esphome.const import __version__

    if __version__ != "2026.10.0":
        raise RuntimeError("Manifest URL regressions require the pinned ESPHome 2026.10.0")
    installed = resources.files("esphome.components.http_request.update")
    return {name: installed.joinpath(name).read_text() for name in PATCH._PATCHES}


class HttpRequestUpdateSourceTest(unittest.TestCase):
    def test_http_ota_patch_is_pinned_and_idempotent(self) -> None:
        source = resources.files("esphome.components.http_request.ota").joinpath("ota_http_request.cpp").read_text()
        patched, changed = PATCH._transform("ota_http_request.cpp", source)
        self.assertTrue(changed)
        self.assertEqual(PATCH._transform("ota_http_request.cpp", patched), (patched, False))
        with self.assertRaisesRegex(RuntimeError, "unexpected"):
            PATCH._transform("ota_http_request.cpp", source.replace("uint8_t OtaHttpRequestComponent::do_ota_() {", "changed"))
        self.assertLess(patched.index("bool acquired ="), patched.index("uint8_t buf["))
        self.assertLess(patched.index("bool acquired ="), patched.index("!this->http_get_md5_()"))

    def test_actual_manual_and_update_ota_admission_waits_or_fails_without_io(self) -> None:
        ota = resources.files("esphome.components.http_request.ota").joinpath("ota_http_request.cpp").read_text()
        ota = PATCH._transform("ota_http_request.cpp", ota)[0]
        flash = "void OtaHttpRequestComponent::flash() {" + ota.split("void OtaHttpRequestComponent::flash() {", 1)[1].split(
            "void OtaHttpRequestComponent::cleanup_", 1
        )[0]
        admission = ota.split(
            "uint8_t OtaHttpRequestComponent::do_ota_() {", 1
        )[1].split("  uint8_t buf[", 1)[0]
        image_error = "  if (container == nullptr || container->status_code != HTTP_STATUS_OK) {" + ota.split(
            "  if (container == nullptr || container->status_code != HTTP_STATUS_OK) {", 1
        )[1].split("  // we will compute MD5", 1)[0]
        update = upstream_sources()["http_request_update.cpp"]
        perform = "void HttpRequestUpdate::perform(bool force) {" + update.split(
            "void HttpRequestUpdate::perform(bool force) {", 1
        )[1].split("\n}  // namespace", 1)[0]
        fixture = (ROOT / "tests/fixtures/http_request_ota_admission.cpp").read_text()
        source = fixture.replace("// GENERATED_FLASH", flash).replace("// GENERATED_ADMISSION", admission).replace(
            "// GENERATED_PERFORM", perform).replace("// GENERATED_IMAGE_ERROR", image_error)
        with tempfile.TemporaryDirectory(prefix="openquatt-ota-admission-") as temporary:
            path = Path(temporary) / "test.cpp"
            executable = Path(temporary) / "test"
            path.write_text(source)
            for flags in ((), ("-DUSE_OPENQUATT_WEATHER",)):
                run_process([*compiler_command("-DUSE_OTA_STATE_LISTENER", *flags), "-I",
                             str(ROOT / "components/openquatt_weather"), str(path), "-o", str(executable)],
                            check=True, timeout=COMPILE_TIMEOUT_SECONDS)
                run_process([str(executable)], check=True, timeout=RUN_TIMEOUT_SECONDS)

    def test_generated_ota_group_is_validated_before_writes_and_upgrades_existing_update_patch(self) -> None:
        ota = resources.files("esphome.components.http_request.ota").joinpath("ota_http_request.cpp").read_text()
        originals = upstream_sources()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core = SimpleNamespace(relative_src_path=lambda *parts: root.joinpath(*parts))
            paths = {}
            for name, source in originals.items():
                path = core.relative_src_path("esphome", "components", "http_request", "update", name)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(source)
                paths[name] = path
            path = core.relative_src_path("esphome", "components", "http_request", "ota", "ota_http_request.cpp")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("changed upstream")
            with self.assertRaisesRegex(RuntimeError, "unexpected"):
                PATCH._patch_generated_sources(core)
            for name, source in originals.items():
                self.assertEqual(paths[name].read_text(), source)
                paths[name].write_text(PATCH._transform(name, source)[0])
            path.write_text(ota)
            PATCH._patch_generated_sources(core)
            self.assertEqual(path.read_text(), PATCH._transform("ota_http_request.cpp", ota)[0])
            PATCH._patch_generated_sources(core)
            self.assertEqual(path.read_text(), PATCH._transform("ota_http_request.cpp", ota)[0])

    def test_patch_rejects_changed_or_partially_patched_sources(self) -> None:
        originals = upstream_sources()
        for name, source in originals.items():
            patched, changed = PATCH._transform(name, source)
            self.assertTrue(changed)
            self.assertEqual(PATCH._transform(name, patched), (patched, False))
            old, new, _ = PATCH._PATCHES[name][-1]
            with self.assertRaisesRegex(RuntimeError, "unexpected"):
                PATCH._transform(name, source.replace(old, "upstream changed"))
            with self.assertRaisesRegex(RuntimeError, "unexpected"):
                PATCH._transform(name, source.replace(old, new, 1))

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            core = SimpleNamespace(relative_src_path=lambda *parts: root.joinpath(*parts))
            paths = {}
            for name, source in originals.items():
                path = core.relative_src_path("esphome", "components", "http_request", "update", name)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(PATCH._transform(name, source)[0] if name.endswith(".h") else source)
                paths[name] = path
            before = {name: path.read_bytes() for name, path in paths.items()}
            with self.assertRaisesRegex(RuntimeError, "partially patched"):
                PATCH._patch_generated_sources(core)
            self.assertEqual({name: path.read_bytes() for name, path in paths.items()}, before)

    def test_worker_uses_only_frozen_source_until_deferred_completion(self) -> None:
        cpp = PATCH._transform("http_request_update.cpp", upstream_sources()["http_request_update.cpp"])[0]
        worker = cpp.split("void HttpRequestUpdate::update_task(void *params) {", 1)[1].split(
            "void HttpRequestUpdate::perform", 1
        )[0]
        self.assertNotIn("this_update->source_url_", worker)
        self.assertEqual(worker.count("this_update->task_source_url_.c_str()"), 4)
        self.assertNotIn("task_source_url_ =", worker)
        self.assertLess(worker.rindex("task_source_url_"), worker.index("this_update->defer("))
        self.assertIn("this_update->update_task_handle_ = nullptr;", worker)

    def test_generated_cpp_owns_temporary_urls_and_freezes_each_request(self) -> None:
        sources = {name: PATCH._transform(name, source)[0] for name, source in upstream_sources().items()}
        # Compile the actual generated class and update() method. Dependencies
        # are stubs; task execution/completion is controlled by the harness.
        header = re.sub(r"^#(?:include|pragma).*\n", "", sources["http_request_update.h"], flags=re.MULTILINE)
        header = header.replace(" protected:", " public:").replace(" private:", " public:")
        cpp = sources["http_request_update.cpp"]
        update = "void HttpRequestUpdate::update() {" + cpp.split("void HttpRequestUpdate::update() {", 1)[1].split(
            "void HttpRequestUpdate::update_task", 1
        )[0]
        relative_url = cpp.split("    // Merge source_url_ and firmware_url\n", 1)[1].split(
            "\n#ifdef ESPHOME_PROJECT_VERSION", 1
        )[0]
        completion = cpp.split("this_update->defer([this_update, result]() {\n", 1)[1].split(
            "    if (result->error_str != nullptr)", 1
        )[0]
        fixture = (ROOT / "tests/fixtures/http_request_update_source.cpp").read_text()
        source = fixture.replace("// GENERATED_HEADER", header).replace("// GENERATED_UPDATE", update).replace(
            "// GENERATED_RELATIVE_URL", relative_url
        ).replace("// GENERATED_COMPLETION", completion)
        with tempfile.TemporaryDirectory(prefix="openquatt-update-source-") as temporary:
            path = Path(temporary) / "test.cpp"
            executable = Path(temporary) / "test"
            path.write_text(source)
            for target_flags in (("-DUSE_ESP32",), (), ("-DUSE_ESP32", "-DUSE_OPENQUATT_WEATHER")):
                run_process(
                    [*compiler_command(*target_flags), "-I", str(ROOT / "components/openquatt_weather"),
                     str(path), "-o", str(executable)],
                    check=True, timeout=COMPILE_TIMEOUT_SECONDS,
                )
                run_process([str(executable)], check=True, timeout=RUN_TIMEOUT_SECONDS)


if __name__ == "__main__":
    unittest.main()
