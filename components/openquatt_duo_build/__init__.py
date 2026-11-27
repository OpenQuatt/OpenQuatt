"""Build options for the generated Q Duo application source."""

import esphome.config_validation as cv
from esphome.components import esp32
from esphome.components.esp32.const import VARIANT_ESP32S3
from esphome.const import Framework

DEPENDENCIES = ["esp32"]
CONFIG_SCHEMA = cv.All(
    cv.Schema({}),
    esp32.only_on_variant(supported=[VARIANT_ESP32S3]),
    cv.only_with_framework(Framework.ESP_IDF),
)


async def to_code(config):
    # Keep literal pools within L32R range in ESPHome's large generated main.cpp.
    # The shared precompiled header uses the default literal placement.
    esp32.add_src_cmake(
        'set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp" APPEND '
        'PROPERTY COMPILE_OPTIONS "-mtext-section-literals")\n'
        'set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp" '
        "PROPERTY SKIP_PRECOMPILE_HEADERS ON)"
    )
