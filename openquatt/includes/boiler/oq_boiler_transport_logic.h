#pragma once

#ifndef OPENQUATT_OQ_BOILER_TRANSPORT_LOGIC_H_
#define OPENQUATT_OQ_BOILER_TRANSPORT_LOGIC_H_

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

}  // namespace oq_boiler_transport

#endif  // OPENQUATT_OQ_BOILER_TRANSPORT_LOGIC_H_
