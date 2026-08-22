import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, socket, switch, text
from esphome.const import CONF_ID

AUTO_LOAD = ["sensor", "binary_sensor", "switch", "text"]

openquatt_cic_ns = cg.esphome_ns.namespace("openquatt_cic")
OpenQuattCIC = openquatt_cic_ns.class_("OpenQuattCIC", cg.PollingComponent)

CONF_ENABLED_SWITCH = "enabled_switch"
CONF_URL_TEXT = "url_text"
CONF_BACKOFF_START_MS = "backoff_start_ms"
CONF_BACKOFF_MAX_MS = "backoff_max_ms"
CONF_MIN_URL_LEN = "min_url_len"
CONF_STALE_AFTER_MS = "stale_after_ms"
CONF_FEED_ERROR_TRIP_N = "feed_error_trip_n"
CONF_TIMEOUT_MS = "timeout_ms"
CONF_RESPONSE_BUFFER_SIZE = "response_buffer_size"
CONF_DIAGNOSTIC_PUBLISH_INTERVAL = "diagnostic_publish_interval"
CONF_WATER_SUPPLY_TEMP = "water_supply_temp"
CONF_FLOW_RATE = "flow_rate"
CONF_CIC_BOILER_WATER_PRESSURE = "cic_boiler_water_pressure"
CONF_CIC_CONTROL_SETPOINT = "cic_control_setpoint"
CONF_CIC_ROOM_SETPOINT = "cic_room_setpoint"
CONF_CIC_ROOM_TEMP = "cic_room_temp"
CONF_CIC_CH_ENABLED = "cic_ch_enabled"
CONF_CIC_CH_ENABLE_VALID = "cic_ch_enable_valid"
CONF_CIC_COOLING_ENABLED = "cic_cooling_enabled"
CONF_FEED_OK = "feed_ok"
CONF_DATA_STALE = "data_stale"
CONF_PAUSE_SENSOR = "pause_sensor"
CONF_BACKOFF_SENSOR = "backoff_sensor"
CONF_LAST_SUCCESS_AGE_SENSOR = "last_success_age_sensor"
CONF_REQUEST_COUNT_SENSOR = "request_count_sensor"
CONF_SUCCESS_COUNT_SENSOR = "success_count_sensor"
CONF_FAILURE_COUNT_SENSOR = "failure_count_sensor"
CONF_LAST_DURATION_SENSOR = "last_duration_sensor"
CONF_MAX_DURATION_SENSOR = "max_duration_sensor"
CONF_LAST_STATUS_CODE_SENSOR = "last_status_code_sensor"


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattCIC),
        cv.Required(CONF_ENABLED_SWITCH): cv.use_id(switch.Switch),
        cv.Required(CONF_URL_TEXT): cv.use_id(text.Text),
        cv.Optional(CONF_BACKOFF_START_MS, default=5000): cv.positive_int,
        cv.Optional(CONF_BACKOFF_MAX_MS, default=120000): cv.positive_int,
        cv.Optional(CONF_MIN_URL_LEN, default=10): cv.positive_int,
        cv.Optional(CONF_STALE_AFTER_MS, default=600000): cv.positive_int,
        cv.Optional(CONF_FEED_ERROR_TRIP_N, default=3): cv.positive_int,
        cv.Optional(CONF_TIMEOUT_MS, default=2000): cv.positive_int,
        cv.Optional(CONF_RESPONSE_BUFFER_SIZE, default=2048): cv.positive_int,
        cv.Optional(CONF_DIAGNOSTIC_PUBLISH_INTERVAL, default="60s"): cv.positive_time_period_milliseconds,
        cv.Required(CONF_WATER_SUPPLY_TEMP): cv.use_id(sensor.Sensor),
        cv.Required(CONF_FLOW_RATE): cv.use_id(sensor.Sensor),
        cv.Required(CONF_CIC_BOILER_WATER_PRESSURE): cv.use_id(sensor.Sensor),
        cv.Required(CONF_CIC_CONTROL_SETPOINT): cv.use_id(sensor.Sensor),
        cv.Required(CONF_CIC_ROOM_SETPOINT): cv.use_id(sensor.Sensor),
        cv.Required(CONF_CIC_ROOM_TEMP): cv.use_id(sensor.Sensor),
        cv.Required(CONF_CIC_CH_ENABLED): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_CIC_CH_ENABLE_VALID): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_CIC_COOLING_ENABLED): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_FEED_OK): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_DATA_STALE): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_PAUSE_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_BACKOFF_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_LAST_SUCCESS_AGE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_REQUEST_COUNT_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_SUCCESS_COUNT_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_FAILURE_COUNT_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_LAST_DURATION_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_MAX_DURATION_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_LAST_STATUS_CODE_SENSOR): cv.use_id(sensor.Sensor),
    }
).extend(cv.polling_component_schema("5s"))
CONFIG_SCHEMA = cv.All(
    CONFIG_SCHEMA,
    socket.consume_sockets(1, "openquatt_cic"),
)


async def to_code(config):
    cg.add_global(openquatt_cic_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    enabled_switch = await cg.get_variable(config[CONF_ENABLED_SWITCH])
    cg.add(var.set_enabled_switch(enabled_switch))
    url_text = await cg.get_variable(config[CONF_URL_TEXT])
    cg.add(var.set_url_text(url_text))

    cg.add(var.set_backoff_start_ms(config[CONF_BACKOFF_START_MS]))
    cg.add(var.set_backoff_max_ms(config[CONF_BACKOFF_MAX_MS]))
    cg.add(var.set_min_url_len(config[CONF_MIN_URL_LEN]))
    cg.add(var.set_stale_after_ms(config[CONF_STALE_AFTER_MS]))
    cg.add(var.set_feed_error_trip_n(config[CONF_FEED_ERROR_TRIP_N]))
    cg.add(var.set_timeout_ms(config[CONF_TIMEOUT_MS]))
    cg.add(var.set_response_buffer_size(config[CONF_RESPONSE_BUFFER_SIZE]))
    cg.add(var.set_diagnostic_publish_interval_ms(config[CONF_DIAGNOSTIC_PUBLISH_INTERVAL].total_milliseconds))

    for key in [
        CONF_WATER_SUPPLY_TEMP,
        CONF_FLOW_RATE,
        CONF_CIC_BOILER_WATER_PRESSURE,
        CONF_CIC_CONTROL_SETPOINT,
        CONF_CIC_ROOM_SETPOINT,
        CONF_CIC_ROOM_TEMP,
        CONF_CIC_CH_ENABLED,
        CONF_CIC_CH_ENABLE_VALID,
        CONF_CIC_COOLING_ENABLED,
        CONF_FEED_OK,
        CONF_DATA_STALE,
        CONF_PAUSE_SENSOR,
        CONF_BACKOFF_SENSOR,
        CONF_LAST_SUCCESS_AGE_SENSOR,
    ]:
        entity = await cg.get_variable(config[key])
        cg.add(getattr(var, f"set_{key}")(entity))

    for key in [
        CONF_REQUEST_COUNT_SENSOR,
        CONF_SUCCESS_COUNT_SENSOR,
        CONF_FAILURE_COUNT_SENSOR,
        CONF_LAST_DURATION_SENSOR,
        CONF_MAX_DURATION_SENSOR,
        CONF_LAST_STATUS_CODE_SENSOR,
    ]:
        if key in config:
            entity = await cg.get_variable(config[key])
            cg.add(getattr(var, f"set_{key}")(entity))
