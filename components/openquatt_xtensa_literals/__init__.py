"""Keep Xtensa literal pools close to their users in ESPHome's main.cpp."""

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
    # The generated lambdas share one large translation unit. Separate literal
    # pools can exceed the L32R range as unrelated lambdas are added.
    esp32.add_src_cmake(
        'set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"\n'
        "    PROPERTIES\n"
        "      COMPILE_OPTIONS -mtext-section-literals\n"
        "      SKIP_PRECOMPILE_HEADERS ON)"
    )
