import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import esp32
from esphome.components.esp32.const import VARIANT_ESP32S3
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32", "psram", "web_server", "openquatt_web_auth"]
AUTO_LOAD = ["web_server_base"]
CONFLICTS_WITH = ["bluetooth_proxy", "esp32_ble_server", "ble_client", "esp32_improv"]

ns = cg.esphome_ns.namespace("openquatt_ble")
OpenQuattBle = ns.class_("OpenQuattBle", cg.Component)
WebAuth = cg.esphome_ns.namespace("openquatt_web_auth").class_("OpenQuattWebAuth", cg.Component)

COMMON_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(OpenQuattBle),
    cv.Required("web_auth"): cv.use_id(WebAuth),
}).extend(cv.COMPONENT_SCHEMA)


def _use_bluedroid_tracker(value):
    from esphome.components.esp32_ble_tracker import ESP32BLETracker

    return cv.use_id(ESP32BLETracker)(value)


CONFIG_SCHEMA = cv.typed_schema(
    {
        "bluedroid": COMMON_SCHEMA.extend({
            cv.GenerateID("tracker"): _use_bluedroid_tracker,
        }),
        "nimble": COMMON_SCHEMA,
    },
    key="backend",
    default_type="bluedroid",
)


def _validate_backend(config):
    if config["backend"] == "nimble":
        full_config = fv.full_config.get()
        if "esp32_ble" in full_config or "esp32_ble_tracker" in full_config:
            raise cv.Invalid("The NimBLE observer cannot run with ESPHome's ESP32 BLE stack")
        if esp32.get_esp32_variant() != VARIANT_ESP32S3:
            raise cv.Invalid("The OpenQuatt NimBLE trial requires an ESP32-S3")
    return config


FINAL_VALIDATE_SCHEMA = _validate_backend


async def to_code(config):
    cg.add_define("USE_OPENQUATT_BLE")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if config["backend"] == "nimble":
        cg.add_define("USE_OPENQUATT_BLE_NIMBLE")
        esp32.request_bluetooth()
        esp32.request_software_coexistence()
        for option, value in {
            "CONFIG_BT_BLUEDROID_ENABLED": False,
            "CONFIG_BT_NIMBLE_ENABLED": True,
            "CONFIG_BT_NIMBLE_ROLE_CENTRAL": False,
            "CONFIG_BT_NIMBLE_ROLE_PERIPHERAL": False,
            "CONFIG_BT_NIMBLE_ROLE_BROADCASTER": False,
            "CONFIG_BT_NIMBLE_ROLE_OBSERVER": True,
            "CONFIG_BT_NIMBLE_SECURITY_ENABLE": False,
            "CONFIG_BT_NIMBLE_NVS_PERSIST": False,
            "CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL": True,
            "CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE": 4096,
            "CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT": 16,
            "CONFIG_BT_NIMBLE_TRANSPORT_EVT_DISCARD_COUNT": 8,
            "CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT": False,
            "CONFIG_BT_NIMBLE_GATT_SERVER": False,
        }.items():
            esp32.add_idf_sdkconfig_option(option, value)
    else:
        cg.add(var.set_tracker(await cg.get_variable(config["tracker"])))
    cg.add(var.set_web_auth(await cg.get_variable(config["web_auth"])))
