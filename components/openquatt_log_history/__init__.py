import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import time
from esphome.const import CONF_ID

AUTO_LOAD = ["logger", "time", "web_server_base"]

CONF_CLOCK = "clock"

openquatt_log_history_ns = cg.esphome_ns.namespace("openquatt_log_history")
OpenQuattLogHistory = openquatt_log_history_ns.class_("OpenQuattLogHistory", cg.Component)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattLogHistory),
        cv.Required(CONF_CLOCK): cv.use_id(time.RealTimeClock),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_global(openquatt_log_history_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    clock = await cg.get_variable(config[CONF_CLOCK])
    cg.add(var.set_clock(clock))
