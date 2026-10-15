#pragma once

#include "oq_hp_boot_resume.h"
#include "oq_compressor_frequency_runtime.h"
#include "../service/tasks/oq_manual_hp_logic.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_boot_resume_runtime {
inline float minimum_flow_lph = NAN;
struct Sources {
  bool permission_valid{false}, permission_on{false}, flow_fresh{false};
  float flow_lph{NAN};
};
// Installed at boot, called synchronously on the main loop at every write.
// Keep source resolution out of the Modbus guard's dependency graph.
inline Sources (*read_sources)(uint64_t now) = nullptr;
#if OQ_TOPOLOGY_DUO
#define OQ_RESUME_SECONDARY_ID(suffix) id(hp2_##suffix)
#else
#define OQ_RESUME_SECONDARY_ID(suffix) id(hp1_##suffix)
#endif

struct Safety {
  bool abort;
  bool ready;
};

inline Safety capture(bool hp1) {
  const auto& raw = hp1 ? oq_sources::hp1
#if OQ_TOPOLOGY_DUO
                        : oq_sources::hp2;
#else
                        : oq_sources::hp1;
#endif
  const uint64_t now = oq_sources::monotonic_ms();
  constexpr auto fresh_ms = oq_boot_resume::TELEMETRY_FRESH_MS;
  const auto sources = read_sources == nullptr ? Sources{} : read_sources(now);
  const bool permission_valid = sources.permission_valid;
  const bool permission_on = sources.permission_on;
  const bool flow_fresh = sources.flow_fresh;
  const bool flow_ok = flow_fresh && std::isfinite(minimum_flow_lph) && sources.flow_lph >= minimum_flow_lph;
  const bool identity = hp1 ? id(hp1_odu_generation_detection_complete) &&
                                  !id(hp1_odu_generation_revalidation_required) &&
                                  !id(hp1_runtime_frequency_revalidation_required)
                            : OQ_RESUME_SECONDARY_ID(odu_generation_detection_complete) &&
                                  !OQ_RESUME_SECONDARY_ID(odu_generation_revalidation_required) &&
                                  !OQ_RESUME_SECONDARY_ID(runtime_frequency_revalidation_required);
  const auto& service = hp1 ? id(hp1_odu_defrost) : OQ_RESUME_SECONDARY_ID(odu_defrost);
  const bool measurements =
      raw.water_in.fresh(now, fresh_ms) && raw.water_out.fresh(now, fresh_ms) && raw.outside.fresh(now, fresh_ms);
  const bool cycle_fresh = raw.working_mode.fresh(now, fresh_ms) && raw.compressor_frequency.fresh(now, fresh_ms) &&
                           raw.defrost.fresh(now, fresh_ms) && raw.status_2119.fresh(now, fresh_ms);
  const bool normal_heating = cycle_fresh && raw.working_mode.value == 2.0f && raw.compressor_frequency.value > 0.5f &&
                              raw.defrost.value == 0.0f &&
                              (static_cast<uint16_t>(raw.status_2119.value) & 0x0008U) == 0U;
  const int level = id(oq_incident_manager).boot_resume_level(hp1 ? 1U : 2U);
  const auto frequency = oq_frequency_runtime::capture();
  const bool level_allowed = frequency.frequency_allowed(hp1, 2, level);
  const bool enabled = id(oq_enabled).has_state() && id(oq_enabled).state;
  const bool controls = enabled && !id(oq_commissioning_active) && !oq_manual_hp::owns_control() &&
                        id(oq_control_mode_code) != 5 && id(oq_strategy_active_code) != 1;
  const bool invalid_measurement = (raw.water_in.received && !raw.water_in.valid) ||
                                   (raw.water_out.received && !raw.water_out.valid) ||
                                   (raw.outside.received && !raw.outside.valid);
  const bool explicit_abort =
      invalid_measurement || (id(oq_enabled).has_state() && !id(oq_enabled).state) || id(oq_commissioning_active) ||
      oq_manual_hp::owns_control() || id(oq_control_mode_code) == 5 || id(oq_strategy_active_code) == 1 ||
      (service.loaded() && !service.automatic()) || (permission_valid && !permission_on) ||
      id(oq_water_temp_hard_trip_active) || id(oq_lowflow_fault_active) ||
      (flow_fresh && std::isfinite(minimum_flow_lph) && !flow_ok) ||
      (raw.defrost.fresh(now, fresh_ms) && raw.defrost.value != 0.0f) ||
      (raw.status_2119.fresh(now, fresh_ms) && (static_cast<uint16_t>(raw.status_2119.value) & 0x0008U) != 0U) ||
      (identity && measurements && level > 0 && !level_allowed);
  return {explicit_abort, controls && permission_valid && permission_on && identity && measurements && normal_heating &&
                              flow_ok && level_allowed};
}

// Called before enqueue AND dispatch, so an old queued command cannot escape
// the acquisition window. Only the actuator can commit adoption and seed state.
inline int update(bool hp1, bool allow_resume = false, bool inputs_ready = false) {
  const unsigned hp = hp1 ? 1U : 2U;
  if (!id(oq_incident_manager).boot_resume_pending(hp, millis())) return 0;
  const auto safety = capture(hp1);
  return id(oq_incident_manager)
      .update_boot_resume(hp, safety.abort, safety.ready, inputs_ready, allow_resume, millis());
}
#undef OQ_RESUME_SECONDARY_ID
}  // namespace oq_boot_resume_runtime
#endif
