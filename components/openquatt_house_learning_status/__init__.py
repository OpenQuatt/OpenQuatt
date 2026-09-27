import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import web_server
from esphome.components.esp32 import include_builtin_idf_component
from esphome.const import CONF_ID, CONF_WEB_SERVER_ID

AUTO_LOAD = ["web_server_base"]
DEPENDENCIES = ["openquatt_web_auth", "psram", "web_server"]

CONF_WEB_AUTH = "web_auth"

openquatt_house_learning_status_ns = cg.esphome_ns.namespace(
    "openquatt_house_learning_status"
)
OpenQuattHouseLearningStatus = openquatt_house_learning_status_ns.class_(
    "OpenQuattHouseLearningStatus", cg.Component
)
openquatt_web_auth_ns = cg.esphome_ns.namespace("openquatt_web_auth")
OpenQuattWebAuth = openquatt_web_auth_ns.class_("OpenQuattWebAuth", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattHouseLearningStatus),
        cv.GenerateID(CONF_WEB_SERVER_ID): cv.use_id(web_server.WebServer),
        cv.Required(CONF_WEB_AUTH): cv.use_id(OpenQuattWebAuth),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    include_builtin_idf_component("esp_http_server")

    cg.add_global(openquatt_house_learning_status_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    web_server_var = await cg.get_variable(config[CONF_WEB_SERVER_ID])
    web_auth = await cg.get_variable(config[CONF_WEB_AUTH])
    cg.add(var.set_web_server(web_server_var))
    cg.add(var.set_web_auth(web_auth))
