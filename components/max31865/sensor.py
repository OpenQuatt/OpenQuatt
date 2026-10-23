# ESPHome 2026.9.0, modified 2026-10-20: RTD settling and optional diagnostics. See LICENSE.
import esphome.codegen as cg
from esphome.components import sensor, spi
import esphome.config_validation as cv
from esphome.const import (
    CONF_MAINS_FILTER,
    CONF_REFERENCE_RESISTANCE,
    CONF_RTD_NOMINAL_RESISTANCE,
    CONF_RTD_WIRES,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)
from esphome.types import ConfigType

CODEOWNERS = ["@DAVe3283"]
DEPENDENCIES = ["spi"]

max31865_ns = cg.esphome_ns.namespace("max31865")
MAX31865Sensor = max31865_ns.class_(
    "MAX31865Sensor", sensor.Sensor, cg.PollingComponent, spi.SPIDevice
)

MAX31865ConfigFilter = max31865_ns.enum("MAX31865ConfigFilter")
FILTER = {
    "50HZ": MAX31865ConfigFilter.FILTER_50HZ,
    "60HZ": MAX31865ConfigFilter.FILTER_60HZ,
}

def validate_probe_sequence(config):
    if config["manual_fault_settle"].total_milliseconds and (
        not config["separate_fault_clear"] or not config["bias_settle"].total_milliseconds
    ):
        raise cv.Invalid("Manual fault timing requires separate_fault_clear and bias_settle > 0ms")
    return config


def probe_delay(value):
    return cv.All(cv.positive_time_period_milliseconds, cv.Range(max=cv.TimePeriod(milliseconds=100)))(value)


CONFIG_SCHEMA = (
    sensor.sensor_schema(
        MAX31865Sensor,
        unit_of_measurement=UNIT_CELSIUS,
        accuracy_decimals=2,
        device_class=DEVICE_CLASS_TEMPERATURE,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    .extend(
        {
            cv.Required(CONF_REFERENCE_RESISTANCE): cv.All(
                cv.resistance, cv.Range(min=100, max=10000)
            ),
            cv.Required(CONF_RTD_NOMINAL_RESISTANCE): cv.All(
                cv.resistance, cv.Range(min=100, max=1000)
            ),
            cv.Optional(CONF_MAINS_FILTER, default="60HZ"): cv.enum(
                FILTER, upper=True, space=""
            ),
            cv.Optional(CONF_RTD_WIRES, default=4): cv.int_range(min=2, max=4),
            cv.Optional("separate_fault_clear", default=False): cv.boolean,
            cv.Optional("bias_settle", default="0ms"): probe_delay,
            cv.Optional("manual_fault_settle", default="0ms"): probe_delay,
            cv.Optional("conversion_settle", default="0ms"): probe_delay,
            cv.Optional("conversion_read_margin", default="0ms"): probe_delay,
            cv.Optional("diagnostics", default=False): cv.boolean,
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(spi.spi_device_schema())
)
CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_probe_sequence)


async def to_code(config: ConfigType) -> None:
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)
    cg.add(var.set_reference_resistance(config[CONF_REFERENCE_RESISTANCE]))
    cg.add(var.set_nominal_resistance(config[CONF_RTD_NOMINAL_RESISTANCE]))
    cg.add(var.set_filter(config[CONF_MAINS_FILTER]))
    cg.add(var.set_num_rtd_wires(config[CONF_RTD_WIRES]))
    cg.add(var.set_probe_sequence(
        config["separate_fault_clear"],
        config["bias_settle"].total_milliseconds,
        config["manual_fault_settle"].total_milliseconds,
        config["conversion_settle"].total_milliseconds,
    ))
    cg.add(var.set_conversion_read_margin(config["conversion_read_margin"].total_milliseconds))
    cg.add(var.set_diagnostics(config["diagnostics"]))
