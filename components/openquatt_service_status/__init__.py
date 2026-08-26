import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import globals as globals_component, web_server
from esphome.const import CONF_ID, CONF_WEB_SERVER_ID

AUTO_LOAD = ["globals", "web_server_base"]
DEPENDENCIES = ["web_server"]

openquatt_service_status_ns = cg.esphome_ns.namespace("openquatt_service_status")
OpenQuattServiceStatus = openquatt_service_status_ns.class_("OpenQuattServiceStatus", cg.Component)

CONF_GLOBAL_KEYS = [
    "control_mode_code",
    "commissioning_active",
    "commissioning_task_code",
    "commissioning_status",
    "boiler_result_w",
    "boiler_confidence",
    "boiler_quality",
    "boiler_status",
    "flow_autotune_status",
    "flow_kp_suggested",
    "flow_ki_suggested",
    "air_purge_active",
    "air_purge_status",
    "air_purge_remaining",
    "air_purge_phase",
    "air_purge_target_ipwm",
    "manual_flow_active",
    "manual_flow_status",
    "manual_flow_target_ipwm",
    "manual_hp_active",
    "manual_hp_status",
    "manual_hp_guard_status",
    "hp_water_calibration_active",
    "hp_water_calibration_status",
    "hp_water_calibration_remaining",
    "hp_water_calibration_phase",
    "hp_water_calibration_spread",
    "hp_water_calibration_supply_delta",
    "hp_water_calibration_stable_progress",
    "hp_water_calibration_stable_required",
    "hp_water_calibration_result_reference",
    "hp_water_calibration_result_spread_before",
    "hp_water_calibration_result_expected_spread",
    "hp_water_calibration_result_hp1_in_raw_avg",
    "hp_water_calibration_result_hp1_out_raw_avg",
    "hp_water_calibration_result_hp2_in_raw_avg",
    "hp_water_calibration_result_hp2_out_raw_avg",
    "hp_water_calibration_result_supply_raw_avg",
    "hp_water_calibration_result_supply_offset",
    "hp_water_calibration_result_supply_source",
]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(OpenQuattServiceStatus),
        cv.GenerateID(CONF_WEB_SERVER_ID): cv.use_id(web_server.WebServer),
        **{
            cv.Required(key): cv.use_id(globals_component.GlobalsComponent)
            for key in CONF_GLOBAL_KEYS
        },
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_global(openquatt_service_status_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    web_server_var = await cg.get_variable(config[CONF_WEB_SERVER_ID])
    cg.add(var.set_web_server(web_server_var))

    for key in CONF_GLOBAL_KEYS:
        value = await cg.get_variable(config[key])
        cg.add(getattr(var, f"set_{key}")(value))
