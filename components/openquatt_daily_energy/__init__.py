import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, time
from esphome.const import CONF_ID

from esphome.components.openquatt_energy_history import OpenQuattEnergyHistory

DEPENDENCIES = ["openquatt_energy_history", "sensor", "time"]

CONF_ARCHIVE = "archive"
CONF_CLOCK = "clock"
CONF_POWER = "power"
CONF_TOTAL = "total"
CHANNELS = (
    "electrical_input",
    "heating_input",
    "cooling_input",
    "heatpump_heat",
    "heatpump_cooling",
    "boiler_heat",
    "system_heat",
)

namespace = cg.esphome_ns.namespace("openquatt_daily_energy")
OpenQuattDailyEnergy = namespace.class_("OpenQuattDailyEnergy", cg.Component)
channel_schema = cv.Schema(
    {
        cv.Required(CONF_POWER): cv.use_id(sensor.Sensor),
        cv.Required(CONF_TOTAL): cv.use_id(sensor.Sensor),
    }
)
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattDailyEnergy),
        cv.Required(CONF_CLOCK): cv.use_id(time.RealTimeClock),
        cv.Required(CONF_ARCHIVE): cv.use_id(OpenQuattEnergyHistory),
        **{cv.Required(channel): channel_schema for channel in CHANNELS},
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_clock(await cg.get_variable(config[CONF_CLOCK])))
    cg.add(var.set_archive(await cg.get_variable(config[CONF_ARCHIVE])))
    for index, name in enumerate(CHANNELS):
        channel = config[name]
        cg.add(
            var.set_channel(
                index,
                await cg.get_variable(channel[CONF_POWER]),
                await cg.get_variable(channel[CONF_TOTAL]),
            )
        )
