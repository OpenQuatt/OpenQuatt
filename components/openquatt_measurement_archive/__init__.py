import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch, text_sensor
from esphome.components.esp32 import include_builtin_idf_component
from esphome.const import CONF_ID, CONF_RESTORE_MODE

DEPENDENCIES = ["esp32", "psram", "openquatt_weather", "openquatt_web_auth", "web_server"]
AUTO_LOAD = ["text_sensor", "switch"]
ns = cg.esphome_ns.namespace("openquatt_measurement_archive")
MeasurementArchive = ns.class_("MeasurementArchive", cg.Component)
ArchiveEnabledSwitch = ns.class_("ArchiveEnabledSwitch", switch.Switch)
WebAuth = cg.esphome_ns.namespace("openquatt_web_auth").class_("OpenQuattWebAuth", cg.Component)


def _validate_archive_switch(config):
    if config[CONF_RESTORE_MODE] != "DISABLED":
        raise cv.Invalid("The archive switch uses verified storage", path=[CONF_RESTORE_MODE])
    return config


CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MeasurementArchive),
    cv.Required("web_auth"): cv.use_id(WebAuth),
    cv.Required("enabled_switch"): cv.All(
        switch.switch_schema(ArchiveEnabledSwitch, block_inverted=True, default_restore_mode="DISABLED"),
        _validate_archive_switch,
    ),
    cv.Optional("status"): text_sensor.text_sensor_schema(),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_define("USE_OPENQUATT_MEASUREMENT_ARCHIVE")
    include_builtin_idf_component("nvs_flash")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_web_auth(await cg.get_variable(config["web_auth"])))
    cg.add(var.set_switch(await switch.new_switch(config["enabled_switch"])))
    if "status" in config:
        cg.add(var.set_status(await text_sensor.new_text_sensor(config["status"])))
