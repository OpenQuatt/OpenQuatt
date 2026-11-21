import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select, sensor, switch, time
from esphome.const import CONF_ID

AUTO_LOAD = ["select", "sensor", "switch", "time", "web_server_base"]

CONF_ENABLED_SWITCH = "enabled_switch"
CONF_CLOCK = "clock"
CONF_HOURLY_RETENTION_SELECT = "hourly_retention_select"
CONF_DAILY_COUNTERS = "daily_counters"
DAILY_COUNTER_CHANNELS = (
    "electrical_input",
    "heating_input",
    "cooling_input",
    "heatpump_heat_output",
    "heatpump_cooling_output",
    "boiler_heat_output",
    "system_heat_output",
)

openquatt_energy_history_ns = cg.esphome_ns.namespace("openquatt_energy_history")
OpenQuattEnergyHistory = openquatt_energy_history_ns.class_(
    "OpenQuattEnergyHistory", cg.Component
)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattEnergyHistory),
        cv.Required(CONF_ENABLED_SWITCH): cv.use_id(switch.Switch),
        cv.Required(CONF_CLOCK): cv.use_id(time.RealTimeClock),
        cv.Required(CONF_DAILY_COUNTERS): cv.Schema(
            {cv.Required(channel): cv.use_id(sensor.Sensor) for channel in DAILY_COUNTER_CHANNELS}
        ),
        cv.Optional(CONF_HOURLY_RETENTION_SELECT): cv.use_id(select.Select),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_global(openquatt_energy_history_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    enabled_switch = await cg.get_variable(config[CONF_ENABLED_SWITCH])
    cg.add(var.set_enabled_switch(enabled_switch))

    clock = await cg.get_variable(config[CONF_CLOCK])
    cg.add(var.set_clock(clock))

    counters = config[CONF_DAILY_COUNTERS]
    daily_counters = [await cg.get_variable(counters[channel]) for channel in DAILY_COUNTER_CHANNELS]
    cg.add(var.set_daily_counters(cg.ArrayInitializer(*daily_counters)))

    if CONF_HOURLY_RETENTION_SELECT in config:
        hourly_retention_select = await cg.get_variable(config[CONF_HOURLY_RETENTION_SELECT])
        cg.add(var.set_hourly_retention_select(hourly_retention_select))
