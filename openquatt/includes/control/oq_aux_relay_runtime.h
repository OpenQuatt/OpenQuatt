#pragma once

#include "oq_aux_relay_logic.h"

#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
namespace oq_aux_relay_runtime {

class Runtime {
 public:
  void external_command(bool relay_on) {
    const auto function = selected_function_();
    if (!oq_aux_relay::apply_external_command(state_, function, relay_on)) {
      id(controller_aux_relay).publish_state(state_.relay_on);
      return;
    }
    ESP_LOGI("quatt.aux_relay", "Aux relay %s (external control)", relay_on ? "ON" : "OFF");
    apply_output_(relay_on);
  }

  void tick() {
    const auto decision = oq_aux_relay::update(
        state_, {selected_function_(), id(oq_control_mode_code), id(oq_cm1_next_after),
                 id(oq_aux_wait_for_supply_temp).state, id(oq_system_supply_temp).state, id(oq_aux_heat_start_c).state,
                 id(oq_aux_cool_start_c).state, id(oq_aux_temp_hysteresis_c).state});
    const char* status = oq_aux_relay::status_text(decision.status);
    if (!decision.external_control && decision.changed) {
      ESP_LOGI("quatt.aux_relay", "Aux relay %s: %s", decision.relay_on ? "ON" : "OFF", status);
      apply_output_(decision.relay_on);
    }
    if (!id(oq_aux_relay_active).has_state() || id(oq_aux_relay_active).state != decision.relay_on) {
      id(oq_aux_relay_active).publish_state(decision.relay_on);
    }
    if (!have_status_ || decision.status != last_status_) {
      id(oq_aux_relay_status).publish_state(status);
      last_status_ = decision.status;
      have_status_ = true;
    }
  }

 private:
  static oq_aux_relay::Function selected_function_() {
    if (!id(oq_aux_relay_function).has_state()) return oq_aux_relay::Function::DISABLED;
    const auto& option = id(oq_aux_relay_function).current_option();
    if (option == "Heating demand") return oq_aux_relay::Function::HEATING;
    if (option == "Cooling demand") return oq_aux_relay::Function::COOLING;
    if (option == "Heating or cooling demand") return oq_aux_relay::Function::HEATING_OR_COOLING;
    if (option == "External control") return oq_aux_relay::Function::EXTERNAL;
    return oq_aux_relay::Function::DISABLED;
  }

  static void apply_output_(bool relay_on) {
    if (relay_on) {
      id(controller_aux_relay_out).turn_on();
    } else {
      id(controller_aux_relay_out).turn_off();
    }
    id(controller_aux_relay).publish_state(relay_on);
  }

  oq_aux_relay::State state_{};
  bool have_status_ = false;
  oq_aux_relay::Status last_status_ = oq_aux_relay::Status::DISABLED;
};

inline Runtime& runtime() {
  static Runtime value;
  return value;
}

}  // namespace oq_aux_relay_runtime
#endif
