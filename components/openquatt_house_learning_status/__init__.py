from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import web_server
from esphome.components.esp32 import include_builtin_idf_component
from esphome.const import CONF_ID, CONF_TRIGGER_ID, CONF_WEB_SERVER_ID
from esphome.core.config import StartupTrigger

AUTO_LOAD = ["web_server_base"]
DEPENDENCIES = ["openquatt_web_auth", "psram", "web_server"]

CONF_WEB_AUTH = "web_auth"
CONF_RESTORE_SETTINGS = "restore_settings"
CONF_CHANGE_LINE = "change_line"

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
        cv.Optional(CONF_RESTORE_SETTINGS): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(StartupTrigger)}, single=True
        ),
        cv.Optional(CONF_CHANGE_LINE): cv.lambda_,
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
    if CONF_RESTORE_SETTINGS in config:
        restore_settings = config[CONF_RESTORE_SETTINGS]
        # Template numbers initialize at 800. The strategy interval starts at 600.
        trigger = cg.new_Pvariable(restore_settings[CONF_TRIGGER_ID], 799.0)
        await cg.register_component(trigger, restore_settings)
        await automation.build_automation(trigger, [], restore_settings)
    if CONF_CHANGE_LINE in config:
        variables = [(cg.float_, name) for name in ("cold", "rated", "cutoff", "new_rated", "new_cutoff")]
        change_line = await cg.process_lambda(
            config[CONF_CHANGE_LINE], variables + [(cg.bool_, "restore")], return_type=cg.uint8
        )
        cg.add(var.set_change_line(change_line))
