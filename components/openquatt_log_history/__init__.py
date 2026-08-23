import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch, time
from esphome.components.esp32 import add_idf_sdkconfig_option
from esphome.const import CONF_ID

AUTO_LOAD = ["logger", "switch", "time", "web_server_base"]

CONF_ENABLED_SWITCH = "enabled_switch"
CONF_CLOCK = "clock"
CONF_BUILD_SOURCE_REPOSITORY = "build_source_repository"
CONF_BUILD_SOURCE_COMMIT = "build_source_commit"
CONF_BUILD_TARGET = "build_target"
CONF_BUILD_EPOCH = "build_epoch"
CONF_FIRMWARE_VERSION = "firmware_version"
CONF_RELEASE_CHANNEL = "release_channel"
CONF_HARDWARE_PROFILE = "hardware_profile"
CONF_TOPOLOGY = "topology"
CONF_CONNECTION = "connection"

openquatt_log_history_ns = cg.esphome_ns.namespace("openquatt_log_history")
OpenQuattLogHistory = openquatt_log_history_ns.class_("OpenQuattLogHistory", cg.Component)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattLogHistory),
        cv.Required(CONF_ENABLED_SWITCH): cv.use_id(switch.Switch),
        cv.Required(CONF_CLOCK): cv.use_id(time.RealTimeClock),
        cv.Required(CONF_BUILD_SOURCE_REPOSITORY): cv.All(
            cv.string_strict, cv.Length(min=1, max=97)
        ),
        cv.Required(CONF_BUILD_SOURCE_COMMIT): cv.All(
            cv.string_strict, cv.Length(min=1, max=40)
        ),
        cv.Required(CONF_BUILD_TARGET): cv.All(
            cv.string_strict, cv.Length(min=1, max=96)
        ),
        cv.Required(CONF_BUILD_EPOCH): cv.uint64_t,
        cv.Required(CONF_FIRMWARE_VERSION): cv.All(
            cv.string_strict, cv.Length(min=1, max=32)
        ),
        cv.Required(CONF_RELEASE_CHANNEL): cv.All(
            cv.string_strict, cv.Length(min=1, max=16)
        ),
        cv.Required(CONF_HARDWARE_PROFILE): cv.All(
            cv.string_strict, cv.Length(min=1, max=32)
        ),
        cv.Required(CONF_TOPOLOGY): cv.All(
            cv.string_strict, cv.Length(min=1, max=16)
        ),
        cv.Required(CONF_CONNECTION): cv.All(
            cv.string_strict, cv.Length(min=1, max=16)
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    add_idf_sdkconfig_option("CONFIG_APP_RETRIEVE_LEN_ELF_SHA", 64)
    cg.add_global(openquatt_log_history_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    enabled_switch = await cg.get_variable(config[CONF_ENABLED_SWITCH])
    cg.add(var.set_enabled_switch(enabled_switch))

    clock = await cg.get_variable(config[CONF_CLOCK])
    cg.add(var.set_clock(clock))
    cg.add(var.set_build_source_repository(config[CONF_BUILD_SOURCE_REPOSITORY]))
    cg.add(var.set_build_source_commit(config[CONF_BUILD_SOURCE_COMMIT]))
    cg.add(var.set_build_target(config[CONF_BUILD_TARGET]))
    cg.add(var.set_build_epoch(config[CONF_BUILD_EPOCH]))
    cg.add(var.set_firmware_version(config[CONF_FIRMWARE_VERSION]))
    cg.add(var.set_release_channel(config[CONF_RELEASE_CHANNEL]))
    cg.add(var.set_hardware_profile(config[CONF_HARDWARE_PROFILE]))
    cg.add(var.set_topology(config[CONF_TOPOLOGY]))
    cg.add(var.set_connection(config[CONF_CONNECTION]))
