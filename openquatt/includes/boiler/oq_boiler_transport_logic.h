#pragma once

#ifndef OPENQUATT_OQ_BOILER_TRANSPORT_LOGIC_H_
#define OPENQUATT_OQ_BOILER_TRANSPORT_LOGIC_H_

#include <math.h>
#include <stdint.h>

namespace oq_boiler_transport {

// Single source for which transport owns oq_boiler_transport_active.
// R1 owns via relay state, OpenTherm owns via link + CH active.
inline bool compute_otb_transport_active(bool link_available, bool ch_has_state, bool ch_state) {
  return link_available && ch_has_state && ch_state;
}

// Guard for the periodic OTB adapter: it must not overwrite R1-owned state.
inline bool otb_may_update_transport(bool opentherm_selected) { return opentherm_selected; }

inline bool should_clear_on_field_stale(bool opentherm_selected, bool field_is_stale) {
  return opentherm_selected && field_is_stale;
}

struct CommandAdapterInputs {
  bool opentherm_selected = false;
  bool runtime_available = false;
  bool link_available = false;
  bool status_fresh = false;
  bool output_requested = false;
  float target_c = NAN;
  float flow_lph = NAN;
  float minimum_flow_lph = 0.0f;
  bool previously_active = false;
  bool applied_target_has_state = false;
  float applied_target_c = NAN;
};

struct CommandAdapterDecision {
  bool target_valid = false;
  bool flow_valid = false;
  bool command_active = false;
  bool applied_start = false;
  bool applied_stop = false;
  bool withdraw_controller_request = false;
  bool prioritize_off_frames = false;
  bool write_target = false;
  float target_to_write_c = 0.0f;
};

inline CommandAdapterDecision evaluate_command_adapter(const CommandAdapterInputs& in) {
  CommandAdapterDecision out;
  out.target_valid = !isnan(in.target_c) && in.target_c > 0.0f && in.target_c <= 90.0f;
  out.flow_valid = !isnan(in.flow_lph) && in.flow_lph >= in.minimum_flow_lph;
  out.command_active = in.opentherm_selected && in.runtime_available && in.link_available && in.status_fresh &&
                       in.output_requested && out.target_valid && out.flow_valid;
  out.applied_start = !in.previously_active && out.command_active;
  out.applied_stop = in.previously_active && !out.command_active;
  out.withdraw_controller_request = out.applied_stop && in.output_requested;
  out.prioritize_off_frames = out.applied_stop && in.opentherm_selected && in.link_available;
  out.target_to_write_c = out.command_active ? in.target_c : 0.0f;
  out.write_target = !in.applied_target_has_state || isnan(in.applied_target_c) ||
                     fabsf(in.applied_target_c - out.target_to_write_c) >= 0.05f;
  return out;
}

}  // namespace oq_boiler_transport

#endif  // OPENQUATT_OQ_BOILER_TRANSPORT_LOGIC_H_
