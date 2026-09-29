import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number, openquatt_web_auth, web_server
from esphome.const import CONF_ID, CONF_WEB_SERVER_ID

AUTO_LOAD = ["json", "web_server_base"]
DEPENDENCIES = ["web_server"]

CONF_WEB_AUTH = "web_auth"
CONF_CURVE_POINTS = "curve_points"

openquatt_entities_ns = cg.esphome_ns.namespace("openquatt_entities")
OpenQuattEntities = openquatt_entities_ns.class_("OpenQuattEntities", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattEntities),
        cv.GenerateID(CONF_WEB_SERVER_ID): cv.use_id(web_server.WebServer),
        cv.Required(CONF_WEB_AUTH): cv.use_id(openquatt_web_auth.OpenQuattWebAuth),
        cv.Required(CONF_CURVE_POINTS): cv.All(cv.ensure_list(cv.use_id(number.Number)), cv.Length(min=6, max=6)),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_global(openquatt_entities_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    web_server_var = await cg.get_variable(config[CONF_WEB_SERVER_ID])
    cg.add(var.set_web_server(web_server_var))
    web_auth_var = await cg.get_variable(config[CONF_WEB_AUTH])
    cg.add(var.set_web_auth(web_auth_var))
    for index, point_id in enumerate(config[CONF_CURVE_POINTS]):
        point_var = await cg.get_variable(point_id)
        cg.add(var.set_curve_point(index, point_var))
