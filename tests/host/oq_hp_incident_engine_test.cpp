#include "openquatt/includes/incidents/oq_hp_incident_engine.h"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace oq_incidents;

namespace {

FaultWordsObservation words(uint32_t now_ms, uint16_t r2119 = 0U, uint16_t r2120 = 0U, uint16_t r2121 = 0U,
                            bool fresh = true) {
  FaultWordsObservation observation;
  observation.now_ms = now_ms;
  observation.words = {{r2119, r2120, r2121}};
  observation.fresh = {{fresh, fresh, fresh}};
  return observation;
}

RunObservation frequency(uint32_t now_ms, float frequency_hz, bool mode_matches = true, bool fresh = true,
                         bool stop_mode_confirmed = true) {
  RunObservation observation;
  observation.now_ms = now_ms;
  observation.fresh = fresh;
  observation.mode_matches_request = mode_matches;
  observation.stop_mode_confirmed = stop_mode_confirmed;
  observation.compressor_frequency_valid = true;
  observation.compressor_frequency_hz = frequency_hz;
  return observation;
}

void establish_healthy_link(HpIncidentEngine& engine, uint32_t start_ms) {
  engine.observe_link_round(start_ms, true);
  engine.observe_link_round(start_ms + 30000U, true);
  engine.observe_link_round(start_ms + 60000U, true);
  assert(engine.outputs().link_state == LinkState::HEALTHY);
}

void confirm_stopped(HpIncidentEngine& engine, uint32_t start_ms) {
  engine.observe_run(frequency(start_ms, 0.0F));
  engine.observe_run(frequency(start_ms + 10000U, 0.0F));
  assert(engine.outputs().run_state == RunState::STOPPED);
}

void test_catalog_classification() {
  const IncidentDefinition oil_return = definition_for(2119U, 3U);
  assert(oil_return.category == IncidentCategory::STATUS);
  assert(!has_effect(oil_return.effects, IncidentEffect::BLOCK_START));

  const IncidentDefinition speed_limit = definition_for(2119U, 5U);
  assert(speed_limit.category == IncidentCategory::PROTECTION);
  assert(has_effect(speed_limit.effects, IncidentEffect::LIMIT_CAPACITY));
  assert(!has_effect(speed_limit.effects, IncidentEffect::STOP_COMPRESSOR));
  assert(speed_limit.recovery_condition == RecoveryCondition::AFTER_STABLE_READS);

  const IncidentDefinition preheat = definition_for(2119U, 6U);
  assert(has_effect(preheat.effects, IncidentEffect::BLOCK_START));
  assert(preheat.fallback_policy == FallbackPolicy::NEVER);
  assert(preheat.user_action == UserAction::WAIT_FOR_AUTOMATIC_RECOVERY);
  assert(preheat.recovery_condition == RecoveryCondition::PREHEAT_COMPLETE);
  assert(preheat.presentation_key[0] != '\0');

  const IncidentDefinition lock = definition_for(2120U, 4U);
  assert(lock.clear_policy == ClearPolicy::AFTER_CONFIRMED_ODU_POWER_CYCLE);

  const IncidentDefinition pump = definition_for(2121U, 13U);
  assert(has_effect(pump.effects, IncidentEffect::PUMP_UNAVAILABLE));
  assert(has_effect(pump.effects, IncidentEffect::ALLOW_CM4));

  const IncidentDefinition unknown = definition_for(2121U, 12U);
  assert(unknown.documentation_confidence == DocumentationConfidence::REVIEW_REQUIRED);
  assert(has_effect(unknown.effects, IncidentEffect::BLOCK_START));
  assert(!has_effect(unknown.effects, IncidentEffect::STOP_COMPRESSOR));
  assert(unknown.fallback_policy == FallbackPolicy::NEVER);
}

void test_short_link_dip_and_confirmed_loss() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);
  assert(engine.request_start(80101U));
  engine.observe_run(frequency(80102U, 12.0F));
  assert(engine.outputs().run_state == RunState::RUNNING);

  engine.observe_link_round(90100U, false);
  assert(engine.outputs().link_state == LinkState::SUSPECT);
  assert(!engine.outputs().must_stop);
  assert(engine.outputs().run_state == RunState::RUNNING);
  engine.observe_link_round(100100U, true);
  assert(engine.outputs().link_state == LinkState::HEALTHY);
  assert(engine.outputs().run_state == RunState::RUNNING);

  engine.observe_link_round(110100U, false);
  engine.observe_link_round(141100U, false);
  assert(engine.outputs().link_state == LinkState::SUSPECT);
  engine.observe_link_round(142100U, false);
  assert(engine.outputs().link_state == LinkState::LOST);
  assert(engine.outputs().must_stop);
  assert(engine.outputs().run_state == RunState::STOPPING);
  assert(engine.outputs().stop_confirmation_pending);
  assert(!engine.outputs().stop_unconfirmed);

  engine.observe_link_round(152100U, true);
  assert(engine.outputs().link_state == LinkState::RECOVERING);
  engine.observe_link_round(182100U, true);
  assert(engine.outputs().link_state == LinkState::RECOVERING);
  engine.observe_link_round(212100U, true);
  assert(engine.outputs().link_state == LinkState::HEALTHY);
  assert(!engine.request_start(212101U));
}

void test_link_loss_invalidates_old_stop_confirmation() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);
  assert(engine.outputs().stop_confirmed);

  engine.observe_link_round(90100U, false);
  assert(engine.outputs().link_state == LinkState::SUSPECT);
  assert(engine.outputs().stop_confirmed);
  assert(!engine.outputs().must_stop);
  assert(!engine.outputs().fallback_cause_present);

  engine.observe_link_round(121100U, false);
  engine.observe_link_round(122100U, false);
  assert(engine.outputs().link_state == LinkState::LOST);
  assert(engine.outputs().stop_confirmation_pending);
  assert(!engine.outputs().stop_unconfirmed);
  assert(!engine.outputs().stop_confirmed);
  assert(engine.outputs().fallback_cause_present);
  assert(!engine.outputs().fallback_eligible);

  assert(engine.request_stop(122101U));
  engine.observe_run(frequency(123100U, 0.0F));
  assert(!engine.outputs().stop_confirmed);
  assert(!engine.outputs().fallback_eligible);
  engine.observe_run(frequency(124100U, 0.0F));
  assert(engine.outputs().stop_confirmed);
  assert(!engine.outputs().stop_confirmation_pending);
  assert(engine.outputs().fallback_eligible);
}

void test_link_loss_rearms_an_inflight_stop() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);
  assert(engine.request_start(80101U));
  engine.observe_run(frequency(80102U, 12.0F));
  assert(engine.request_stop(90101U));
  engine.observe_run(frequency(90102U, 0.0F));
  assert(engine.outputs().run_state == RunState::STOPPING);

  engine.observe_link_round(100100U, false);
  engine.observe_link_round(131100U, false);
  engine.observe_link_round(132100U, false);
  assert(engine.outputs().link_state == LinkState::LOST);
  assert(engine.outputs().run_state == RunState::STOPPING);
  assert(engine.outputs().stop_confirmation_pending);
  assert(!engine.outputs().stop_unconfirmed);

  // The next actuator pass must register a new stop and generation baseline.
  assert(engine.request_stop(132101U));
  engine.observe_run(frequency(132102U, 0.0F));
  assert(!engine.outputs().stop_confirmed);
  engine.observe_run(frequency(132103U, 0.0F));
  assert(engine.outputs().stop_confirmed);
}

void test_new_hard_fault_invalidates_old_stop_confirmation() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);
  assert(engine.outputs().stop_confirmed);

  constexpr uint16_t kHardFault = 1U << 0U;
  engine.observe_fault_words(words(80101U, kHardFault));
  assert(engine.outputs().stop_confirmed);
  engine.observe_fault_words(words(90101U, kHardFault));
  assert(engine.outputs().fallback_cause_present);
  assert(engine.outputs().run_state == RunState::STOPPING);
  assert(engine.outputs().stop_confirmation_pending);
  assert(!engine.outputs().stop_unconfirmed);
  assert(!engine.outputs().stop_confirmed);
  assert(!engine.outputs().fallback_eligible);

  engine.observe_run(frequency(90102U, 0.0F));
  engine.observe_run(frequency(90103U, 0.0F));
  assert(engine.outputs().stop_confirmation_pending);
  assert(!engine.outputs().stop_confirmed);

  assert(engine.request_stop(90104U));
  engine.observe_run(frequency(100102U, 0.0F));
  assert(!engine.outputs().stop_confirmed);
  assert(!engine.request_stop(100103U));
  engine.observe_run(frequency(110102U, 0.0F));
  assert(engine.outputs().stop_confirmed);
  assert(!engine.outputs().stop_confirmation_pending);
  assert(engine.outputs().fallback_eligible);
}

void test_new_hard_fault_discards_partial_stop_confirmation() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);
  assert(engine.request_start(80101U));
  engine.observe_run(frequency(80102U, 12.0F));
  assert(engine.request_stop(90101U));
  engine.observe_run(frequency(90102U, 0.0F));
  assert(engine.outputs().run_state == RunState::STOPPING);

  constexpr uint16_t kHardFault = 1U << 0U;
  engine.observe_fault_words(words(100101U, kHardFault));
  engine.observe_fault_words(words(110101U, kHardFault));
  assert(engine.outputs().run_state == RunState::STOPPING);
  assert(engine.outputs().stop_confirmation_pending);
  assert(!engine.outputs().stop_unconfirmed);
  assert(!engine.outputs().stop_confirmed);
  assert(!engine.outputs().fallback_eligible);

  assert(engine.request_stop(110102U));
  engine.observe_run(frequency(110103U, 0.0F));
  assert(!engine.outputs().stop_confirmed);
  engine.observe_run(frequency(110104U, 0.0F));
  assert(engine.outputs().stop_confirmed);
  assert(engine.outputs().fallback_eligible);
}

void test_status_clears_without_acknowledgeable_history() {
  HpIncidentEngine engine;
  constexpr uint16_t kOilReturnStatus = 1U << 3U;
  const IncidentId id = incident_id(2119U, 3U);

  engine.observe_fault_words(words(100U, kOilReturnStatus));
  assert(engine.incident(id).confirmed_active);
  assert(!engine.incident(id).latched);

  engine.observe_fault_words(words(200U));
  assert(!engine.incident(id).confirmed_active);
  assert(!engine.incident(id).latched);
  assert(!engine.acknowledge(id));
}

void test_bootstrap_dip_does_not_bypass_recovery() {
  HpIncidentEngine engine;
  engine.observe_link_round(100U, false);
  assert(engine.outputs().link_state == LinkState::SUSPECT);
  engine.observe_link_round(10100U, true);
  assert(engine.outputs().link_state == LinkState::RECOVERING);
  engine.observe_link_round(40100U, true);
  engine.observe_link_round(70100U, true);
  assert(engine.outputs().link_state == LinkState::HEALTHY);
}

void test_link_timer_handles_millis_wrap() {
  EngineTuning tuning;
  tuning.link_recovery_ms = 0U;
  tuning.link_recovery_rounds = 1U;
  HpIncidentEngine engine(tuning);
  engine.observe_link_round(UINT32_MAX - 100U, true);
  assert(engine.outputs().link_state == LinkState::HEALTHY);

  engine.observe_link_round(UINT32_MAX - 20000U, false);
  engine.observe_link_round(0U, false);
  assert(engine.outputs().link_state == LinkState::SUSPECT);
  engine.observe_link_round(15000U, false);
  assert(engine.outputs().link_state == LinkState::LOST);
}

void test_fault_debounce_and_stale_does_not_clear() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);

  constexpr uint16_t kHardFault = 1U << 0U;
  engine.observe_fault_words(words(80101U, kHardFault));
  assert(!engine.outputs().must_stop);
  engine.observe_fault_words(words(90101U, kHardFault));
  assert(engine.outputs().must_stop);
  assert(engine.outputs().fault_active);
  assert(!engine.outputs().fallback_eligible);
  assert(engine.request_stop(90102U));
  engine.observe_run(frequency(90103U, 0.0F));
  assert(!engine.outputs().fallback_eligible);
  engine.observe_run(frequency(90104U, 0.0F));
  assert(engine.outputs().fallback_eligible);
  assert(engine.incident(2119U, 0U).occurrence_count == 1U);

  const uint32_t first_seen_ms = engine.incident(2119U, 0U).first_seen_ms;
  engine.observe_fault_words(words(95101U));
  engine.observe_fault_words(words(96101U, kHardFault));
  assert(engine.incident(2119U, 0U).confirmed_active);
  assert(engine.incident(2119U, 0U).first_seen_ms == first_seen_ms);

  engine.observe_fault_words(words(100101U, 0U, 0U, 0U, false));
  assert(engine.outputs().must_stop);
  assert(engine.incident(2119U, 0U).confirmed_active);

  engine.observe_fault_words(words(110101U));
  engine.observe_fault_words(words(120101U));
  assert(engine.outputs().must_stop);
  engine.observe_fault_words(words(130101U));
  assert(!engine.incident(2119U, 0U).confirmed_active);
  assert(engine.incident(2119U, 0U).latched);
  assert(engine.outputs().protection_state == ProtectionState::FAULT_RECOVERY);
  assert(!engine.outputs().available_for_start);
  assert(engine.outputs().fallback_cause_present);
  assert(engine.outputs().fallback_eligible);

  engine.observe_fault_words(words(160101U));
  engine.observe_fault_words(words(190101U));
  assert(engine.outputs().protection_state == ProtectionState::CLEAR);
  assert(engine.outputs().available_for_start);
  assert(!engine.outputs().fallback_cause_present);
  assert(engine.acknowledge(incident_id(2119U, 0U)));
  assert(!engine.incident(2119U, 0U).latched);
}

void test_acknowledging_active_fault_only_releases_latch_after_clear() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);
  constexpr uint16_t kHardFault = 1U << 1U;
  engine.observe_fault_words(words(80101U, kHardFault));
  engine.observe_fault_words(words(90101U, kHardFault));
  const IncidentId id = incident_id(2119U, 1U);
  assert(engine.acknowledge(id));
  assert(engine.incident(id).confirmed_active);
  assert(engine.incident(id).latched);

  engine.observe_fault_words(words(100101U));
  engine.observe_fault_words(words(110101U));
  engine.observe_fault_words(words(120101U));
  assert(!engine.incident(id).confirmed_active);
  assert(!engine.incident(id).latched);
}

void test_non_fallback_protections() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);

  engine.observe_fault_words(words(70100U, 1U << 5U));
  assert(engine.outputs().protection_state == ProtectionState::LIMITED);
  assert(engine.outputs().available_for_start);
  assert(!engine.outputs().fallback_cause_present);

  engine.observe_fault_words(words(80100U, 1U << 6U));
  assert(engine.outputs().protection_state == ProtectionState::START_BLOCKED);
  assert(!engine.outputs().available_for_start);
  assert(!engine.outputs().must_stop);
  assert(!engine.outputs().fallback_cause_present);
}

void test_preheat_does_not_hide_another_start_block() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  constexpr uint16_t kPreheat = 1U << 6U;
  constexpr uint16_t kUnknownStartBlock = 1U << 9U;
  engine.observe_fault_words(words(70100U, kPreheat, kUnknownStartBlock));
  engine.observe_fault_words(words(80100U, kPreheat, kUnknownStartBlock));
  assert(engine.outputs().protection_state == ProtectionState::START_BLOCKED);
  assert(engine.outputs().primary_incident_id == incident_id(2120U, 9U));
}

void test_run_feedback_and_stale_stop_sample() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);

  assert(engine.request_start(80101U));
  engine.observe_run(frequency(90101U, 0.0F, false));
  assert(engine.outputs().run_state == RunState::WAIT_MODE);
  engine.observe_run(frequency(100101U, 0.0F, true));
  assert(engine.outputs().run_state == RunState::WAIT_COMPRESSOR);
  engine.observe_run(frequency(110101U, 12.0F));
  assert(engine.outputs().run_state == RunState::RUNNING);
  assert(engine.outputs().running_confirmed);

  engine.request_stop(120101U);
  engine.observe_run(frequency(130101U, 0.0F));
  assert(engine.outputs().run_state == RunState::STOPPING);
  engine.observe_run(frequency(140101U, 0.0F, true, false));
  assert(engine.outputs().run_state == RunState::STOPPING);
  engine.observe_run(frequency(150101U, 0.0F));
  assert(engine.outputs().run_state == RunState::STOPPED);
  assert(engine.outputs().stop_confirmed);
}

void test_passive_standby_feedback_keeps_stop_confirmed() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);

  engine.observe_run(frequency(80101U, 0.0F));
  assert(engine.outputs().run_state == RunState::STOPPED);
  assert(engine.outputs().stop_confirmed);

  engine.observe_run(frequency(90101U, 0.0F));
  assert(engine.outputs().run_state == RunState::STOPPED);
  assert(engine.outputs().stop_confirmed);
}

void test_transient_wrong_mode_is_not_accepted_as_start() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);

  assert(engine.request_start(80101U));
  engine.observe_run(frequency(90101U, 12.0F, false));
  assert(engine.outputs().run_state == RunState::WAIT_MODE);
  assert(!engine.outputs().running_confirmed);
  assert(!engine.outputs().start_timed_out);

  engine.observe_run(frequency(100101U, 12.0F, true));
  assert(engine.outputs().run_state == RunState::RUNNING);
  assert(engine.outputs().running_confirmed);
  assert(!engine.outputs().start_mode_ack_timed_out);
  assert(!engine.outputs().start_timed_out);
}

void test_persistent_wrong_mode_requires_safe_stop_and_retry() {
  EngineTuning tuning;
  tuning.mode_ack_timeout_ms = 30000U;
  tuning.start_timeout_ms = 120000U;
  HpIncidentEngine engine(tuning);
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);

  assert(engine.request_start(80101U));
  engine.observe_run(frequency(90101U, 12.0F, false));
  engine.tick(110100U);
  assert(!engine.outputs().start_timed_out);
  engine.tick(110101U);
  assert(engine.outputs().start_mode_ack_timed_out);
  assert(engine.outputs().start_timed_out);
  assert(engine.outputs().must_stop);
  assert(!engine.outputs().running_confirmed);
  assert(engine.outputs().run_state == RunState::WAIT_MODE);
  assert(!engine.outputs().fallback_eligible);

  assert(engine.request_stop(110102U));
  engine.observe_run(frequency(120102U, 0.0F));
  engine.observe_run(frequency(130102U, 0.0F));
  assert(engine.outputs().stop_confirmed);
  assert(engine.outputs().fallback_eligible);
  assert(engine.start_failure_reset_status() == StartFailureResetResult::READY);
  assert(engine.clear_start_failure(130103U));
  assert(engine.outputs().available_for_start);
}

void test_stop_requires_post_command_mode_confirmation() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);
  assert(engine.request_start(80101U));
  engine.observe_run(frequency(80102U, 12.0F));
  assert(engine.request_stop(90101U));

  engine.observe_run(frequency(100101U, 0.0F, true, true, false));
  engine.observe_run(frequency(110101U, 0.0F, true, true, false));
  assert(engine.outputs().run_state == RunState::STOPPING);
  assert(!engine.outputs().stop_confirmed);

  engine.observe_run(frequency(120101U, 0.0F, true, true, true));
  assert(engine.outputs().run_state == RunState::STOPPING);
  engine.observe_run(frequency(130101U, 0.0F, true, true, true));
  assert(engine.outputs().run_state == RunState::STOPPED);
  assert(engine.outputs().stop_confirmed);
}

void test_repeated_stop_request_does_not_restart_timeout() {
  EngineTuning tuning;
  tuning.stop_confirm_timeout_ms = 60000U;
  HpIncidentEngine engine(tuning);
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);
  assert(engine.request_start(80101U));
  engine.observe_run(frequency(80102U, 12.0F));
  assert(engine.request_stop(90101U));
  assert(!engine.request_stop(140101U));
  engine.tick(150102U);
  assert(engine.outputs().run_state == RunState::STOP_UNCONFIRMED);
  assert(!engine.outputs().stop_confirmation_pending);
}

void test_revalidation_only_becomes_unconfirmed_after_timeout() {
  EngineTuning tuning;
  tuning.stop_confirm_timeout_ms = 60000U;
  HpIncidentEngine engine(tuning);
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);

  constexpr uint16_t kHardFault = 1U << 0U;
  engine.observe_fault_words(words(80101U, kHardFault));
  engine.observe_fault_words(words(90101U, kHardFault));
  assert(engine.outputs().stop_confirmation_pending);
  assert(!engine.outputs().stop_unconfirmed);
  assert(!engine.outputs().available_for_start);
  assert(engine.outputs().must_stop);
  assert(!engine.outputs().fallback_eligible);

  assert(engine.request_stop(90102U));
  engine.tick(150101U);
  assert(engine.outputs().stop_confirmation_pending);
  assert(!engine.outputs().stop_unconfirmed);
  engine.tick(150102U);
  assert(!engine.outputs().stop_confirmation_pending);
  assert(engine.outputs().stop_unconfirmed);
  assert(engine.outputs().must_stop);
  assert(!engine.outputs().fallback_eligible);
}

void test_preheat_pauses_start_watchdog() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);

  assert(engine.request_start(80101U));
  engine.observe_fault_words(words(90101U, 1U << 6U));
  engine.tick(300101U);
  assert(!engine.outputs().start_mode_ack_timed_out);
  assert(!engine.outputs().start_timed_out);

  engine.observe_fault_words(words(310101U));
  engine.observe_fault_words(words(320101U));
  engine.tick(339101U);
  assert(!engine.outputs().start_mode_ack_timed_out);
  engine.tick(341101U);
  assert(engine.outputs().start_mode_ack_timed_out);
  assert(!engine.outputs().start_timed_out);
}

void test_start_timeout_requires_safe_stop_and_explicit_recovery() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);

  assert(engine.request_start(80101U));
  engine.tick(200102U);
  assert(engine.outputs().start_timed_out);
  assert(engine.outputs().must_stop);
  assert(!engine.outputs().available_for_start);
  assert(engine.outputs().fallback_cause_present);
  assert(!engine.outputs().fallback_eligible);
  assert(engine.start_failure_reset_status() == StartFailureResetResult::STOP_NOT_CONFIRMED);
  assert(engine.outputs().primary_incident_id == kStartFailedIncidentId);
  assert(!engine.clear_start_failure(200103U));
  assert(!engine.acknowledge(kStartFailedIncidentId));
  assert(engine.outputs().start_timed_out);

  engine.request_stop(200104U);
  engine.observe_run(frequency(210104U, 0.0F));
  engine.observe_run(frequency(220104U, 0.0F));
  assert(engine.outputs().stop_confirmed);
  assert(engine.outputs().fallback_eligible);
  assert(engine.start_failure_reset_status() == StartFailureResetResult::READY);
  assert(engine.clear_start_failure(220105U));
  assert(!engine.outputs().start_timed_out);
  assert(!engine.outputs().fallback_cause_present);
  assert(engine.outputs().available_for_start);
  assert(engine.start_failure_reset_status() == StartFailureResetResult::NO_START_FAILURE);
  assert(!engine.clear_start_failure(220106U));
}

void test_start_failure_retry_waits_for_fault_recovery() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);
  assert(engine.request_start(80101U));
  engine.tick(200102U);
  assert(engine.outputs().start_timed_out);

  constexpr uint16_t kHardFault = 1U << 0U;
  engine.observe_fault_words(words(200103U, kHardFault));
  engine.observe_fault_words(words(210103U, kHardFault));
  assert(engine.request_stop(210104U));
  engine.observe_run(frequency(220104U, 0.0F));
  engine.observe_run(frequency(230104U, 0.0F));
  assert(engine.start_failure_reset_status() == StartFailureResetResult::HARD_FAULT_ACTIVE);
  assert(!engine.clear_start_failure(230105U));

  engine.observe_fault_words(words(240104U));
  engine.observe_fault_words(words(250104U));
  engine.observe_fault_words(words(260104U));
  assert(engine.start_failure_reset_status() == StartFailureResetResult::FAULT_RECOVERY_PENDING);
  assert(!engine.clear_start_failure(260105U));

  engine.observe_fault_words(words(290104U));
  engine.observe_fault_words(words(320104U));
  assert(engine.start_failure_reset_status() == StartFailureResetResult::READY);
  assert(engine.clear_start_failure(320105U));
}

void test_stop_unconfirmed_blocks_fallback() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);
  assert(engine.request_start(80101U));
  engine.observe_run(frequency(80102U, 12.0F));
  engine.request_stop(90101U);
  engine.tick(150102U);
  assert(engine.outputs().stop_unconfirmed);
  assert(engine.outputs().must_stop);
  assert(!engine.outputs().fallback_eligible);
  assert(engine.outputs().primary_incident_id == kStopUnconfirmedIncidentId);

  engine.observe_run(frequency(150103U, 12.0F));
  assert(engine.outputs().stop_unconfirmed);
  assert(!engine.outputs().stop_confirmed);
  engine.observe_run(frequency(150104U, 0.0F));
  assert(engine.outputs().stop_unconfirmed);
  assert(!engine.outputs().stop_confirmed);
  engine.observe_run(frequency(150105U, 0.0F));
  assert(!engine.outputs().stop_unconfirmed);
  assert(engine.outputs().stop_confirmed);
}

void test_power_cycle_latch_requires_explicit_confirmation() {
  HpIncidentEngine engine;
  establish_healthy_link(engine, 100U);
  confirm_stopped(engine, 60101U);

  constexpr uint16_t kPowerCycleFault = 1U << 4U;
  engine.observe_fault_words(words(80101U, 0U, kPowerCycleFault));
  engine.observe_fault_words(words(90101U, 0U, kPowerCycleFault));
  assert(engine.outputs().must_stop);
  assert(!engine.has_cleared_power_cycle_latch());
  assert(!engine.confirm_odu_power_cycle(90102U));
  assert(engine.incident(2120U, 4U).latched);

  engine.observe_fault_words(words(100101U));
  engine.observe_fault_words(words(110101U));
  engine.observe_fault_words(words(120101U));
  assert(!engine.incident(2120U, 4U).confirmed_active);
  assert(engine.incident(2120U, 4U).latched);
  assert(engine.has_cleared_power_cycle_latch());
  assert(engine.outputs().must_stop);
  assert(!engine.acknowledge(incident_id(2120U, 4U)));
  assert(engine.incident(2120U, 4U).latched);
  assert(!engine.incident(2120U, 4U).acknowledged);

  assert(engine.confirm_odu_power_cycle(130101U));
  assert(!engine.incident(2120U, 4U).latched);
  assert(engine.incident(2120U, 4U).acknowledged);
  assert(!engine.has_cleared_power_cycle_latch());
  assert(!engine.confirm_odu_power_cycle(130102U));
  assert(engine.outputs().protection_state == ProtectionState::FAULT_RECOVERY);
}

}  // namespace

int main() {
  test_catalog_classification();
  test_short_link_dip_and_confirmed_loss();
  test_link_loss_invalidates_old_stop_confirmation();
  test_link_loss_rearms_an_inflight_stop();
  test_new_hard_fault_invalidates_old_stop_confirmation();
  test_new_hard_fault_discards_partial_stop_confirmation();
  test_bootstrap_dip_does_not_bypass_recovery();
  test_link_timer_handles_millis_wrap();
  test_fault_debounce_and_stale_does_not_clear();
  test_acknowledging_active_fault_only_releases_latch_after_clear();
  test_status_clears_without_acknowledgeable_history();
  test_non_fallback_protections();
  test_preheat_does_not_hide_another_start_block();
  test_run_feedback_and_stale_stop_sample();
  test_passive_standby_feedback_keeps_stop_confirmed();
  test_transient_wrong_mode_is_not_accepted_as_start();
  test_persistent_wrong_mode_requires_safe_stop_and_retry();
  test_stop_requires_post_command_mode_confirmation();
  test_repeated_stop_request_does_not_restart_timeout();
  test_revalidation_only_becomes_unconfirmed_after_timeout();
  test_preheat_pauses_start_watchdog();
  test_start_timeout_requires_safe_stop_and_explicit_recovery();
  test_start_failure_retry_waits_for_fault_recovery();
  test_stop_unconfirmed_blocks_fallback();
  test_power_cycle_latch_requires_explicit_confirmation();
  std::cout << "oq_hp_incident_engine_test: ok\n";
  return 0;
}
