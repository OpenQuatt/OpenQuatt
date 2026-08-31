#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "oq_supervisory_safety_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_supervisory_safety_runtime {

struct TickConfig {
  uint32_t now_ms;
  bool thermal_request;
  float minimum_flow_lph;
  uint32_t low_flow_fault_s;
  uint32_t flow_recover_s;
  float frost_on_c;
  float frost_off_c;
  uint32_t frost_nan_grace_s;
};

class Runtime {
 public:
  oq_supervisory_safety::Output tick(const TickConfig& tick) {
    if (!this->hydrated_) {
      this->state_.low_flow_fault_active = id(oq_lowflow_fault_active);
      this->state_.frost_active = id(oq_cm_frost_prev);
      this->hydrated_ = true;
    }

    const oq_supervisory_safety::Input input{
        tick.now_ms,
        tick.thermal_request,
        id(flow_rate_selected).has_state(),
        id(flow_rate_selected).state,
        id(outside_temp_selected).has_state(),
        id(outside_temp_selected).state,
    };
    const oq_supervisory_safety::Config config{
        tick.minimum_flow_lph,
        oq_supervisory_safety::seconds_to_ms(tick.low_flow_fault_s),
        oq_supervisory_safety::seconds_to_ms(tick.flow_recover_s),
        tick.frost_on_c,
        tick.frost_off_c,
        oq_supervisory_safety::seconds_to_ms(tick.frost_nan_grace_s),
    };
    const auto output = oq_supervisory_safety::step(input, config, this->state_);
    this->state_ = output.state;
    id(oq_lowflow_fault_active) = output.state.low_flow_fault_active;
    id(oq_cm_frost_prev) = output.frost_active;
    this->publish_binary_if_changed_(id(oq_lowflow_fault_active_bs), output.state.low_flow_fault_active);

    if (output.low_flow_fault_started) {
      id(oq_decision_log)
          .emit(openquatt_decision_log::EVENT_DECISION_BLOCKED, openquatt_decision_log::SUBJECT_SYSTEM,
                openquatt_decision_log::REASON_FLOW_TOO_LOW, openquatt_decision_log::SEVERITY_LIMITED, 1,
                openquatt_decision_log::STATE_LIMITED, openquatt_decision_log::STATE_BLOCKED,
                output.flow_valid ? this->int16_limited_(input.flow_lph) : 0, 0,
                this->int16_limited_(tick.minimum_flow_lph));
    }

    if (oq_supervisory_safety::force_standby(output.state.low_flow_fault_active, id(hp1_defrost).state,
                                             this->selected_level_(id(hp1_compressor_level)),
                                             id(hp1_last_applied_level))) {
      this->set_select_option_(id(hp1_set_working_mode), "Standby");
      this->set_select_option_(id(hp1_compressor_level), "0");
    }
#if OQ_TOPOLOGY_DUO
    if (oq_supervisory_safety::force_standby(output.state.low_flow_fault_active, id(hp2_defrost).state,
                                             this->selected_level_(id(hp2_compressor_level)),
                                             id(hp2_last_applied_level))) {
      this->set_select_option_(id(hp2_set_working_mode), "Standby");
      this->set_select_option_(id(hp2_compressor_level), "0");
    }
#endif
    return output;
  }

 private:
  template <typename T>
  static int selected_level_(T& select) {
    return select.has_state() ? static_cast<int>(select.active_index().value_or(-1)) : -1;
  }

  template <typename T>
  static void set_select_option_(T& select, const char* option) {
    if (select.has_state() && select.current_option() == option) return;
    auto call = select.make_call();
    call.set_option(option);
    call.perform();
  }

  template <typename T>
  static void publish_binary_if_changed_(T& sensor, bool value) {
    if (!sensor.has_state() || sensor.state != value) sensor.publish_state(value);
  }

  static int16_t int16_limited_(float value) {
    if (!std::isfinite(value)) return 0;
    return static_cast<int16_t>(lroundf(std::max(-32768.0f, std::min(32767.0f, value))));
  }

  bool hydrated_ = false;
  oq_supervisory_safety::State state_;
};

inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}

}  // namespace oq_supervisory_safety_runtime
#endif
