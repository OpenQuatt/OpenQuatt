import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE
from esphome.components import binary_sensor, psram, sensor, socket
from esphome.components.esp32 import (
    add_idf_component,
    get_esp32_variant,
    idf_version,
    include_builtin_idf_component,
)
from esphome.components.esp32.const import VARIANT_ESP32S3
from esphome.const import CONF_ID

CONF_BOOTSTRAP_BROKER = "bootstrap_broker"
CONF_BOOTSTRAP_PORT = "bootstrap_port"
CONF_BOOTSTRAP_USERNAME = "bootstrap_username"
CONF_BOOTSTRAP_PASSWORD = "bootstrap_password"
CONF_DEW_POINT_TOPIC = "dew_point_topic"
CONF_DEW_POINT_STALE = "dew_point_stale"
CONF_DEW_POINT_SENSOR = "dew_point_sensor"
CONF_DEW_POINT_AGE_SENSOR = "dew_point_age_sensor"
CONF_DEW_POINT_VALID_BINARY_SENSOR = "dew_point_valid_binary_sensor"
CONF_OUTSIDE_TEMPERATURE_TOPIC = "outside_temperature_topic"
CONF_OUTSIDE_TEMPERATURE_STALE = "outside_temperature_stale"
CONF_OUTSIDE_TEMPERATURE_SENSOR = "outside_temperature_sensor"
CONF_OUTSIDE_TEMPERATURE_AGE_SENSOR = "outside_temperature_age_sensor"
CONF_OUTSIDE_TEMPERATURE_VALID_BINARY_SENSOR = "outside_temperature_valid_binary_sensor"
CONF_ROOM_TEMPERATURE_TOPIC = "room_temperature_topic"
CONF_ROOM_TEMPERATURE_STALE = "room_temperature_stale"
CONF_ROOM_TEMPERATURE_SENSOR = "room_temperature_sensor"
CONF_ROOM_TEMPERATURE_AGE_SENSOR = "room_temperature_age_sensor"
CONF_ROOM_TEMPERATURE_VALID_BINARY_SENSOR = "room_temperature_valid_binary_sensor"
CONF_ROOM_SETPOINT_TOPIC = "room_setpoint_topic"
CONF_ROOM_SETPOINT_STALE = "room_setpoint_stale"
CONF_ROOM_SETPOINT_SENSOR = "room_setpoint_sensor"
CONF_ROOM_SETPOINT_AGE_SENSOR = "room_setpoint_age_sensor"
CONF_ROOM_SETPOINT_VALID_BINARY_SENSOR = "room_setpoint_valid_binary_sensor"
CONF_HEATING_SUPPLY_TARGET_TOPIC = "heating_supply_target_topic"
CONF_HEATING_SUPPLY_TARGET_STALE = "heating_supply_target_stale"
CONF_HEATING_SUPPLY_TARGET_SENSOR = "heating_supply_target_sensor"
CONF_HEATING_SUPPLY_TARGET_AGE_SENSOR = "heating_supply_target_age_sensor"
CONF_HEATING_SUPPLY_TARGET_VALID_BINARY_SENSOR = "heating_supply_target_valid_binary_sensor"
CONF_HEATING_ENABLE_TOPIC = "heating_enable_topic"
CONF_HEATING_ENABLE_STALE = "heating_enable_stale"
CONF_HEATING_ENABLE_BINARY_SENSOR = "heating_enable_binary_sensor"
CONF_HEATING_ENABLE_AGE_SENSOR = "heating_enable_age_sensor"
CONF_HEATING_ENABLE_VALID_BINARY_SENSOR = "heating_enable_valid_binary_sensor"
CONF_COOLING_ENABLE_TOPIC = "cooling_enable_topic"
CONF_COOLING_ENABLE_STALE = "cooling_enable_stale"
CONF_COOLING_ENABLE_BINARY_SENSOR = "cooling_enable_binary_sensor"
CONF_COOLING_ENABLE_AGE_SENSOR = "cooling_enable_age_sensor"
CONF_COOLING_ENABLE_VALID_BINARY_SENSOR = "cooling_enable_valid_binary_sensor"
CONF_DEFAULT_ENABLED = "default_enabled"

openquatt_mqtt_config_ns = cg.esphome_ns.namespace("openquatt_mqtt_config")
OpenQuattMqttConfig = openquatt_mqtt_config_ns.class_("OpenQuattMqttConfig", cg.Component)

AUTO_LOAD = ["web_server_base"]
DEPENDENCIES = ["psram"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattMqttConfig),
        cv.Optional(CONF_BOOTSTRAP_BROKER, default=""): cv.All(cv.string_strict, cv.Length(max=64)),
        cv.Optional(CONF_BOOTSTRAP_PORT, default=1883): cv.port,
        cv.Optional(CONF_BOOTSTRAP_USERNAME, default=""): cv.All(cv.string_strict, cv.Length(max=64)),
        cv.Optional(CONF_BOOTSTRAP_PASSWORD, default=""): cv.sensitive(
            cv.All(cv.string_strict, cv.Length(max=128))
        ),
        cv.Required(CONF_DEW_POINT_TOPIC): cv.All(cv.subscribe_topic, cv.Length(max=96)),
        cv.Required(CONF_DEW_POINT_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_DEW_POINT_AGE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_DEW_POINT_VALID_BINARY_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_DEW_POINT_STALE, default="900s"): cv.positive_time_period_milliseconds,
        cv.Required(CONF_OUTSIDE_TEMPERATURE_TOPIC): cv.All(cv.subscribe_topic, cv.Length(max=96)),
        cv.Required(CONF_OUTSIDE_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_OUTSIDE_TEMPERATURE_AGE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_OUTSIDE_TEMPERATURE_VALID_BINARY_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_OUTSIDE_TEMPERATURE_STALE, default="1800s"): cv.positive_time_period_milliseconds,
        cv.Required(CONF_ROOM_TEMPERATURE_TOPIC): cv.All(cv.subscribe_topic, cv.Length(max=96)),
        cv.Required(CONF_ROOM_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_ROOM_TEMPERATURE_AGE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_ROOM_TEMPERATURE_VALID_BINARY_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_ROOM_TEMPERATURE_STALE, default="600s"): cv.positive_time_period_milliseconds,
        cv.Required(CONF_ROOM_SETPOINT_TOPIC): cv.All(cv.subscribe_topic, cv.Length(max=96)),
        cv.Required(CONF_ROOM_SETPOINT_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_ROOM_SETPOINT_AGE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_ROOM_SETPOINT_VALID_BINARY_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_ROOM_SETPOINT_STALE, default="0s"): cv.positive_time_period_milliseconds,
        cv.Required(CONF_HEATING_SUPPLY_TARGET_TOPIC): cv.All(cv.subscribe_topic, cv.Length(max=96)),
        cv.Required(CONF_HEATING_SUPPLY_TARGET_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_HEATING_SUPPLY_TARGET_AGE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_HEATING_SUPPLY_TARGET_VALID_BINARY_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_HEATING_SUPPLY_TARGET_STALE, default="900s"): cv.positive_time_period_milliseconds,
        cv.Required(CONF_HEATING_ENABLE_TOPIC): cv.All(cv.subscribe_topic, cv.Length(max=96)),
        cv.Required(CONF_HEATING_ENABLE_BINARY_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_HEATING_ENABLE_AGE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_HEATING_ENABLE_VALID_BINARY_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_HEATING_ENABLE_STALE, default="0s"): cv.positive_time_period_milliseconds,
        cv.Required(CONF_COOLING_ENABLE_TOPIC): cv.All(cv.subscribe_topic, cv.Length(max=96)),
        cv.Required(CONF_COOLING_ENABLE_BINARY_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_COOLING_ENABLE_AGE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_COOLING_ENABLE_VALID_BINARY_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_COOLING_ENABLE_STALE, default="0s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_DEFAULT_ENABLED, default=False): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)
CONFIG_SCHEMA = cv.All(
    CONFIG_SCHEMA,
    socket.consume_sockets(1, "openquatt_mqtt_config"),
)


async def to_code(config):
    if CORE.is_esp32:
        if get_esp32_variant() == VARIANT_ESP32S3:
            psram.request_external_task_stack()
        if idf_version() >= cv.Version(6, 0, 0):
            add_idf_component(name="espressif/mqtt", ref="1.0.0")
        else:
            include_builtin_idf_component("mqtt")
        include_builtin_idf_component("json")

    cg.add_global(openquatt_mqtt_config_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_bootstrap_broker(config[CONF_BOOTSTRAP_BROKER]))
    cg.add(var.set_bootstrap_port(config[CONF_BOOTSTRAP_PORT]))
    cg.add(var.set_bootstrap_username(config[CONF_BOOTSTRAP_USERNAME]))
    cg.add(var.set_bootstrap_password(config[CONF_BOOTSTRAP_PASSWORD]))
    cg.add(var.set_dew_point_topic(config[CONF_DEW_POINT_TOPIC]))
    cg.add(var.set_dew_point_stale_ms(config[CONF_DEW_POINT_STALE].total_milliseconds))
    dew_point_sensor = await cg.get_variable(config[CONF_DEW_POINT_SENSOR])
    cg.add(var.set_dew_point_sensor(dew_point_sensor))
    dew_point_age_sensor = await cg.get_variable(config[CONF_DEW_POINT_AGE_SENSOR])
    cg.add(var.set_dew_point_age_sensor(dew_point_age_sensor))
    dew_point_valid_binary_sensor = await cg.get_variable(config[CONF_DEW_POINT_VALID_BINARY_SENSOR])
    cg.add(var.set_dew_point_valid_binary_sensor(dew_point_valid_binary_sensor))
    cg.add(var.set_outside_temperature_topic(config[CONF_OUTSIDE_TEMPERATURE_TOPIC]))
    cg.add(var.set_outside_temperature_stale_ms(config[CONF_OUTSIDE_TEMPERATURE_STALE].total_milliseconds))
    outside_temperature_sensor = await cg.get_variable(config[CONF_OUTSIDE_TEMPERATURE_SENSOR])
    cg.add(var.set_outside_temperature_sensor(outside_temperature_sensor))
    outside_temperature_age_sensor = await cg.get_variable(config[CONF_OUTSIDE_TEMPERATURE_AGE_SENSOR])
    cg.add(var.set_outside_temperature_age_sensor(outside_temperature_age_sensor))
    outside_temperature_valid_binary_sensor = await cg.get_variable(
        config[CONF_OUTSIDE_TEMPERATURE_VALID_BINARY_SENSOR]
    )
    cg.add(var.set_outside_temperature_valid_binary_sensor(outside_temperature_valid_binary_sensor))
    cg.add(var.set_room_temperature_topic(config[CONF_ROOM_TEMPERATURE_TOPIC]))
    cg.add(var.set_room_temperature_stale_ms(config[CONF_ROOM_TEMPERATURE_STALE].total_milliseconds))
    room_temperature_sensor = await cg.get_variable(config[CONF_ROOM_TEMPERATURE_SENSOR])
    cg.add(var.set_room_temperature_sensor(room_temperature_sensor))
    room_temperature_age_sensor = await cg.get_variable(config[CONF_ROOM_TEMPERATURE_AGE_SENSOR])
    cg.add(var.set_room_temperature_age_sensor(room_temperature_age_sensor))
    room_temperature_valid_binary_sensor = await cg.get_variable(config[CONF_ROOM_TEMPERATURE_VALID_BINARY_SENSOR])
    cg.add(var.set_room_temperature_valid_binary_sensor(room_temperature_valid_binary_sensor))
    cg.add(var.set_room_setpoint_topic(config[CONF_ROOM_SETPOINT_TOPIC]))
    cg.add(var.set_room_setpoint_stale_ms(config[CONF_ROOM_SETPOINT_STALE].total_milliseconds))
    room_setpoint_sensor = await cg.get_variable(config[CONF_ROOM_SETPOINT_SENSOR])
    cg.add(var.set_room_setpoint_sensor(room_setpoint_sensor))
    room_setpoint_age_sensor = await cg.get_variable(config[CONF_ROOM_SETPOINT_AGE_SENSOR])
    cg.add(var.set_room_setpoint_age_sensor(room_setpoint_age_sensor))
    room_setpoint_valid_binary_sensor = await cg.get_variable(config[CONF_ROOM_SETPOINT_VALID_BINARY_SENSOR])
    cg.add(var.set_room_setpoint_valid_binary_sensor(room_setpoint_valid_binary_sensor))
    cg.add(var.set_heating_supply_target_topic(config[CONF_HEATING_SUPPLY_TARGET_TOPIC]))
    cg.add(var.set_heating_supply_target_stale_ms(config[CONF_HEATING_SUPPLY_TARGET_STALE].total_milliseconds))
    heating_supply_target_sensor = await cg.get_variable(config[CONF_HEATING_SUPPLY_TARGET_SENSOR])
    cg.add(var.set_heating_supply_target_sensor(heating_supply_target_sensor))
    heating_supply_target_age_sensor = await cg.get_variable(config[CONF_HEATING_SUPPLY_TARGET_AGE_SENSOR])
    cg.add(var.set_heating_supply_target_age_sensor(heating_supply_target_age_sensor))
    heating_supply_target_valid_binary_sensor = await cg.get_variable(
        config[CONF_HEATING_SUPPLY_TARGET_VALID_BINARY_SENSOR]
    )
    cg.add(var.set_heating_supply_target_valid_binary_sensor(heating_supply_target_valid_binary_sensor))
    cg.add(var.set_heating_enable_topic(config[CONF_HEATING_ENABLE_TOPIC]))
    cg.add(var.set_heating_enable_stale_ms(config[CONF_HEATING_ENABLE_STALE].total_milliseconds))
    heating_enable_binary_sensor = await cg.get_variable(config[CONF_HEATING_ENABLE_BINARY_SENSOR])
    cg.add(var.set_heating_enable_binary_sensor(heating_enable_binary_sensor))
    heating_enable_age_sensor = await cg.get_variable(config[CONF_HEATING_ENABLE_AGE_SENSOR])
    cg.add(var.set_heating_enable_age_sensor(heating_enable_age_sensor))
    heating_enable_valid_binary_sensor = await cg.get_variable(config[CONF_HEATING_ENABLE_VALID_BINARY_SENSOR])
    cg.add(var.set_heating_enable_valid_binary_sensor(heating_enable_valid_binary_sensor))
    cg.add(var.set_cooling_enable_topic(config[CONF_COOLING_ENABLE_TOPIC]))
    cg.add(var.set_cooling_enable_stale_ms(config[CONF_COOLING_ENABLE_STALE].total_milliseconds))
    cooling_enable_binary_sensor = await cg.get_variable(config[CONF_COOLING_ENABLE_BINARY_SENSOR])
    cg.add(var.set_cooling_enable_binary_sensor(cooling_enable_binary_sensor))
    cooling_enable_age_sensor = await cg.get_variable(config[CONF_COOLING_ENABLE_AGE_SENSOR])
    cg.add(var.set_cooling_enable_age_sensor(cooling_enable_age_sensor))
    cooling_enable_valid_binary_sensor = await cg.get_variable(config[CONF_COOLING_ENABLE_VALID_BINARY_SENSOR])
    cg.add(var.set_cooling_enable_valid_binary_sensor(cooling_enable_valid_binary_sensor))
    cg.add(var.set_default_enabled(config[CONF_DEFAULT_ENABLED]))
