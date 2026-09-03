#pragma once

#include <math.h>
#include <stdint.h>

#include <algorithm>
#include <string>

#include "oq_compressor_frequency_runtime.h"
#include "oq_heating_curve_logic.h"
#include "oq_power_house_dispatch_logic.h"
#include "oq_thermal_request_logic.h"
#include "../odu/oq_odu_compressor_levels.h"
#include "../service/tasks/oq_manual_hp_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_thermal_request_runtime {

#if OQ_TOPOLOGY_DUO
#define OQ_REQUEST_SECONDARY_ID(suffix) id(hp2_##suffix)
#else
#define OQ_REQUEST_SECONDARY_ID(suffix) id(hp1_##suffix)
#endif

struct TickConfig {
  uint32_t now_ms;
  uint32_t tick_ms;
  uint32_t curve_ms;
  uint32_t power_house_ms;
  uint32_t minimum_off_s;
  int minimum_runtime_floor_s;
  int demand_max_f;
  float minimum_flow_lph;
};

class Runtime {
 public:
  void tick(const TickConfig& config) {
    const int cm_code = id(oq_control_mode_code);
    const auto mode = oq_request::resolve_mode_context(cm_code, id(oq_heat_mode_code));
    const bool manual_active = oq_manual_hp::owns_control();
    const uint32_t loop_target_ms =
        manual_active || mode.cooling ? config.tick_ms : (mode.curve ? config.curve_ms : config.power_house_ms);

    const bool startup_inhibit_active = this->update_startup_inhibit_(config.now_ms, config.minimum_off_s);
    if (id(oq_heat_last_mode_code) != mode.control_flavor_code) {
      id(oq_heat_last_mode_code) = mode.control_flavor_code;
      id(oq_heat_last_loop_ms) = 0;
    }
    const auto cadence = oq_request::cadence_decision(config.now_ms, id(oq_heat_last_loop_ms), loop_target_ms);
    if (!cadence.due) return;
    id(oq_heat_last_loop_ms) = config.now_ms;

    const int demand_max_f = std::max(0, config.demand_max_f);
    const int raw =
        oq_request::clamp_level(mode.cooling ? id(oq_cooling_demand_raw) : id(oq_demand_raw), 0, demand_max_f);
    const int filtered = oq_request::clamp_level(id(oq_demand_filtered), 0, demand_max_f);
    const int cap = oq_request::clamp_level(id(oq_power_cap_f), 0, demand_max_f);
    const int post_cap = std::min(filtered, cap);
    this->track_measured_start_(true, config.now_ms);
#if OQ_TOPOLOGY_DUO
    this->track_measured_start_(false, config.now_ms);
#endif

    const auto frequency = oq_frequency_runtime::capture();
    const int minimum_runtime_s = oq_request::minimum_runtime_seconds(
        id(oq_min_runtime_min).has_state(), id(oq_min_runtime_min).state, config.minimum_runtime_floor_s);
    const uint32_t minimum_runtime_ms = minimum_runtime_s > 0 ? static_cast<uint32_t>(minimum_runtime_s) * 1000UL : 0;

    if (manual_active) {
      this->run_manual_(config, mode, startup_inhibit_active, minimum_runtime_ms, frequency, raw, post_cap, cap);
      return;
    }
    if (!mode.cm_allows_hp) {
      this->run_inactive_(config, mode, startup_inhibit_active, minimum_runtime_ms, frequency, raw, post_cap, cap);
      return;
    }
    this->run_automatic_(config, mode, startup_inhibit_active, minimum_runtime_ms, frequency, raw, post_cap, cap);
  }

 private:
  bool update_startup_inhibit_(uint32_t now_ms, uint32_t minimum_off_s) {
    if (id(oq_boot_startup_inhibit_until_ms) == 0) {
      const uint32_t inhibit_ms = minimum_off_s * 1000UL;
      id(oq_boot_startup_inhibit_until_ms) = now_ms + inhibit_ms;
      id(oq_boot_startup_inhibit_active) = inhibit_ms > 0;
      if (!id(oq_boot_startup_inhibit_logged) && inhibit_ms > 0) {
        ESP_LOGI("quatt", "Startup inhibit armed for %us after reboot; compressors remain blocked.",
                 static_cast<unsigned int>(minimum_off_s));
        id(oq_boot_startup_inhibit_logged) = true;
        id(oq_boot_startup_inhibit_cleared_logged) = false;
      }
    }
    const bool active = oq_request::deadline_pending(now_ms, id(oq_boot_startup_inhibit_until_ms));
    id(oq_boot_startup_inhibit_active) = active;
    if (!active && id(oq_boot_startup_inhibit_until_ms) > 0 && id(oq_boot_startup_inhibit_logged) &&
        !id(oq_boot_startup_inhibit_cleared_logged)) {
      ESP_LOGI("quatt", "Startup inhibit cleared after reboot; compressors may resume.");
      id(oq_boot_startup_inhibit_cleared_logged) = true;
    }
    return active;
  }

  void track_measured_start_(bool is_hp1, uint32_t now_ms) {
    const float raw = is_hp1 ? id(hp1_working_mode).state : OQ_REQUEST_SECONDARY_ID(working_mode).state;
    const bool thermal = oq_request::thermal_mode_matches(raw, 1) || oq_request::thermal_mode_matches(raw, 2);
    bool& previous = is_hp1 ? id(hp1_prev_heating_mode_active) : id(hp2_prev_heating_mode_active);
    if (thermal && !previous) {
      (is_hp1 ? id(hp1_last_real_heat_start_ms) : id(hp2_last_real_heat_start_ms)) = now_ms;
    }
    previous = thermal;
  }

  bool measured_thermal_(bool is_hp1) const {
    const float raw = is_hp1 ? id(hp1_working_mode).state : OQ_REQUEST_SECONDARY_ID(working_mode).state;
    return oq_request::thermal_mode_matches(raw, 1) || oq_request::thermal_mode_matches(raw, 2);
  }

  int selected_level_(bool is_hp1) const {
    if (is_hp1) {
      if (!id(hp1_compressor_level).has_state()) return -1;
      const auto index = id(hp1_compressor_level).active_index();
      return index.has_value() ? static_cast<int>(index.value()) : -1;
    }
    if (!OQ_REQUEST_SECONDARY_ID(compressor_level).has_state()) return -1;
    const auto index = OQ_REQUEST_SECONDARY_ID(compressor_level).active_index();
    return index.has_value() ? static_cast<int>(index.value()) : -1;
  }

  int last_applied_(bool is_hp1) const {
    return is_hp1 ? id(hp1_last_applied_level) : OQ_REQUEST_SECONDARY_ID(last_applied_level);
  }

  bool mode_is_cooling_(bool is_hp1) const {
    const float raw = is_hp1 ? id(hp1_working_mode).state : OQ_REQUEST_SECONDARY_ID(working_mode).state;
    return oq_request::thermal_mode_matches(raw, 1);
  }

  bool target_is_cooling_(bool is_hp1) const {
    if (is_hp1) {
      return oq_request::target_option_matches_mode(id(hp1_set_working_mode).has_state(),
                                                    id(hp1_set_working_mode).current_option(), 1);
    }
    return oq_request::target_option_matches_mode(OQ_REQUEST_SECONDARY_ID(set_working_mode).has_state(),
                                                  OQ_REQUEST_SECONDARY_ID(set_working_mode).current_option(), 1);
  }

  bool cooling_hold_(bool is_hp1, int level) const {
    return level > 0 && (this->mode_is_cooling_(is_hp1) || this->target_is_cooling_(is_hp1));
  }

  int hold_mode_(int hp1_level, int hp2_level) const {
    return oq_request::hold_request_mode_code(hp1_level, hp2_level, this->cooling_hold_(true, hp1_level),
#if OQ_TOPOLOGY_DUO
                                              this->cooling_hold_(false, hp2_level)
#else
                                              false
#endif
    );
  }

  int defrost_hold_(bool is_hp1, const oq_frequency_runtime::Context& frequency) const {
    const bool defrost = is_hp1 ? id(hp1_4_way_valve).state : OQ_REQUEST_SECONDARY_ID(4_way_valve).state;
    const int previous_physical =
        is_hp1 ? id(hp1_last_commanded_physical_level) : OQ_REQUEST_SECONDARY_ID(last_commanded_physical_level);
    return oq_odu::resolve_retained_level(defrost, this->mode_is_cooling_(is_hp1) || this->target_is_cooling_(is_hp1),
                                          this->selected_level_(is_hp1), this->last_applied_(is_hp1), previous_physical,
                                          frequency.configured_v2, frequency.snapshot(is_hp1))
        .control_level;
  }

  int minimum_runtime_hold_(bool is_hp1, uint32_t now_ms, uint32_t minimum_runtime_ms, bool startup_inhibit) const {
    const uint32_t last_start = is_hp1 ? id(hp1_last_real_heat_start_ms) : id(hp2_last_real_heat_start_ms);
    const bool blocked = id(oq_water_temp_hard_trip_active) || id(oq_lowflow_fault_active) || startup_inhibit;
    return oq_request::min_runtime_hold_required(
               0, blocked, oq_request::min_runtime_window_active(now_ms, last_start, minimum_runtime_ms),
               this->last_applied_(is_hp1), this->measured_thermal_(is_hp1))
               ? 1
               : 0;
  }

  int physical_limit_(bool is_hp1, int mode_code, const oq_frequency_runtime::Context& frequency) const {
    return oq_odu::physical_level_limit(frequency.configured_v2, frequency.snapshot(is_hp1), mode_code);
  }

  int allowed_(bool is_hp1, int mode_code, int level, const oq_frequency_runtime::Context& frequency) const {
    return frequency.pick_allowed_level(is_hp1, mode_code, level, 1, oq_odu::MODEL_LEVEL_MAX);
  }

  void publish_optimizer_reason_(const char* reason) {
#if OQ_TOPOLOGY_DUO
    const std::string value(reason ? reason : "inactive");
    if (value == this->last_optimizer_reason_) return;
    id(oq_duo_optimizer_reason).publish_state(value.c_str());
    ESP_LOGI("quatt.req", "Optimizer reason: %s", value.c_str());
    this->last_optimizer_reason_ = value;
#else
    (void)reason;
#endif
  }

  void publish_request_(int mode_code, int hp1_level, int hp2_level, int strategy_code, const char* reason,
                        const oq_frequency_runtime::Context& frequency) {
    const bool manual = strategy_code == oq_request::STRATEGY_MANUAL_HP;
    const auto request = oq_request::make_published_request(
        mode_code, hp1_level, hp2_level, strategy_code,
        manual ? this->physical_limit_(true, mode_code, frequency) : oq_odu::MODEL_LEVEL_MAX,
        manual ? this->physical_limit_(false, mode_code, frequency) : oq_odu::MODEL_LEVEL_MAX);
    id(oq_request_mode_code) = request.mode_code;
    id(oq_request_hp1_level) = request.hp1_level;
    id(oq_request_hp2_level) = request.hp2_level;
    id(oq_request_owner_hp) = request.owner_hp;
    id(oq_request_topology_code) = request.topology_code;
    id(oq_request_strategy_code) = request.strategy_code;

    const std::string value(reason ? reason : "inactive");
    if (value == this->last_request_reason_) return;
    id(oq_request_reason).publish_state(value.c_str());
    ESP_LOGI("quatt.req", "Request reason: %s (mode=%d strategy=%d owner=%d topology=%d hp1=%d hp2=%d)", value.c_str(),
             request.mode_code, request.strategy_code, request.owner_hp, request.topology_code, request.hp1_level,
             request.hp2_level);
    this->last_request_reason_ = value;
  }

  void update_startup_event_(uint32_t now_ms, uint32_t minimum_off_s, int cm_code, bool startup_inhibit,
                             int target_mode_code, int desired_hp1, int desired_hp2) {
    const bool has_request = desired_hp1 > 0 || desired_hp2 > 0;
    const bool blocked = startup_inhibit && has_request;
    const uint8_t subject = desired_hp1 > 0 && desired_hp2 > 0 ? openquatt_decision_log::SUBJECT_BOTH
                            : desired_hp1 > 0                  ? openquatt_decision_log::SUBJECT_HP1
                            : desired_hp2 > 0                  ? openquatt_decision_log::SUBJECT_HP2
                                                               : openquatt_decision_log::SUBJECT_UNKNOWN;
    const uint8_t request_mode = static_cast<uint8_t>(oq_request::clamp_level(target_mode_code, 0, 255));
    const bool context_changed = blocked && id(oq_boot_startup_inhibit_event_active) &&
                                 (id(oq_boot_startup_inhibit_event_subject) != subject ||
                                  id(oq_boot_startup_inhibit_event_mode) != request_mode);
    if (context_changed) {
      this->emit_startup_event_(openquatt_decision_log::EVENT_STARTUP_INHIBIT_REFRESH, subject, request_mode,
                                openquatt_decision_log::STATE_BLOCKED, openquatt_decision_log::STATE_BLOCKED, now_ms,
                                minimum_off_s, cm_code);
      id(oq_boot_startup_inhibit_event_started_ms) = now_ms;
      id(oq_boot_startup_inhibit_event_subject) = subject;
      id(oq_boot_startup_inhibit_event_mode) = request_mode;
    } else if (id(oq_boot_startup_inhibit_event_active) && !blocked) {
      this->emit_startup_event_(
          openquatt_decision_log::EVENT_STARTUP_INHIBIT_CLEAR, id(oq_boot_startup_inhibit_event_subject),
          id(oq_boot_startup_inhibit_event_mode), openquatt_decision_log::STATE_BLOCKED,
          has_request ? openquatt_decision_log::STATE_ACTIVE : openquatt_decision_log::STATE_STANDBY, now_ms,
          minimum_off_s, cm_code);
      id(oq_boot_startup_inhibit_event_active) = false;
      id(oq_boot_startup_inhibit_event_started_ms) = 0;
      id(oq_boot_startup_inhibit_event_subject) = openquatt_decision_log::SUBJECT_UNKNOWN;
      id(oq_boot_startup_inhibit_event_mode) = 0;
    }
    if (blocked && !id(oq_boot_startup_inhibit_event_active)) {
      id(oq_boot_startup_inhibit_event_active) = true;
      id(oq_boot_startup_inhibit_event_started_ms) = now_ms;
      id(oq_boot_startup_inhibit_event_subject) = subject;
      id(oq_boot_startup_inhibit_event_mode) = request_mode;
      this->emit_startup_event_(openquatt_decision_log::EVENT_STARTUP_INHIBIT_START, subject, request_mode,
                                openquatt_decision_log::STATE_STANDBY, openquatt_decision_log::STATE_BLOCKED, now_ms,
                                minimum_off_s, cm_code);
    }
  }

  void emit_startup_event_(uint16_t event_code, uint8_t subject, uint8_t request_mode, uint8_t from_state,
                           uint8_t to_state, uint32_t now_ms, uint32_t minimum_off_s, int cm_code) {
    uint16_t duration_s = 0;
    if (id(oq_boot_startup_inhibit_event_started_ms) > 0) {
      const uint32_t elapsed_s = (now_ms - id(oq_boot_startup_inhibit_event_started_ms)) / 1000UL;
      duration_s = elapsed_s > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(elapsed_s);
    }
    const uint32_t remaining_s = oq_request::deadline_pending(now_ms, id(oq_boot_startup_inhibit_until_ms))
                                     ? (id(oq_boot_startup_inhibit_until_ms) - now_ms + 999UL) / 1000UL
                                     : 0;
    id(oq_decision_log)
        .emit(event_code, subject, openquatt_decision_log::REASON_STARTUP_INHIBIT,
              openquatt_decision_log::SEVERITY_NORMAL, static_cast<uint8_t>(oq_request::clamp_level(cm_code, 0, 255)),
              from_state, to_state, static_cast<int16_t>(request_mode),
              static_cast<int16_t>(std::min<uint32_t>(remaining_s, INT16_MAX)),
              static_cast<int16_t>(std::min<uint32_t>(minimum_off_s, INT16_MAX)), duration_s);
  }

  const char* manual_reason_(oq_request::ManualReason reason) const {
    if (reason == oq_request::MANUAL_SAFETY_STOP) return "manual_hp_safety_stop";
    if (reason == oq_request::MANUAL_STARTUP_INHIBIT) return "manual_hp_startup_inhibit";
    if (reason == oq_request::MANUAL_MODE_CONFLICT) return "manual_hp_mode_conflict";
    return "manual_hp";
  }

  void run_manual_(const TickConfig& config, const oq_request::ModeContext& mode, bool startup_inhibit,
                   uint32_t minimum_runtime_ms, const oq_frequency_runtime::Context& frequency, int raw, int post_cap,
                   int cap) {
    const int hp1_mode = oq_request::clamp_level(id(oq_manual_hp1_mode_code), 0, 2);
#if OQ_TOPOLOGY_DUO
    const int hp2_mode = oq_request::clamp_level(id(oq_manual_hp2_mode_code), 0, 2);
#else
    const int hp2_mode = 0;
#endif
    const float flow = id(flow_rate_selected).state;
    const bool flow_ok =
        oq_request::finite_value_at_least(id(flow_rate_selected).has_state(), flow, config.minimum_flow_lph);
    const bool safety_stop = id(oq_water_temp_hard_trip_active) || id(oq_lowflow_fault_active) || !flow_ok;
    const int hp1_hold = this->minimum_runtime_hold_(true, config.now_ms, minimum_runtime_ms, startup_inhibit);
#if OQ_TOPOLOGY_DUO
    const int hp2_hold = this->minimum_runtime_hold_(false, config.now_ms, minimum_runtime_ms, startup_inhibit);
#else
    const int hp2_hold = 0;
#endif
    const oq_request::ManualRequestInput input{
        OQ_TOPOLOGY_DUO,
        hp1_mode,
        hp2_mode,
        static_cast<int>(roundf(id(oq_manual_hp1_level).state)),
        static_cast<int>(roundf(id(oq_manual_hp2_level).state)),
        this->physical_limit_(true, hp1_mode, frequency),
        this->physical_limit_(false, hp2_mode, frequency),
        hp1_hold,
        hp2_hold,
        this->cooling_hold_(true, hp1_hold),
#if OQ_TOPOLOGY_DUO
        this->cooling_hold_(false, hp2_hold),
#else
        false,
#endif
        id(oq_manual_hp_stop_requested),
        safety_stop,
        startup_inhibit,
    };
    const auto request = oq_request::arbitrate_manual_request(input);
    id(oq_manual_hp_mode_allowed) = request.mode_allowed;
    this->update_startup_event_(config.now_ms, config.minimum_off_s, id(oq_control_mode_code), startup_inhibit,
                                hp1_mode > 0 ? hp1_mode : hp2_mode, request.desired_hp1_level,
                                request.desired_hp2_level);
    this->publish_optimizer_reason_("manual_hp");
    this->publish_request_(request.mode_code, request.hp1_level, request.hp2_level, oq_request::STRATEGY_MANUAL_HP,
                           this->manual_reason_(request.reason), frequency);
    id(oq_actuator_hp1_req) = request.hp1_level;
    id(oq_actuator_hp2_req) = request.hp2_level;
    this->publish_debug_(mode, config.now_ms, raw, post_cap, cap, request.hp1_level, request.hp2_level);
  }

  void run_inactive_(const TickConfig& config, const oq_request::ModeContext& mode, bool startup_inhibit,
                     uint32_t minimum_runtime_ms, const oq_frequency_runtime::Context& frequency, int raw, int post_cap,
                     int cap) {
    const int hp1_runtime = this->minimum_runtime_hold_(true, config.now_ms, minimum_runtime_ms, startup_inhibit);
    const int hp1_level = hp1_runtime > 0 ? hp1_runtime : this->defrost_hold_(true, frequency);
#if OQ_TOPOLOGY_DUO
    const int hp2_runtime = this->minimum_runtime_hold_(false, config.now_ms, minimum_runtime_ms, startup_inhibit);
    const int hp2_level = hp2_runtime > 0 ? hp2_runtime : this->defrost_hold_(false, frequency);
#else
    const int hp2_level = 0;
#endif
    const bool any_hold = hp1_level > 0 || hp2_level > 0;
    oq_request::reset_dual_runtime_state(id(oq_dual_hp_enabled), id(oq_dual_hp_enable_hold_elapsed_accum_min),
                                         id(oq_dual_hp_disable_hold_elapsed_accum_min),
                                         id(oq_dual_hp_emergency_hold_elapsed_accum_min), id(oq_curve_single_owner_hp),
                                         id(oq_duo_request_hold_until_ms), 0);
    this->publish_optimizer_reason_(any_hold ? "keep_current" : "inactive");
    this->update_startup_event_(config.now_ms, config.minimum_off_s, id(oq_control_mode_code), startup_inhibit, 0, 0,
                                0);
    this->publish_request_(any_hold ? this->hold_mode_(hp1_level, hp2_level) : 0, hp1_level, hp2_level,
                           oq_request::STRATEGY_INACTIVE, any_hold ? "inactive_cm_hold" : "inactive_cm", frequency);
    id(oq_actuator_hp1_req) = hp1_level;
    id(oq_actuator_hp2_req) = hp2_level;
    this->publish_debug_(mode, config.now_ms, raw, post_cap, cap, hp1_level, hp2_level);
  }

  void run_automatic_(const TickConfig& config, const oq_request::ModeContext& mode, bool startup_inhibit,
                      uint32_t minimum_runtime_ms, const oq_frequency_runtime::Context& frequency, int raw,
                      int post_cap, int cap) {
#if OQ_TOPOLOGY_DUO
    const bool lead_is_hp1 = id(hp1_minutes) <= OQ_REQUEST_SECONDARY_ID(minutes);
#else
    constexpr bool lead_is_hp1 = true;
    oq_request::reset_dual_runtime_state(id(oq_dual_hp_enabled), id(oq_dual_hp_enable_hold_elapsed_accum_min),
                                         id(oq_dual_hp_disable_hold_elapsed_accum_min),
                                         id(oq_dual_hp_emergency_hold_elapsed_accum_min), id(oq_curve_single_owner_hp),
                                         id(oq_duo_request_hold_until_ms), 1);
#endif
    this->publish_lead_(lead_is_hp1);
    const oq_request::StrategyRequestInput input{
        mode,
        OQ_TOPOLOGY_DUO,
        id(oq_cooling_request_hp1_level),
        id(oq_cooling_request_hp2_level),
        id(oq_cooling_request_owner_hp),
        id(oq_cooling_request_reason_code),
        id(oq_curve_dispatch_hp1_level),
        id(oq_curve_dispatch_hp2_level),
        id(oq_curve_request_owner_hp),
        id(oq_curve_capacity_mode_code),
        id(oq_ph_request_hp1_level),
        id(oq_ph_request_hp2_level),
        id(oq_ph_request_owner_hp),
    };
    auto request = oq_request::select_strategy_request(input);
#if OQ_TOPOLOGY_DUO
    if (!mode.power_house) {
      id(oq_duo_request_hold_until_ms) = 0;
      this->publish_optimizer_reason_("curve_mode");
    }
#endif
    if (mode.power_house) request.reason = oq_power_house_dispatch::request_reason_name(id(oq_ph_request_reason_code));
    this->publish_request_(mode.thermal_mode_code, request.hp1_level, request.hp2_level, mode.strategy_code,
                           request.reason, frequency);
    int hp1_level = this->allowed_(true, mode.thermal_mode_code, id(oq_request_hp1_level), frequency);
#if OQ_TOPOLOGY_DUO
    int hp2_level = this->allowed_(false, mode.thermal_mode_code, id(oq_request_hp2_level), frequency);
#else
    int hp2_level = 0;
#endif

    if (!mode.power_house) {
      const auto tuning = oq_curve::control_profile(
          id(oq_curve_control_profile).has_state() ? id(oq_curve_control_profile).current_option() : std::string());
      const uint32_t up_hold_ms =
          static_cast<uint32_t>((id(oq_curve_regime_code) == 1 ? tuning.recovery_up_hold_s : tuning.steady_up_hold_s)) *
          1000UL;
      const uint32_t down_hold_ms = static_cast<uint32_t>(tuning.steady_down_hold_s) * 1000UL;
      hp1_level = oq_request::limit_level_slew(hp1_level, id(hp1_last_applied_level), mode.cooling, config.now_ms,
                                               id(hp1_last_level_change_ms), up_hold_ms, down_hold_ms);
#if OQ_TOPOLOGY_DUO
      hp2_level = oq_request::limit_level_slew(hp2_level, OQ_REQUEST_SECONDARY_ID(last_applied_level), mode.cooling,
                                               config.now_ms, id(hp2_last_level_change_ms), up_hold_ms, down_hold_ms);
      oq_request::limit_duo_to_one_change(hp1_level, hp2_level, id(hp1_last_applied_level),
                                          OQ_REQUEST_SECONDARY_ID(last_applied_level), lead_is_hp1);
#endif
    }

    const bool hard_stop = id(oq_water_temp_hard_trip_active) || id(oq_lowflow_fault_active) ||
                           (!mode.cooling && id(oq_cold_start_hp_blocked));
    if (hard_stop) hp1_level = hp2_level = 0;
    this->update_startup_event_(config.now_ms, config.minimum_off_s, id(oq_control_mode_code), startup_inhibit,
                                mode.thermal_mode_code, hp1_level, hp2_level);
    if (startup_inhibit) hp1_level = hp2_level = 0;
    const bool hold_blocked = hard_stop || startup_inhibit;
    this->apply_minimum_runtime_(true, hp1_level, hold_blocked, mode.cooling, config.now_ms, minimum_runtime_ms,
                                 mode.thermal_mode_code, frequency);
#if OQ_TOPOLOGY_DUO
    this->apply_minimum_runtime_(false, hp2_level, hold_blocked, mode.cooling, config.now_ms, minimum_runtime_ms,
                                 mode.thermal_mode_code, frequency);
#else
    hp2_level = 0;
#endif
    hp1_level = this->allowed_(true, mode.thermal_mode_code, hp1_level, frequency);
#if OQ_TOPOLOGY_DUO
    hp2_level = this->allowed_(false, mode.thermal_mode_code, hp2_level, frequency);
#endif
    id(oq_actuator_hp1_req) = hp1_level;
    id(oq_actuator_hp2_req) = hp2_level;
    this->publish_debug_(mode, config.now_ms, raw, post_cap, cap, hp1_level, hp2_level);
  }

  void apply_minimum_runtime_(bool is_hp1, int& level, bool blocked, bool cooling, uint32_t now_ms,
                              uint32_t minimum_runtime_ms, int mode_code,
                              const oq_frequency_runtime::Context& frequency) {
    const uint32_t last_start = is_hp1 ? id(hp1_last_real_heat_start_ms) : id(hp2_last_real_heat_start_ms);
    const bool active = oq_request::min_runtime_window_active(now_ms, last_start, minimum_runtime_ms);
    const bool hold = oq_request::min_runtime_hold_required(level, blocked, active, this->last_applied_(is_hp1),
                                                            this->measured_thermal_(is_hp1));
    if (hold) level = this->allowed_(is_hp1, mode_code, 1, frequency);
    bool& last_hold = is_hp1 ? this->last_hp1_min_runtime_hold_ : this->last_hp2_min_runtime_hold_;
    if (hold == last_hold) return;
    if (hold) {
      const uint32_t elapsed_ms = now_ms - last_start;
      const uint32_t remaining_s =
          minimum_runtime_ms > elapsed_ms ? (minimum_runtime_ms - elapsed_ms + 999UL) / 1000UL : 0;
      ESP_LOGI("quatt.req",
               cooling ? "HP%d cooling stop deferred by minimum runtime: requesting minimum level for %u s"
                       : "HP%d minimum runtime hold active: keeping level 1 for %u s",
               is_hp1 ? 1 : 2, static_cast<unsigned int>(remaining_s));
    } else {
      ESP_LOGI("quatt.req", "HP%d minimum runtime hold cleared", is_hp1 ? 1 : 2);
    }
    last_hold = hold;
  }

  void publish_lead_(bool lead_is_hp1) {
    const int lead = lead_is_hp1 ? 1 : 2;
    if (id(oq_last_lead_hp) == lead) return;
    id(oq_last_lead_hp) = lead;
#if OQ_TOPOLOGY_DUO
    ESP_LOGI("quatt", "Runtime lead changed: %s (hp1_minutes=%d, hp2_minutes=%d)", lead_is_hp1 ? "HP1" : "HP2",
             id(hp1_minutes), OQ_REQUEST_SECONDARY_ID(minutes));
#else
    ESP_LOGI("quatt", "Runtime lead changed: HP1 (single topology)");
#endif
  }

  void publish_debug_(const oq_request::ModeContext& mode, uint32_t now_ms, int raw, int post_cap, int cap,
                      int hp1_request, int hp2_request) {
    const int strategy = id(oq_strategy_active_code);
    const char* mode_name = strategy == 1 ? "COOL" : strategy == 2 ? "CURVE" : strategy == 3 ? "PH" : "INACTIVE";
    const char* phase = id(oq_strategy_phase_text).has_state() ? id(oq_strategy_phase_text).state.c_str() : "idle";
    const uint32_t off_since = id(oq_curve_off_since_ms);
    const uint32_t off_age_s = off_since > 0 ? (now_ms - off_since) / 1000UL : 0;
    int hp1_current = -1;
    if (id(hp1_compressor_level).has_state()) {
      hp1_current = static_cast<int>(id(hp1_compressor_level).active_index().value_or(-1));
    }
    int hp2_current = -1;
    int hp2_runtime = 0;
    float hp2_working_mode_value = 0.0f;
    int hp2_applied = 0;
#if OQ_TOPOLOGY_DUO
    if (OQ_REQUEST_SECONDARY_ID(compressor_level).has_state()) {
      hp2_current = static_cast<int>(OQ_REQUEST_SECONDARY_ID(compressor_level).active_index().value_or(-1));
    }
    hp2_runtime = OQ_REQUEST_SECONDARY_ID(minutes);
    hp2_working_mode_value = OQ_REQUEST_SECONDARY_ID(working_mode).state;
    hp2_applied = OQ_REQUEST_SECONDARY_ID(last_applied_level);
#endif
    char buffer[768];
    snprintf(buffer, sizeof(buffer),
             "mode=%s cm=%d phase=%s raw=%d f=%d cap=%d pre=%d post=%d dual=%d on=%d off=%d em=%d lead=%d "
             "owner=%d req=%d+%d app=%d+%d cur=%d+%d wm=%.0f/%.0f sp=%.2f pv=%.2f room=%.2f/%.2f rtgt=%.2f "
             "phouse=%.0f preq=%.0f out=%.2f ri=%d off_age=%lus rt=%d/%d",
             mode_name, id(oq_control_mode_code), phase, raw, post_cap, cap, id(oq_curve_demand_pre_guardrail),
             id(oq_demand_curve), static_cast<int>(id(oq_dual_hp_enabled)),
             oq_request::whole_minutes_floor(id(oq_dual_hp_enable_hold_elapsed_accum_min)),
             oq_request::whole_minutes_floor(id(oq_dual_hp_disable_hold_elapsed_accum_min)),
             oq_request::whole_minutes_floor(id(oq_dual_hp_emergency_hold_elapsed_accum_min)), id(oq_last_lead_hp),
             id(oq_curve_single_owner_hp), hp1_request, hp2_request, id(hp1_last_applied_level), hp2_applied,
             hp1_current, hp2_current, id(hp1_working_mode).state, hp2_working_mode_value,
             id(oq_strategy_supply_target_temp), id(oq_system_supply_temp).state, id(room_temp_selected).state,
             id(room_setpoint_selected).state, id(room_setpoint_selected).state,
             mode.power_house ? id(oq_phouse_house_w).state : NAN, id(oq_strategy_requested_power_w),
             id(outside_temp_selected).state, static_cast<int>(id(oq_curve_restart_inhibit_active)),
             static_cast<unsigned long>(off_age_s), id(hp1_minutes), hp2_runtime);
    const std::string value(buffer);
    if (value == this->last_debug_) return;
    ESP_LOGD("quatt.req", "%s", value.c_str());
    this->last_debug_ = value;
  }

  std::string last_debug_;
  std::string last_optimizer_reason_;
  std::string last_request_reason_;
  bool last_hp1_min_runtime_hold_{false};
  bool last_hp2_min_runtime_hold_{false};
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

#undef OQ_REQUEST_SECONDARY_ID

}  // namespace oq_thermal_request_runtime
#endif
