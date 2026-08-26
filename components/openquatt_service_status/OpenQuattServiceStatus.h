#pragma once

#include <esp_http_server.h>

#include <string>

#include "esphome/components/globals/globals_component.h"
#include "esphome/components/web_server/web_server.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

namespace esphome {
namespace openquatt_service_status {

class OpenQuattServiceStatus : public Component {
 public:
  using BoolGlobal = globals::GlobalsComponent<bool>;
  using FloatGlobal = globals::GlobalsComponent<float>;
  using IntGlobal = globals::GlobalsComponent<int>;
  using StringGlobal = globals::GlobalsComponent<std::string>;

  void set_web_server(web_server::WebServer* web_server) { this->web_server_ = web_server; }

  void set_control_mode_code(IntGlobal* value) { this->control_mode_code_ = value; }
  void set_commissioning_active(BoolGlobal* value) { this->commissioning_active_ = value; }
  void set_commissioning_task_code(IntGlobal* value) { this->commissioning_task_code_ = value; }
  void set_commissioning_status(StringGlobal* value) { this->commissioning_status_ = value; }
  void set_boiler_result_w(FloatGlobal* value) { this->boiler_result_w_ = value; }
  void set_boiler_confidence(FloatGlobal* value) { this->boiler_confidence_ = value; }
  void set_boiler_quality(StringGlobal* value) { this->boiler_quality_ = value; }
  void set_boiler_status(StringGlobal* value) { this->boiler_status_ = value; }
  void set_flow_autotune_status(StringGlobal* value) { this->flow_autotune_status_ = value; }
  void set_flow_kp_suggested(FloatGlobal* value) { this->flow_kp_suggested_ = value; }
  void set_flow_ki_suggested(FloatGlobal* value) { this->flow_ki_suggested_ = value; }
  void set_air_purge_active(BoolGlobal* value) { this->air_purge_active_ = value; }
  void set_air_purge_status(StringGlobal* value) { this->air_purge_status_ = value; }
  void set_air_purge_remaining(IntGlobal* value) { this->air_purge_remaining_ = value; }
  void set_air_purge_phase(IntGlobal* value) { this->air_purge_phase_ = value; }
  void set_air_purge_target_ipwm(IntGlobal* value) { this->air_purge_target_ipwm_ = value; }
  void set_manual_flow_active(BoolGlobal* value) { this->manual_flow_active_ = value; }
  void set_manual_flow_status(StringGlobal* value) { this->manual_flow_status_ = value; }
  void set_manual_flow_target_ipwm(IntGlobal* value) { this->manual_flow_target_ipwm_ = value; }
  void set_manual_hp_active(BoolGlobal* value) { this->manual_hp_active_ = value; }
  void set_manual_hp_status(StringGlobal* value) { this->manual_hp_status_ = value; }
  void set_manual_hp_guard_status(StringGlobal* value) { this->manual_hp_guard_status_ = value; }
  void set_hp_water_calibration_active(BoolGlobal* value) { this->hp_water_calibration_active_ = value; }
  void set_hp_water_calibration_status(StringGlobal* value) { this->hp_water_calibration_status_ = value; }
  void set_hp_water_calibration_remaining(IntGlobal* value) { this->hp_water_calibration_remaining_ = value; }
  void set_hp_water_calibration_phase(IntGlobal* value) { this->hp_water_calibration_phase_ = value; }
  void set_hp_water_calibration_spread(FloatGlobal* value) { this->hp_water_calibration_spread_ = value; }
  void set_hp_water_calibration_supply_delta(FloatGlobal* value) { this->hp_water_calibration_supply_delta_ = value; }
  void set_hp_water_calibration_stable_progress(IntGlobal* value) {
    this->hp_water_calibration_stable_progress_ = value;
  }
  void set_hp_water_calibration_stable_required(IntGlobal* value) {
    this->hp_water_calibration_stable_required_ = value;
  }
  void set_hp_water_calibration_result_reference(FloatGlobal* value) {
    this->hp_water_calibration_result_reference_ = value;
  }
  void set_hp_water_calibration_result_spread_before(FloatGlobal* value) {
    this->hp_water_calibration_result_spread_before_ = value;
  }
  void set_hp_water_calibration_result_expected_spread(FloatGlobal* value) {
    this->hp_water_calibration_result_expected_spread_ = value;
  }
  void set_hp_water_calibration_result_hp1_in_raw_avg(FloatGlobal* value) {
    this->hp_water_calibration_result_hp1_in_raw_avg_ = value;
  }
  void set_hp_water_calibration_result_hp1_out_raw_avg(FloatGlobal* value) {
    this->hp_water_calibration_result_hp1_out_raw_avg_ = value;
  }
  void set_hp_water_calibration_result_hp2_in_raw_avg(FloatGlobal* value) {
    this->hp_water_calibration_result_hp2_in_raw_avg_ = value;
  }
  void set_hp_water_calibration_result_hp2_out_raw_avg(FloatGlobal* value) {
    this->hp_water_calibration_result_hp2_out_raw_avg_ = value;
  }
  void set_hp_water_calibration_result_supply_raw_avg(FloatGlobal* value) {
    this->hp_water_calibration_result_supply_raw_avg_ = value;
  }
  void set_hp_water_calibration_result_supply_offset(FloatGlobal* value) {
    this->hp_water_calibration_result_supply_offset_ = value;
  }
  void set_hp_water_calibration_result_supply_source(StringGlobal* value) {
    this->hp_water_calibration_result_supply_source_ = value;
  }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void write_status(httpd_req_t* req) const;

 protected:
  web_server::WebServer* web_server_{nullptr};
  IntGlobal* control_mode_code_{nullptr};
  BoolGlobal* commissioning_active_{nullptr};
  IntGlobal* commissioning_task_code_{nullptr};
  StringGlobal* commissioning_status_{nullptr};
  FloatGlobal* boiler_result_w_{nullptr};
  FloatGlobal* boiler_confidence_{nullptr};
  StringGlobal* boiler_quality_{nullptr};
  StringGlobal* boiler_status_{nullptr};
  StringGlobal* flow_autotune_status_{nullptr};
  FloatGlobal* flow_kp_suggested_{nullptr};
  FloatGlobal* flow_ki_suggested_{nullptr};
  BoolGlobal* air_purge_active_{nullptr};
  StringGlobal* air_purge_status_{nullptr};
  IntGlobal* air_purge_remaining_{nullptr};
  IntGlobal* air_purge_phase_{nullptr};
  IntGlobal* air_purge_target_ipwm_{nullptr};
  BoolGlobal* manual_flow_active_{nullptr};
  StringGlobal* manual_flow_status_{nullptr};
  IntGlobal* manual_flow_target_ipwm_{nullptr};
  BoolGlobal* manual_hp_active_{nullptr};
  StringGlobal* manual_hp_status_{nullptr};
  StringGlobal* manual_hp_guard_status_{nullptr};
  BoolGlobal* hp_water_calibration_active_{nullptr};
  StringGlobal* hp_water_calibration_status_{nullptr};
  IntGlobal* hp_water_calibration_remaining_{nullptr};
  IntGlobal* hp_water_calibration_phase_{nullptr};
  FloatGlobal* hp_water_calibration_spread_{nullptr};
  FloatGlobal* hp_water_calibration_supply_delta_{nullptr};
  IntGlobal* hp_water_calibration_stable_progress_{nullptr};
  IntGlobal* hp_water_calibration_stable_required_{nullptr};
  FloatGlobal* hp_water_calibration_result_reference_{nullptr};
  FloatGlobal* hp_water_calibration_result_spread_before_{nullptr};
  FloatGlobal* hp_water_calibration_result_expected_spread_{nullptr};
  FloatGlobal* hp_water_calibration_result_hp1_in_raw_avg_{nullptr};
  FloatGlobal* hp_water_calibration_result_hp1_out_raw_avg_{nullptr};
  FloatGlobal* hp_water_calibration_result_hp2_in_raw_avg_{nullptr};
  FloatGlobal* hp_water_calibration_result_hp2_out_raw_avg_{nullptr};
  FloatGlobal* hp_water_calibration_result_supply_raw_avg_{nullptr};
  FloatGlobal* hp_water_calibration_result_supply_offset_{nullptr};
  StringGlobal* hp_water_calibration_result_supply_source_{nullptr};
};

}  // namespace openquatt_service_status
}  // namespace esphome
