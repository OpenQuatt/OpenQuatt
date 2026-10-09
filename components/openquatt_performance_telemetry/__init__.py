import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, select, sensor, switch, time
from esphome.const import ENTITY_CATEGORY_CONFIG

from esphome.components import openquatt_usage_telemetry

DEPENDENCIES = ["psram", "openquatt_usage_telemetry"]


CONF_TRANSPORT = "transport"
CONF_CLOCK = "clock"
CONF_SETUP_COMPLETE_SENSOR = "setup_complete_sensor"
CONF_CHOICE_CONFIGURED = "choice_configured"
CONF_PROMPT_HANDLED = "prompt_handled"
CONF_GENERATION_SELECT = "generation_select"
CONF_OUTSIDE_TEMP_SENSOR = "outside_temp_sensor"
CONF_FLOW_SENSOR = "flow_sensor"
CONF_FIRMWARE_VERSION = "firmware_version"
CONF_TOPOLOGY = "topology"


openquatt_performance_telemetry_ns = cg.esphome_ns.namespace("openquatt_performance_telemetry")
OpenQuattPerformanceTelemetry = openquatt_performance_telemetry_ns.class_(
    "OpenQuattPerformanceTelemetry", switch.Switch, cg.Component
)


def hp_schema(prefix):
    return {
        cv.Required(f"{prefix}_working_mode"): cv.use_id(sensor.Sensor),
        cv.Required(f"{prefix}_frequency"): cv.use_id(sensor.Sensor),
        cv.Required(f"{prefix}_level"): cv.use_id(sensor.Sensor),
        cv.Required(f"{prefix}_input_power"): cv.use_id(sensor.Sensor),
        cv.Required(f"{prefix}_heat_power"): cv.use_id(sensor.Sensor),
        cv.Required(f"{prefix}_pump_power"): cv.use_id(sensor.Sensor),
        cv.Required(f"{prefix}_water_in"): cv.use_id(sensor.Sensor),
        cv.Required(f"{prefix}_water_out"): cv.use_id(sensor.Sensor),
        cv.Required(f"{prefix}_defrost"): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(f"{prefix}_oil_return"): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(f"{prefix}_four_way_valve"): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(f"{prefix}_bottom_plate_heater"): cv.use_id(binary_sensor.BinarySensor),
    }


CONFIG_SCHEMA = switch.switch_schema(
    OpenQuattPerformanceTelemetry,
    icon="mdi:chart-bell-curve-cumulative",
    entity_category=ENTITY_CATEGORY_CONFIG,
    default_restore_mode="DISABLED",
).extend(
    {
        cv.Required(CONF_TRANSPORT): cv.use_id(openquatt_usage_telemetry.OpenQuattUsageTelemetry),
        cv.Required(CONF_CLOCK): cv.use_id(time.RealTimeClock),
        cv.Required(CONF_SETUP_COMPLETE_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_CHOICE_CONFIGURED): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_PROMPT_HANDLED): binary_sensor.binary_sensor_schema(),
        cv.Required(CONF_GENERATION_SELECT): cv.use_id(select.Select),
        cv.Required(CONF_OUTSIDE_TEMP_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_FLOW_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_FIRMWARE_VERSION): cv.All(cv.string_strict, cv.Length(max=32)),
        cv.Required(CONF_TOPOLOGY): cv.All(cv.string_strict, cv.Length(max=16)),
        **hp_schema("hp1"),
        **hp_schema("hp2"),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_global(openquatt_performance_telemetry_ns.using)
    var = await switch.new_switch(config)
    await cg.register_component(var, config)

    setters = {
        CONF_TRANSPORT: "set_transport",
        CONF_CLOCK: "set_clock",
        CONF_SETUP_COMPLETE_SENSOR: "set_setup_complete_sensor",
        CONF_GENERATION_SELECT: "set_generation_select",
        CONF_OUTSIDE_TEMP_SENSOR: "set_outside_temp_sensor",
        CONF_FLOW_SENSOR: "set_flow_sensor",
    }
    for key, setter in setters.items():
        value = await cg.get_variable(config[key])
        cg.add(getattr(var, setter)(value))

    choice_configured = await binary_sensor.new_binary_sensor(config[CONF_CHOICE_CONFIGURED])
    cg.add(var.set_choice_configured_sensor(choice_configured))
    if CONF_PROMPT_HANDLED in config:
        prompt_handled = await binary_sensor.new_binary_sensor(config[CONF_PROMPT_HANDLED])
        cg.add(var.set_prompt_handled_sensor(prompt_handled))
    cg.add(var.set_firmware_version(config[CONF_FIRMWARE_VERSION]))
    cg.add(var.set_topology(config[CONF_TOPOLOGY]))

    for hp in ("hp1", "hp2"):
        for field in (
            "working_mode",
            "frequency",
            "level",
            "input_power",
            "heat_power",
            "pump_power",
            "water_in",
            "water_out",
            "defrost",
            "oil_return",
            "four_way_valve",
            "bottom_plate_heater",
        ):
            value = await cg.get_variable(config[f"{hp}_{field}"])
            cg.add(getattr(var, f"set_{hp}_{field}")(value))
