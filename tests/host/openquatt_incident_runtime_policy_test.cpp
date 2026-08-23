#include "components/openquatt_decision_log/OpenQuattUrgentFlushPolicy.h"
#include "components/openquatt_incident_manager/OpenQuattIncidentPolicy.h"
#include "openquatt/includes/incidents/oq_manual_reset_latch_policy.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

using esphome::openquatt_decision_log::UrgentFlushPolicy;
using esphome::openquatt_incident_manager::all_hp_outputs_safe_for_fallback;
using esphome::openquatt_incident_manager::all_unavailable_hps_allow_fallback;
using esphome::openquatt_incident_manager::apply_persistence_initialization_gate;
using esphome::openquatt_incident_manager::apply_persistence_safety_gate;
using esphome::openquatt_incident_manager::availability_baseline_ready;
using esphome::openquatt_incident_manager::HpThermalCommand;
using esphome::openquatt_incident_manager::incident_storage_failure_outputs;
using esphome::openquatt_incident_manager::link_round_timeout_elapsed;
using esphome::openquatt_incident_manager::perform_start_failure_retry;
using esphome::openquatt_incident_manager::post_command_feedback_complete;
using esphome::openquatt_incident_manager::run_observation_is_fresh;
using esphome::openquatt_incident_manager::should_emit_operator_stop_confirmation;
using esphome::openquatt_incident_manager::should_emit_start_confirmation;
using esphome::openquatt_incident_manager::should_emit_stop_confirmation;
using esphome::openquatt_incident_manager::thermal_command_for_expected_mode;
using oq_incidents::ManualResetLatchMarker;
using oq_incidents::ManualResetLatchPersistencePolicy;
using oq_incidents::ManualResetLatchStorage;

namespace {

oq_incidents::RunObservation stopped_observation(uint32_t now_ms) {
  oq_incidents::RunObservation observation;
  observation.now_ms = now_ms;
  observation.fresh = true;
  observation.mode_matches_request = true;
  observation.stop_mode_confirmed = true;
  observation.compressor_frequency_valid = true;
  observation.compressor_frequency_hz = 0.0F;
  return observation;
}

void test_confirmation_audit_policy() {
  assert(thermal_command_for_expected_mode(1U) == HpThermalCommand::COOLING);
  assert(thermal_command_for_expected_mode(3U) == HpThermalCommand::COOLING);
  assert(thermal_command_for_expected_mode(2U) == HpThermalCommand::HEATING);
  assert(thermal_command_for_expected_mode(0U) == HpThermalCommand::UNKNOWN);

  assert(!should_emit_start_confirmation(false, false, true));
  assert(should_emit_start_confirmation(true, false, true));
  assert(!should_emit_stop_confirmation(false, false, true));
  assert(should_emit_stop_confirmation(true, false, true));
  assert(!should_emit_stop_confirmation(true, true, true));
  assert(!should_emit_operator_stop_confirmation(false, true));
  assert(!should_emit_operator_stop_confirmation(true, false));
  assert(should_emit_operator_stop_confirmation(true, true));

  assert(!availability_baseline_ready(oq_incidents::LinkState::BOOTSTRAP, false));
  assert(!availability_baseline_ready(oq_incidents::LinkState::SUSPECT, false));
  assert(!availability_baseline_ready(oq_incidents::LinkState::RECOVERING, false));
  assert(!availability_baseline_ready(oq_incidents::LinkState::HEALTHY, true));
  assert(availability_baseline_ready(oq_incidents::LinkState::HEALTHY, false));
  assert(availability_baseline_ready(oq_incidents::LinkState::LOST, false));

  assert(!post_command_feedback_complete(10U, 10U, 20U, 20U));
  assert(!post_command_feedback_complete(11U, 10U, 20U, 20U));
  assert(post_command_feedback_complete(11U, 10U, 21U, 20U));
  assert(post_command_feedback_complete(0U, std::numeric_limits<uint32_t>::max(), 0U,
                                        std::numeric_limits<uint32_t>::max()));

  // Passive boot observations may establish that an already-idle HP is
  // stopped. A commanded stop or revalidation must be armed and use feedback
  // newer than its write.
  assert(run_observation_is_fresh(false, true, false, false, false));
  assert(!run_observation_is_fresh(false, true, true, false, false));
  assert(!run_observation_is_fresh(false, true, true, true, false));
  assert(run_observation_is_fresh(false, true, true, true, true));
  assert(!run_observation_is_fresh(false, true, false, true, false));
  assert(run_observation_is_fresh(false, true, false, true, true));
  assert(!run_observation_is_fresh(true, false, false, false, false));
  assert(run_observation_is_fresh(true, false, false, false, true));
}

void test_start_failure_retry_production_policy() {
  using oq_incidents::StartFailureResetResult;
  assert(perform_start_failure_retry(nullptr, 0U) == StartFailureResetResult::HP_NOT_CONFIGURED);

  oq_incidents::HpIncidentEngine engine;
  assert(perform_start_failure_retry(&engine, 1U) == StartFailureResetResult::NO_START_FAILURE);
  engine.observe_link_round(100U, true);
  engine.observe_link_round(30100U, true);
  engine.observe_link_round(60100U, true);
  engine.observe_run(stopped_observation(60101U));
  engine.observe_run(stopped_observation(70101U));
  assert(engine.request_start(80101U));
  engine.tick(200102U);
  assert(perform_start_failure_retry(&engine, 200103U) == StartFailureResetResult::STOP_NOT_CONFIRMED);

  assert(engine.request_stop(200104U));
  engine.observe_run(stopped_observation(210104U));
  engine.observe_run(stopped_observation(220104U));
  assert(perform_start_failure_retry(&engine, 220105U) == StartFailureResetResult::CLEARED);
  assert(!engine.outputs().start_timed_out);
  assert(perform_start_failure_retry(&engine, 220106U) == StartFailureResetResult::NO_START_FAILURE);
}

void test_link_round_timeout_tolerates_scan_jitter() {
  constexpr uint32_t kTimeoutMs = 15000U;
  assert(!link_round_timeout_elapsed(10000U, 0U, kTimeoutMs));
  assert(!link_round_timeout_elapsed(11000U, 0U, kTimeoutMs));
  assert(!link_round_timeout_elapsed(14999U, 0U, kTimeoutMs));
  assert(link_round_timeout_elapsed(15000U, 0U, kTimeoutMs));

  oq_incidents::HpIncidentEngine engine;
  uint32_t last_round_ms = 100U;
  for (uint32_t round = 1U; round <= 7U; ++round) {
    const uint32_t now_ms = 100U + round * 11000U;
    assert(!link_round_timeout_elapsed(now_ms, last_round_ms, kTimeoutMs));
    engine.observe_link_round(now_ms, true);
    last_round_ms = now_ms;
  }
  assert(engine.outputs().link_state == oq_incidents::LinkState::HEALTHY);

  engine.observe_link_round(last_round_ms + 15000U, false);
  assert(engine.outputs().link_state == oq_incidents::LinkState::SUSPECT);
  engine.observe_link_round(last_round_ms + 30000U, false);
  assert(engine.outputs().link_state == oq_incidents::LinkState::SUSPECT);
  engine.observe_link_round(last_round_ms + 45000U, false);
  assert(engine.outputs().link_state == oq_incidents::LinkState::LOST);
}

void test_duo_fallback_coverage_fails_closed() {
  std::array<oq_incidents::DerivedOutputs, 2U> outputs{};
  outputs[0].fallback_cause_present = true;
  outputs[1].fallback_cause_present = false;
  assert(!all_unavailable_hps_allow_fallback(outputs.data(), outputs.size()));

  outputs[1].fallback_cause_present = true;
  assert(all_unavailable_hps_allow_fallback(outputs.data(), outputs.size()));
  assert(!all_hp_outputs_safe_for_fallback(outputs.data(), outputs.size()));

  outputs[1].available_for_start = true;
  assert(!all_unavailable_hps_allow_fallback(outputs.data(), outputs.size()));
  assert(!all_unavailable_hps_allow_fallback(nullptr, outputs.size()));
  assert(!all_unavailable_hps_allow_fallback(outputs.data(), 0U));
}

void test_incident_storage_failure_forces_safe_outputs() {
  const oq_incidents::DerivedOutputs outputs = incident_storage_failure_outputs();
  assert(outputs.link_state == oq_incidents::LinkState::BOOTSTRAP);
  assert(outputs.protection_state == oq_incidents::ProtectionState::FAULT_ACTIVE);
  assert(!outputs.available_for_start);
  assert(outputs.must_stop);
  assert(outputs.fault_active);
  assert(outputs.protection_active);
  assert(!outputs.stop_confirmed);
  assert(outputs.stop_unconfirmed);
  assert(!outputs.fallback_cause_present);
  assert(!outputs.fallback_eligible);
  assert(oq_incidents::has_effect(outputs.active_effects, oq_incidents::IncidentEffect::BLOCK_START));
  assert(oq_incidents::has_effect(outputs.active_effects, oq_incidents::IncidentEffect::STOP_COMPRESSOR));
  assert(oq_incidents::has_effect(outputs.active_effects, oq_incidents::IncidentEffect::BLOCK_BOILER));

  const std::array<oq_incidents::DerivedOutputs, 1U> failed{outputs};
  assert(!all_unavailable_hps_allow_fallback(failed.data(), failed.size()));
  assert(!all_hp_outputs_safe_for_fallback(failed.data(), failed.size()));
}

void test_persistence_failure_vetoes_real_fault_fallback() {
  std::array<oq_incidents::DerivedOutputs, 2U> outputs{};
  for (auto& output : outputs) {
    output.fallback_cause_present = true;
    output.fallback_eligible = true;
    output.stop_confirmed = true;
  }
  assert(all_unavailable_hps_allow_fallback(outputs.data(), outputs.size()));

  outputs[1] = apply_persistence_safety_gate(outputs[1], true);
  assert(outputs[1].fallback_cause_present);
  assert(!outputs[1].fallback_eligible);
  assert(oq_incidents::has_effect(outputs[1].active_effects, oq_incidents::IncidentEffect::BLOCK_BOILER));
  assert(!all_unavailable_hps_allow_fallback(outputs.data(), outputs.size()));
}

void test_fallback_output_confirmation() {
  std::array<oq_incidents::DerivedOutputs, 2U> outputs{};
  outputs[0].stop_confirmed = true;
  outputs[1].stop_confirmed = true;
  assert(all_hp_outputs_safe_for_fallback(outputs.data(), outputs.size()));

  outputs[1].stop_unconfirmed = true;
  assert(!all_hp_outputs_safe_for_fallback(outputs.data(), outputs.size()));
}

void test_persistence_overlay_is_fail_closed_and_causally_consistent() {
  oq_incidents::DerivedOutputs outputs{};
  outputs.available_for_start = true;
  outputs.fallback_eligible = true;
  const oq_incidents::DerivedOutputs blocked = apply_persistence_safety_gate(outputs, true);
  assert(!blocked.available_for_start);
  assert(!blocked.fallback_eligible);
  assert(blocked.fault_active);
  assert(blocked.protection_state == oq_incidents::ProtectionState::START_BLOCKED);
  assert(blocked.primary_incident_id == oq_incidents::kPersistenceFailureIncidentId);
  assert(blocked.active_incident_count == 1U);
  assert(oq_incidents::has_effect(blocked.active_effects, oq_incidents::IncidentEffect::BLOCK_BOILER));

  const oq_incidents::DerivedOutputs unchanged = apply_persistence_safety_gate(outputs, false);
  assert(unchanged.available_for_start);
  assert(unchanged.active_incident_count == 0U);
}

void test_first_urgent_flush_is_not_delayed_by_boot_interval() {
  UrgentFlushPolicy policy;
  constexpr uint64_t kCoalesceUs = 2000000ULL;
  constexpr uint64_t kMinIntervalUs = 15000000ULL;
  policy.request(1000000ULL, 10U);
  assert(!policy.should_attempt(2999999ULL, kCoalesceUs, kMinIntervalUs));
  assert(policy.should_attempt(3000000ULL, kCoalesceUs, kMinIntervalUs));
}

void test_new_urgent_event_is_not_cleared_by_inflight_flush() {
  UrgentFlushPolicy policy;
  constexpr uint64_t kCoalesceUs = 2000000ULL;
  constexpr uint64_t kMinIntervalUs = 15000000ULL;
  policy.request(1000000ULL, 20U);
  assert(policy.should_attempt(3000000ULL, kCoalesceUs, kMinIntervalUs));
  policy.mark_attempt(3000000ULL);

  policy.request(3100000ULL, 21U);
  policy.mark_target_persisted(4000000ULL, 20U);
  assert(policy.pending());
  assert(policy.requested_event_seq() == 21U);
  assert(!policy.should_attempt(6000000ULL, kCoalesceUs, kMinIntervalUs));
  assert(policy.should_attempt(19000000ULL, kCoalesceUs, kMinIntervalUs));
  policy.mark_attempt(19000000ULL);
  policy.mark_target_persisted(20000000ULL, 21U);
  assert(!policy.pending());
}

void test_failed_flush_obeys_retry_window_and_sequence_wrap() {
  UrgentFlushPolicy policy;
  policy.request(100U, std::numeric_limits<uint32_t>::max());
  policy.request(101U, 0U);
  assert(policy.requested_event_seq() == 0U);
  policy.mark_attempt(2000100U);
  policy.mark_failure(2000100U, 30000000ULL);
  assert(!policy.should_attempt(32000099U, 2000000ULL, 15000000ULL));
  assert(policy.should_attempt(32000100U, 2000000ULL, 15000000ULL));
  policy.mark_target_persisted(32000100U, 0U);
  assert(!policy.pending());
}

void test_urgent_flush_requires_exact_target_and_protects_ring_interval() {
  UrgentFlushPolicy policy;
  policy.request(100U, 42U);
  assert(policy.protects_unpersisted_sequence(40U, 39U));
  assert(policy.protects_unpersisted_sequence(42U, 39U));
  assert(!policy.protects_unpersisted_sequence(43U, 39U));
  assert(!policy.protects_unpersisted_sequence(39U, 39U));

  // A later sequence is not proof that the requested safety event was
  // actually part of the flash batch.
  policy.mark_target_persisted(200U, 43U);
  assert(policy.pending());
  assert(policy.requested_event_seq() == 42U);
  policy.mark_target_persisted(300U, 42U);
  assert(!policy.pending());
}

void test_manual_reset_missing_all_waits_for_observed_baseline() {
  ManualResetLatchPersistencePolicy policy;
  ManualResetLatchStorage storage_a{};
  ManualResetLatchStorage storage_b{};
  ManualResetLatchMarker marker{};
  const auto result =
      policy.load(false, storage_a, false, storage_b, false, marker, oq_incidents::kManualResetAllHpMask);
  assert(result == ManualResetLatchPersistencePolicy::LoadResult::INITIALIZATION_REQUIRED);
  assert(!policy.ready());
  assert(policy.initialization_pending());
  assert(policy.fault_mask() == oq_incidents::kManualResetAllHpMask);
  assert(!policy.should_attempt_persist(100U, 60000U));

  oq_incidents::DerivedOutputs outputs;
  outputs.available_for_start = true;
  const oq_incidents::DerivedOutputs gated = apply_persistence_initialization_gate(outputs, true);
  assert(!gated.available_for_start);
  assert(!gated.fault_active);
  assert(oq_incidents::has_effect(gated.active_effects, oq_incidents::IncidentEffect::BLOCK_BOILER));

  policy.complete_initialization(0U);
  assert(policy.should_attempt_persist(101U, 60000U));
  assert(policy.persistence_target_mask() == 0U);
  policy.mark_persist_success(0U, 101U);
  assert(policy.ready());
  assert(!policy.initialization_pending());
  assert(policy.persisted_mask() == 0U);
  assert(policy.fault_mask() == 0U);
  assert(!policy.should_attempt_persist(102U, 60000U));

  policy.observe_runtime_latches(oq_incidents::kManualResetHp1Mask);
  assert(policy.should_attempt_persist(103U, 60000U));
  assert(policy.persistence_target_mask() == oq_incidents::kManualResetHp1Mask);
}

void test_manual_reset_v1_upgrade_rebuilds_from_observed_faults() {
  ManualResetLatchStorage storage_a{};
  storage_a.version = oq_incidents::kManualResetLegacyStorageVersion;
  storage_a.latch_mask = oq_incidents::kManualResetAllHpMask;
  ManualResetLatchStorage storage_b = storage_a;
  ManualResetLatchMarker marker{};
  marker.version = oq_incidents::kManualResetLegacyStorageVersion;

  ManualResetLatchPersistencePolicy policy;
  assert(policy.load(true, storage_a, true, storage_b, true, marker, oq_incidents::kManualResetAllHpMask) ==
         ManualResetLatchPersistencePolicy::LoadResult::INITIALIZATION_REQUIRED);
  assert(policy.initialization_pending());
  assert(policy.persisted_mask() == 0U);
  policy.complete_initialization(oq_incidents::kManualResetHp2Mask);
  assert(policy.persistence_target_mask() == oq_incidents::kManualResetHp2Mask);
}

void test_manual_reset_restart_restore_and_partial_clear() {
  ManualResetLatchStorage stored_a{};
  stored_a.latch_mask = oq_incidents::kManualResetHp1Mask;
  ManualResetLatchStorage stored_b = stored_a;
  ManualResetLatchMarker marker{};

  ManualResetLatchPersistencePolicy restored;
  assert(restored.load(true, stored_a, true, stored_b, true, marker, oq_incidents::kManualResetAllHpMask) ==
         ManualResetLatchPersistencePolicy::LoadResult::RESTORED);
  assert(restored.persisted_mask() == oq_incidents::kManualResetHp1Mask);

  oq_incidents::HpIncidentEngine engine;
  const oq_incidents::IncidentId manual_reset_id = oq_incidents::incident_id(2120U, 4U);
  assert(engine.restore_power_cycle_latch(manual_reset_id));
  assert(engine.has_cleared_power_cycle_latch());
  assert(engine.outputs().must_stop);
  assert(!engine.outputs().available_for_start);

  // Power loss after only one redundant slot was cleared must conservatively
  // restore the OR of both slots.
  ManualResetLatchStorage partially_cleared{};
  ManualResetLatchPersistencePolicy after_partial_clear;
  assert(after_partial_clear.load(true, partially_cleared, true, stored_b, true, marker,
                                  oq_incidents::kManualResetAllHpMask) ==
         ManualResetLatchPersistencePolicy::LoadResult::RESTORED);
  assert(after_partial_clear.persisted_mask() == oq_incidents::kManualResetHp1Mask);
  assert(after_partial_clear.should_attempt_persist(100U, 60000U));
}

void test_manual_reset_confirm_is_per_hp_and_write_failure_safe() {
  ManualResetLatchStorage stored_a{};
  stored_a.latch_mask = oq_incidents::kManualResetAllHpMask;
  ManualResetLatchStorage stored_b = stored_a;
  ManualResetLatchMarker marker{};
  ManualResetLatchPersistencePolicy policy;
  assert(policy.load(true, stored_a, true, stored_b, true, marker, oq_incidents::kManualResetAllHpMask) ==
         ManualResetLatchPersistencePolicy::LoadResult::RESTORED);

  oq_incidents::HpIncidentEngine hp1;
  assert(hp1.restore_power_cycle_latch(oq_incidents::incident_id(2120U, 4U)));
  uint8_t target_mask = 0U;
  assert(policy.confirmation_target(1U, hp1.has_cleared_power_cycle_latch(), &target_mask));
  assert(target_mask == oq_incidents::kManualResetHp2Mask);

  // A failed/partial clear is not applied to runtime.
  ManualResetLatchStorage cleared_a{};
  cleared_a.latch_mask = target_mask;
  assert(
      !oq_incidents::redundant_manual_reset_state_matches(true, cleared_a, true, stored_b, true, marker, target_mask));
  policy.mark_confirmation_failure(1U, 100U);
  assert(hp1.has_power_cycle_latch());
  assert(policy.persisted_mask() == oq_incidents::kManualResetAllHpMask);
  assert((policy.fault_mask() & oq_incidents::kManualResetHp1Mask) != 0U);

  // Once both copies verify, persistent state changes first and only HP1 is
  // released; HP2 remains latched in storage.
  ManualResetLatchStorage cleared_b = cleared_a;
  assert(
      oq_incidents::redundant_manual_reset_state_matches(true, cleared_a, true, cleared_b, true, marker, target_mask));
  policy.mark_persist_success(target_mask, 200U);
  assert(hp1.confirm_odu_power_cycle(200U));
  assert(!hp1.has_power_cycle_latch());
  assert(policy.persisted_mask() == oq_incidents::kManualResetHp2Mask);
}

void test_manual_reset_ambiguous_storage_repairs_conservatively() {
  ManualResetLatchStorage invalid{};
  invalid.magic = 0U;
  ManualResetLatchStorage valid{};
  ManualResetLatchMarker marker{};
  ManualResetLatchPersistencePolicy policy;
  assert(policy.load(true, invalid, true, valid, true, marker, oq_incidents::kManualResetAllHpMask) ==
         ManualResetLatchPersistencePolicy::LoadResult::RECOVERY_REQUIRED);
  assert(!policy.ready());
  assert(!policy.load_failed());
  assert(policy.recovery_required());
  assert(policy.persisted_mask() == oq_incidents::kManualResetAllHpMask);
  assert(policy.fault_mask() == oq_incidents::kManualResetAllHpMask);
  assert(policy.should_attempt_persist(100U, 60000U));
  assert(policy.persistence_target_mask() == oq_incidents::kManualResetAllHpMask);
  uint8_t target_mask = 0U;
  assert(!policy.confirmation_target(1U, true, &target_mask));

  oq_incidents::HpIncidentEngine hp1;
  assert(hp1.restore_power_cycle_latch(oq_incidents::incident_id(2120U, 4U)));
  policy.mark_persist_success(oq_incidents::kManualResetAllHpMask, 101U);
  assert(policy.ready());
  assert(!policy.recovery_required());
  assert(hp1.has_cleared_power_cycle_latch());
  assert(!hp1.outputs().available_for_start);
  assert(policy.confirmation_target(1U, true, &target_mask));
  assert(target_mask == oq_incidents::kManualResetHp2Mask);
  policy.mark_persist_success(target_mask, 102U);
  assert(hp1.confirm_odu_power_cycle(102U));
  assert(!hp1.has_power_cycle_latch());
}

void test_manual_reset_brownout_and_missing_slot_never_restore_empty() {
  ManualResetLatchStorage empty{};
  ManualResetLatchMarker marker{};

  ManualResetLatchPersistencePolicy missing_slot;
  assert(missing_slot.load(true, empty, false, empty, true, marker, oq_incidents::kManualResetAllHpMask) ==
         ManualResetLatchPersistencePolicy::LoadResult::RECOVERY_REQUIRED);
  assert(missing_slot.persisted_mask() == oq_incidents::kManualResetAllHpMask);
  assert(missing_slot.should_attempt_persist(100U, 60000U));

  ManualResetLatchPersistencePolicy missing_marker;
  assert(missing_marker.load(true, empty, true, empty, false, marker, oq_incidents::kManualResetAllHpMask) ==
         ManualResetLatchPersistencePolicy::LoadResult::RECOVERY_REQUIRED);
  assert(missing_marker.persisted_mask() == oq_incidents::kManualResetAllHpMask);
  assert(missing_marker.persistence_target_mask() == oq_incidents::kManualResetAllHpMask);
}

}  // namespace

int main() {
  test_confirmation_audit_policy();
  test_start_failure_retry_production_policy();
  test_link_round_timeout_tolerates_scan_jitter();
  test_duo_fallback_coverage_fails_closed();
  test_incident_storage_failure_forces_safe_outputs();
  test_fallback_output_confirmation();
  test_persistence_overlay_is_fail_closed_and_causally_consistent();
  test_persistence_failure_vetoes_real_fault_fallback();
  test_first_urgent_flush_is_not_delayed_by_boot_interval();
  test_new_urgent_event_is_not_cleared_by_inflight_flush();
  test_failed_flush_obeys_retry_window_and_sequence_wrap();
  test_urgent_flush_requires_exact_target_and_protects_ring_interval();
  test_manual_reset_missing_all_waits_for_observed_baseline();
  test_manual_reset_v1_upgrade_rebuilds_from_observed_faults();
  test_manual_reset_restart_restore_and_partial_clear();
  test_manual_reset_confirm_is_per_hp_and_write_failure_safe();
  test_manual_reset_ambiguous_storage_repairs_conservatively();
  test_manual_reset_brownout_and_missing_slot_never_restore_empty();
  std::cout << "test_openquatt_incident_runtime_policy: ok\n";
  return 0;
}
