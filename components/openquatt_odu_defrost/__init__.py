import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import modbus_controller
from esphome.components.esp32 import include_builtin_idf_component
from esphome.const import CONF_ID

AUTO_LOAD = ["web_server_base"]
DEPENDENCIES = ["modbus_controller", "openquatt_odu_eeprom_dump", "openquatt_web_auth", "web_server"]
MULTI_CONF = True
ns = cg.esphome_ns.namespace("openquatt_odu_defrost")
OpenQuattOduDefrost = ns.class_("OpenQuattOduDefrost", cg.Component)
Dump = cg.esphome_ns.namespace("openquatt_odu_eeprom_dump").class_("OpenQuattOduEepromDump", cg.Component)
Auth = cg.esphome_ns.namespace("openquatt_web_auth").class_("OpenQuattWebAuth", cg.Component)
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(OpenQuattOduDefrost),
    cv.Required("controller"): cv.use_id(modbus_controller.ModbusController),
    cv.Required("eeprom_dump"): cv.use_id(Dump),
    cv.Required("web_auth"): cv.use_id(Auth),
    cv.Required("hp_index"): cv.int_range(min=1, max=2),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    include_builtin_idf_component("esp_http_server")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    for key in ("controller", "eeprom_dump", "web_auth"):
        cg.add(getattr(var, "set_" + key)(await cg.get_variable(config[key])))
    cg.add(var.set_hp_index(config["hp_index"]))
