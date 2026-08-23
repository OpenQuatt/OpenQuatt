from esphome import pins
import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import CONF_PIN, STATE_CLASS_MEASUREMENT

CODEOWNERS = ["@jeroen85"]

CONF_EDGE_100 = "edge_100"
CONF_PULSE_20 = "pulse_20"
CONF_KF_LPM_PER_HZ = "kf_lpm_per_hz"
CONF_Q0_LPM = "q0_lpm"

openquatt_flow_filter_probe_ns = cg.esphome_ns.namespace("openquatt_flow_filter_probe")
OpenQuattFlowFilterProbe = openquatt_flow_filter_probe_ns.class_(
    "OpenQuattFlowFilterProbe", sensor.Sensor, cg.Component
)

CONFIG_SCHEMA = sensor.sensor_schema(
    OpenQuattFlowFilterProbe,
    unit_of_measurement="L/h",
    accuracy_decimals=1,
    state_class=STATE_CLASS_MEASUREMENT,
).extend(
    {
        cv.Required(CONF_PIN): pins.internal_gpio_input_pin_schema,
        cv.Required(CONF_EDGE_100): sensor.sensor_schema(
            unit_of_measurement="L/h",
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_PULSE_20): sensor.sensor_schema(
            unit_of_measurement="L/h",
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_KF_LPM_PER_HZ): cv.positive_float,
        cv.Optional(CONF_Q0_LPM, default=0.0): cv.float_,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))

    edge_100 = await sensor.new_sensor(config[CONF_EDGE_100])
    pulse_20 = await sensor.new_sensor(config[CONF_PULSE_20])
    cg.add(var.set_edge_100_sensor(edge_100))
    cg.add(var.set_pulse_20_sensor(pulse_20))
    cg.add(var.set_kf_lpm_per_hz(config[CONF_KF_LPM_PER_HZ]))
    cg.add(var.set_q0_lpm(config[CONF_Q0_LPM]))
