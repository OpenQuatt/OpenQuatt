import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.components.openquatt_web_auth import OpenQuattWebAuth
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32", "openquatt_web_auth", "web_server_base"]
recovery_ns = cg.esphome_ns.namespace("openquatt_recovery")
OpenQuattRecovery = recovery_ns.class_("OpenQuattRecovery", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(OpenQuattRecovery),
    cv.Required("web_auth"): cv.use_id(OpenQuattWebAuth),
    cv.Required("button"): cv.use_id(binary_sensor.BinarySensor),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_web_auth(await cg.get_variable(config["web_auth"])))
    cg.add(var.set_button(await cg.get_variable(config["button"])))
