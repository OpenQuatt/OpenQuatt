"""Build-local manifest validation and serialized HTTPS OTA for ESPHome 2026.11.1."""

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
        (
            "  void update() override;\n",
            "  void update() override;\n"
            "  uint32_t manifest_check_timeout_ms() const;\n"
            "  bool manifest_result_matches(const std::string &source) const {\n"
            "    return this->manifest_result_current_ && !this->status_has_error() &&\n"
            "           this->source_url_ != nullptr && this->source_url_ == source;\n"
            "  }\n"
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "  void set_manifest_check_paused(bool (*paused)()) { this->manifest_check_paused_ = paused; }\n"
            "#endif\n",
            1,
        ),
        (
            "  bool source_url_owned_{false};",
            "  bool manifest_result_current_{false};\n"
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "  static constexpr uint32_t WEATHER_CHECK_RETRY_ID = 1;\n"
            "  static constexpr uint32_t WEATHER_CHECK_RETRY_MS = 250;\n"
            "  bool manifest_waiting_for_weather_{false};\n"
            "  bool (*manifest_check_paused_)(){nullptr};\n"
            "#endif\n"
            "  bool source_url_owned_{false};",
            1,
        ),
        (
            "  void set_source_url_static(const char *source_url) {\n",
            "  void set_source_url_static(const char *source_url) {\n"
            "    this->manifest_result_current_ = false;\n",
            1,
        ),
    ),
    "http_request_update.cpp": (
        (
            '#include <cstring>\n',
            '#include <cstring>\n'
            '#include <new>\n'
            "#ifdef USE_OPENQUATT_WEATHER\n"
            '#include "esphome/components/openquatt_weather/OpenQuattWeatherPolicy.h"\n'
            '#include "esphome/components/openquatt_weather/WeatherHttpsSlot.h"\n'
            "#endif\n",
            1,
        ),
        (
            "  // Copy before releasing,",
            "  if (this->source_url_ == nullptr || strlen(this->source_url_) != len ||\n"
            "      memcmp(this->source_url_, source_url, len) != 0)\n"
            "    this->manifest_result_current_ = false;\n"
            "  // Copy before releasing,",
            1,
        ),
        (
            "void HttpRequestUpdate::update() {\n",
            "void HttpRequestUpdate::update() {\n"
            "  this->manifest_result_current_ = false;\n"
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
            "  auto *result = new TaskResult();\n",
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
            "  auto *result = new (std::nothrow) TaskResult();\n"
            "  if (result == nullptr) {\n"
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "    openquatt_weather::https_slot.release();\n"
            "#endif\n"
            '    this->status_set_error(LOG_STR("Failed to allocate manifest check"));\n'
            "    return;\n"
            "  }\n",
            1,
        ),
        (
            '  if (xTaskCreate(HttpRequestUpdate::update_task, "update_task", 8192, result, 1, nullptr) != pdPASS) {\n',
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "  constexpr uint32_t stack_size = 6144;\n"
            "#else\n"
            "  constexpr uint32_t stack_size = 8192;\n"
            "#endif\n"
            '  if (xTaskCreate(HttpRequestUpdate::update_task, "update_task", stack_size, result, 1, nullptr) != pdPASS) {\n'
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "    openquatt_weather::https_slot.release();\n"
            "#endif\n"
            '    this->status_set_error(LOG_STR("Failed to start manifest check"));\n',
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
            "  this_update->defer([this_update, result]() {\n",
            "  this_update->defer([this_update, result]() {\n"
            "    const bool current = this_update->source_url_ != nullptr && result->source_url != nullptr &&\n"
            "                         strcmp(this_update->source_url_, result->source_url) == 0;\n",
            1,
        ),
        (
            "    if (result->error_str != nullptr) {\n",
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "    openquatt_weather::https_slot.release();\n"
            "#endif\n"
            "    if (!current) {\n"
            "      delete result;\n"
            "      return;\n"
            "    }\n"
            "    if (result->error_str != nullptr) {\n",
            1,
        ),
        (
            "    this_update->status_clear_error();\n",
            "    this_update->manifest_result_current_ = true;\n"
            "    this_update->status_clear_error();\n",
            1,
        ),
    ),
}


_OTA_PATCHES = {
    "ota_http_request.cpp": (
        (
            '#include "ota_http_request.h"\n',
            '#include "ota_http_request.h"\n'
            "#ifdef USE_OPENQUATT_WEATHER\n"
            '#include "esphome/components/openquatt_weather/WeatherHttpsSlot.h"\n'
            "#endif\n",
            1,
        ),
        (
            "uint8_t OtaHttpRequestComponent::do_ota_() {\n",
            "uint8_t OtaHttpRequestComponent::do_ota_() {\n"
            "#ifdef USE_OPENQUATT_WEATHER\n"
            "  // OTA_STARTED has cancelled weather. Its worker releases only after TLS cleanup.\n"
            "  const uint32_t admission_started = millis();\n"
            "  bool acquired = openquatt_weather::https_slot.try_acquire();\n"
            "  while (!acquired && static_cast<uint32_t>(millis() - admission_started) < 2000U) {\n"
            "    App.feed_wdt();\n"
            "    delay(1);\n"
            "    if (static_cast<uint32_t>(millis() - admission_started) < 2000U)\n"
            "      acquired = openquatt_weather::https_slot.try_acquire();\n"
            "  }\n"
            "  if (!acquired) {\n"
            '    ESP_LOGW(TAG, "HTTPS still busy; OTA download not started");\n'
            "    return OTA_CONNECTION_ERROR;\n"
            "  }\n"
            "  struct HttpsLease {\n"
            "    ~HttpsLease() { openquatt_weather::https_slot.release(); }\n"
            "  } https_lease;\n"
            "#endif\n",
            1,
        ),
    ),
}


def _transform(name: str, source: str) -> tuple[str, bool]:
    replacements = (_PATCHES if name in _PATCHES else _OTA_PATCHES)[name]
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
    # Validate all sources before replacing any. OTA is a separate patch group:
    # an existing generated update pair may already have the ownership fix.
    for name in _OTA_PATCHES:
        path = Path(core.relative_src_path("esphome", "components", "http_request", "ota", name))
        patched, changed = _transform(name, path.read_bytes().decode("utf-8"))
        prepared.append((path, patched, changed))
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
    _LOGGER.info("OpenQuatt: current manifest validation and serialized HTTPS OTA active")


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
