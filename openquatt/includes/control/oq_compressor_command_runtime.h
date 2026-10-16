#pragma once
#include "oq_compressor_command_guard.h"
#include "oq_boot_resume_runtime.h"
#include "oq_compressor_frequency_runtime.h"
#include "../service/tasks/oq_manual_hp_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_command_runtime {
// Updated by the actuator on the same ESPHome loop before defrost acceptance.
inline float minimum_defrost_flow_lph = NAN;
#if OQ_TOPOLOGY_DUO
#define OQ_COMMAND_SECONDARY_ID(suffix) id(hp2_##suffix)
#else
#define OQ_COMMAND_SECONDARY_ID(suffix) id(hp1_##suffix)
#endif

inline bool guarded_write(bool hp1, uint16_t address, int value, bool queued = false) {
  const unsigned hp = hp1 ? 1U : 2U;
  if (oq_boot_resume_runtime::ota_hold(hp1)) return false;
  oq_boot_resume_runtime::update(hp1);
  if (id(oq_incident_manager).boot_resume_pending(hp, millis())) return false;
  if (address == 2010 || address == 2015) {
#if OQ_TOPOLOGY_DUO
    // A stopped peer can own the shared circulation during running acquisition.
    oq_boot_resume_runtime::update(!hp1);
    if (id(oq_incident_manager).boot_resume_pending(hp1 ? 2U : 1U, millis())) return false;
#endif
    if (oq_boot_resume_runtime::ota_hold(!hp1)) return false;
    return true;
  }
  if (address == 2006) return true;
  const auto incident = id(oq_incident_manager).get_outputs(hp);
  const bool hard_stop = id(oq_water_temp_hard_trip_active) || id(oq_lowflow_fault_active) || incident.must_stop ||
                         incident.run_state == oq_incidents::RunState::STOPPING ||
                         incident.run_state == oq_incidents::RunState::STOP_UNCONFIRMED;
  auto& service = hp1 ? id(hp1_odu_defrost) : OQ_COMMAND_SECONDARY_ID(odu_defrost);
  const bool fault = hard_stop || service.cycle.telemetry_fault(millis());
  if (queued && address == 3999 && value == 4) {
    const bool online = hp1 ? id(hp1_is_online) : OQ_COMMAND_SECONDARY_ID(is_online);
    const bool identity =
        hp1 ? id(hp1_odu_generation_detection_complete) && !id(hp1_odu_generation_revalidation_required)
            : OQ_COMMAND_SECONDARY_ID(odu_generation_detection_complete) &&
                  !OQ_COMMAND_SECONDARY_ID(odu_generation_revalidation_required);
    const float hz = hp1 ? id(hp1_compressor_frequency).state : OQ_COMMAND_SECONDARY_ID(compressor_frequency).state;
    const bool flow = id(flow_rate_selected).has_state() &&
                      oq_defrost::minimum_flow_ready(id(flow_rate_selected).state, minimum_defrost_flow_lph);
#if OQ_TOPOLOGY_DUO
    const auto& peer = hp1 ? id(hp2_odu_defrost) : id(hp1_odu_defrost);
    const bool peer_ready = !peer.busy() && !peer.cycle.observed_active() && peer.cycle.fresh(millis());
#else
    const bool peer_ready = true;
#endif
    return !fault && online && identity && service.forced_write_pending(millis()) && flow && peer_ready &&
           service.automatic() && !id(oq_commissioning_active) && incident.running_confirmed &&
           !id(oq_incident_manager).startup_inhibited(hp) && service.sample_fresh(0, millis()) && std::isfinite(hz) &&
           hz > 0 && (service.cycle.mode == 2 || service.cycle.mode == 3);
  }
  // A dispatch callback must not call normal_write_allowed: its safety-stop
  // branch clears queues and would invalidate the currently selected frame.
  if (queued) {
    if (!(value == 0 && fault) &&
        (!service.cycle.fresh(millis()) || service.cycle.observed_active() || service.holding()))
      return false;
  } else if (!service.normal_write_allowed(value, fault))
    return false;
  if (value == 0) return true;  // Stops never depend on temperature data.
  if (fault) return false;
  auto& mode = hp1 ? id(hp1_set_working_mode) : OQ_COMMAND_SECONDARY_ID(set_working_mode);
  auto& level = hp1 ? id(hp1_compressor_level) : OQ_COMMAND_SECONDARY_ID(compressor_level);
  const auto selected = level.active_index();
  if (address == 1999 && queued && (!selected.has_value() || value != static_cast<int>(selected.value()))) return false;
  const int mode_code =
      mode.has_state() ? (mode.current_option() == "Heating" ? 2 : (mode.current_option() == "Cooling" ? 1 : 0)) : 0;
  if (address == 3999 && queued && value != mode_code) return false;
  const int active_mode = address == 3999 ? value : mode_code;
  if (active_mode != 1 && active_mode != 2) return false;
  if (active_mode == 1) return true;  // Cooling has its own existing safety path.
  const auto snapshot = oq_odu::decode_runtime_frequency_snapshot(
      hp1 ? id(hp1_runtime_frequency_snapshot_storage) : OQ_COMMAND_SECONDARY_ID(runtime_frequency_snapshot_storage));
  const auto temperature = oq_temperature_runtime::capture(hp1, snapshot);
  if (address == 3999) {
    if (temperature.allowed_levels == 0) return false;
    if (!queued) return true;
    if (!selected.has_value() || selected.value() == 0 || !temperature.allows(static_cast<int>(selected.value())))
      return false;
    // A queued heating-mode write must also respect the current automatic
    // frequency cap/exclusion of its optimistic selected stand.
    value = static_cast<int>(selected.value());
  }
  if (!temperature.allows(value)) return false;
  // Manual service keeps its own user-frequency policy, but not an exception
  // to the temperature envelope or the normal-heating 90 Hz safety ceiling.
  if (oq_manual_hp::owns_control()) return true;
  const auto& cap = id(oq_silent_active).state ? id(oq_silent_max_frequency_hz) : id(oq_day_max_frequency_hz);
  const auto& excluded_min =
      hp1 ? id(hp1_excluded_frequency_min_hz) : OQ_COMMAND_SECONDARY_ID(excluded_frequency_min_hz);
  const auto& excluded_max =
      hp1 ? id(hp1_excluded_frequency_max_hz) : OQ_COMMAND_SECONDARY_ID(excluded_frequency_max_hz);
  return oq_frequency_policy::frequency_allowed(
      oq_odu::frequency_for_physical_level(snapshot.heating, value), oq_frequency_runtime::read_hz(cap),
      {oq_frequency_runtime::read_hz(excluded_min), oq_frequency_runtime::read_hz(excluded_max)});
}

inline bool queued_command(bool hp1, const uint8_t* pdu, size_t size) {
  const bool allowed = oq_command_guard::allows(
      pdu, size, [=](uint16_t address, uint16_t value) { return guarded_write(hp1, address, value, true); });
  if (!allowed)
    (hp1 ? id(hp1_compressor_command_revalidation_required)
         : OQ_COMMAND_SECONDARY_ID(compressor_command_revalidation_required)) = true;
  return allowed;
}
inline bool queued_address(uint8_t address, const uint8_t* pdu, size_t size) {
  if (address == id(hp1).device_address()) return queued_command(true, pdu, size);
#if OQ_TOPOLOGY_DUO
  if (address == id(hp2).device_address()) return queued_command(false, pdu, size);
#endif
  return true;
}
#undef OQ_COMMAND_SECONDARY_ID
}  // namespace oq_command_runtime
#endif
