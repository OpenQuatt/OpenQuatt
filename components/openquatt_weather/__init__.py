import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, socket, switch, text, text_sensor, time
from esphome.components.esp32 import include_builtin_idf_component, require_certificate_bundle
from esphome.const import CONF_ID

AUTO_LOAD = ["json", "sensor", "text", "text_sensor", "binary_sensor"]
DEPENDENCIES = ["esp32", "psram", "time", "ota", "openquatt_web_auth", "web_server"]

ns = cg.esphome_ns.namespace("openquatt_weather")
OpenQuattWeather = ns.class_("OpenQuattWeather", cg.PollingComponent)
WeatherLocationText = ns.class_("WeatherLocationText", text.Text)
WebAuth = cg.esphome_ns.namespace("openquatt_web_auth").class_("OpenQuattWebAuth", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OpenQuattWeather),
            cv.Required("location"): text.text_schema(WeatherLocationText, mode="TEXT"),
            cv.Required("clock"): cv.use_id(time.RealTimeClock),
            cv.Required("enabled_switch"): cv.use_id(switch.Switch),
            cv.Required("pause_sensor"): cv.use_id(binary_sensor.BinarySensor),
            cv.Required("web_auth"): cv.use_id(WebAuth),
            cv.Optional("status"): text_sensor.text_sensor_schema(),
            cv.Optional("resolved_location"): text_sensor.text_sensor_schema(),
            cv.Optional("forecast_age"): sensor.sensor_schema(unit_of_measurement="s", accuracy_decimals=0),
            cv.Optional("issued_epoch"): text_sensor.text_sensor_schema(),
            cv.Optional("request_count"): sensor.sensor_schema(accuracy_decimals=0),
            cv.Optional("failure_count"): sensor.sensor_schema(accuracy_decimals=0),
            cv.Optional("worker_stack_free"): sensor.sensor_schema(unit_of_measurement="B", accuracy_decimals=0),
            cv.Optional("last_duration"): sensor.sensor_schema(unit_of_measurement="ms", accuracy_decimals=0),
            cv.Optional("temperature_30m"): sensor.sensor_schema(unit_of_measurement="°C", accuracy_decimals=2),
            cv.Optional("temperature_1h"): sensor.sensor_schema(unit_of_measurement="°C", accuracy_decimals=2),
            cv.Optional("temperature_3h"): sensor.sensor_schema(unit_of_measurement="°C", accuracy_decimals=2),
        }
    ).extend(cv.polling_component_schema("10s")),
    socket.consume_sockets(1, "openquatt_weather"),
)


async def to_code(config):
    cg.add_define("USE_OPENQUATT_WEATHER")
    require_certificate_bundle()
    include_builtin_idf_component("esp-tls")
    include_builtin_idf_component("mbedtls")
    include_builtin_idf_component("nvs_flash")
    include_builtin_idf_component("esp_http_server")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_location_text(await text.new_text(config["location"], min_length=0, max_length=64)))
    for key in ("clock", "enabled_switch", "pause_sensor"):
        cg.add(getattr(var, f"set_{key}")(await cg.get_variable(config[key])))
    cg.add(var.set_web_auth(await cg.get_variable(config["web_auth"])))
    for key in ("status", "resolved_location", "issued_epoch"):
        if key in config:
            cg.add(getattr(var, f"set_{key}")(await text_sensor.new_text_sensor(config[key])))
    for key in ("forecast_age", "request_count", "failure_count", "worker_stack_free", "last_duration",
                "temperature_30m", "temperature_1h", "temperature_3h"):
        if key in config:
            cg.add(getattr(var, f"set_{key}")(await sensor.new_sensor(config[key])))
