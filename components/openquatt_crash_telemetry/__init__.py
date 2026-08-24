import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, socket, switch, text_sensor
from esphome.components.esp32 import add_idf_sdkconfig_option
from esphome.const import CONF_ID

DEPENDENCIES = ["logger", "psram", "openquatt_usage_telemetry"]

CONF_BROKER = "broker"
CONF_PORT = "port"
CONF_TLS = "tls"
CONF_USERNAME = "username"
CONF_PASSWORD = "password"
CONF_TOPIC = "topic"
CONF_USAGE_SWITCH = "usage_switch"
CONF_INSTALLATION_ID_SENSOR = "installation_id_sensor"
CONF_SETUP_COMPLETE_SENSOR = "setup_complete_sensor"
CONF_SOURCE_REPOSITORY = "source_repository"
CONF_SOURCE_COMMIT = "source_commit"
CONF_BUILD_TARGET = "build_target"
CONF_RELEASE_MANIFEST_URL = "release_manifest_url"
CONF_FIRMWARE_VERSION = "firmware_version"
CONF_RELEASE_CHANNEL = "release_channel"
CONF_HARDWARE_PROFILE = "hardware_profile"
CONF_TOPOLOGY = "topology"
CONF_CONNECTION = "connection"

openquatt_crash_telemetry_ns = cg.esphome_ns.namespace("openquatt_crash_telemetry")
OpenQuattCrashTelemetry = openquatt_crash_telemetry_ns.class_("OpenQuattCrashTelemetry", cg.Component)


def validate_config(config):
    if config[CONF_PASSWORD] and not config[CONF_USERNAME]:
        raise cv.Invalid("username is required when password is configured")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OpenQuattCrashTelemetry),
            cv.Optional(CONF_BROKER, default=""): cv.All(
                cv.string_strict, cv.Length(max=128)
            ),
            cv.Optional(CONF_PORT, default=8883): cv.port,
            cv.Optional(CONF_TLS, default=True): cv.boolean,
            cv.Optional(CONF_USERNAME, default=""): cv.All(
                cv.string_strict, cv.Length(max=96)
            ),
            cv.Optional(CONF_PASSWORD, default=""): cv.sensitive(
                cv.All(cv.string_strict, cv.Length(max=192))
            ),
            cv.Required(CONF_TOPIC): cv.All(cv.publish_topic, cv.Length(max=128)),
            cv.Required(CONF_USAGE_SWITCH): cv.use_id(switch.Switch),
            cv.Required(CONF_INSTALLATION_ID_SENSOR): cv.use_id(text_sensor.TextSensor),
            cv.Required(CONF_SETUP_COMPLETE_SENSOR): cv.use_id(binary_sensor.BinarySensor),
            cv.Required(CONF_SOURCE_REPOSITORY): cv.All(
                cv.string_strict, cv.Length(min=1, max=97)
            ),
            cv.Required(CONF_SOURCE_COMMIT): cv.All(
                cv.string_strict, cv.Length(min=1, max=40)
            ),
            cv.Required(CONF_BUILD_TARGET): cv.All(
                cv.string_strict, cv.Length(min=1, max=96)
            ),
            cv.Optional(CONF_RELEASE_MANIFEST_URL, default=""): cv.All(
                cv.string_strict, cv.Length(max=256)
            ),
            cv.Required(CONF_FIRMWARE_VERSION): cv.All(
                cv.string_strict, cv.Length(max=32)
            ),
            cv.Required(CONF_RELEASE_CHANNEL): cv.All(
                cv.string_strict, cv.Length(max=16)
            ),
            cv.Required(CONF_HARDWARE_PROFILE): cv.All(
                cv.string_strict, cv.Length(max=32)
            ),
            cv.Required(CONF_TOPOLOGY): cv.All(
                cv.string_strict, cv.Length(max=16)
            ),
            cv.Required(CONF_CONNECTION): cv.All(
                cv.string_strict, cv.Length(max=16)
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_config,
    socket.consume_sockets(1, "openquatt_crash_telemetry"),
)


async def to_code(config):
    add_idf_sdkconfig_option("CONFIG_APP_RETRIEVE_LEN_ELF_SHA", 64)

    cg.add_global(openquatt_crash_telemetry_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_broker(config[CONF_BROKER]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_tls(config[CONF_TLS]))
    cg.add(var.set_username(config[CONF_USERNAME]))
    cg.add(var.set_password(config[CONF_PASSWORD]))
    cg.add(var.set_topic(config[CONF_TOPIC]))
    usage_switch = await cg.get_variable(config[CONF_USAGE_SWITCH])
    cg.add(var.set_usage_switch(usage_switch))
    installation_id_sensor = await cg.get_variable(config[CONF_INSTALLATION_ID_SENSOR])
    cg.add(var.set_installation_id_sensor(installation_id_sensor))
    setup_complete_sensor = await cg.get_variable(config[CONF_SETUP_COMPLETE_SENSOR])
    cg.add(var.set_setup_complete_sensor(setup_complete_sensor))
    cg.add(var.set_source_repository(config[CONF_SOURCE_REPOSITORY]))
    cg.add(var.set_source_commit(config[CONF_SOURCE_COMMIT]))
    cg.add(var.set_build_target(config[CONF_BUILD_TARGET]))
    cg.add(var.set_release_manifest_url(config[CONF_RELEASE_MANIFEST_URL]))
    cg.add(var.set_firmware_version(config[CONF_FIRMWARE_VERSION]))
    cg.add(var.set_release_channel(config[CONF_RELEASE_CHANNEL]))
    cg.add(var.set_hardware_profile(config[CONF_HARDWARE_PROFILE]))
    cg.add(var.set_topology(config[CONF_TOPOLOGY]))
    cg.add(var.set_connection(config[CONF_CONNECTION]))
