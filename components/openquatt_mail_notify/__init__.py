import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, socket, time
from esphome.components.esp32 import include_builtin_idf_component, require_certificate_bundle
from esphome.components.ota import request_ota_state_listeners
from esphome.const import CONF_ID

AUTO_LOAD = ["web_server_base"]
DEPENDENCIES = [
    "esp32",
    "network",
    "psram",
    "time",
    "ota",
    "openquatt_web_auth",
    "openquatt_decision_log",
    "web_server",
]

CONF_DECISION_LOG = "decision_log"
CONF_WEB_AUTH = "web_auth"
CONF_CLOCK = "clock"
CONF_PAUSE_SENSOR = "pause_sensor"

ns = cg.esphome_ns.namespace("openquatt_mail_notify")
OpenQuattMailNotify = ns.class_("OpenQuattMailNotify", cg.Component)
DecisionLog = cg.esphome_ns.namespace("openquatt_decision_log").class_("OpenQuattDecisionLog", cg.Component)
WebAuth = cg.esphome_ns.namespace("openquatt_web_auth").class_("OpenQuattWebAuth", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OpenQuattMailNotify),
            cv.Required(CONF_DECISION_LOG): cv.use_id(DecisionLog),
            cv.Required(CONF_WEB_AUTH): cv.use_id(WebAuth),
            cv.Required(CONF_CLOCK): cv.use_id(time.RealTimeClock),
            cv.Optional(CONF_PAUSE_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    socket.consume_sockets(1, "openquatt_mail_notify"),
)


async def to_code(config):
    require_certificate_bundle()
    request_ota_state_listeners()
    include_builtin_idf_component("esp-tls")
    include_builtin_idf_component("mbedtls")
    include_builtin_idf_component("nvs_flash")
    include_builtin_idf_component("esp_http_server")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_decision_log(await cg.get_variable(config[CONF_DECISION_LOG])))
    cg.add(var.set_web_auth(await cg.get_variable(config[CONF_WEB_AUTH])))
    cg.add(var.set_clock(await cg.get_variable(config[CONF_CLOCK])))
    if CONF_PAUSE_SENSOR in config:
        cg.add(var.set_pause_sensor(await cg.get_variable(config[CONF_PAUSE_SENSOR])))
