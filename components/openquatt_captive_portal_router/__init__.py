import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["web_server_base"]
DEPENDENCIES = ["captive_portal", "web_server"]

openquatt_captive_portal_router_ns = cg.esphome_ns.namespace(
    "openquatt_captive_portal_router"
)
OpenQuattCaptivePortalRouter = openquatt_captive_portal_router_ns.class_(
    "OpenQuattCaptivePortalRouter", cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {cv.GenerateID(): cv.declare_id(OpenQuattCaptivePortalRouter)}
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_global(openquatt_captive_portal_router_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
