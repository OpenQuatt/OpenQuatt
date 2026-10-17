import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import factory_reset, modbus_controller
from esphome.components.esp32 import include_builtin_idf_component
from esphome.const import CONF_ID

DEPENDENCIES = ["modbus_controller", "openquatt_odu_eeprom_dump", "openquatt_odu_settings", "openquatt_odu_defrost", "openquatt_web_auth", "web_server", "factory_reset"]
MULTI_CONF = True
ns = cg.esphome_ns.namespace("openquatt_odu_eeprom")
OpenQuattOduEeprom = ns.class_("OpenQuattOduEeprom", cg.Component)

def component(name, cls):
    return cg.esphome_ns.namespace(name).class_(cls, cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(OpenQuattOduEeprom),
    cv.Required("controller"): cv.use_id(modbus_controller.ModbusController),
    cv.Required("eeprom_dump"): cv.use_id(component("openquatt_odu_eeprom_dump", "OpenQuattOduEepromDump")),
    cv.Required("settings"): cv.use_id(component("openquatt_odu_settings", "OpenQuattOduSettings")),
    cv.Required("defrost"): cv.use_id(component("openquatt_odu_defrost", "OpenQuattOduDefrost")),
    cv.Required("web_auth"): cv.use_id(component("openquatt_web_auth", "OpenQuattWebAuth")),
    cv.Required("factory_reset_counter"): cv.use_id(factory_reset.FactoryResetComponent),
    cv.Required("hp_index"): cv.int_range(min=1, max=2),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    include_builtin_idf_component("esp_http_server")
    include_builtin_idf_component("nvs_flash")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    for key in ("controller", "eeprom_dump", "settings", "defrost", "web_auth", "factory_reset_counter"):
        cg.add(getattr(var, f"set_{key}")(await cg.get_variable(config[key])))
    cg.add(var.set_hp_index(config["hp_index"]))
