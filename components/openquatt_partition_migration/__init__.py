"""Early Q NVS expansion using public ESP-IDF APIs; no ESPHome core patch."""
import esphome.codegen as cg
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
    cg.add_build_flag("-Wl,--wrap=app_main")
    # IDF's default-chip guard otherwise rejects the primary table sector.
    # This SDK choice is global, also after startup; the adapter has its own
    # exact write allowlist. Keep the lifetime limitation explicit in the docs.
    esp32.add_idf_sdkconfig_option("CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED", True)
    esp32.add_idf_sdkconfig_option("CONFIG_SPI_FLASH_DANGEROUS_WRITE_ABORTS", False)
    esp32.add_idf_sdkconfig_option("CONFIG_SPI_FLASH_DANGEROUS_WRITE_FAILS", False)
