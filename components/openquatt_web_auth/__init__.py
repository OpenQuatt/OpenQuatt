import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_sdkconfig_option
from esphome.const import CONF_ID
from esphome.core import CORE
from esphome.coroutine import CoroPriority, coroutine_with_priority

CONF_BOOTSTRAP_USERNAME = "bootstrap_username"
CONF_BOOTSTRAP_PASSWORD = "bootstrap_password"
CONF_DEFAULT_AUTH_ENABLED = "default_auth_enabled"
# Keep in sync with openquatt/base/common.yaml sdkconfig_options.
HTTPD_MAX_REQ_HDR_LEN = 4096

openquatt_web_auth_ns = cg.esphome_ns.namespace("openquatt_web_auth")
OpenQuattWebAuth = openquatt_web_auth_ns.class_("OpenQuattWebAuth", cg.Component)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattWebAuth),
        cv.Required(CONF_BOOTSTRAP_USERNAME): cv.All(cv.string_strict, cv.Length(min=1, max=32)),
        cv.Required(CONF_BOOTSTRAP_PASSWORD): cv.sensitive(
            cv.All(cv.string_strict, cv.Length(min=1, max=64))
        ),
        cv.Optional(CONF_DEFAULT_AUTH_ENABLED, default=True): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)


@coroutine_with_priority(CoroPriority.FINAL)
async def _restore_httpd_request_header_limit():
    # ESPHome 2026.8 web_server_idf overwrites the explicit sdkconfig value at
    # its normal codegen priority. Reapply it after all component codegen.
    add_idf_sdkconfig_option("CONFIG_HTTPD_MAX_REQ_HDR_LEN", HTTPD_MAX_REQ_HDR_LEN)


async def to_code(config):
    cg.add_global(openquatt_web_auth_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_bootstrap_username(config[CONF_BOOTSTRAP_USERNAME]))
    cg.add(var.set_bootstrap_password(config[CONF_BOOTSTRAP_PASSWORD]))
    cg.add(var.set_default_auth_enabled(config[CONF_DEFAULT_AUTH_ENABLED]))
    CORE.add_job(_restore_httpd_request_header_limit)
