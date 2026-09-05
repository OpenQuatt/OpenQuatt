import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import modbus_controller
from esphome.components.esp32 import include_builtin_idf_component
from esphome.const import CONF_ID

AUTO_LOAD = ["web_server_base"]
DEPENDENCIES = [
    "modbus_controller",
    "openquatt_odu_eeprom_dump",
    "openquatt_web_auth",
    "web_server",
]
MULTI_CONF = True

CONF_CONTROLLER = "controller"
CONF_EEPROM_DUMP = "eeprom_dump"
CONF_HP_INDEX = "hp_index"
CONF_WEB_AUTH = "web_auth"

settings_ns = cg.esphome_ns.namespace("openquatt_odu_settings")
OpenQuattOduSettings = settings_ns.class_("OpenQuattOduSettings", cg.Component)
eeprom_ns = cg.esphome_ns.namespace("openquatt_odu_eeprom_dump")
OpenQuattOduEepromDump = eeprom_ns.class_("OpenQuattOduEepromDump", cg.Component)
web_auth_ns = cg.esphome_ns.namespace("openquatt_web_auth")
OpenQuattWebAuth = web_auth_ns.class_("OpenQuattWebAuth", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattOduSettings),
        cv.Required(CONF_CONTROLLER): cv.use_id(modbus_controller.ModbusController),
        cv.Required(CONF_EEPROM_DUMP): cv.use_id(OpenQuattOduEepromDump),
        cv.Required(CONF_WEB_AUTH): cv.use_id(OpenQuattWebAuth),
        cv.Required(CONF_HP_INDEX): cv.int_range(min=1, max=2),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    include_builtin_idf_component("esp_http_server")

    cg.add_global(settings_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    controller = await cg.get_variable(config[CONF_CONTROLLER])
    eeprom_dump = await cg.get_variable(config[CONF_EEPROM_DUMP])
    web_auth = await cg.get_variable(config[CONF_WEB_AUTH])
    cg.add(var.set_controller(controller))
    cg.add(var.set_eeprom_dump(eeprom_dump))
    cg.add(var.set_web_auth(web_auth))
    cg.add(var.set_hp_index(config[CONF_HP_INDEX]))
