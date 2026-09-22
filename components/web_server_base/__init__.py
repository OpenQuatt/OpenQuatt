import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, __version__
from esphome.core import CORE, coroutine_with_priority
from esphome.coroutine import CoroPriority
from esphome.types import ConfigType

CODEOWNERS = ["@esphome/core"]
DEPENDENCIES = ["network"]


AUTO_LOAD = ["web_server_idf"]


web_server_base_ns = cg.esphome_ns.namespace("web_server_base")
WebServerBase = web_server_base_ns.class_("WebServerBase")

CONF_WEB_SERVER_BASE_ID = "web_server_base_id"


def _consume_web_server_base_sockets(config: ConfigType) -> ConfigType:
    """Count the listening socket shared by web_server and captive_portal once."""
    from esphome.components import socket

    socket.consume_sockets(1, "web_server_base", socket.SocketType.TCP_LISTEN)(config)
    return config


def _validate_version(config: ConfigType) -> ConfigType:
    if __version__ != "2026.9.0":
        raise cv.Invalid(
            "OpenQuatt web_server_base compatibility override requires ESPHome 2026.9.0; "
            "review the upstream auth changes before updating this override."
        )
    if not CORE.is_esp32:
        raise cv.Invalid("OpenQuatt web_server_base override supports ESP32 targets only")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(WebServerBase),
        }
    ),
    _consume_web_server_base_sockets,
    _validate_version,
)


@coroutine_with_priority(CoroPriority.WEB_SERVER_BASE)
async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(cg.RawExpression(f"{web_server_base_ns}::global_web_server_base = {var}"))

    # Count for StaticVector in web_server_idf; matches the header added in init().
    cg.add_define("WEB_SERVER_DEFAULT_HEADERS_COUNT", 1)
