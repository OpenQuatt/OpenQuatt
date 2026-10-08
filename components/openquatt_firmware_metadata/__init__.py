import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import openquatt_web_auth
from esphome.const import CONF_ID

AUTO_LOAD = ["web_server_base"]
DEPENDENCIES = ["web_server", "openquatt_web_auth"]
CONF_WEB_AUTH = "web_auth"

openquatt_firmware_metadata_ns = cg.esphome_ns.namespace("openquatt_firmware_metadata")
OpenQuattFirmwareMetadata = openquatt_firmware_metadata_ns.class_(
    "OpenQuattFirmwareMetadata", cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattFirmwareMetadata),
        cv.Required(CONF_WEB_AUTH): cv.use_id(openquatt_web_auth.OpenQuattWebAuth),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_global(openquatt_firmware_metadata_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    web_auth = await cg.get_variable(config[CONF_WEB_AUTH])
    cg.add(var.set_web_auth(web_auth))
