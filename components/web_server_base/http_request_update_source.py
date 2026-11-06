"""Build-local manifest URL ownership and HTTPS scheduling for ESPHome 2026.10.0."""

from __future__ import annotations

from functools import wraps
import logging
from pathlib import Path
import tempfile
from typing import Any


_LOGGER = logging.getLogger(__name__)
_ENABLED_KEY = "openquatt_http_request_update_source"
_HOOK_MARKER = "_openquatt_http_request_update_source"
_PATCHES = {
    "http_request_update.h": (
        ("#pragma once\n", "#pragma once\n\n#include <string>\n", 1),
        (
            "  void update() override;\n",
            "  void update() override;\n"
            "  uint32_t manifest_check_timeout_ms() const;\n"
            "  const std::string &get_source_url() const { return this->source_url_; }\n"
            "  bool manifest_result_matches(const std::string &source) const {\n"
            "#ifdef USE_ESP32\n"
            "    if (this->update_task_handle_ != nullptr) return false;\n"
            "#endif\n"
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "    if (this->manifest_waiting_for_weather_) return false;\n"
            "#endif\n"
            "    return !this->status_has_error() && this->source_url_ == source && this->task_source_url_ == source;\n"
            "  }\n"
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "  void set_manifest_check_paused(bool (*paused)()) { this->manifest_check_paused_ = paused; }\n"
            "#endif\n",
            1,
        ),
        (
            "  const char *source_url_{nullptr};  // literal from codegen",
            "  // Setters run on the main loop; the worker only reads its frozen snapshot.\n"
            "  std::string source_url_;\n"
            "  std::string task_source_url_;\n"
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "  static constexpr uint32_t WEATHER_CHECK_RETRY_ID = 1;\n"
            "  static constexpr uint32_t WEATHER_CHECK_RETRY_MS = 250;\n"
            "  bool manifest_waiting_for_weather_{false};\n"
            "  bool (*manifest_check_paused_)(){nullptr};\n"
            "#endif",
            1,
        ),
    ),
    "http_request_update.cpp": (
        (
            '#include "http_request_update.h"\n',
            '#include "http_request_update.h"\n'
            "#ifdef USE_OPENQUATT_WEATHER\n"
            '#include "esphome/components/openquatt_weather/OpenQuattWeatherPolicy.h"\n'
            '#include "esphome/components/openquatt_weather/WeatherHttpsSlot.h"\n'
            "#endif\n",
            1,
        ),
        (
            "void HttpRequestUpdate::update() {\n",
            "void HttpRequestUpdate::update() {\n"
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "  if (this->manifest_waiting_for_weather_) {\n"
            "    this->cancel_timeout(WEATHER_CHECK_RETRY_ID);\n"
            "    this->manifest_waiting_for_weather_ = false;\n"
            "    openquatt_weather::https_slot.set_manifest_waiting(false);\n"
            "  }\n"
            "#endif\n",
            1,
        ),
        (
            '  xTaskCreate(HttpRequestUpdate::update_task, "update_task", 8192, (void *) this, 1, &this->update_task_handle_);',
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "  if (!openquatt_weather::https_slot.try_acquire()) {\n"
            "    this->manifest_waiting_for_weather_ = true;\n"
            "    openquatt_weather::https_slot.set_manifest_waiting(true);\n"
            "    this->set_timeout(WEATHER_CHECK_RETRY_ID, WEATHER_CHECK_RETRY_MS, [this]() {\n"
            "      this->manifest_waiting_for_weather_ = false;\n"
            "      openquatt_weather::https_slot.set_manifest_waiting(false);\n"
            "      if (this->state_ != update::UPDATE_STATE_INSTALLING &&\n"
            "          !(this->manifest_check_paused_ && this->manifest_check_paused_()))\n"
            "        this->update();\n"
            "    });\n"
            "    return;\n"
            "  }\n"
            "#endif\n"
            "  this->task_source_url_ = this->source_url_;\n"
            '  if (xTaskCreate(HttpRequestUpdate::update_task, "update_task", 8192, (void *) this, 1,\n'
            "                  &this->update_task_handle_) != pdPASS) {\n"
            "    this->update_task_handle_ = nullptr;\n"
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "    openquatt_weather::https_slot.release();\n"
            "#endif\n"
            '    this->status_set_error(LOG_STR("Failed to start manifest check"));\n'
            "  }",
            1,
        ),
        (
            "void HttpRequestUpdate::update_task(void *params) {\n",
            "uint32_t HttpRequestUpdate::manifest_check_timeout_ms() const {\n"
            "  constexpr uint32_t result_timeout_ms = 20000;\n"
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "  if (this->manifest_waiting_for_weather_)\n"
            "    return result_timeout_ms + oq_weather::kRequestDeadlineMs + 2 * WEATHER_CHECK_RETRY_MS;\n"
            "#endif\n"
            "  return result_timeout_ms;\n"
            "}\n\n"
            "void HttpRequestUpdate::update_task(void *params) {\n",
            1,
        ),
        (
            "    this_update->update_task_handle_ = nullptr;\n",
            "    this_update->update_task_handle_ = nullptr;\n"
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "    // The worker has closed and released the HTTP container on every path.\n"
            "    openquatt_weather::https_slot.release();\n"
            "#endif\n",
            1,
        ),
        (
            "#else\n  this->update_task(this);",
            "#else\n  this->task_source_url_ = this->source_url_;\n  this->update_task(this);",
            1,
        ),
        ("this_update->source_url_", "this_update->task_source_url_.c_str()", 4),
    ),
}


def _transform(name: str, source: str) -> tuple[str, bool]:
    replacements = _PATCHES[name]
    # The include anchor is also a prefix of its replacement.
    def original_present(old: str, new: str, count: int) -> bool:
        return source.count(old) == count and new not in source

    def patched_present(old: str, new: str, count: int) -> bool:
        return source.count(new) == count and source.replace(new, "").count(old) == 0

    if all(patched_present(*replacement) for replacement in replacements):
        return source, False
    if not all(original_present(*replacement) for replacement in replacements):
        raise RuntimeError(
            f"OpenQuatt manifest URL ownership fix: unexpected {name}; "
            "review the ESPHome update worker before upgrading this compatibility patch."
        )
    for old, new, count in replacements:
        source = source.replace(old, new, count)
    return source, True


def _patch_generated_sources(core: Any) -> None:
    prepared = []
    for name in _PATCHES:
        path = Path(core.relative_src_path("esphome", "components", "http_request", "update", name))
        source = path.read_bytes().decode("utf-8")
        patched, changed = _transform(name, source)
        prepared.append((path, patched, changed))
    if len({changed for _, _, changed in prepared}) != 1:
        raise RuntimeError("OpenQuatt manifest URL ownership fix: partially patched update component; review both files.")
    for path, source, changed in prepared:
        if not changed:
            continue
        # Break hard links: never mutate the installed ESPHome package.
        temporary_path = None
        try:
            with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as temporary:
                temporary_path = Path(temporary.name)
                temporary.write(source.encode("utf-8"))
            temporary_path.replace(path)
        finally:
            if temporary_path is not None:
                temporary_path.unlink(missing_ok=True)
    _LOGGER.info("OpenQuatt: owned manifest URLs and immutable update-task snapshot active")


def install_update_source_patch(core: Any, writer: Any) -> None:
    """Enable the version-gated fix for this config, after copying upstream sources."""
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
