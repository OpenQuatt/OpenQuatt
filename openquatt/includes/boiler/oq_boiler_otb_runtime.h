#pragma once

#include "oq_boiler_transport_logic.h"
#include "../control/oq_boiler_runtime.h"

#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
namespace oq_boiler_otb_runtime {

inline void withdraw_command() {
  id(oq_otb_ch_enable).turn_off();
  auto call = id(oq_otb_t_set_command).make_call();
  call.set_value(0.0f);
  call.perform();
}

inline void connection_changed(bool opentherm_selected) {
  if (id(oq_otb_hub_ready)) {
    if (opentherm_selected) {
      oq_otb::startup_probe_state.end();
      id(oq_otb_startup_probe_active) = false;
      id(oq_boiler_connection_mismatch_state) = false;
      id(oq_boiler_connection_mismatch).publish_state(false);
      id(oq_otb_hub).resume_polling();
    } else {
      id(oq_otb_withdraw_and_flush).execute();
      oq_otb::startup_probe_state.begin((uint32_t)millis());
      id(oq_otb_startup_probe_active) = true;
      id(oq_boiler_connection_mismatch_state) = false;
      id(oq_boiler_connection_mismatch).publish_state(false);
      id(boiler_relay).turn_off();
      id(oq_otb_hub)
          .start_priority_polling(esphome::opentherm::MessageId::STATUS, esphome::opentherm::MessageId::CH_SETPOINT);
    }
  }
  oq_otb::telemetry_state.reset_link_session();
  id(oq_otb_link_initialized) = true;
  id(oq_otb_link_available_state) = false;
  id(otb_link_available).publish_state(false);
  id(oq_otb_invalidate_telemetry).execute();
}

inline void apply_command(float minimum_flow_lph, uint32_t status_timeout_ms) {
  const bool opentherm_selected =
      id(oq_boiler_connection).has_state() && id(oq_boiler_connection).current_option() == "OpenTherm";
  const auto decision = oq_boiler_transport::evaluate_command_adapter({
      opentherm_selected,
      !id(oq_runtime_polling_paused).state && !id(oq_boiler_runtime_pause_state),
      id(oq_otb_link_available_state),
      oq_otb::telemetry_state.field_is_fresh(oq_otb::FIELD_STATUS, (uint32_t)millis(), status_timeout_ms),
      id(oq_boiler_output_request),
      id(oq_boiler_output_target_temperature_c),
      id(flow_rate_selected).state,
      minimum_flow_lph,
      id(oq_otb_applied_command_active),
      id(oq_otb_t_set_command).has_state(),
      id(oq_otb_t_set_command).state,
  });

  if (decision.applied_stop) oq_otb::start_handshake_state.cancel();
  if (decision.withdraw_controller_request) {
    id(oq_boiler_output_request) = false;
    id(oq_boiler_output_target_temperature_c) = NAN;
    id(oq_boiler_output_last_change_ms) = (uint32_t)millis();
    id(oq_boiler_block_reason_code) = decision.flow_valid && decision.target_valid
                                          ? oq_boiler::BLOCK_TRANSPORT_UNAVAILABLE
                                          : oq_boiler::BLOCK_TARGET_INVALID;
  }
  if (decision.prioritize_off_frames) {
    id(oq_otb_hub)
        .prioritize_messages(esphome::opentherm::MessageId::STATUS, esphome::opentherm::MessageId::CH_SETPOINT);
  }
  if (decision.write_target) {
    auto call = id(oq_otb_t_set_command).make_call();
    call.set_value(decision.target_to_write_c);
    call.perform();
  }
  if (decision.command_active) {
    id(oq_otb_ch_enable).turn_on();
    if (decision.applied_start) {
      id(oq_otb_hub)
          .defer_priority_messages(esphome::opentherm::MessageId::CH_SETPOINT, esphome::opentherm::MessageId::STATUS);
    }
  } else {
    id(oq_otb_ch_enable).turn_off();
  }
  id(oq_otb_applied_command_active) = decision.command_active;
  if (oq_boiler_transport::otb_may_update_transport(opentherm_selected)) {
    id(oq_boiler_transport_active) = oq_boiler_transport::compute_otb_transport_active(
        id(oq_otb_link_available_state), id(otb_ch_active).has_state(), id(otb_ch_active).state);
  }
}

inline void link_watch(uint32_t link_timeout_ms, uint32_t field_timeout_ms) {
  const uint32_t now_ms = (uint32_t)millis();
  oq_otb::telemetry_state.expire_response_session_if_stale(now_ms, link_timeout_ms);
  const bool available = oq_otb::telemetry_state.transport_is_available(now_ms, link_timeout_ms, field_timeout_ms);
  const bool changed = !id(oq_otb_link_initialized) || available != id(oq_otb_link_available_state);
  if (!changed) return;

  id(oq_otb_link_initialized) = true;
  id(oq_otb_link_available_state) = available;
  id(otb_link_available).publish_state(available);
  ESP_LOGI("quatt.otb", "Boiler OpenTherm link %s", available ? "available" : "unavailable");

  const bool opentherm_selected =
      id(oq_boiler_connection).has_state() && id(oq_boiler_connection).current_option() == "OpenTherm";
  if (opentherm_selected) {
    oq_boiler_runtime::runtime().selected_transport_link_changed(available);
    id(oq_otb_ch_enable).turn_off();
    if (!id(oq_otb_t_set_command).has_state() || fabsf(id(oq_otb_t_set_command).state) >= 0.05f) {
      auto call = id(oq_otb_t_set_command).make_call();
      call.set_value(0.0f);
      call.perform();
    }
  }
  if (!available) id(oq_otb_invalidate_telemetry).execute();
}

}  // namespace oq_boiler_otb_runtime
#endif
