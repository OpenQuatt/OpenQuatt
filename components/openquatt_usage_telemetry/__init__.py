import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    binary_sensor,
    openquatt_log_history,
    openquatt_mqtt_config,
    psram,
    select,
    sensor,
    socket,
    switch,
    text_sensor,
    time,
)
from esphome.components.esp32 import (
    add_idf_sdkconfig_option,
    add_idf_component,
    get_esp32_variant,
    idf_version,
    include_builtin_idf_component,
)
from esphome.components.esp32.const import VARIANT_ESP32S3
from esphome.const import ENTITY_CATEGORY_CONFIG
from esphome.core import CORE

DEPENDENCIES = ["psram", "openquatt_log_history"]


CONF_BROKER = "broker"
CONF_PORT = "port"
CONF_TLS = "tls"
CONF_USERNAME = "username"
CONF_PASSWORD = "password"
CONF_TOPIC = "topic"
CONF_CLOCK = "clock"
CONF_INSTALLATION_ID = "installation_id"
CONF_SETUP_COMPLETE_SENSOR = "setup_complete_sensor"
CONF_CHOICE_CONFIGURED = "choice_configured"
CONF_INTERVAL = "interval"
CONF_FIRMWARE_VERSION = "firmware_version"
CONF_RELEASE_CHANNEL = "release_channel"
CONF_HARDWARE_PROFILE = "hardware_profile"
CONF_TOPOLOGY = "topology"
CONF_CONNECTION = "connection"
CONF_QUATT_HYBRID_GENERATION_SELECT = "quatt_hybrid_generation_select"
CONF_FLOW_SOURCE_SELECT = "flow_source_select"
CONF_Q_FLOW_SOURCE_SELECT = "q_flow_source_select"
CONF_HEATING_STRATEGY_SELECT = "heating_strategy_select"
CONF_ROOM_TEMPERATURE_SOURCE_SELECT = "room_temperature_source_select"
CONF_ROOM_SETPOINT_SOURCE_SELECT = "room_setpoint_source_select"
CONF_OUTSIDE_TEMPERATURE_SOURCE_SELECT = "outside_temperature_source_select"
CONF_HEATING_ENABLE_SOURCE_SELECT = "heating_enable_source_select"
CONF_COOLING_ENABLE_SOURCE_SELECT = "cooling_enable_source_select"
CONF_COOLING_DEW_POINT_SOURCE_SELECT = "cooling_dew_point_source_select"
CONF_LOOP_TIME_SENSOR = "loop_time_sensor"
CONF_INTERNAL_TEMPERATURE_SENSOR = "internal_temperature_sensor"
CONF_WIFI_SIGNAL_SENSOR = "wifi_signal_sensor"
CONF_CIC_POLLING_SWITCH = "cic_polling_switch"
CONF_CIC_COMPATIBILITY_SWITCH = "cic_compatibility_switch"
CONF_OT_THERMOSTAT_SWITCH = "ot_thermostat_switch"
CONF_BOILER_ASSIST_SWITCH = "boiler_assist_switch"
CONF_BOILER_CONNECTION_SELECT = "boiler_connection_select"
CONF_MQTT_CONFIG = "mqtt_config"
CONF_TREND_RAM_SWITCH = "trend_ram_switch"
CONF_TREND_FLASH_SWITCH = "trend_flash_switch"
CONF_DECISION_LOG_FLASH_SWITCH = "decision_log_flash_switch"
CONF_ENERGY_HISTORY_FLASH_SWITCH = "energy_history_flash_switch"
CONF_RAM_LOG_HISTORY_SWITCH = "ram_log_history_switch"
CONF_CRASH_PROVIDER = "crash_provider"

openquatt_usage_telemetry_ns = cg.esphome_ns.namespace("openquatt_usage_telemetry")
OpenQuattUsageTelemetry = openquatt_usage_telemetry_ns.class_(
    "OpenQuattUsageTelemetry", switch.Switch, cg.Component
)


def validate_config(config):
    if config[CONF_PASSWORD] and not config[CONF_USERNAME]:
        raise cv.Invalid("username is required when password is configured")
    return config


CONFIG_SCHEMA = cv.All(
    switch.switch_schema(
        OpenQuattUsageTelemetry,
        icon="mdi:chart-box-outline",
        entity_category=ENTITY_CATEGORY_CONFIG,
        default_restore_mode="DISABLED",
    )
    .extend(
        {
            cv.Optional(CONF_BROKER, default=""): cv.All(cv.string_strict, cv.Length(max=128)),
            cv.Optional(CONF_PORT, default=8883): cv.port,
            cv.Optional(CONF_TLS, default=True): cv.boolean,
            cv.Optional(CONF_USERNAME, default=""): cv.All(cv.string_strict, cv.Length(max=96)),
            cv.Optional(CONF_PASSWORD, default=""): cv.sensitive(
                cv.All(cv.string_strict, cv.Length(max=192))
            ),
            cv.Required(CONF_TOPIC): cv.All(cv.publish_topic, cv.Length(max=128)),
            cv.Required(CONF_CLOCK): cv.use_id(time.RealTimeClock),
            cv.Required(CONF_INSTALLATION_ID): text_sensor.text_sensor_schema(),
            cv.Required(CONF_SETUP_COMPLETE_SENSOR): cv.use_id(binary_sensor.BinarySensor),
            cv.Required(CONF_CHOICE_CONFIGURED): binary_sensor.binary_sensor_schema(),
            cv.Optional(CONF_INTERVAL, default="1h"): cv.positive_time_period_milliseconds,
            cv.Required(CONF_FIRMWARE_VERSION): cv.All(cv.string_strict, cv.Length(max=32)),
            cv.Required(CONF_RELEASE_CHANNEL): cv.All(cv.string_strict, cv.Length(max=16)),
            cv.Required(CONF_HARDWARE_PROFILE): cv.All(cv.string_strict, cv.Length(max=32)),
            cv.Required(CONF_TOPOLOGY): cv.All(cv.string_strict, cv.Length(max=16)),
            cv.Required(CONF_CONNECTION): cv.All(cv.string_strict, cv.Length(max=16)),
            cv.Required(CONF_QUATT_HYBRID_GENERATION_SELECT): cv.use_id(select.Select),
            cv.Required(CONF_FLOW_SOURCE_SELECT): cv.use_id(select.Select),
            cv.Optional(CONF_Q_FLOW_SOURCE_SELECT): cv.use_id(select.Select),
            cv.Required(CONF_HEATING_STRATEGY_SELECT): cv.use_id(select.Select),
            cv.Required(CONF_ROOM_TEMPERATURE_SOURCE_SELECT): cv.use_id(select.Select),
            cv.Required(CONF_ROOM_SETPOINT_SOURCE_SELECT): cv.use_id(select.Select),
            cv.Required(CONF_OUTSIDE_TEMPERATURE_SOURCE_SELECT): cv.use_id(select.Select),
            cv.Required(CONF_HEATING_ENABLE_SOURCE_SELECT): cv.use_id(select.Select),
            cv.Required(CONF_COOLING_ENABLE_SOURCE_SELECT): cv.use_id(select.Select),
            cv.Required(CONF_COOLING_DEW_POINT_SOURCE_SELECT): cv.use_id(select.Select),
            cv.Required(CONF_LOOP_TIME_SENSOR): cv.use_id(sensor.Sensor),
            cv.Required(CONF_INTERNAL_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_WIFI_SIGNAL_SENSOR): cv.use_id(sensor.Sensor),
            cv.Required(CONF_CIC_POLLING_SWITCH): cv.use_id(switch.Switch),
            cv.Optional(CONF_CIC_COMPATIBILITY_SWITCH): cv.use_id(switch.Switch),
            cv.Optional(CONF_OT_THERMOSTAT_SWITCH): cv.use_id(switch.Switch),
            cv.Required(CONF_BOILER_ASSIST_SWITCH): cv.use_id(switch.Switch),
            cv.Optional(CONF_BOILER_CONNECTION_SELECT): cv.use_id(select.Select),
            cv.Required(CONF_MQTT_CONFIG): cv.use_id(openquatt_mqtt_config.OpenQuattMqttConfig),
            cv.Required(CONF_TREND_RAM_SWITCH): cv.use_id(switch.Switch),
            cv.Required(CONF_TREND_FLASH_SWITCH): cv.use_id(switch.Switch),
            cv.Required(CONF_DECISION_LOG_FLASH_SWITCH): cv.use_id(switch.Switch),
            cv.Required(CONF_ENERGY_HISTORY_FLASH_SWITCH): cv.use_id(switch.Switch),
            cv.Required(CONF_RAM_LOG_HISTORY_SWITCH): cv.use_id(switch.Switch),
            cv.Required(CONF_CRASH_PROVIDER): cv.use_id(
                openquatt_log_history.OpenQuattLogHistory
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    validate_config,
    socket.consume_sockets(1, "openquatt_usage_telemetry"),
)


async def to_code(config):
    if CORE.is_esp32:
        if get_esp32_variant() == VARIANT_ESP32S3:
            psram.request_external_task_stack()
        if idf_version() >= cv.Version(6, 0, 0):
            add_idf_component(name="espressif/mqtt", ref="1.0.0")
        else:
            include_builtin_idf_component("mqtt")
        add_idf_sdkconfig_option("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE", True)
        add_idf_sdkconfig_option("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL", True)

    cg.add_global(openquatt_usage_telemetry_ns.using)
    var = await switch.new_switch(config)
    await cg.register_component(var, config)

    cg.add(var.set_broker(config[CONF_BROKER]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_tls(config[CONF_TLS]))
    cg.add(var.set_username(config[CONF_USERNAME]))
    cg.add(var.set_password(config[CONF_PASSWORD]))
    cg.add(var.set_topic(config[CONF_TOPIC]))
    clock = await cg.get_variable(config[CONF_CLOCK])
    cg.add(var.set_clock(clock))
    installation_id_sensor = await text_sensor.new_text_sensor(config[CONF_INSTALLATION_ID])
    cg.add(var.set_installation_id_sensor(installation_id_sensor))
    setup_complete_sensor = await cg.get_variable(config[CONF_SETUP_COMPLETE_SENSOR])
    cg.add(var.set_setup_complete_sensor(setup_complete_sensor))
    choice_configured_sensor = await binary_sensor.new_binary_sensor(config[CONF_CHOICE_CONFIGURED])
    cg.add(var.set_choice_configured_sensor(choice_configured_sensor))
    cg.add(var.set_interval_ms(config[CONF_INTERVAL].total_milliseconds))
    cg.add(var.set_firmware_version(config[CONF_FIRMWARE_VERSION]))
    cg.add(var.set_release_channel(config[CONF_RELEASE_CHANNEL]))
    cg.add(var.set_hardware_profile(config[CONF_HARDWARE_PROFILE]))
    cg.add(var.set_topology(config[CONF_TOPOLOGY]))
    cg.add(var.set_connection(config[CONF_CONNECTION]))
    quatt_hybrid_generation_select = await cg.get_variable(
        config[CONF_QUATT_HYBRID_GENERATION_SELECT]
    )
    cg.add(var.set_quatt_hybrid_generation_select(quatt_hybrid_generation_select))
    flow_source_select = await cg.get_variable(config[CONF_FLOW_SOURCE_SELECT])
    cg.add(var.set_flow_source_select(flow_source_select))
    if q_flow_source_select_id := config.get(CONF_Q_FLOW_SOURCE_SELECT):
        q_flow_source_select = await cg.get_variable(q_flow_source_select_id)
        cg.add(var.set_q_flow_source_select(q_flow_source_select))
    heating_strategy_select = await cg.get_variable(config[CONF_HEATING_STRATEGY_SELECT])
    cg.add(var.set_heating_strategy_select(heating_strategy_select))
    room_temperature_source_select = await cg.get_variable(
        config[CONF_ROOM_TEMPERATURE_SOURCE_SELECT]
    )
    cg.add(var.set_room_temperature_source_select(room_temperature_source_select))
    room_setpoint_source_select = await cg.get_variable(
        config[CONF_ROOM_SETPOINT_SOURCE_SELECT]
    )
    cg.add(var.set_room_setpoint_source_select(room_setpoint_source_select))
    outside_temperature_source_select = await cg.get_variable(
        config[CONF_OUTSIDE_TEMPERATURE_SOURCE_SELECT]
    )
    cg.add(var.set_outside_temperature_source_select(outside_temperature_source_select))
    heating_enable_source_select = await cg.get_variable(
        config[CONF_HEATING_ENABLE_SOURCE_SELECT]
    )
    cg.add(var.set_heating_enable_source_select(heating_enable_source_select))
    cooling_enable_source_select = await cg.get_variable(
        config[CONF_COOLING_ENABLE_SOURCE_SELECT]
    )
    cg.add(var.set_cooling_enable_source_select(cooling_enable_source_select))
    cooling_dew_point_source_select = await cg.get_variable(
        config[CONF_COOLING_DEW_POINT_SOURCE_SELECT]
    )
    cg.add(var.set_cooling_dew_point_source_select(cooling_dew_point_source_select))
    loop_time_sensor = await cg.get_variable(config[CONF_LOOP_TIME_SENSOR])
    cg.add(var.set_loop_time_sensor(loop_time_sensor))
    internal_temperature_sensor = await cg.get_variable(config[CONF_INTERNAL_TEMPERATURE_SENSOR])
    cg.add(var.set_internal_temperature_sensor(internal_temperature_sensor))
    if wifi_signal_sensor_id := config.get(CONF_WIFI_SIGNAL_SENSOR):
        wifi_signal_sensor = await cg.get_variable(wifi_signal_sensor_id)
        cg.add(var.set_wifi_signal_sensor(wifi_signal_sensor))
    cic_polling_switch = await cg.get_variable(config[CONF_CIC_POLLING_SWITCH])
    cg.add(var.set_cic_polling_switch(cic_polling_switch))
    if cic_compatibility_switch_id := config.get(CONF_CIC_COMPATIBILITY_SWITCH):
        cic_compatibility_switch = await cg.get_variable(cic_compatibility_switch_id)
        cg.add(var.set_cic_compatibility_switch(cic_compatibility_switch))
    if ot_thermostat_switch_id := config.get(CONF_OT_THERMOSTAT_SWITCH):
        ot_thermostat_switch = await cg.get_variable(ot_thermostat_switch_id)
        cg.add(var.set_ot_thermostat_switch(ot_thermostat_switch))
    boiler_assist_switch = await cg.get_variable(config[CONF_BOILER_ASSIST_SWITCH])
    cg.add(var.set_boiler_assist_switch(boiler_assist_switch))
    if boiler_connection_select_id := config.get(CONF_BOILER_CONNECTION_SELECT):
        boiler_connection_select = await cg.get_variable(boiler_connection_select_id)
        cg.add(var.set_boiler_connection_select(boiler_connection_select))
    mqtt_config = await cg.get_variable(config[CONF_MQTT_CONFIG])
    cg.add(var.set_mqtt_config(mqtt_config))
    trend_ram_switch = await cg.get_variable(config[CONF_TREND_RAM_SWITCH])
    cg.add(var.set_trend_ram_switch(trend_ram_switch))
    trend_flash_switch = await cg.get_variable(config[CONF_TREND_FLASH_SWITCH])
    cg.add(var.set_trend_flash_switch(trend_flash_switch))
    decision_log_flash_switch = await cg.get_variable(config[CONF_DECISION_LOG_FLASH_SWITCH])
    cg.add(var.set_decision_log_flash_switch(decision_log_flash_switch))
    energy_history_flash_switch = await cg.get_variable(config[CONF_ENERGY_HISTORY_FLASH_SWITCH])
    cg.add(var.set_energy_history_flash_switch(energy_history_flash_switch))
    ram_log_history_switch = await cg.get_variable(config[CONF_RAM_LOG_HISTORY_SWITCH])
    cg.add(var.set_ram_log_history_switch(ram_log_history_switch))
    crash_provider = await cg.get_variable(config[CONF_CRASH_PROVIDER])
    cg.add(var.set_crash_provider(crash_provider))
