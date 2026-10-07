import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["number"]

CONF_WRITE_INTERVAL = "write_interval"
CONF_LIMIT_2H = "warning_limit_2h"
CONF_LIMIT_72H = "warning_limit_72h"

limits_ns = cg.esphome_ns.namespace("openquatt_compressor_limits")
CompressorLimits = limits_ns.class_("CompressorLimits", cg.Component)
WarningLimitNumber = limits_ns.class_("WarningLimitNumber", number.Number)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CompressorLimits),
        cv.Optional(CONF_WRITE_INTERVAL, default="60s"): cv.positive_time_period_milliseconds,
        cv.Required(CONF_LIMIT_2H): number.number_schema(WarningLimitNumber),
        cv.Required(CONF_LIMIT_72H): number.number_schema(WarningLimitNumber),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_write_interval(config[CONF_WRITE_INTERVAL].total_milliseconds))
    for index, key, maximum in (
        (0, CONF_LIMIT_2H, 20),
        (1, CONF_LIMIT_72H, 120),
    ):
        limit = await number.new_number(config[key], min_value=1, max_value=maximum, step=1)
        cg.add(var.set_limit(index, limit))
