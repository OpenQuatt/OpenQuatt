#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "../performance/hp_perf_frequency.h"
#include "../service/oq_service_logic.h"
#include "../service/tasks/oq_manual_hp_logic.h"
#include "oq_cold_start_probe.h"
#include "oq_supervisory_state_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_supervisory_state_runtime {

struct TickConfig {
  uint32_t loop_s;
  float duo_current_limit_v1_a;
  float duo_current_limit_v2_a;
  float duo_current_limit_v2_max_a;
  float electrical_current_limit_min_a;
  float mains_voltage_v;
  uint32_t power_peak_trip_s;
  uint32_t power_soft_trip_s;
  uint32_t power_recover_s;
  uint32_t power_measurement_stale_s;
  int power_cap_nan_f;
  int power_cap_max_f;
  uint32_t cm_prepost_s;
  float cm_min_flow_lph;
  float low_load_fallback_off_w;
  float low_load_fallback_on_w;
  uint32_t low_load_dyn_cache_max_s;
  uint32_t ph_start_confirm_s;
  uint32_t cm2_idle_exit_s;
  uint32_t cm2_min_run_s;
  uint32_t cm_flow_fault_s;
  uint32_t cm_flow_recover_s;
  float cm_frost_on_c;
  float cm_frost_off_c;
  uint32_t cm_frost_nan_grace_s;
  float hp_cold_start_min_c;
  float hp_cold_start_assist_release_c;
  uint32_t cm_override_max_s;
  bool otb_supported;
  uint32_t boiler_transport_settle_s;
  float cm3_curve_on_delta_c;
  float cm3_curve_off_delta_c;
  uint32_t cm3_promote_s;
  uint32_t cm3_demote_s;
  uint32_t cm3_min_run_s;
  uint32_t cm0_sticky_wait_s;
  uint32_t cm0_sticky_run_s;
  float cm0_pump_stop_ipwm;
  float sticky_pwm;
};

class Runtime {
 public:
  void tick(const TickConfig& tick) {
    // -------------------------------------------------
    // Main phases:
    // 1) Power limiter (safety net on total input power)
    // 2) Flow interlock + frost detection
    // 3) resolve_desired_cm() (override + CM1 timers + CM3 promote/demote)
    // 4) apply_silent_window() (diagnostics + low-noise mode)
    // 5) apply_sticky_pump_policy() (CM0 sticky + pump/PWM ownership)
    // -------------------------------------------------
    const uint32_t now_ms = (uint32_t)millis();

    auto now_time = id(oq_time).now();
    const bool time_valid = now_time.is_valid();
    const auto openquatt_resume_at = id(oq_openquatt_resume_at).state_as_esptime();
    const bool openquatt_resume_scheduled = openquatt_resume_at.year >= 2024 &&
                                            openquatt_resume_at.fields_in_range(false, false) &&
                                            openquatt_resume_at.timestamp != -1;

    if (!id(oq_enabled).state && time_valid && openquatt_resume_scheduled && now_time >= openquatt_resume_at) {
      id(oq_enabled).turn_on();
    }
    oq_supervisory_power_runtime::runtime().tick(
        {now_ms, tick.loop_s, tick.duo_current_limit_v1_a, tick.duo_current_limit_v2_a, tick.duo_current_limit_v2_max_a,
         tick.electrical_current_limit_min_a, tick.mains_voltage_v, tick.power_peak_trip_s, tick.power_soft_trip_s,
         tick.power_recover_s, tick.power_measurement_stale_s, tick.power_cap_nan_f, tick.power_cap_max_f});

    const uint32_t prepost_ms = (uint32_t)(tick.cm_prepost_s * 1000UL);
    const float min_flow_lph = tick.cm_min_flow_lph;
    // -------------------------------------------------
    // Helpers: minimize Modbus writes (write-on-change)
    // -------------------------------------------------
    auto set_select_option = [&](auto& sel, const char* opt) {
      if (!sel.has_state() || sel.current_option() != opt) {
        auto c = sel.make_call();
        c.set_option(opt);
        c.perform();
      }
    };

    auto set_number_value = [&](auto& num, float value, float tol) {
      const float cur = num.state;
      if (isnan(cur) || fabsf(cur - value) > tol) {
        auto c = num.make_call();
        c.set_value(value);
        c.perform();
      }
    };
    auto publish_binary_if_changed = [](auto& bs, bool value) {
      if (!bs.has_state() || bs.state != value) bs.publish_state(value);
    };
    auto publish_text_if_changed = [](auto& ts, const std::string& value, std::string& last_value) {
      if (value != last_value) {
        ts.publish_state(value);
        last_value = value;
      }
    };
    auto cm_code_to_id = [](int cm) -> const char* {
      if (cm == 100) return "CM100";
      if (cm == 98) return "CM98";
      if (cm == 5) return "CM5";
      if (cm == 4) return "CM4";
      if (cm == 3) return "CM3";
      if (cm == 2) return "CM2";
      if (cm == 1) return "CM1";
      return "CM0";
    };
    auto cm_id_to_code = [](const std::string& cm) -> int {
      if (cm == "CM100") return 100;
      if (cm == "CM98") return 98;
      if (cm == "CM5") return 5;
      if (cm == "CM4") return 4;
      if (cm == "CM3") return 3;
      if (cm == "CM2") return 2;
      if (cm == "CM1") return 1;
      return 0;
    };
    auto cm_code_to_label = [&](int cm) -> std::string {
      const char* cm_id = cm_code_to_id(cm);
      if (strcmp(cm_id, "CM100") == 0) return std::string("CM100 - Commissioning");
      if (strcmp(cm_id, "CM0") == 0) return std::string("CM0 - Standby");
      if (strcmp(cm_id, "CM1") == 0) return std::string("CM1 - Preflow/Postflow");
      if (strcmp(cm_id, "CM2") == 0) return std::string("CM2 - Heating - Heat Pump Only");
      if (strcmp(cm_id, "CM3") == 0) return std::string("CM3 - Heating - Heat Pump + Boiler");
      if (strcmp(cm_id, "CM4") == 0) return std::string("CM4 - Heating - Boiler Fallback");
      if (strcmp(cm_id, "CM5") == 0) return std::string("CM5 - Cooling");
      if (strcmp(cm_id, "CM98") == 0) return std::string("CM98 - Anti-Freeze Protection - Water Circulation");
      return std::string("Unknown");
    };
    auto ms_window_active = [&](uint32_t until_ms) -> bool {
      if (until_ms == 0) return false;
      return static_cast<uint32_t>(until_ms - now_ms) < 0x80000000UL;
    };

    // -------------------------------------------------
    // 1) Thermal demand
    //    Baseline heating demand: demand_filtered > 0
    //    Baseline cooling demand: selected enable + request + permit
    //    CM2 idle-exit: if both HPs are commanded/measured idle
    //    for a while, force demand false to allow CM2->CM1->CM0.
    // -------------------------------------------------
    const char* cur_cm_state = id(oq_control_mode).state.c_str();
    const bool in_cm2 = strcmp(cur_cm_state, "CM2") == 0;
    const bool openquatt_enabled = id(oq_enabled).state;
    const int strategy_active_code = id(oq_strategy_active_code);
    const bool heating_strategy_active = (strategy_active_code == 2 || strategy_active_code == 3);
    const bool heating_req_raw = heating_strategy_active && id(oq_strategy_heat_request_active);
    const bool cooling_req_raw = id(cooling_enable_selected).has_state() && id(cooling_enable_selected).state &&
                                 id(cooling_request_active).has_state() && id(cooling_request_active).state &&
                                 id(cooling_permitted_core).has_state() && id(cooling_permitted_core).state;
    const bool cooling_req = cooling_req_raw && openquatt_enabled;
    const bool power_house_active = (strategy_active_code == 3);
    const bool heating_curve_active = (strategy_active_code == 2);

    const int hp1_lvl = id(hp1_last_applied_level);
    const int hp1_cmd_lvl =
        id(hp1_compressor_level).has_state() ? (int)id(hp1_compressor_level).active_index().value_or(-1) : -1;
#if OQ_TOPOLOGY_DUO
    const int hp2_lvl = id(hp2_last_applied_level);
    const int hp2_cmd_lvl =
        id(hp2_compressor_level).has_state() ? (int)id(hp2_compressor_level).active_index().value_or(-1) : -1;
    const bool both_levels_off = (hp1_lvl <= 0) && (hp2_lvl <= 0);

    const float hp1_mode_raw = id(hp1_working_mode).state;
    const float hp2_mode_raw = id(hp2_working_mode).state;
    const bool hp1_mode_valid = !isnan(hp1_mode_raw);
    const bool hp2_mode_valid = !isnan(hp2_mode_raw);
    const bool hp1_heating = hp1_mode_valid && ((int)roundf(hp1_mode_raw) == 2);
    const bool hp2_heating = hp2_mode_valid && ((int)roundf(hp2_mode_raw) == 2);
    const bool hp1_cooling = hp1_mode_valid && ((int)roundf(hp1_mode_raw) == 1);
    const bool hp2_cooling = hp2_mode_valid && ((int)roundf(hp2_mode_raw) == 1);
    const bool hp1_target_heating =
        id(hp1_set_working_mode).has_state() && id(hp1_set_working_mode).current_option() == "Heating";
    const bool hp2_target_heating =
        id(hp2_set_working_mode).has_state() && id(hp2_set_working_mode).current_option() == "Heating";
    const bool hp1_target_cooling =
        id(hp1_set_working_mode).has_state() && id(hp1_set_working_mode).current_option() == "Cooling";
    const bool hp2_target_cooling =
        id(hp2_set_working_mode).has_state() && id(hp2_set_working_mode).current_option() == "Cooling";
    const bool hp1_active_guard =
        hp1_heating || hp1_cooling || hp1_target_heating || hp1_target_cooling || (hp1_lvl > 0);
    const bool hp2_active_guard =
        hp2_heating || hp2_cooling || hp2_target_heating || hp2_target_cooling || (hp2_lvl > 0);
    const bool any_hp_active_guard = hp1_active_guard || hp2_active_guard;
    const bool any_hp_compressor_active = hp1_lvl > 0 || hp2_lvl > 0 || hp1_cmd_lvl > 0 || hp2_cmd_lvl > 0;
#else
    const int hp2_lvl = 0;
    const int hp2_cmd_lvl = -1;
    const bool both_levels_off = (hp1_lvl <= 0);

    const float hp1_mode_raw = id(hp1_working_mode).state;
    const bool hp1_mode_valid = !isnan(hp1_mode_raw);
    const bool hp1_heating = hp1_mode_valid && ((int)roundf(hp1_mode_raw) == 2);
    const bool hp1_cooling = hp1_mode_valid && ((int)roundf(hp1_mode_raw) == 1);
    const bool hp1_target_heating =
        id(hp1_set_working_mode).has_state() && id(hp1_set_working_mode).current_option() == "Heating";
    const bool hp1_target_cooling =
        id(hp1_set_working_mode).has_state() && id(hp1_set_working_mode).current_option() == "Cooling";
    const bool hp1_active_guard =
        hp1_heating || hp1_cooling || hp1_target_heating || hp1_target_cooling || (hp1_lvl > 0);
    const bool hp2_active_guard = false;
    const bool any_hp_active_guard = hp1_active_guard;
    const bool any_hp_compressor_active = hp1_lvl > 0 || hp1_cmd_lvl > 0;
#endif
    const bool both_units_idle = !any_hp_active_guard;

    const float p_req_w = power_house_active ? id(oq_strategy_requested_power_w) : NAN;
    const float outside_c = id(outside_temp_selected).state;
    const float supply_c = id(oq_system_supply_temp).state;
    float live_minimum_power_w = NAN;
    if (power_house_active && std::isfinite(outside_c) && std::isfinite(supply_c)) {
      live_minimum_power_w = oq_perf::interp_power_th_w_hz(oq_perf::model_frequency_hz(1), outside_c, supply_c);
      if (!std::isfinite(live_minimum_power_w) || live_minimum_power_w <= 0.0f) live_minimum_power_w = NAN;
    }
    const auto low_load = oq_supervisory_state::update_low_load(
        {
            now_ms,
            power_house_active,
            heating_req_raw,
            openquatt_enabled,
            p_req_w,
            live_minimum_power_w,
            id(oq_low_load_dyn_off_factor).state,
            id(oq_low_load_dyn_on_factor).state,
            id(oq_low_load_min_hysteresis_w).state,
            tick.low_load_fallback_off_w,
            tick.low_load_fallback_on_w,
            oq_supervisory_state::seconds_to_ms(tick.low_load_dyn_cache_max_s),
        },
        {
            id(oq_lowload_heat_latch),
            id(oq_low_load_pmin_dyn_lkg_w),
            id(oq_low_load_dyn_last_valid_ms),
            id(oq_cm2_reentry_block_until_ms),
        });
    id(oq_lowload_heat_latch) = low_load.state.heat_latched;
    id(oq_low_load_pmin_dyn_lkg_w) = low_load.state.cached_minimum_power_w;
    id(oq_low_load_dyn_last_valid_ms) = low_load.state.cached_minimum_power_updated_ms;
    id(oq_cm2_reentry_block_until_ms) = low_load.state.reentry_block_until_ms;
    id(oq_low_load_pmin_dyn_w) = low_load.effective_minimum_power_w;
    id(oq_low_load_off_dyn_w) = low_load.off_threshold_w;
    id(oq_low_load_on_dyn_w) = low_load.on_threshold_w;
    id(oq_low_load_dyn_source_state) = low_load.source_code;
    bool heating_req = low_load.heating_request;
    bool heating_preflow_req = false;
    const float low_load_off_w = low_load.off_threshold_w;
    bool reentry_block_active = low_load.reentry_block_active;
    if (!power_house_active) id(oq_ph_start_confirm_since_ms) = 0;

    const bool heating_enable_valid_now = id(heating_enable_valid).has_state() && id(heating_enable_valid).state;
    const bool heating_enable_selected_now =
        id(heating_enable_selected).has_state() && id(heating_enable_selected).state;
    heating_req = oq_hp_supervisory::apply_heating_enable_gate(heating_req, heating_enable_valid_now,
                                                               heating_enable_selected_now);

    // Power House startup confirmation:
    // from a non-heating state, demand must stay active briefly before we enter CM1/CM2.
    // This filters short-lived P_req spikes (for example sun on the outside sensor)
    // before they turn into a real compressor start.
    if (power_house_active) {
      const bool start_pending_scope = !in_cm2 && !any_hp_active_guard;
      const auto startup = oq_supervisory_state::power_house_start(
          now_ms, heating_req, start_pending_scope, id(oq_ph_fast_intent_code) != 0,
          oq_supervisory_state::seconds_to_ms(tick.ph_start_confirm_s),
          {id(oq_ph_start_confirm_since_ms) != 0, id(oq_ph_start_confirm_since_ms)});
      id(oq_ph_start_confirm_since_ms) = startup.state.timing ? startup.state.since_ms : 0;
      heating_preflow_req = startup.preflow_request;
      heating_req = startup.heating_request;
    }

    const bool curve_mode_active = (strategy_active_code == 2);
    const uint32_t cm2_startup_grace_ms = oq_supervisory_state::seconds_to_ms(tick.cm2_min_run_s);
    const bool cm2_startup_grace_active =
        in_cm2 && id(oq_cm2_entered_ms) != 0 && ((uint32_t)(now_ms - id(oq_cm2_entered_ms)) < cm2_startup_grace_ms);
    const bool ph_high_load_idle_exit_block =
        power_house_active && !isnan(p_req_w) && !isnan(low_load_off_w) && (p_req_w > low_load_off_w);
    const auto idle_exit = oq_supervisory_state::update_idle_exit(
        {
            now_ms,
            oq_supervisory_state::seconds_to_ms(tick.cm2_idle_exit_s),
            in_cm2,
            heating_req,
            curve_mode_active,
            both_levels_off,
            both_units_idle,
            cm2_startup_grace_active,
            ph_high_load_idle_exit_block,
        },
        {id(oq_cm2_idle_since_ms) != 0, id(oq_cm2_idle_since_ms)});
    id(oq_cm2_idle_since_ms) = idle_exit.state.timing ? idle_exit.state.since_ms : 0;
    const bool cm2_idle_exit_trip = idle_exit.trip;
    if (cm2_idle_exit_trip) heating_req = false;
    if (power_house_active) {
      const uint32_t reentry_block_ms = (uint32_t)lroundf(id(oq_low_load_reentry_block_s).state) * 1000UL;
      // Pad A behavior: set block only when CM2 idle-exit trips.
      if (cm2_idle_exit_trip && reentry_block_ms > 0) {
        id(oq_cm2_reentry_block_until_ms) = now_ms + reentry_block_ms;
        reentry_block_active = true;
      }
      // During active block, CM2 may not be re-entered from non-CM2 states.
      if (!in_cm2 && reentry_block_active) {
        heating_req = false;
        heating_preflow_req = false;
      }
    }
    const bool manual_hp_thermal_req =
        oq_manual_hp::owns_control() &&
        ((int)roundf(id(oq_manual_hp1_level).state) > 0 || (int)roundf(id(oq_manual_hp2_level).state) > 0 ||
         id(oq_actuator_hp1_req) > 0 || id(oq_actuator_hp2_req) > 0);
    const bool heating_flow_req = heating_req || heating_preflow_req;
    const bool thermal_req = heating_flow_req || cooling_req || manual_hp_thermal_req;

    // -------------------------------------------------
    // 2) Flow interlock status + timers
    // -------------------------------------------------
    const auto safety = oq_supervisory_safety_runtime::runtime().tick(
        {now_ms, thermal_req, min_flow_lph, tick.cm_flow_fault_s, tick.cm_flow_recover_s, tick.cm_frost_on_c,
         tick.cm_frost_off_c, tick.cm_frost_nan_grace_s});
    const bool flow_valid = safety.flow_valid;
    const bool flow_low = safety.flow_low;
    const bool flow_ok = safety.flow_ok;
    const bool frost = safety.frost_active;

    // -------------------------------------------------
    // 2b) Cold water start
    //    - below 5°C: keep compressors stopped; CM4 may preheat only
    //      when reserve heating is explicitly allowed
    //    - 5..12°C: HP start is allowed; configured auxiliary assist
    //      remains active until every ODU outlet reaches 12°C
    //    - >=12°C: normal heating logic owns the session
    // -------------------------------------------------
    bool cold_start_blocked = false;
    bool cold_start_below_minimum = false;
    bool cold_start_assist_requested = false;
    bool cold_start_released_now = false;
    if (!heating_flow_req) {
      id(oq_cold_start_session_active) = false;
      id(oq_cold_start_pending) = false;
      id(oq_cold_start_sample_after_ms) = 0;
      id(oq_cold_start_hp_blocked) = false;
      id(oq_cold_start_assist_active) = false;
    } else {
      if (!id(oq_cold_start_session_active)) {
        id(oq_cold_start_session_active) = true;
        id(oq_cold_start_pending) = !any_hp_compressor_active;
        id(oq_cold_start_sample_after_ms) = 0;
        id(oq_cold_start_assist_active) = false;
      }

      if (id(oq_cold_start_pending)) {
        // After flow loss, require another sample taken after flow recovers.
        if (!flow_ok) id(oq_cold_start_sample_after_ms) = 0;
        if (flow_ok && id(oq_cold_start_sample_after_ms) == 0) {
          id(oq_cold_start_sample_after_ms) = now_ms;
        }

        const float hp1_raw_c = id(hp1_water_out_temp_raw).state;
        const float hp1_offset_c = id(hp1_water_out_temp_offset).state;
        const float hp1_out_c = isnan(hp1_raw_c) || isnan(hp1_offset_c) ? NAN : hp1_raw_c + hp1_offset_c;
#if OQ_TOPOLOGY_DUO
        const float hp2_raw_c = id(hp2_water_out_temp_raw).state;
        const float hp2_offset_c = id(hp2_water_out_temp_offset).state;
        const float hp2_out_c = isnan(hp2_raw_c) || isnan(hp2_offset_c) ? NAN : hp2_raw_c + hp2_offset_c;
        const oq_hp_supervisory::ColdStartWaterSample hp2_cold_start_sample{true, hp2_out_c,
                                                                            id(hp2_water_out_temp_last_update_ms)};
#else
        const float hp2_out_c = NAN;
        const oq_hp_supervisory::ColdStartWaterSample hp2_cold_start_sample{false, hp2_out_c, 0};
#endif
        const auto cold_start = oq_hp_supervisory::evaluate_cold_start(
            id(oq_cold_start_sample_after_ms),
            oq_hp_supervisory::ColdStartWaterSample{true, hp1_out_c, id(hp1_water_out_temp_last_update_ms)},
            hp2_cold_start_sample, tick.hp_cold_start_min_c, tick.hp_cold_start_assist_release_c);

        if (cold_start.released) {
          id(oq_cold_start_pending) = false;
          cold_start_released_now = true;
        } else {
          cold_start_blocked = !any_hp_compressor_active && !cold_start.hp_start_allowed;
          cold_start_below_minimum =
              !any_hp_compressor_active && cold_start.samples_ready && !cold_start.hp_start_allowed;
          cold_start_assist_requested = cold_start.auxiliary_assist_recommended &&
                                        id(oq_aux_heat_source_present).state && id(oq_boiler_assist_enabled).state;
        }
      }
      id(oq_cold_start_hp_blocked) = cold_start_blocked;
    }
    const bool probe_allowed = heating_flow_req && id(oq_cold_start_pending) && flow_ok &&
                               !id(oq_runtime_polling_paused).state && openquatt_enabled &&
                               id(oq_cm_override).current_option() == "Auto" && id(oq_control_mode_code) != 100;
    this->hp1_water_probe_.poll(now_ms, id(oq_cold_start_sample_after_ms),
                                probe_allowed && id(hp1_is_online) && !id(hp1_odu_eeprom_dump).is_active(), &id(hp1),
                                &id(hp1_water_out_temp_raw));
#if OQ_TOPOLOGY_DUO
    this->hp2_water_probe_.poll(now_ms, id(oq_cold_start_sample_after_ms),
                                probe_allowed && id(hp2_is_online) && !id(hp2_odu_eeprom_dump).is_active(), &id(hp2),
                                &id(hp2_water_out_temp_raw));
#endif
    // -------------------------------------------------
    // 4) CM selection with CM1 timer (pre/postflow) + flow interlock
    // -------------------------------------------------
    const char* cur_cm = id(oq_control_mode).state.c_str();
    std::string cm_transition_reason = "control decision updated";
    uint8_t cm1_event_reason = openquatt_decision_log::REASON_UNKNOWN;
    oq_hp_fallback::FallbackDecision fallback_decision;
    bool fallback_requested = false;
    bool no_hp_available_confirmed = false;
    bool cm3_fallback_handover_wait = false;
    static oq_hp_supervisory::Cm4ResumeTracker cm4_resume_tracker;
    auto resolve_desired_cm = [&]() -> int {
      // Helper: start CM1 window
      auto start_cm1 = [&](int next_after) -> void {
        id(oq_cm1_until_ms) = now_ms + prepost_ms;
        id(oq_cm1_next_after) = next_after;  // 0=CM0, 2=CM2, 4=CM4, 5=CM5, 98=CM98
        cm1_event_reason = (next_after == 0) ? openquatt_decision_log::REASON_FLOW_POSTFLOW
                                             : openquatt_decision_log::REASON_FLOW_PREFLOW;
        cm_transition_reason = (next_after == 2)    ? "pre/postflow hold for heating"
                               : (next_after == 4)  ? "preflow hold for boiler fallback"
                               : (next_after == 5)  ? "pre/postflow hold for cooling"
                               : (next_after == 98) ? "pre/postflow hold for frost"
                                                    : "postflow hold for standby";
        ESP_LOGI("supervisory", "CM1 hold started: next_after=%d, heating_req=%d, cooling_req=%d", next_after,
                 (int)heating_req, (int)cooling_req);
      };

      // Determine base target (without CM1 timer)
      int base_target = oq_hp_supervisory::base_control_mode(cooling_req, heating_req, frost);

      if (heating_preflow_req) base_target = 1;

      if (heating_req && cold_start_blocked) {
        base_target = 1;
      }

      // Apply flow interlock: if thermal output is requested but low-flow fault => hold in CM1.
      if (thermal_req && (id(oq_lowflow_fault_active) || flow_low)) {
        base_target = 1;  // force CM1 as safe holding state
      }

      // Handle CM1 window:
      // - If we're currently in CM1 and timer is running, keep CM1 until expiry.
      // - When CM1 expires, move to oq_cm1_next_after (but recompute if heating/frost changed).
      // Supervisory override (test/commissioning)
      // - Auto (normal logic)
      // - Force CM0 / Force CM1 / Force CM98 (auto-expire)
      const int selected_override_mode = id(oq_cm_override).active_index().value_or(0);
      const auto override = oq_supervisory_state::update_override(
          now_ms, selected_override_mode, oq_supervisory_state::seconds_to_ms(tick.cm_override_max_s),
          {id(oq_override_last_mode), id(oq_override_since_ms) != 0, id(oq_override_since_ms)});
      int override_mode = override.effective_mode;
      id(oq_override_last_mode) = override.state.last_mode;
      id(oq_override_since_ms) = override.state.timing ? override.state.since_ms : 0;
      if (override.expired) {
        const char* override_name = (selected_override_mode == 1)   ? "CM0"
                                    : (selected_override_mode == 2) ? "CM1"
                                                                    : "CM98";
        ESP_LOGI("supervisory", "%s override expired after %u s; returning to Auto.", override_name,
                 (unsigned int)tick.cm_override_max_s);
        auto call = id(oq_cm_override).make_call();
        call.set_option("Auto");
        call.perform();
      }

      const bool commissioning_in_progress = id(oq_commissioning_request_pending) || id(oq_commissioning_active);
      const int current_cm_code = cm_id_to_code(std::string(cur_cm));
      const uint8_t available_hp_count = id(oq_incident_manager).available_hp_count();
      const bool raw_availability_complete = id(oq_incident_manager).availability_complete();
      const bool every_unavailable_hp_allows_fallback = id(oq_incident_manager).all_unavailable_hps_allow_fallback();
      const bool fallback_outputs_safe = id(oq_incident_manager).all_fallback_outputs_safe();
      const bool fallback_opentherm_selected =
          id(oq_boiler_connection).has_state() && id(oq_boiler_connection).current_option() == "OpenTherm";
      const bool fallback_transport_available = oq_boiler::transport_available_for_selection(
          !id(oq_runtime_polling_paused).state && !id(oq_boiler_runtime_pause_state), fallback_opentherm_selected,
          tick.otb_supported, id(oq_otb_link_available_state), id(oq_otb_startup_probe_active),
          id(oq_boiler_connection_mismatch_state));
      const bool fallback_transport_settled = oq_boiler::settle_period_elapsed(
          id(oq_boiler_transport_settle_required), now_ms, id(oq_boiler_transport_change_ms),
          (uint32_t)(tick.boiler_transport_settle_s * 1000UL));
      oq_hp_supervisory::FallbackEvaluationInputs fallback_inputs;
      fallback_inputs.current_mode = current_cm_code;
      fallback_inputs.heating_demand = heating_req;
      fallback_inputs.fallback_enabled =
          id(oq_aux_heat_source_present).state && id(oq_boiler_fault_fallback_enabled).state;
      fallback_inputs.available_hp_count = available_hp_count;
      fallback_inputs.raw_availability_complete = raw_availability_complete;
      fallback_inputs.every_unavailable_hp_has_fallback_cause = every_unavailable_hp_allows_fallback;
      fallback_inputs.all_hp_outputs_safe = fallback_outputs_safe;
      fallback_inputs.cold_start_blocked = cold_start_below_minimum;
      fallback_inputs.flow_valid = flow_valid;
      fallback_inputs.flow_sufficient = flow_ok && !id(oq_lowflow_fault_active);
      fallback_inputs.supply_temperature_valid = !isnan(id(water_supply_temp_selected).state);
      fallback_inputs.boiler_guards_clear = !id(oq_water_temp_boiler_inhibit_active) &&
                                            !id(oq_water_temp_hard_trip_active) && fallback_transport_available &&
                                            fallback_transport_settled;
      fallback_inputs.cooling_active = cooling_req;
      fallback_inputs.frost_active = frost;
      fallback_inputs.commissioning_active = commissioning_in_progress;
      fallback_inputs.override_active = override_mode != 0;
      const auto fallback_evaluation = oq_hp_supervisory::evaluate_fallback(fallback_inputs);
      fallback_decision = fallback_evaluation.decision;
      no_hp_available_confirmed = fallback_evaluation.no_hp_available_confirmed;
      fallback_requested = fallback_evaluation.fallback_requested;
      cm3_fallback_handover_wait = fallback_evaluation.cm3_handover_wait;
      cm4_resume_tracker.observe_fallback_request(fallback_requested, current_cm_code);

      if (!cooling_req && heating_req) {
        if (fallback_decision.cm4_allowed) {
          base_target = 4;
        } else if (no_hp_available_confirmed) {
          // Includes temporary protections without an allowed fallback
          // cause: keep circulation but do not pretend CM2 is available.
          base_target = 1;
        }
      }

      int desired_local = 0;
      if (override_mode != 0) {
        // Clear transition timers to avoid surprises when returning to Auto
        id(oq_cm1_until_ms) = 0;
        id(oq_cm1_next_after) = 0;
        id(oq_cm3_need_since_ms) = 0;
        id(oq_cm3_demote_since_ms) = 0;
        if (override_mode == 1) {
          desired_local = 0;  // CM0
          cm_transition_reason = "supervisory override: Force CM0";
        } else if (override_mode == 2) {
          desired_local = 1;  // CM1
          cm_transition_reason = "supervisory override: Force CM1";
        } else if (override_mode == 3) {
          desired_local = 98;  // CM98
          cm_transition_reason = "supervisory override: Force CM98";
        }
      } else {
        if (commissioning_in_progress) {
          id(oq_cm1_until_ms) = 0;
          id(oq_cm1_next_after) = 0;
          id(oq_cm3_need_since_ms) = 0;
          id(oq_cm3_demote_since_ms) = 0;
          desired_local = 100;
          cm_transition_reason = "commissioning task active";
        } else {
          // Start the 30 s circulation window on the first heating request,
          // while flow and fresh ODU samples are still being acquired.
          if (oq_supervisory_state::start_heating_preflow(heating_flow_req, cooling_req, any_hp_active_guard,
                                                          current_cm_code, id(oq_cm1_until_ms), base_target)) {
            start_cm1(2);
          }
          // Snapshot CM1 timer state (before we touch globals)
          const bool cm1_timer_active = (id(oq_cm1_until_ms) != 0);
          const bool cm1_timer_running = cm1_timer_active && ms_window_active(id(oq_cm1_until_ms));
          const bool cm1_timer_expired = cm1_timer_active && !cm1_timer_running;
          const int cm1_next_after = id(oq_cm1_next_after);

          // 1) If we're in CM1 and timer is still running -> stay CM1
          if (cm1_timer_running && (strcmp(cur_cm, "CM1") == 0 || (id(oq_cm1_next_after) == 2 && heating_flow_req))) {
            desired_local = 1;
            cm_transition_reason = "CM1 hold timer active";

            // 2) If we're in CM1 and timer just expired -> advance to next_after
          } else if (cm1_timer_expired && strcmp(cur_cm, "CM1") == 0) {
            if (heating_req && base_target == 4 && fallback_decision.cm4_allowed) {
              desired_local = 4;
            } else if (heating_req && no_hp_available_confirmed && base_target == 1) {
              desired_local = 1;
            } else if (cm1_next_after == 2) {
              if (heating_req && base_target == 2)
                desired_local = 2;
              else if (oq_supervisory_state::hold_expired_heating_preflow(cm1_next_after, heating_flow_req,
                                                                          base_target))
                desired_local = 1;
              else if (base_target == 98)
                desired_local = 98;
              else
                desired_local = 0;
            } else if (cm1_next_after == 4) {
              // Never advance on timer state alone: all incident,
              // stop-confirmation, flow and boiler guards are fresh.
              desired_local = 1;
            } else if (cm1_next_after == 5) {
              if (cooling_req && base_target == 5)
                desired_local = 5;
              else if (base_target == 98)
                desired_local = 98;
              else
                desired_local = 0;
            } else if (cm1_next_after == 0) {
              // Re-evaluate heating/frost when postflow expires.
              // If demand recovered during CM1-postflow, resume CM2/CM5 directly
              // instead of forcing an avoidable CM0 hop (anti-flip/chatter).
              // Scope this behavior to heating-curve strategy only; keep
              // Power House behavior unchanged.
              if (cooling_req && base_target == 5)
                desired_local = 5;
              else if (strategy_active_code == 2 && heating_req && base_target == 2)
                desired_local = 2;
              else if (base_target == 98)
                desired_local = 98;
              else
                desired_local = 0;
            } else if (cm1_next_after == 98) {
              desired_local = 98;
            } else {
              desired_local = (base_target == 5) ? 5 : ((base_target == 2) ? 2 : (base_target == 98 ? 98 : 0));
            }

            cm_transition_reason = (desired_local == 2)    ? "CM1 hold expired -> heating"
                                   : (desired_local == 4)  ? "CM1 hold expired -> boiler fallback"
                                   : (desired_local == 5)  ? "CM1 hold expired -> cooling"
                                   : (desired_local == 98) ? "CM1 hold expired -> frost"
                                                           : "CM1 hold expired -> standby";
            ESP_LOGI("supervisory",
                     "CM1 hold expired: next_after=%d, heating_req=%d, cooling_req=%d, base_target=%d -> desired=%d",
                     cm1_next_after, (int)heating_req, (int)cooling_req, base_target, desired_local);

            // Keep completed preflow while confirmation, flow or fresh water
            // samples are pending. Guards must pass on this tick to enter CM2.
            if (!oq_supervisory_state::hold_expired_heating_preflow(cm1_next_after, heating_flow_req, base_target)) {
              id(oq_cm1_until_ms) = 0;
              id(oq_cm1_next_after) = 0;
            }
          } else {
            // 3) Not in CM1 (or no CM1 window active) -> normal transition logic
            if (cm1_timer_expired) {
              id(oq_cm1_until_ms) = 0;
              id(oq_cm1_next_after) = 0;
            }

            if (cooling_req) {
              if (base_target == 5) {
                if (strcmp(cur_cm, "CM5") == 0) {
                  desired_local = 5;
                  cm_transition_reason = "cooling already active";
                } else {
                  start_cm1(5);
                  desired_local = 1;
                  cm_transition_reason = "cooling request waiting for CM1 hold";
                }
              } else {
                desired_local = 1;  // flow interlock holding state
                // Raw flow is normally still building when the pump first starts.
                // Only call it low flow after the configured fault delay has elapsed.
                cm1_event_reason = id(oq_lowflow_fault_active) ? openquatt_decision_log::REASON_FLOW_TOO_LOW
                                                               : openquatt_decision_log::REASON_FLOW_PREFLOW;
                cm_transition_reason = "cooling request held by flow interlock";
              }
            } else if (heating_preflow_req && !heating_req) {
              if (strcmp(cur_cm, "CM1") != 0) start_cm1(2);
              desired_local = 1;
              cm_transition_reason = "Power House demand confirmation overlaps heating preflow";
            } else if (heating_req) {
              const auto heating_mode = oq_hp_supervisory::decide_heating_mode({
                  current_cm_code,
                  base_target,
                  cm3_fallback_handover_wait,
                  cm4_resume_tracker.resume_mode(),
                  available_hp_count > 0,
                  power_house_active,
                  id(oq_aux_heat_source_present).state && id(oq_boiler_assist_enabled).state,
              });
              if (heating_mode.start_cm1_for_mode >= 0) {
                start_cm1(heating_mode.start_cm1_for_mode);
              }
              desired_local = heating_mode.desired_mode;
              cm_transition_reason = heating_mode.transition_reason;
              if (heating_mode.flow_interlock_hold) {
                cm1_event_reason = id(oq_lowflow_fault_active) ? openquatt_decision_log::REASON_FLOW_TOO_LOW
                                                               : openquatt_decision_log::REASON_FLOW_PREFLOW;
              }
            } else {
              if (strcmp(cur_cm, "CM2") == 0 || strcmp(cur_cm, "CM3") == 0 || strcmp(cur_cm, "CM4") == 0 ||
                  strcmp(cur_cm, "CM5") == 0) {
                start_cm1(0);  // postflow to CM0
                desired_local = 1;
                cm_transition_reason = "postflow before standby";
              } else {
                desired_local = (base_target == 98) ? 98 : 0;
                cm_transition_reason = (desired_local == 98) ? "frost protection" : "no thermal demand";
              }
            }
          }

          // Safety: never leave CM1 to CM0 while any HP still reports/targets activity.
          // Keep CM1 (pump-on holding state) until both HPs are effectively idle.
          if (strcmp(cur_cm, "CM1") == 0 && !thermal_req && base_target == 0 && any_hp_active_guard) {
            desired_local = 1;
          }

          // CM3 is the sole boiler-assist owner for both heating strategies.
          // Transport selection affects availability, never strategy intent.
          const int cm_now = (strcmp(cur_cm, "CM4") == 0)    ? 4
                             : (strcmp(cur_cm, "CM3") == 0)  ? 3
                             : (strcmp(cur_cm, "CM2") == 0)  ? 2
                             : (strcmp(cur_cm, "CM5") == 0)  ? 5
                             : (strcmp(cur_cm, "CM1") == 0)  ? 1
                             : (strcmp(cur_cm, "CM98") == 0) ? 98
                                                             : 0;
          const bool boiler_assist_enabled = id(oq_aux_heat_source_present).state && id(oq_boiler_assist_enabled).state;
          const bool opentherm_selected =
              id(oq_boiler_connection).has_state() && id(oq_boiler_connection).current_option() == "OpenTherm";
          const bool boiler_transport_available =
              !opentherm_selected || (tick.otb_supported && id(oq_otb_link_available_state));
          const bool strategy_output_current =
              oq_boiler::strategy_output_is_current(id(oq_strategy_output_valid), id(oq_strategy_output_source_code),
                                                    (uint8_t)strategy_active_code, id(oq_strategy_output_updated_ms));
          const float rated_boiler_w = id(oq_boiler_rated_heat_power).state;
          const bool boiler_strategy_command_available =
              strategy_output_current && (!power_house_active || (!isnan(id(oq_strategy_requested_power_w)) &&
                                                                  !isnan(id(oq_strategy_hp_expected_power_w)) &&
                                                                  !isnan(rated_boiler_w) && rated_boiler_w > 0.0f));
          const bool boiler_assist_available = boiler_assist_enabled && boiler_transport_available &&
                                               (cold_start_assist_requested || boiler_strategy_command_available);
          if (heating_strategy_active && boiler_assist_available && !fallback_requested && !no_hp_available_confirmed) {
            bool need_on = false;
            bool ok_off = true;
            const char* promote_reason = "boiler assist promoted to CM3";
            const char* demote_reason = "boiler assist cleared, demoting to CM2";

            if (cold_start_assist_requested) {
              need_on = true;
              ok_off = false;
              promote_reason = "cold water start promoted to CM3";
              demote_reason = "cold water start released, demoting to CM2";
            } else if (power_house_active) {
              const auto assist = oq_boiler::power_house_assist(id(oq_P_deficit_w), id(oq_cm3_deficit_on_w).state,
                                                                id(oq_cm3_deficit_off_w).state);
              need_on = assist.need_on;
              ok_off = assist.okay_off;
              promote_reason = "power-house deficit promoted to CM3";
              demote_reason = "power-house deficit cleared, demoting to CM2";
            } else if (heating_curve_active) {
              const auto assist = oq_boiler::heating_curve_assist(
                  heating_req, id(oq_strategy_hp_saturated), id(oq_strategy_supply_target_temp),
                  id(oq_system_supply_temp).state, tick.cm3_curve_on_delta_c, tick.cm3_curve_off_delta_c);
              need_on = assist.need_on;
              ok_off = assist.okay_off;
              promote_reason = "heating-curve saturation promoted to CM3";
              demote_reason = "heating-curve target recovered, demoting to CM2";
            }

            const uint32_t T_ON_MS = (uint32_t)(tick.cm3_promote_s * 1000UL);
            const uint32_t T_OFF_MS = (uint32_t)(tick.cm3_demote_s * 1000UL);
            const uint32_t MIN_CM3_MS = (uint32_t)(tick.cm3_min_run_s * 1000UL);
            const uint32_t MIN_CM2_MS = (uint32_t)(tick.cm2_min_run_s * 1000UL);

            if (cold_start_assist_requested && (desired_local == 2 || cm_now == 2 || cm_now == 3 || cm_now == 4)) {
              desired_local = 3;
              id(oq_cold_start_assist_active) = true;
              id(oq_cm3_need_since_ms) = 0;
              id(oq_cm3_demote_since_ms) = 0;
              cm_transition_reason = promote_reason;
            } else if (cm_now == 2) {
              if (now_ms - id(oq_cm_last_change_ms) >= MIN_CM2_MS) {
                if (need_on) {
                  if (id(oq_cm3_need_since_ms) == 0) id(oq_cm3_need_since_ms) = now_ms;
                  if (now_ms - id(oq_cm3_need_since_ms) >= T_ON_MS) {
                    desired_local = 3;
                    id(oq_cm3_need_since_ms) = 0;
                    id(oq_cm3_demote_since_ms) = 0;
                    cm_transition_reason = promote_reason;
                  }
                } else {
                  id(oq_cm3_need_since_ms) = 0;
                }
              }
            } else if (cm_now == 3) {
              const bool minimum_run_elapsed = now_ms - id(oq_cm_last_change_ms) >= MIN_CM3_MS;
              bool demote_confirmation_elapsed = false;
              if (minimum_run_elapsed) {
                if (ok_off) {
                  if (id(oq_cm3_demote_since_ms) == 0) id(oq_cm3_demote_since_ms) = now_ms;
                  demote_confirmation_elapsed = now_ms - id(oq_cm3_demote_since_ms) >= T_OFF_MS;
                } else {
                  id(oq_cm3_demote_since_ms) = 0;
                }
              } else {
                id(oq_cm3_demote_since_ms) = 0;
              }
              if (oq_boiler::cm3_should_hold(minimum_run_elapsed, ok_off, demote_confirmation_elapsed)) {
                desired_local = 3;
              } else {
                desired_local = 2;
                id(oq_cm3_need_since_ms) = 0;
                id(oq_cm3_demote_since_ms) = 0;
                cm_transition_reason = demote_reason;
              }
            } else {
              id(oq_cm3_need_since_ms) = 0;
              id(oq_cm3_demote_since_ms) = 0;
            }
          } else {
            id(oq_cm3_need_since_ms) = 0;
            id(oq_cm3_demote_since_ms) = 0;
            if (cm_now == 3 && heating_req && base_target == 2 && !cm3_fallback_handover_wait) {
              desired_local = 2;
              if (!boiler_assist_enabled) {
                cm_transition_reason = "boiler/CV assist disabled, returning to CM2";
              } else if (!boiler_transport_available) {
                cm_transition_reason = "selected boiler transport unavailable, returning to CM2";
              } else {
                cm_transition_reason = "heating strategy unavailable for boiler assist, returning to CM2";
              }
            }
          }

          if (cold_start_released_now && id(oq_cold_start_assist_active) && cm_now == 3) {
            desired_local = 2;
            id(oq_cm3_need_since_ms) = 0;
            id(oq_cm3_demote_since_ms) = 0;
            cm_transition_reason = "cold water start reached release temperature";
          }
          if (!cold_start_assist_requested || desired_local != 3) {
            id(oq_cold_start_assist_active) = false;
          }
        }
      }
      cm4_resume_tracker.finish_after_decision(heating_req,
                                               cooling_req || frost || commissioning_in_progress || override_mode != 0,
                                               fallback_requested, available_hp_count > 0);
      return desired_local;
    };
    const int desired = resolve_desired_cm();
    const char* desired_cm = cm_code_to_id(desired);
    const bool next_mode_owns_boiler = desired == 3 || desired == 4 ||
                                       (desired == 100 && id(oq_commissioning_active) &&
                                        id(oq_commissioning_task_code) == oq_commissioning::TASK_BOILER_POWER_TEST);
    const bool current_mode_owns_boiler =
        strcmp(cur_cm, "CM3") == 0 || strcmp(cur_cm, "CM4") == 0 ||
        (strcmp(cur_cm, "CM100") == 0 &&
         (id(oq_boiler_command_source_code) == oq_boiler::COMMAND_SOURCE_COMMISSIONING ||
          id(oq_boiler_output_request)));
    if (!next_mode_owns_boiler && (current_mode_owns_boiler || id(oq_boiler_output_request))) {
      // Withdraw at the ownership edge itself. The staggered dispatcher
      // must not leave R1 or OpenTherm honoring the previous mode for up
      // to another boiler-control cadence.
      id(oq_boiler_command_valid) = false;
      id(oq_boiler_command_demand_present) = false;
      id(oq_boiler_command_heat_request) = false;
      id(oq_boiler_command_requested_power_w) = NAN;
      id(oq_boiler_command_target_temperature_c) = NAN;
      id(oq_boiler_command_source_code) = oq_boiler::COMMAND_SOURCE_NONE;
      id(oq_boiler_command_updated_ms) = now_ms;
      if (id(oq_boiler_output_request)) {
        id(oq_boiler_output_last_change_ms) = now_ms;
      }
      id(oq_boiler_output_request) = false;
      id(oq_boiler_output_target_temperature_c) = NAN;
      id(oq_boiler_transport_active) = false;
      id(oq_boiler_block_reason_code) = oq_boiler::BLOCK_NO_HEAT_REQUEST;
      id(boiler_relay).turn_off();
      shutdown_boiler_transport_();
    }
    id(oq_control_mode_code) = desired;
    id(oq_incident_manager)
        .set_fallback_status(fallback_requested, desired == 4,
                             fallback_requested ? static_cast<uint8_t>(fallback_decision.block_reason)
                                                : static_cast<uint8_t>(oq_hp_fallback::FallbackBlockReason::NONE));
    const bool cooling_preflow = desired == 1 && (cooling_req || id(oq_cm1_next_after) == 5);
    if (desired == 5 || cooling_preflow) {
      id(oq_cooling_energy_session_active) = true;
    } else if (desired != 1 || id(oq_cm1_next_after) != 0) {
      // Keep the cooling session latched only during CM1 postflow to CM0.
      id(oq_cooling_energy_session_active) = false;
    }
    // -------------------------------------------------
    // 5) Publish CM (writes-only-on-change)
    // -------------------------------------------------
    static uint32_t cm1_hold_started_ms = 0;
    static uint8_t cm1_hold_reason = openquatt_decision_log::REASON_UNKNOWN;
    static bool cm_event_baseline_ready = false;
    // A normal preflow can turn into a real low-flow block while CM1 remains
    // active. Preserve that promotion so the eventual clear event explains
    // that a start blockade was lifted instead of reporting a normal preflow.
    if (id(oq_control_mode).state == "CM1" && id(oq_lowflow_fault_active) &&
        cm1_hold_reason == openquatt_decision_log::REASON_FLOW_PREFLOW) {
      cm1_hold_reason = openquatt_decision_log::REASON_FLOW_TOO_LOW;
    }
    if (id(oq_control_mode).state != desired_cm) {
      const std::string previous_cm = id(oq_control_mode).state;
      const int previous_cm_code = cm_id_to_code(previous_cm);
      id(oq_control_mode).publish_state(desired_cm);
      ESP_LOGI("supervisory", "ControlMode transition: %s -> %s (%s)",
               previous_cm.empty() ? "(unset)" : previous_cm.c_str(), desired_cm, cm_transition_reason.c_str());
      if (cm_event_baseline_ready && !previous_cm.empty()) {
        static const oq_hp_supervisory::ControlModeLogCodes log_codes{
            {
                openquatt_decision_log::REASON_UNKNOWN,
                openquatt_decision_log::REASON_BOILER_FALLBACK,
                openquatt_decision_log::REASON_HP_RECOVERED,
                openquatt_decision_log::REASON_FALLBACK_BLOCKED,
                openquatt_decision_log::REASON_COMMISSIONING,
                openquatt_decision_log::REASON_SUPERVISORY_OVERRIDE,
                openquatt_decision_log::REASON_FROST_PROTECTION,
                openquatt_decision_log::REASON_COOLING_REQUEST,
                openquatt_decision_log::REASON_HEATING_REQUEST,
                openquatt_decision_log::REASON_COOLING_REQUEST_CLEARED,
                openquatt_decision_log::REASON_HEATING_REQUEST_CLEARED,
                openquatt_decision_log::REASON_HP_RECOVERY_WAIT,
            },
            {
                openquatt_decision_log::SEVERITY_NORMAL,
                openquatt_decision_log::SEVERITY_LIMITED,
                openquatt_decision_log::SEVERITY_FAULT,
            },
            {
                openquatt_decision_log::STATE_IDLE,
                openquatt_decision_log::STATE_STANDBY,
                openquatt_decision_log::STATE_ACTIVE,
                openquatt_decision_log::STATE_FALLBACK,
            },
        };
        const auto transition_log = oq_hp_supervisory::classify_control_mode_transition(
            {
                previous_cm_code,
                desired,
                fallback_requested,
                cm_transition_reason.rfind("supervisory override", 0) == 0,
                cm1_event_reason,
            },
            log_codes);
        id(oq_decision_log)
            .emit(openquatt_decision_log::EVENT_CONTROL_MODE_CHANGE, openquatt_decision_log::SUBJECT_SYSTEM,
                  transition_log.reason, transition_log.severity, (uint8_t)desired, transition_log.from_state,
                  transition_log.to_state, (int16_t)previous_cm_code, (int16_t)desired);
      }
      // DEBUG-RUNTIME: anchor CM dwell timers on actual mode transitions.
      id(oq_cm_last_change_ms) = now_ms;
      if (desired == 2) {
        id(oq_cm2_entered_ms) = now_ms;
      }
      auto is_cm1_log_reason = [](uint8_t reason) -> bool {
        return reason == openquatt_decision_log::REASON_FLOW_PREFLOW ||
               reason == openquatt_decision_log::REASON_FLOW_POSTFLOW ||
               reason == openquatt_decision_log::REASON_FLOW_TOO_LOW;
      };
      const bool was_cm1 = (previous_cm == "CM1");
      const bool is_cm1 = (desired == 1);
      if (!was_cm1 && is_cm1 && is_cm1_log_reason(cm1_event_reason)) {
        cm1_hold_started_ms = now_ms;
        cm1_hold_reason = cm1_event_reason;
        const uint8_t hold_severity = cm1_hold_reason == openquatt_decision_log::REASON_FLOW_TOO_LOW
                                          ? openquatt_decision_log::SEVERITY_LIMITED
                                          : openquatt_decision_log::SEVERITY_NORMAL;
        id(oq_decision_log)
            .emit(openquatt_decision_log::EVENT_FLOW_HOLD_START, openquatt_decision_log::SUBJECT_SYSTEM,
                  cm1_hold_reason, hold_severity, 1, openquatt_decision_log::STATE_STANDBY,
                  openquatt_decision_log::STATE_LIMITED, (int16_t)id(oq_cm1_next_after));
      } else if (was_cm1 && !is_cm1 && is_cm1_log_reason(cm1_hold_reason)) {
        uint16_t duration_s = 0;
        if (cm1_hold_started_ms != 0) {
          const uint32_t elapsed_s = (now_ms - cm1_hold_started_ms) / 1000UL;
          duration_s = elapsed_s > UINT16_MAX ? UINT16_MAX : (uint16_t)elapsed_s;
        }
        const uint8_t next_state =
            desired == 0 ? openquatt_decision_log::STATE_STANDBY : openquatt_decision_log::STATE_ACTIVE;
        id(oq_decision_log)
            .emit(openquatt_decision_log::EVENT_FLOW_HOLD_CLEAR, openquatt_decision_log::SUBJECT_SYSTEM,
                  cm1_hold_reason, openquatt_decision_log::SEVERITY_NORMAL, 1, openquatt_decision_log::STATE_LIMITED,
                  next_state, (int16_t)desired, 0, 0, duration_s);
        cm1_hold_started_ms = 0;
        cm1_hold_reason = openquatt_decision_log::REASON_UNKNOWN;
      }
      static uint32_t cm98_started_ms = 0;
      const bool was_cm98 = (previous_cm == "CM98");
      const bool is_cm98 = (desired == 98);
      if (!was_cm98 && is_cm98) {
        cm98_started_ms = now_ms;
        id(oq_decision_log)
            .emit(openquatt_decision_log::EVENT_FROST_PROTECTION_START, openquatt_decision_log::SUBJECT_SYSTEM,
                  openquatt_decision_log::REASON_FROST_PROTECTION, openquatt_decision_log::SEVERITY_LIMITED, 98,
                  openquatt_decision_log::STATE_STANDBY, openquatt_decision_log::STATE_ACTIVE);
      } else if (was_cm98 && !is_cm98) {
        uint16_t duration_s = 0;
        if (cm98_started_ms != 0) {
          const uint32_t elapsed_s = (now_ms - cm98_started_ms) / 1000UL;
          duration_s = elapsed_s > UINT16_MAX ? UINT16_MAX : (uint16_t)elapsed_s;
        }
        cm98_started_ms = 0;
        id(oq_decision_log)
            .emit(openquatt_decision_log::EVENT_FROST_PROTECTION_CLEAR, openquatt_decision_log::SUBJECT_SYSTEM,
                  openquatt_decision_log::REASON_FROST_PROTECTION, openquatt_decision_log::SEVERITY_NORMAL, 98,
                  openquatt_decision_log::STATE_ACTIVE, openquatt_decision_log::STATE_STANDBY, 0, 0, 0, duration_s);
      }
    }
    cm_event_baseline_ready = true;
    static std::string last_control_mode_label;
    publish_text_if_changed(id(oq_control_mode_label), cm_code_to_label(desired), last_control_mode_label);

    auto apply_silent_window = [&]() -> void {
      // Silent window + diagnostics + low-noise mode selection.
      bool silent_active = false;
      const std::string silent_override =
          id(oq_silent_mode_override).active_index().has_value()
              ? id(oq_silent_mode_override).at(id(oq_silent_mode_override).active_index().value()).value()
              : std::string("Schedule");

      std::string now_hhmm = "invalid";
      if (time_valid) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", now_time.hour, now_time.minute);
        now_hhmm = std::string(buf);
      }
      static std::string last_now_hhmm;
      publish_text_if_changed(id(oq_time_now_hhmm), now_hhmm, last_now_hhmm);

      auto s = id(oq_silent_start_time).state_as_esptime();
      auto e = id(oq_silent_end_time).state_as_esptime();
      char win_buf[16];
      snprintf(win_buf, sizeof(win_buf), "%02d:%02d-%02d:%02d", s.hour, s.minute, e.hour, e.minute);
      std::string window_hhmm(win_buf);
      static std::string last_window_hhmm;
      publish_text_if_changed(id(oq_silent_window_hhmm), window_hhmm, last_window_hhmm);

      const oq_supervisory_state::SilentOverride override_mode =
          silent_override == "On"    ? oq_supervisory_state::SilentOverride::ON
          : silent_override == "Off" ? oq_supervisory_state::SilentOverride::OFF
                                     : oq_supervisory_state::SilentOverride::SCHEDULE;
      const auto silent =
          oq_supervisory_state::silent_window(time_valid, now_time.hour * 60 + now_time.minute, s.hour * 60 + s.minute,
                                              e.hour * 60 + e.minute, override_mode);
      silent_active = silent.active;
      const std::string silent_status(silent.status);

      static std::string last_silent_status;
      publish_text_if_changed(id(oq_silent_status), silent_status, last_silent_status);

      static bool last_time_valid = false;
      if (time_valid && !last_time_valid) {
        id(oq_silent_active).publish_state(silent_active);
      }
      last_time_valid = time_valid;

      if (id(oq_silent_active).state != silent_active) {
        id(oq_silent_active).publish_state(silent_active);
      }

      // Manual HP service must exercise the requested compressor behavior
      // without the scheduled low-noise policy. Restore it automatically
      // when the service task releases ownership.
      const bool hp_silent_active = silent_active && !oq_manual_hp::owns_control();
      const char* silent_opt = hp_silent_active ? "On" : "Off";
      set_select_option(id(hp1_low_noise_mode), silent_opt);
#if OQ_TOPOLOGY_DUO
      set_select_option(id(hp2_low_noise_mode), silent_opt);
#endif
    };

    auto apply_sticky_pump_policy = [&]() -> void {
      // Sticky Pump Protection (CM0-only) + pump/PWM ownership.
      const bool in_cm0 = (strcmp(desired_cm, "CM0") == 0);
      const uint32_t sticky_wait_ms = oq_supervisory_state::seconds_to_ms(tick.cm0_sticky_wait_s);
      const uint32_t sticky_run_ms = oq_supervisory_state::seconds_to_ms(tick.cm0_sticky_run_s);
      const auto sticky = oq_supervisory_state::update_sticky_pump(
          now_ms, in_cm0, sticky_wait_ms, sticky_run_ms,
          {id(oq_cm0_since_ms) != 0, id(oq_cm0_since_ms), id(oq_sticky_until_ms)});
      id(oq_cm0_since_ms) = sticky.state.cm0_timing ? sticky.state.cm0_since_ms : 0;
      id(oq_sticky_until_ms) = sticky.state.active_until_ms;
      if (sticky.started) {
        const uint32_t sticky_duration_s_raw = sticky_run_ms / 1000UL;
        const uint16_t sticky_duration_s =
            sticky_duration_s_raw > UINT16_MAX ? UINT16_MAX : (uint16_t)sticky_duration_s_raw;
        id(oq_decision_log)
            .emit(openquatt_decision_log::EVENT_STICKY_PUMP_RUN, openquatt_decision_log::SUBJECT_PUMP,
                  openquatt_decision_log::REASON_STICKY_PROTECTION, openquatt_decision_log::SEVERITY_NORMAL, 0,
                  openquatt_decision_log::STATE_STANDBY, openquatt_decision_log::STATE_ACTIVE, 0, 0, 0,
                  sticky_duration_s);
      }
      if (in_cm0) publish_binary_if_changed(id(oq_sticky_active), sticky.active);

      const bool sticky_active = in_cm0 && sticky.active;
      const bool cm100_task_active =
          (strcmp(desired_cm, "CM100") == 0) && (id(oq_commissioning_task_code) != oq_commissioning::TASK_NONE);
      // Pump failsafe: never stop circulation while any HP is still active.
      // CM100 idle is a service stand, not an active circulation mode.
      const bool pump_on = (strcmp(desired_cm, "CM0") != 0 && strcmp(desired_cm, "CM100") != 0) || cm100_task_active ||
                           sticky_active || any_hp_active_guard;
      set_select_option(id(hp1_set_pump_mode), pump_on ? "On" : "Off");
#if OQ_TOPOLOGY_DUO
      set_select_option(id(hp2_set_pump_mode), pump_on ? "On" : "Off");
#endif

      if (strcmp(desired_cm, "CM0") == 0) {
        const float stop_pwm = tick.cm0_pump_stop_ipwm;
        const float sticky_pwm = tick.sticky_pwm;
        const float target_pwm = sticky_active ? sticky_pwm : stop_pwm;
        set_number_value(id(hp1_pump_speed), target_pwm, 1.0f);
#if OQ_TOPOLOGY_DUO
        set_number_value(id(hp2_pump_speed), target_pwm, 1.0f);
#endif
      } else {
        publish_binary_if_changed(id(oq_sticky_active), false);
      }
    };

    apply_silent_window();
    apply_sticky_pump_policy();
  }

 private:
  oq_cold_start::WaterProbe hp1_water_probe_;
#if OQ_TOPOLOGY_DUO
  oq_cold_start::WaterProbe hp2_water_probe_;
#endif
  static void shutdown_boiler_transport_() {
#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
    id(oq_otb_ch_enable).turn_off();
    auto call = id(oq_otb_t_set_command).make_call();
    call.set_value(0.0f);
    call.perform();
#endif
  }
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_supervisory_state_runtime
#endif
