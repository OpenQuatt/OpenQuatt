#pragma once
#include "oq_compressor_command_guard.h"
#include "oq_compressor_frequency_runtime.h"
#include "../service/tasks/oq_manual_hp_logic.h"
#include "../odu/oq_odu_eeprom_maintenance.h"

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
  const auto incident = id(oq_incident_manager).get_outputs(hp);
  const bool hard_stop = id(oq_water_temp_hard_trip_active) || id(oq_lowflow_fault_active) || incident.must_stop ||
                         incident.run_state == oq_incidents::RunState::STOPPING ||
                         incident.run_state == oq_incidents::RunState::STOP_UNCONFIRMED;
  auto& service = hp1 ? id(hp1_odu_defrost) : OQ_COMMAND_SECONDARY_ID(odu_defrost);
  const bool fault = hard_stop || service.cycle.telemetry_fault(millis());
  if (oq_eeprom::holding() && !(value == 0 && fault)) return false;
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
  // Preserve mapping/identity validation independently of any temperature model.
  const bool online = hp1 ? id(hp1_is_online) : OQ_COMMAND_SECONDARY_ID(is_online);
  const bool identity =
      hp1 ? id(hp1_odu_generation_detection_complete) : OQ_COMMAND_SECONDARY_ID(odu_generation_detection_complete);
  const bool revalidating = hp1 ? id(hp1_runtime_frequency_revalidation_required)
                                : OQ_COMMAND_SECONDARY_ID(runtime_frequency_revalidation_required);
  const int variant = hp1 ? id(hp1_generation_variant_code) : OQ_COMMAND_SECONDARY_ID(generation_variant_code);
  if (!online || !identity || revalidating || oq_odu::confirmed_variant(identity, variant) != snapshot.variant ||
      snapshot.variant == oq_odu::Variant::UNKNOWN || !snapshot.heating.valid ||
      !oq_odu::validate_frequency_table(snapshot.heating))
    return false;
  if (address == 3999) {
    if (!queued) return true;
    if (!selected.has_value() || selected.value() == 0) return false;
    value = static_cast<int>(selected.value());
  }
  if (value <= 0 || value >= snapshot.heating.level_count) return false;
  // Manual service keeps its existing user-frequency policy. Temperature
  // dependent frequency regulation remains owned by the ODU.
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
inline bool eeprom_runtime_clear(bool hp1) {
  const unsigned hp = hp1 ? 1U : 2U;
  const auto incident = id(oq_incident_manager).get_outputs(hp);
  const auto& service = hp1 ? id(hp1_odu_defrost) : OQ_COMMAND_SECONDARY_ID(odu_defrost);
  const float hz = hp1 ? id(hp1_compressor_frequency).state : OQ_COMMAND_SECONDARY_ID(compressor_frequency).state;
  return !id(oq_water_temp_hard_trip_active) && !id(oq_lowflow_fault_active) && !incident.must_stop &&
         incident.run_state != oq_incidents::RunState::STOPPING &&
         incident.run_state != oq_incidents::RunState::STOP_UNCONFIRMED && !incident.running_confirmed &&
         !id(oq_commissioning_active) && !id(oq_runtime_polling_paused).state &&
         !esphome::web_server_base::global_web_server_base->is_recovery_active() &&
         (hp1 ? id(hp1_is_online) : OQ_COMMAND_SECONDARY_ID(is_online)) &&
         (hp1 ? id(hp1_odu_generation_detection_complete) && !id(hp1_odu_generation_revalidation_required)
              : OQ_COMMAND_SECONDARY_ID(odu_generation_detection_complete) &&
                    !OQ_COMMAND_SECONDARY_ID(odu_generation_revalidation_required)) &&
         service.cycle.fresh(millis()) && !service.cycle.observed_active() && !service.cycle.manual &&
         (service.cycle.mode == 0 ||
          (oq_eeprom::permit.writing_mode && oq_eeprom::permit.hp == hp && service.cycle.mode == 5)) &&
         service.sample_fresh(0, millis()) && std::isfinite(hz) && hz == 0.0f;
}
inline bool queued_address(uint8_t address, const uint8_t* pdu, size_t size) {
  if (oq_eeprom::holding() && size > 0U && (pdu[0] == 6U || pdu[0] == 16U)) {
    if (oq_eeprom::permitted_write(address, pdu, size, millis())) {
      const bool clear = eeprom_runtime_clear(true)
#if OQ_TOPOLOGY_DUO
                         && eeprom_runtime_clear(false)
#endif
          ;
      if (!clear) ++oq_eeprom::stop_epoch;
      return clear;
    }
    // Only the ordinary guarded safety-stop route may interrupt this job.
    if (size != 5U || pdu[0] != 6U || pdu[3] != 0U || pdu[4] != 0U ||
        ((uint16_t(pdu[1]) << 8U | pdu[2]) != 1999U && (uint16_t(pdu[1]) << 8U | pdu[2]) != 3999U))
      return false;
  }
  if (address == id(hp1).device_address()) return queued_command(true, pdu, size);
#if OQ_TOPOLOGY_DUO
  if (address == id(hp2).device_address()) return queued_command(false, pdu, size);
#endif
  return true;
}
#undef OQ_COMMAND_SECONDARY_ID
}  // namespace oq_command_runtime
#endif
