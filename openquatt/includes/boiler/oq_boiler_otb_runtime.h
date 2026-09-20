#pragma once

#include "oq_boiler_transport_logic.h"
#include "oq_otb_connection_state.h"
#include "oq_otb_start_handshake.h"
#include "oq_otb_startup_probe.h"
#include "../control/oq_boiler_runtime.h"

#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
namespace oq_boiler_otb_runtime {

inline void withdraw_command() {
  id(oq_otb_ch_enable).turn_off();
  auto call = id(oq_otb_t_set_command).make_call();
  call.set_value(0.0f);
  call.perform();
}

inline void reset_link_state() {
  oq_otb::telemetry_state.reset_link_session();
  id(oq_otb_link_initialized) = true;
  id(oq_otb_link_available_state) = false;
  id(otb_link_available).publish_state(false);
  id(oq_otb_invalidate_telemetry).execute();
}

inline void enter_dormant_state() {
  if (id(oq_otb_hub_ready)) {
    // If OpenTherm was active, use the existing bounded off handshake before
    // silencing the physical bus. With no live link this returns immediately
    // after withdrawing CH and TSet locally.
    id(oq_otb_hub).set_no_response_expected(false);
    id(oq_otb_withdraw_and_flush).execute();
    oq_otb::startup_probe_state.end();
    id(oq_otb_startup_probe_active) = false;
    id(oq_boiler_connection_mismatch_state) = false;
    id(oq_boiler_connection_mismatch).publish_state(false);
    id(oq_boiler_connection_auto_selected_state) = false;
    id(oq_boiler_connection_auto_selected).publish_state(false);
    id(boiler_relay).turn_off();
    id(oq_otb_hub).set_no_response_expected(false);
    id(oq_otb_hub).suspend_polling();
  }
  id(oq_otb_applied_command_active) = false;
  id(oq_boiler_transport_active) = false;
  oq_otb::connection_verification_state.reset();
  reset_link_state();
}

inline void connection_changed(bool opentherm_selected) {
  if (!id(oq_aux_heat_source_present).state) {
    enter_dormant_state();
    return;
  }

  if (id(oq_otb_hub_ready)) {
    if (opentherm_selected) {
      oq_otb::connection_verification_state.begin_opentherm((uint32_t)millis());
      oq_otb::startup_probe_state.end();
      id(oq_otb_startup_probe_active) = false;
      id(oq_boiler_connection_mismatch_state) = false;
      id(oq_boiler_connection_mismatch).publish_state(false);
      id(oq_otb_hub).set_no_response_expected(false);
      id(oq_otb_hub).resume_polling();
    } else {
      oq_otb::connection_verification_state.begin_r1_probe();
      id(oq_otb_withdraw_and_flush).execute();
      oq_otb::startup_probe_state.begin((uint32_t)millis());
      id(oq_otb_startup_probe_active) = true;
      id(oq_boiler_connection_mismatch_state) = false;
      id(oq_boiler_connection_mismatch).publish_state(false);
      id(boiler_relay).turn_off();
      id(oq_otb_hub).set_no_response_expected(true);
      ESP_LOGI("quatt.boiler", "Verifying boiler OpenTherm connection before enabling R1");
      id(oq_otb_hub)
          .start_priority_polling(esphome::opentherm::MessageId::STATUS, esphome::opentherm::MessageId::CH_SETPOINT);
    }
  }
  reset_link_state();
}

inline void source_presence_changed(bool source_present) {
  if (!source_present) {
    enter_dormant_state();
    ESP_LOGI("quatt.boiler", "Auxiliary heat source not connected; OpenTherm boiler polling disabled");
    return;
  }

  const bool opentherm_selected =
      id(oq_boiler_connection).has_state() && id(oq_boiler_connection).current_option() == "OpenTherm";
  connection_changed(opentherm_selected);
}

inline void apply_dhw_permission(bool opentherm_selected) {
  const bool thermostat_status_valid = id(oq_ot_slave_enabled).state && id(ot_thermostat_status_valid).has_state() &&
                                       id(ot_thermostat_status_valid).state;
  const bool thermostat_dhw_enabled = id(ot_thermostat_dhw_enable).has_state() && id(ot_thermostat_dhw_enable).state;
  const bool dhw_permission = oq_boiler_transport::compute_otb_dhw_permission(
      opentherm_selected, thermostat_status_valid, thermostat_dhw_enabled);
  if (id(oq_otb_dhw_enable).state == dhw_permission) return;

  if (dhw_permission) {
    id(oq_otb_dhw_enable).turn_on();
  } else {
    id(oq_otb_dhw_enable).turn_off();
  }
  // Do not truncate an active request/response exchange. The updated STATUS
  // frame is sent next, before non-control telemetry.
  id(oq_otb_hub)
      .defer_priority_messages(esphome::opentherm::MessageId::STATUS, esphome::opentherm::MessageId::CH_SETPOINT);
}

inline void apply_command(float minimum_flow_lph, uint32_t status_timeout_ms) {
  const bool opentherm_selected = id(oq_aux_heat_source_present).state && id(oq_boiler_connection).has_state() &&
                                  id(oq_boiler_connection).current_option() == "OpenTherm";
  apply_dhw_permission(opentherm_selected);
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

inline void link_watch(uint32_t link_timeout_ms, uint32_t field_timeout_ms, uint32_t verification_timeout_ms) {
  if (!id(oq_aux_heat_source_present).state) {
    if (id(oq_otb_hub_ready) && id(oq_otb_hub).is_polling_enabled()) {
      id(oq_otb_hub).suspend_polling();
    }
    oq_otb::connection_verification_state.reset();
    if (id(oq_otb_link_available_state)) reset_link_state();
    return;
  }

  const uint32_t now_ms = (uint32_t)millis();
  oq_otb::telemetry_state.expire_response_session_if_stale(now_ms, link_timeout_ms);

  const bool connection_state_changed =
      oq_otb::connection_verification_state.update_opentherm(now_ms, verification_timeout_ms, link_timeout_ms);
  if (connection_state_changed) {
    const auto connection_state = oq_otb::connection_verification_state.state();
    if (connection_state == oq_otb::BOILER_CONNECTION_OT_NO_RESPONSE) {
      ESP_LOGW("quatt.otb", "OpenTherm connection not verified: no correlated boiler response received");
    } else if (connection_state == oq_otb::BOILER_CONNECTION_OT_LINK_LOST) {
      ESP_LOGW("quatt.otb", "OpenTherm connection lost after earlier verification");
    }
  }

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
    const uint8_t unavailable_reason = oq_boiler::refine_opentherm_transport_block_reason(
        oq_boiler::BLOCK_TRANSPORT_UNAVAILABLE, true, oq_otb::connection_verification_state.ever_verified(),
        oq_otb::connection_verification_state.currently_verified());
    oq_boiler_runtime::runtime().selected_transport_link_changed(available, unavailable_reason);
    id(oq_otb_ch_enable).turn_off();
    if (!id(oq_otb_t_set_command).has_state() || fabsf(id(oq_otb_t_set_command).state) >= 0.05f) {
      auto call = id(oq_otb_t_set_command).make_call();
      call.set_value(0.0f);
      call.perform();
    }
    if (available) {
      // Session-scoped init-only fields (e.g. ID15 max capacity/min modulation)
      // are cleared on a real link timeout but never re-polled by the repeating
      // sequence. Re-run the initial message sequence on recovery so they come
      // back without requiring a reboot or new session.
      id(oq_otb_hub).resume_polling();
    }
  }
  if (!available) id(oq_otb_invalidate_telemetry).execute();
}

}  // namespace oq_boiler_otb_runtime
#endif
