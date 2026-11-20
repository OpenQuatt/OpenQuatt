#pragma once

#include "oq_power_house_peer_pump_logic.h"

#if OQ_TOPOLOGY_DUO
#include "oq_sensor_source_runtime.h"
#include "../service/tasks/oq_manual_hp_logic.h"
#include "../sources/oq_source_receipt_runtime.h"

namespace oq_peer_pump {

inline FlowEvidence selected_flow() {
  auto& sources = oq_sensor_source::runtime();
  constexpr uint8_t kHp1 = 1U;
  constexpr uint8_t kHp2 = 2U;
  const auto current = sources.flow_selection();
  if (!current.valid || current.invalid_received) return {};

  FlowEvidence evidence;
  evidence.generation = sources.resolved_flow_rate().configuration_generation;
  evidence.route = static_cast<uint8_t>(current.route);
  using Route = oq_input_source::FlowRoute;
  switch (current.route) {
    case Route::HP1:
      evidence.receipt = oq_sources::hp1.flow;
      evidence.contributors = kHp1;
      break;
    case Route::HP2:
      evidence.receipt = oq_sources::hp2.flow;
      evidence.contributors = kHp2;
      break;
    case Route::AGGREGATE:
      evidence.receipt = oq_sources::local_flow;
      evidence.contributors = oq_sources::peer_aggregate_contributors;
      break;
    default:
      return {};
  }
  // Only outdoor-unit registers have producer-side continuity evidence.
  // Treat both physical contributors of a mean as required and exclude any
  // aggregate that used the controller's pulse meter instead of HP1.
  const bool covered = evidence.contributors && !(evidence.contributors & ~(kHp1 | kHp2));
  return covered && evidence.receipt.valid && evidence.receipt.value == current.value ? evidence : FlowEvidence{};
}

inline DuoEstimate observe(DuoEstimator& estimator) {
  const auto flow = selected_flow();
  const uint64_t now = oq_sources::monotonic_ms();
  // A constant, heating-only PI target is the condition evaluated in the
  // field trial. Manual PWM and moving Delta T targets lack that evidence.
  const bool active =
      id(oq_control_mode_code) == 2 && !oq_manual_hp::owns_control() && id(oq_flow_control_mode).has_state() &&
      id(oq_flow_control_mode).active_index().value_or(255) == 0 && id(oq_flow_setpoint_lph).has_state();
  uint64_t flow_invalidated_ms =
      std::max(oq_sources::peer_flow_target_changed_ms, oq_sources::peer_flow_source_changed_ms);
  if (flow.route == static_cast<uint8_t>(oq_input_source::FlowRoute::AGGREGATE)) {
    flow_invalidated_ms = std::max(flow_invalidated_ms, oq_sources::peer_aggregate_changed_ms);
  }
  const auto& hp1 = oq_sources::hp1;
  const auto& hp2 = oq_sources::hp2;
  if ((flow.contributors & 1U) != 0U) {
    flow_invalidated_ms = std::max(flow_invalidated_ms, hp1.flow_evidence_revoked_ms);
  }
  if ((flow.contributors & 2U) != 0U) {
    flow_invalidated_ms = std::max(flow_invalidated_ms, hp2.flow_evidence_revoked_ms);
  }
  const float target = id(oq_flow_setpoint_lph).state;
  const auto hp1_variant = oq_odu::confirmed_variant(
      id(hp1_odu_generation_detection_complete) && !id(hp1_odu_generation_revalidation_required),
      id(hp1_generation_variant_code));
  const auto hp2_variant = oq_odu::confirmed_variant(
      id(hp2_odu_generation_detection_complete) && !id(hp2_odu_generation_revalidation_required),
      id(hp2_generation_variant_code));
  return {estimator.hp1.observe({now, active, id(hp1_is_online), hp1_variant, target, flow, hp1.pump_feedback,
                                 hp1.status_2108, std::max(flow_invalidated_ms, hp1.pump_evidence_revoked_ms)}),
          estimator.hp2.observe({now, active, id(hp2_is_online), hp2_variant, target, flow, hp2.pump_feedback,
                                 hp2.status_2108, std::max(flow_invalidated_ms, hp2.pump_evidence_revoked_ms)})};
}

}  // namespace oq_peer_pump
#endif
