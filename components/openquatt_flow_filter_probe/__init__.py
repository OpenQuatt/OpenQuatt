from esphome import pins
import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import CONF_PIN, STATE_CLASS_MEASUREMENT

CODEOWNERS = ["@jeroen85"]

CONF_EDGE_100 = "edge_100"
CONF_PULSE_13 = "pulse_13"
CONF_PULSE_20 = "pulse_20"
CONF_PULSE_50 = "pulse_50"
CONF_RAW_RISING_HZ = "raw_rising_hz"
CONF_RAW_RISING_COUNT = "raw_rising_count"
CONF_PULSE_WIDTH_MIN = "pulse_width_min"
CONF_PULSE_WIDTH_AVG = "pulse_width_avg"
CONF_PULSE_WIDTH_MAX = "pulse_width_max"
CONF_PULSE_WIDTH_LT20 = "pulse_width_lt20"
CONF_PULSE_WIDTH_20_50 = "pulse_width_20_50"
CONF_PULSE_WIDTH_50_100 = "pulse_width_50_100"
CONF_PULSE_WIDTH_GE100 = "pulse_width_ge100"
CONF_KF_LPM_PER_HZ = "kf_lpm_per_hz"
CONF_Q0_LPM = "q0_lpm"

openquatt_flow_filter_probe_ns = cg.esphome_ns.namespace("openquatt_flow_filter_probe")
OpenQuattFlowFilterProbe = openquatt_flow_filter_probe_ns.class_(
    "OpenQuattFlowFilterProbe", sensor.Sensor, cg.Component
)

FLOW_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement="L/h",
    accuracy_decimals=1,
    state_class=STATE_CLASS_MEASUREMENT,
)

CONFIG_SCHEMA = sensor.sensor_schema(
    OpenQuattFlowFilterProbe,
    unit_of_measurement="L/h",
    accuracy_decimals=1,
    state_class=STATE_CLASS_MEASUREMENT,
).extend(
    {
        cv.Required(CONF_PIN): pins.internal_gpio_input_pin_schema,
        cv.Required(CONF_EDGE_100): FLOW_SENSOR_SCHEMA,
        cv.Required(CONF_PULSE_13): FLOW_SENSOR_SCHEMA,
        cv.Required(CONF_PULSE_20): FLOW_SENSOR_SCHEMA,
        cv.Required(CONF_PULSE_50): FLOW_SENSOR_SCHEMA,
        cv.Required(CONF_RAW_RISING_HZ): sensor.sensor_schema(
            unit_of_measurement="Hz",
            accuracy_decimals=2,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_RAW_RISING_COUNT): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_PULSE_WIDTH_MIN): sensor.sensor_schema(
            unit_of_measurement="us",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_PULSE_WIDTH_AVG): sensor.sensor_schema(
            unit_of_measurement="us",
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_PULSE_WIDTH_MAX): sensor.sensor_schema(
            unit_of_measurement="us",
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_PULSE_WIDTH_LT20): sensor.sensor_schema(
            unit_of_measurement="%",
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_PULSE_WIDTH_20_50): sensor.sensor_schema(
            unit_of_measurement="%",
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_PULSE_WIDTH_50_100): sensor.sensor_schema(
            unit_of_measurement="%",
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_PULSE_WIDTH_GE100): sensor.sensor_schema(
            unit_of_measurement="%",
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

    sensor_setters = [
        (CONF_EDGE_100, var.set_edge_100_sensor),
        (CONF_PULSE_13, var.set_pulse_13_sensor),
        (CONF_PULSE_20, var.set_pulse_20_sensor),
        (CONF_PULSE_50, var.set_pulse_50_sensor),
        (CONF_RAW_RISING_HZ, var.set_raw_rising_hz_sensor),
        (CONF_RAW_RISING_COUNT, var.set_raw_rising_count_sensor),
        (CONF_PULSE_WIDTH_MIN, var.set_pulse_width_min_sensor),
        (CONF_PULSE_WIDTH_AVG, var.set_pulse_width_avg_sensor),
        (CONF_PULSE_WIDTH_MAX, var.set_pulse_width_max_sensor),
        (CONF_PULSE_WIDTH_LT20, var.set_pulse_width_lt20_sensor),
        (CONF_PULSE_WIDTH_20_50, var.set_pulse_width_20_50_sensor),
        (CONF_PULSE_WIDTH_50_100, var.set_pulse_width_50_100_sensor),
        (CONF_PULSE_WIDTH_GE100, var.set_pulse_width_ge100_sensor),
    ]
    for key, setter in sensor_setters:
        child = await sensor.new_sensor(config[key])
        cg.add(setter(child))

    cg.add(var.set_kf_lpm_per_hz(config[CONF_KF_LPM_PER_HZ]))
    cg.add(var.set_q0_lpm(config[CONF_Q0_LPM]))
