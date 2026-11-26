#pragma once

#include "oq_boiler_control_logic.h"
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
#include "../boiler/oq_otb_connection_state.h"
#include "../boiler/oq_otb_telemetry.h"
#include "oq_boiler_cm3_opentherm.h"
#endif

namespace oq_boiler_runtime {

struct Config {
  bool opentherm_supported = false;
  float minimum_flow_lph = 250.0f;
  uint32_t command_max_age_ms = 15000UL;
  uint32_t transport_settle_ms = 1000UL;
  uint32_t relay_min_on_ms = 0;
  uint32_t relay_min_off_ms = 0;
  uint32_t otb_min_on_ms = 0;
  uint32_t otb_min_off_ms = 0;
  uint32_t otb_field_timeout_ms = 0;
  oq_boiler::RelayTargetConfig relay_target = {};
};

class Runtime {
 public:
  void shutdown() {
    reset_cm3_opentherm_();
    id(oq_boiler_command_valid) = false;
    id(oq_boiler_command_heat_request) = false;
    id(oq_boiler_output_request) = false;
    id(oq_boiler_output_target_temperature_c) = NAN;
    id(oq_boiler_transport_active) = false;
    id(oq_boiler_block_reason_code) = oq_boiler::BLOCK_COMMAND_INVALID;
    id(oq_boiler_start_thermal_state_code) = oq_boiler::BOILER_START_THERMAL_IDLE;
    id(oq_boiler_start_thermal_safe_ceiling_c) = NAN;
    id(oq_boiler_relay_target_state_code) = oq_boiler::RELAY_TARGET_NOT_APPLICABLE;
    id(boiler_relay).turn_off();
  }

  bool connection_changed(bool opentherm_selected, bool opentherm_supported) {
    if (opentherm_selected && !opentherm_supported) {
      ESP_LOGW("quatt.boiler", "OpenTherm boiler transport is unsupported on this hardware; restoring R1");
      return false;
    }
    const uint32_t now_ms = (uint32_t)millis();
    id(oq_boiler_transport_change_ms) = now_ms;
    id(oq_boiler_transport_settle_required) = true;
    id(oq_boiler_rearm_required) = true;
    id(oq_boiler_rearm_after_command_ms) = now_ms;
    withdraw_output_(now_ms);
    id(boiler_relay).turn_off();
    return true;
  }

  bool runtime_pause_changed(bool paused) {
    if (paused == id(oq_boiler_runtime_pause_state)) return false;
    const uint32_t now_ms = (uint32_t)millis();
    id(oq_boiler_runtime_pause_state) = paused;
    id(oq_boiler_rearm_required) = true;
    id(oq_boiler_rearm_after_command_ms) = now_ms;
    withdraw_output_(now_ms);
    id(oq_boiler_block_reason_code) =
        paused ? oq_boiler::BLOCK_TRANSPORT_UNAVAILABLE : oq_boiler::BLOCK_AWAITING_FRESH_COMMAND;
    id(boiler_relay).turn_off();
    return true;
  }

  void selected_transport_link_changed(bool available,
                                       uint8_t unavailable_reason = oq_boiler::BLOCK_TRANSPORT_UNAVAILABLE) {
    const uint32_t now_ms = (uint32_t)millis();
    id(oq_boiler_transport_change_ms) = now_ms;
    id(oq_boiler_transport_settle_required) = true;
    id(oq_boiler_rearm_required) = true;
    id(oq_boiler_rearm_after_command_ms) = now_ms;
    withdraw_output_(now_ms);
    id(oq_boiler_block_reason_code) =
        available ? static_cast<uint8_t>(oq_boiler::BLOCK_AWAITING_FRESH_COMMAND) : unavailable_reason;
  }

  bool command_is_fresh(uint32_t maximum_age_ms) const {
    return oq_boiler::command_is_fresh(command_(), (uint32_t)millis(), maximum_age_ms);
  }

  void tick(const Config& config, [[maybe_unused]] float boiler_inlet_c, [[maybe_unused]] float water_cp_j_per_kgk) {
    const uint32_t now_ms = (uint32_t)millis();
    const int control_mode = id(oq_control_mode_code);
    const float supply_c = id(water_supply_temp_selected).state;
    const bool supply_valid = !isnan(supply_c);
    const bool opentherm_selected =
        id(oq_boiler_connection).has_state() && id(oq_boiler_connection).current_option() == "OpenTherm";
    const bool runtime_available = !id(oq_runtime_polling_paused).state && !id(oq_boiler_runtime_pause_state);
    const bool transport_available = oq_boiler::transport_available_for_selection(
        runtime_available, opentherm_selected, config.opentherm_supported, id(oq_otb_link_available_state),
        id(oq_otb_startup_probe_active), id(oq_boiler_connection_mismatch_state));
    const bool transport_settled = oq_boiler::settle_period_elapsed(
        id(oq_boiler_transport_settle_required), now_ms, id(oq_boiler_transport_change_ms), config.transport_settle_ms);
    if (transport_settled) id(oq_boiler_transport_settle_required) = false;

    const auto command = command_();
    const float target_c = command.target_temperature_c;
    const float flow_lph = id(flow_rate_selected).state;
    const bool flow_valid = !isnan(flow_lph);
    const bool flow_sufficient = flow_valid && flow_lph >= config.minimum_flow_lph && !id(oq_lowflow_fault_active);
    const bool target_valid = !isnan(target_c) && target_c > 0.0f && target_c <= 90.0f && flow_sufficient;
    // Same degraded gate as the supervisor: confirmed stops pass, and
    // STOP_UNCONFIRMED after a confirmed link-loss timeout passes. Both call
    // sites must stay in sync so CM4 selection and the physical boiler output
    // cannot disagree.
    const bool fallback_outputs_safe = id(oq_incident_manager).all_fallback_output_gates_clear();
    const auto thermal = start_thermal_decision_(command, opentherm_selected, supply_c, target_c, now_ms, config);
    publish_start_thermal_decision_(thermal, command, supply_c, target_c);

    const bool command_rearmed =
        oq_boiler::command_satisfies_rearm(id(oq_boiler_rearm_required), command, id(oq_boiler_rearm_after_command_ms));
    if (command_rearmed) id(oq_boiler_rearm_required) = false;
    // The R1 relay has no target channel, so the target temperature the command
    // already carries is realised by regulating the binary output around it.
    // OpenTherm keeps receiving the target over the bus and is unaffected.
    const auto relay_target = oq_boiler::evaluate_relay_target(
        {oq_boiler::relay_target_control_applies(command.source, opentherm_selected), command.heat_request,
         id(oq_boiler_output_request), supply_c, target_c, config.relay_target});
    publish_relay_target_state_(relay_target, supply_c, target_c);
    const uint32_t minimum_on_ms = opentherm_selected ? config.otb_min_on_ms : config.relay_min_on_ms;
    const uint32_t minimum_off_ms = opentherm_selected ? config.otb_min_off_ms : config.relay_min_off_ms;
    const oq_boiler::ControllerInput input{
        id(oq_aux_heat_source_present).state,
        id(oq_boiler_assist_enabled).state,
        id(oq_boiler_fault_fallback_enabled).state,
        supply_valid,
        flow_valid,
        flow_sufficient,
        fallback_outputs_safe,
        id(oq_water_temp_boiler_inhibit_active),
        id(oq_water_temp_hard_trip_active),
        id(oq_boiler_connection_mismatch_state),
        transport_available,
        transport_settled,
        command_rearmed,
        thermal.state,
        opentherm_selected,
        target_valid,
        relay_target,
        id(oq_boiler_output_request),
        now_ms,
        config.command_max_age_ms,
        id(oq_boiler_output_last_change_ms),
        minimum_on_ms,
        minimum_off_ms,
    };
    auto decision = oq_boiler::evaluate(command, input);
    float output_target_c =
        oq_boiler::effective_output_target(decision.output_active, opentherm_selected, decision.desired_active,
                                           target_valid, target_c, id(oq_boiler_output_target_temperature_c));
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    const bool power_house_command = opentherm_selected && command.source == oq_boiler::COMMAND_SOURCE_POWER_HOUSE;
    const bool cm3_context = power_house_command && control_mode == 3 && id(oq_strategy_active_code) == 3 &&
                             !id(oq_cold_start_assist_active);
    const auto cm3_input = cm3_context ? cm3_inputs_(config, command, decision, now_ms, flow_lph, flow_sufficient,
                                                     boiler_inlet_c, water_cp_j_per_kgk, thermal.safe_ceiling_c)
                                       : oq_boiler_cm3_opentherm::Inputs{};
    oq_boiler_cm3_opentherm::apply(cm3_opentherm_, cm3_context, cm3_input, decision, output_target_c);
    decision.block_reason = oq_boiler::refine_opentherm_transport_block_reason(
        decision.block_reason, opentherm_selected, oq_otb::connection_verification_state.ever_verified(),
        oq_otb::connection_verification_state.currently_verified());
#endif
    const auto role = oq_boiler::boiler_role_for_source(command.source);
    const bool output_changed = id(oq_boiler_output_request) != decision.output_active;
    id(oq_boiler_output_request) = decision.output_active;
    if (output_changed) id(oq_boiler_output_last_change_ms) = now_ms;
    id(oq_boiler_output_target_temperature_c) = output_target_c;
    id(oq_boiler_block_reason_code) = decision.block_reason;

    const auto controller_log = oq_boiler::classify_boiler_controller_log({role, decision.block_reason}, log_codes_());
    publish_controller_log_(control_mode, supply_c, supply_valid, decision, role, controller_log);
    const auto transition = output_controller_.advance({role, decision.output_active});
    publish_role_transition_(transition, control_mode, supply_c, supply_valid, fallback_outputs_safe, controller_log);
    id(oq_incident_manager)
        .set_boiler_status(static_cast<uint8_t>(output_controller_.role()), output_controller_.heat_enable(),
                           transition.assist_fallback_handover_without_output_edge);
    apply_relay_(opentherm_selected, decision.output_active);
  }

 private:
  void reset_cm3_opentherm_() {
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    cm3_opentherm_ = {};
#endif
  }

#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
  static oq_boiler_cm3_opentherm::Inputs cm3_inputs_(const Config& config, const oq_boiler::BoilerCommand& command,
                                                     const oq_boiler::ControllerDecision& decision, uint32_t now_ms,
                                                     float flow_lph, bool flow_sufficient, float boiler_inlet_c,
                                                     float water_cp_j_per_kgk, float maximum_water_c) {
    const auto status = oq_otb::telemetry_state.field_receipt(oq_otb::FIELD_STATUS);
    const bool status_valid = status.valid && oq_otb::telemetry_state.field_is_fresh(oq_otb::FIELD_STATUS, now_ms,
                                                                                     config.otb_field_timeout_ms);
    const auto fresh_temperature = [&](oq_otb::Field field, float value) {
      return oq_otb::telemetry_state.field_is_fresh(field, now_ms, config.otb_field_timeout_ms) ? value : NAN;
    };
    const bool capacity_valid = oq_otb::telemetry_state.field_is_valid(oq_otb::FIELD_MAX_BOILER_CAPACITY);
    const float return_c = fresh_temperature(oq_otb::FIELD_RETURN_WATER_TEMPERATURE, id(otb_return_water_temp).state);
    const bool flow_loss_confirmed =
        id(oq_lowflow_fault_active) || (isfinite(flow_lph) && flow_lph >= 0.0f && flow_lph < config.minimum_flow_lph);
    bool defrost = id(hp1_defrost).state || id(hp1_4_way_valve).state || id(hp1_odu_defrost).cycle.observed_active();
#if OQ_TOPOLOGY_DUO
    defrost |= id(hp2_defrost).state || id(hp2_4_way_valve).state || id(hp2_odu_defrost).cycle.observed_active();
#endif
    return {
        now_ms,
        command.updated_at_ms,
        decision.desired_active && !decision.force_off,
        decision.output_active,
        id(oq_otb_applied_command_active),
        status_valid,
        (status.value & 0x0008U) != 0,
        (status.value & 0x0004U) != 0,
        (status.value & 0x0001U) != 0,
        defrost,
        flow_sufficient,
        flow_loss_confirmed,
        capacity_valid ? id(otb_max_capacity).state : NAN,
        capacity_valid ? id(otb_min_modulation).state : NAN,
        command.requested_power_w,
        isfinite(return_c) ? return_c : boiler_inlet_c,
        fresh_temperature(oq_otb::FIELD_BOILER_WATER_TEMPERATURE, id(otb_boiler_water_temp).state),
        flow_lph,
        water_cp_j_per_kgk,
        maximum_water_c,
        id(room_temp_selected).state,
        id(room_setpoint_selected).state,
    };
  }

  oq_boiler_cm3_opentherm::Control cm3_opentherm_;
#endif

  static oq_boiler::BoilerCommand command_() {
    return {
        id(oq_boiler_command_valid),
        id(oq_boiler_command_demand_present),
        id(oq_boiler_command_heat_request),
        id(oq_boiler_command_requested_power_w),
        id(oq_boiler_command_target_temperature_c),
        id(oq_boiler_command_source_code),
        id(oq_boiler_command_updated_ms),
    };
  }

  void withdraw_output_(uint32_t now_ms) {
    reset_cm3_opentherm_();
    if (id(oq_boiler_output_request)) {
      id(oq_boiler_output_request) = false;
      id(oq_boiler_output_last_change_ms) = now_ms;
    }
    id(oq_boiler_output_target_temperature_c) = NAN;
    id(oq_boiler_transport_active) = false;
  }

  static oq_boiler::BoilerStartThermalDecision start_thermal_decision_(const oq_boiler::BoilerCommand& command,
                                                                       bool opentherm_selected, float supply_c,
                                                                       float target_c, uint32_t now_ms,
                                                                       const Config& config) {
    const bool commissioning = command.source == oq_boiler::COMMAND_SOURCE_COMMISSIONING;
    if (!command.heat_request && !commissioning) return {};
    float maximum_c = id(max_water_temp_limit_c).state;
    if (isnan(maximum_c)) maximum_c = 60.0f;
    maximum_c = fmaxf(25.0f, fminf(maximum_c, 75.0f));
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    const bool boiler_temperature_fresh =
        opentherm_selected && oq_otb::telemetry_state.field_is_fresh(oq_otb::FIELD_BOILER_WATER_TEMPERATURE, now_ms,
                                                                     config.otb_field_timeout_ms);
    const float boiler_temperature_c = id(otb_boiler_water_temp).has_state() ? id(otb_boiler_water_temp).state : NAN;
#else
    const bool boiler_temperature_fresh = false;
    const float boiler_temperature_c = NAN;
#endif
    if (!commissioning) {
      return oq_boiler::evaluate_boiler_start_water_limit(opentherm_selected, boiler_temperature_fresh,
                                                          boiler_temperature_c, maximum_c);
    }
    return oq_boiler::evaluate_boiler_start_thermal_state(opentherm_selected, boiler_temperature_fresh,
                                                          boiler_temperature_c, supply_c, target_c, maximum_c);
  }

  static float boiler_temperature_() {
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    return id(otb_boiler_water_temp).has_state() ? id(otb_boiler_water_temp).state : NAN;
#else
    return NAN;
#endif
  }

  static void publish_start_thermal_decision_(const oq_boiler::BoilerStartThermalDecision& thermal,
                                              const oq_boiler::BoilerCommand& command, float supply_c, float target_c) {
    const bool changed = id(oq_boiler_start_thermal_state_code) != thermal.state;
    id(oq_boiler_start_thermal_state_code) = thermal.state;
    id(oq_boiler_start_thermal_safe_ceiling_c) = thermal.safe_ceiling_c;
    if (!changed) return;
    ESP_LOGI("quatt.boiler", "Warm-start guard: %s (boiler=%.1fC supply=%.1fC target=%.1fC ceiling=%.1fC source=%u)",
             oq_boiler::boiler_start_thermal_state_text(thermal.state), boiler_temperature_(), supply_c, target_c,
             thermal.safe_ceiling_c, (unsigned int)command.source);
  }

  // Target control is a normal part of R1 operation, so its state is published
  // for the debug recording instead of a new public entity. It answers whether
  // a requested-but-inactive relay is target-satisfied or blocked by a guard.
  static void publish_relay_target_state_(const oq_boiler::RelayTargetDecision& decision, float supply_c,
                                          float target_c) {
    if (id(oq_boiler_relay_target_state_code) == decision.state) return;
    id(oq_boiler_relay_target_state_code) = decision.state;
    if (decision.state == oq_boiler::RELAY_TARGET_NOT_APPLICABLE || decision.state == oq_boiler::RELAY_TARGET_IDLE) {
      return;
    }
    ESP_LOGI("quatt.boiler", "R1 target control: %s (supply=%.1fC target=%.1fC)",
             oq_boiler::relay_target_state_text(decision.state), supply_c, target_c);
  }

  static const oq_boiler::BoilerLogCodes& log_codes_() {
    static const oq_boiler::BoilerLogCodes value{
        {
            openquatt_decision_log::REASON_LESS_POWER,
            openquatt_decision_log::REASON_FLOW_TOO_LOW,
            openquatt_decision_log::REASON_SOFT_GUARD,
            openquatt_decision_log::REASON_SENSOR_FALLBACK,
            openquatt_decision_log::REASON_NO_CANDIDATE,
            openquatt_decision_log::REASON_FALLBACK_BLOCKED,
            openquatt_decision_log::REASON_HP_STOP_UNCONFIRMED,
            openquatt_decision_log::REASON_FLOW_PREFLOW,
            openquatt_decision_log::REASON_BOILER_FALLBACK,
            openquatt_decision_log::REASON_HP_RECOVERED,
            openquatt_decision_log::REASON_COMMISSIONING,
            openquatt_decision_log::REASON_COOLING_REQUEST,
            openquatt_decision_log::REASON_FROST_PROTECTION,
            openquatt_decision_log::REASON_HEATING_REQUEST_CLEARED,
            openquatt_decision_log::REASON_MIN_REST_ACTIVE,
        },
        {openquatt_decision_log::SEVERITY_NORMAL, openquatt_decision_log::SEVERITY_LIMITED},
    };
    return value;
  }

  void publish_controller_log_(int control_mode, float supply_c, bool supply_valid,
                               const oq_boiler::ControllerDecision& decision, oq_boiler::BoilerRole role,
                               const oq_boiler::BoilerLogDecision& controller_log) {
    // Use the controller's own verdict. Recomputing it here would re-report a
    // satisfied target as a blocked boiler, which is a normal control stop and
    // not a fault.
    const bool blocked = decision.blocked;
    const bool became_blocked = blocked && !last_blocked_;
    const bool reason_changed = !have_controller_state_ || decision.block_reason != last_block_reason_;
    if (!have_controller_state_ || decision.output_active != last_allowed_ || reason_changed || became_blocked) {
      const char* reason = oq_boiler::block_reason_text(decision.block_reason);
      if (decision.output_active) {
        const char* context = role == oq_boiler::BoilerRole::COMMISSIONING_CM100 ? "CM100 commissioning task"
                              : role == oq_boiler::BoilerRole::FALLBACK_CM4      ? "CM4 fallback"
                                                                                 : "CM3";
        ESP_LOGI("quatt.boiler", "Boiler enabled: %s and safety guards clear", context);
      } else if (id(oq_water_temp_hard_trip_active)) {
        ESP_LOGW("quatt.boiler", "Boiler blocked: %s", reason);
      } else if (blocked && decision.block_reason == oq_boiler::BLOCK_COMMISSIONING_WAITING) {
        ESP_LOGI("quatt.boiler", "Boiler commissioning: waiting for flow to settle");
      } else if (blocked) {
        ESP_LOGI("quatt.boiler", "Boiler blocked: %s", reason);
      }
    }

    if (blocked && (!last_blocked_ || reason_changed)) {
      id(oq_decision_log)
          .emit(openquatt_decision_log::EVENT_DECISION_BLOCKED, openquatt_decision_log::SUBJECT_CV,
                controller_log.reason_code, controller_log.severity, (uint8_t)control_mode,
                openquatt_decision_log::STATE_STANDBY, openquatt_decision_log::STATE_BLOCKED,
                supply_valid ? (int16_t)lroundf(supply_c * 10.0f) : 0);
    }
    last_allowed_ = decision.output_active;
    last_blocked_ = blocked;
    last_block_reason_ = decision.block_reason;
    have_controller_state_ = true;
  }

  static void emit_role_start_(oq_boiler::BoilerRole role, bool continuous, int control_mode, float supply_c,
                               bool supply_valid) {
    const bool fallback = role == oq_boiler::BoilerRole::FALLBACK_CM4;
    id(oq_decision_log)
        .emit(fallback ? openquatt_decision_log::EVENT_BOILER_FALLBACK_START
                       : openquatt_decision_log::EVENT_BOILER_ASSIST_START,
              openquatt_decision_log::SUBJECT_CV,
              fallback ? openquatt_decision_log::REASON_BOILER_FALLBACK : openquatt_decision_log::REASON_BOILER_ASSIST,
              fallback ? openquatt_decision_log::SEVERITY_ATTENTION : openquatt_decision_log::SEVERITY_NORMAL,
              (uint8_t)control_mode, openquatt_decision_log::STATE_STANDBY,
              fallback ? openquatt_decision_log::STATE_FALLBACK : openquatt_decision_log::STATE_ACTIVE,
              supply_valid ? (int16_t)lroundf(supply_c * 10.0f) : 0, continuous ? 1 : 0);
  }

  static void emit_role_stop_(oq_boiler::BoilerRole role, bool continuous, int control_mode, float supply_c,
                              bool supply_valid, bool fallback_outputs_safe, oq_boiler::BoilerLogReason guard_reason) {
    const bool fallback = role == oq_boiler::BoilerRole::FALLBACK_CM4;
    const auto stop_log = oq_boiler::classify_boiler_stop_log(
        {role, continuous, control_mode, id(oq_strategy_heat_request_active),
         id(oq_boiler_fault_fallback_enabled).state, fallback_outputs_safe, guard_reason},
        log_codes_());
    id(oq_decision_log)
        .emit(fallback ? openquatt_decision_log::EVENT_BOILER_FALLBACK_STOP
                       : openquatt_decision_log::EVENT_BOILER_ASSIST_STOP,
              openquatt_decision_log::SUBJECT_CV, stop_log.reason_code, stop_log.severity, (uint8_t)control_mode,
              fallback ? openquatt_decision_log::STATE_FALLBACK : openquatt_decision_log::STATE_ACTIVE,
              openquatt_decision_log::STATE_STANDBY, supply_valid ? (int16_t)lroundf(supply_c * 10.0f) : 0,
              continuous ? 1 : 0);
  }

  static void publish_role_transition_(const oq_boiler::BoilerOutputTransition& transition, int control_mode,
                                       float supply_c, bool supply_valid, bool fallback_outputs_safe,
                                       const oq_boiler::BoilerLogDecision& controller_log) {
    if (transition.assist_fallback_handover_without_output_edge) {
      emit_role_stop_(transition.previous_role, true, control_mode, supply_c, supply_valid, fallback_outputs_safe,
                      controller_log.reason);
      emit_role_start_(transition.next_role, true, control_mode, supply_c, supply_valid);
    } else if (transition.output_action == oq_boiler::BoilerOutputAction::ENABLE) {
      emit_role_start_(transition.next_role, false, control_mode, supply_c, supply_valid);
    } else if (transition.output_action == oq_boiler::BoilerOutputAction::DISABLE) {
      emit_role_stop_(transition.previous_role, false, control_mode, supply_c, supply_valid, fallback_outputs_safe,
                      controller_log.reason);
    }
  }

  static void apply_relay_(bool opentherm_selected, bool output_active) {
    if (oq_boiler::relay_must_be_off(opentherm_selected, id(oq_otb_startup_probe_active),
                                     id(oq_boiler_connection_mismatch_state)) ||
        !output_active) {
      id(boiler_relay).turn_off();
    } else {
      id(boiler_relay).turn_on();
    }
    if (!opentherm_selected) id(oq_boiler_transport_active) = id(boiler_relay).state;
  }

  oq_boiler::BoilerOutputController output_controller_;
  bool have_controller_state_ = false;
  bool last_allowed_ = false;
  bool last_blocked_ = false;
  uint8_t last_block_reason_ = oq_boiler::BLOCK_NONE;
};

inline Runtime& runtime() {
  static Runtime value;
  return value;
}

}  // namespace oq_boiler_runtime
