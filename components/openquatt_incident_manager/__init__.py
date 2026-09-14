import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, globals as globals_component, time
from esphome.const import CONF_ID
from esphome.core import CORE, CoroPriority, coroutine_with_priority

AUTO_LOAD = ["globals", "time", "web_server_base"]
DEPENDENCIES = [
    "psram",
    "web_server",
    "openquatt_decision_log",
    "openquatt_web_auth",
]

CONF_CLOCK = "clock"
CONF_CONTROL_MODE_CODE = "control_mode_code"
CONF_DECISION_LOG = "decision_log"
CONF_WEB_AUTH = "web_auth"
CONF_MINIMUM_OFF_TIME = "minimum_off_time"
CONF_POLLING_PAUSED = "polling_paused"
CONF_OTA_HANDOFF_ID = "ota_handoff_id"

openquatt_incident_manager_ns = cg.esphome_ns.namespace(
    "openquatt_incident_manager"
)
OpenQuattIncidentManager = openquatt_incident_manager_ns.class_(
    "OpenQuattIncidentManager", cg.Component
)
OpenQuattOtaHandoff = openquatt_incident_manager_ns.class_(
    "OpenQuattOtaHandoff", cg.Component
)

openquatt_decision_log_ns = cg.esphome_ns.namespace("openquatt_decision_log")
OpenQuattDecisionLog = openquatt_decision_log_ns.class_(
    "OpenQuattDecisionLog", cg.Component
)

openquatt_web_auth_ns = cg.esphome_ns.namespace("openquatt_web_auth")
OpenQuattWebAuth = openquatt_web_auth_ns.class_(
    "OpenQuattWebAuth", cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattIncidentManager),
        cv.GenerateID(CONF_OTA_HANDOFF_ID): cv.declare_id(OpenQuattOtaHandoff),
        cv.Required(CONF_CLOCK): cv.use_id(time.RealTimeClock),
        cv.Required(CONF_CONTROL_MODE_CODE): cv.use_id(
            globals_component.GlobalsComponent
        ),
        cv.Required(CONF_DECISION_LOG): cv.use_id(OpenQuattDecisionLog),
        cv.Required(CONF_WEB_AUTH): cv.use_id(OpenQuattWebAuth),
        cv.Required(CONF_MINIMUM_OFF_TIME): cv.positive_time_period_milliseconds,
        cv.Required(CONF_POLLING_PAUSED): cv.use_id(binary_sensor.BinarySensor),
    }
).extend(cv.COMPONENT_SCHEMA)


@coroutine_with_priority(CoroPriority.APPLICATION + 1)
async def to_code(config):
    # Consume before safe_mode's generated early return, not in Component::setup().
    initialize = openquatt_incident_manager_ns.initialize_restart_handoff(config[CONF_MINIMUM_OFF_TIME])
    cg.add(cg.RawExpression(f"if (!{initialize}) {{ esphome::arch_restart(); return; }}"))
    CORE.add_job(_runtime_to_code, config)


async def _runtime_to_code(config):
    # Keep the normal component and its dependencies out of safe mode.
    cg.add_global(openquatt_incident_manager_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_minimum_off_ms(config[CONF_MINIMUM_OFF_TIME]))
    polling_paused = await cg.get_variable(config[CONF_POLLING_PAUSED])
    cg.add(var.set_polling_paused(polling_paused))
    clock = await cg.get_variable(config[CONF_CLOCK])
    cg.add(var.set_clock(clock))
    control_mode_code = await cg.get_variable(config[CONF_CONTROL_MODE_CODE])
    cg.add(var.set_control_mode_code(control_mode_code))
    decision_log = await cg.get_variable(config[CONF_DECISION_LOG])
    cg.add(var.set_decision_log(decision_log))
    web_auth = await cg.get_variable(config[CONF_WEB_AUTH])
    cg.add(var.set_web_auth(web_auth))

    ota_handoff = cg.new_Pvariable(config[CONF_OTA_HANDOFF_ID])
    await cg.register_component(ota_handoff, config)
    cg.add(ota_handoff.set_incident_manager(var))
    cg.add(ota_handoff.set_minimum_off_ms(config[CONF_MINIMUM_OFF_TIME]))
