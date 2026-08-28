import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select, text_sensor
from esphome.const import CONF_ID, ENTITY_CATEGORY_CONFIG


DEPENDENCIES = ["ethernet", "wifi"]
AUTO_LOAD = ["select", "text_sensor"]
MULTI_CONF = False

CONF_ACTIVE_CONNECTION = "active_connection"
CONF_PREFERRED_CONNECTION = "preferred_connection"
CONF_DETECTION_TIMEOUT = "detection_timeout"
CONF_LOSS_TIMEOUT = "loss_timeout"
CONF_STABLE_TIME = "stable_time"
CONF_SWITCH_TIMEOUT = "switch_timeout"

openquatt_network_ns = cg.esphome_ns.namespace("openquatt_network")
OpenQuattNetworkManager = openquatt_network_ns.class_(
    "OpenQuattNetworkManager", cg.Component
)
OpenQuattConnectionSelect = openquatt_network_ns.class_(
    "OpenQuattConnectionSelect", select.Select
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattNetworkManager),
        cv.Required(CONF_ACTIVE_CONNECTION): cv.use_id(text_sensor.TextSensor),
        cv.Required(CONF_PREFERRED_CONNECTION): select.select_schema(
            OpenQuattConnectionSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:connection",
        ),
        cv.Optional(CONF_DETECTION_TIMEOUT, default="10s"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_LOSS_TIMEOUT, default="30s"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_STABLE_TIME, default="2s"):
            cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SWITCH_TIMEOUT, default="30s"):
            cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_define("USE_OPENQUATT_NETWORK")
    cg.add_global(openquatt_network_ns.using)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    active_connection = await cg.get_variable(config[CONF_ACTIVE_CONNECTION])
    cg.add(var.set_active_connection_sensor(active_connection))

    preferred_connection = await select.new_select(
        config[CONF_PREFERRED_CONNECTION],
        options=["WiFi", "Ethernet"],
    )
    cg.add(preferred_connection.set_parent(var))
    cg.add(var.set_preferred_connection_select(preferred_connection))

    cg.add(var.set_detection_timeout(config[CONF_DETECTION_TIMEOUT]))
    cg.add(var.set_loss_timeout(config[CONF_LOSS_TIMEOUT]))
    cg.add(var.set_stable_time(config[CONF_STABLE_TIME]))
    cg.add(var.set_switch_timeout(config[CONF_SWITCH_TIMEOUT]))
