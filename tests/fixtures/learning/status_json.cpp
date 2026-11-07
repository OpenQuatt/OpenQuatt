#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

#include "../../../openquatt/includes/control/oq_ph_daily_collection_state_json.h"
#include "../../../openquatt/includes/control/oq_ph_daily_record_quality_json.h"
#include "../../../openquatt/includes/learning/oq_ph_learning_persistence_logic.h"
#include "../../../openquatt/includes/learning/oq_ph_learning_live_logic.h"
#include "../../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

using namespace oq_ph_learning;
using namespace oq_power_house::learning;

DailyRestartTicket daily_restart_ticket, boot_restart_ticket;
namespace oq_sources {
uint64_t monotonic_ms() { return 10000; }
}  // namespace oq_sources

// Only SDK reads/cache publication are stubbed; the complete production status
// body and real pure learner types below are compiled and executed verbatim.
constexpr unsigned MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2;
unsigned heap_caps_get_free_size(unsigned) { return UINT32_MAX; }
unsigned heap_caps_get_minimum_free_size(unsigned) { return UINT32_MAX; }
unsigned heap_caps_get_largest_free_block(unsigned) { return UINT32_MAX; }
unsigned uxTaskGetStackHighWaterMark(void*) { return UINT32_MAX; }
int oq_control_mode_code = INT32_MIN;
int esp_reset_reason() { return INT32_MIN; }
struct Endpoint {
  std::string largest;
  unsigned publications = 0;
  std::string exported;
  bool publish_export_json(const char* text, size_t length) {
    assert(length > 0 && length < kExportJsonBufferSize && text[length] == '\0');
    exported.assign(text, length);
    return true;
  }
  bool publish_status_json(const char* text, size_t length) {
    assert(length != 0 && length < kStatusJsonBufferSize && text[length] == '\0');
    if (length > largest.size()) largest.assign(text, length);
    ++publications;
    return true;
  }
} oq_ph_learning_status_endpoint;
#define id(value) value
const char* paused_status = nullptr;
const char* paused_reason = nullptr;

#ifdef OQ_TEST_EXPORT_JSON
#include "production_diagnostic_row.inc"
#endif

struct RuntimeStorage {
  PassiveRuntimeStorage learner;
  PassiveRuntimeSummary summary;
  PassiveRuntimeConfig config;
  LearningSourceInput input;
  PassiveTickInput tick;
  SnapshotDiagnostics source_diagnostics;
  LearningJournalStore journal;
  LearningJournalRecords pending_daily;
  struct Source {
    int route = 0;
  } sources[4];
  bool pending_daily_heat_initialized = false;
  uint64_t daily_resume_last_observation_ms = 0;
#ifdef OQ_TEST_EXPORT_JSON
  DiagnosticRow rows[kMaxExportDiagnosticRows];
  size_t row_count = 0, row_next = 0;
#endif
  bool daily_restore_pending = false;
  bool daily_restore_allowed = true, restart_prepared = false;
  DailyRestartTicket boot_restart_ticket;
  DiagnosticCaptureGate diagnostic_capture;
  const void* partition = nullptr;
  const char* daily_resume_status = "waiting_for_sources";
  const char* daily_resume_reason = "checkpoint_context_unavailable";
  DailyRestartDiagnostics daily_restart;
  SnapshotSourceStatus batch_source_status = SnapshotSourceStatus::OK;
  uint64_t max_tick_us = UINT64_MAX;
  char json[kExportJsonBufferSize]{};
};
struct Production {
  RuntimeStorage* storage_ = nullptr;
  static const char* route_name_(int) { return "selected_outside_temperature"; }
  static void publish_export_error_() { assert(false && "production export exceeded its fixed cache"); }
  static void publish_status_error_() { assert(false && "production status exceeded its fixed cache"); }
  static void publish_paused_status_(RuntimeStorage& state) {
    paused_status = state.daily_resume_status;
    paused_reason = state.daily_resume_reason;
  }
  static bool erase_slots_(const void*) { return false; }  // Notice clearing must survive a failed flash erase.
  static bool read_slot_(const void*, size_t, uint8_t*, size_t) { return false; }
#include "production_learning_status.inc"
};

void test_daily_resume_notice_lifecycle() {
  RuntimeStorage state;
  state.daily_resume_status = "discarded";
  state.daily_resume_reason = "restart_unconfirmed";
  auto& start = state.learner.batch_accumulator.start_epoch_s;
  // Waiting for a first observation and starting its replacement day must not
  // erase the explanation before the user can see it.
  Production::update_daily_resume_status_(state, false, 0);
  start = 1000;
  Production::update_daily_resume_status_(state, false, 0);
  assert(strcmp(state.daily_resume_status, "discarded") == 0);
  for (const char* status : {"discarded", "restored"}) {
    for (uint32_t next : {0U, 87400U}) {
      state.daily_resume_status = status;
      state.daily_resume_reason = "restart_unconfirmed";
      start = 1000;
      Production::update_daily_resume_status_(state, false, 1000);
      assert(strcmp(state.daily_resume_status, status) == 0);
      start = next;  // Completion/reset, or a new day with a retained remainder.
      Production::update_daily_resume_status_(state, false, 1000);
      assert(strcmp(state.daily_resume_status, "none") == 0 && strcmp(state.daily_resume_reason, "none") == 0);
      start = 173800;
      Production::update_daily_resume_status_(state, false, next);
      assert(strcmp(state.daily_resume_status, "none") == 0);
    }
  }
  for (const char* status : {"none", "waiting_for_sources", "resuming"}) {
    state.daily_resume_status = status;
    Production::update_daily_resume_status_(state, false, 1000);
    assert(strcmp(state.daily_resume_status, status) == 0);
  }
  for (const auto result : {LearningStatus::COLLECTING, LearningStatus::TIME_DISCONTINUITY}) {
    state.daily_resume_reason = "none";
    state.learner.diagnostics.last_batch_status = result;
    Production::update_daily_resume_status_(state, true, 1000);
    assert(strcmp(state.daily_resume_status, result == LearningStatus::COLLECTING ? "restored" : "discarded") == 0);
    if (result != LearningStatus::COLLECTING) assert(strcmp(state.daily_resume_reason, "none") != 0);
  }
  Production production;
  production.storage_ = &state;
  state.journal.available = state.journal.loaded = true;
  for (int entry : {0, 1, 2}) {
    state.daily_resume_status = "discarded";
    state.daily_resume_reason = "restart_unconfirmed";
    state.daily_restore_pending = true;
    state.learner.initialized = state.learner.batch_accumulator.active = true;
    start = 1000;
    if (entry == 0) {
      paused_status = paused_reason = nullptr;
      production.pause();
      assert(strcmp(paused_status, "none") == 0 && strcmp(paused_reason, "none") == 0);
    } else if (entry == 1) {
      Production::context_change(state);
    } else {
      Production::reset(state);
    }
    assert(!state.daily_restore_pending && strcmp(state.daily_resume_status, "none") == 0 &&
           strcmp(state.daily_resume_reason, "none") == 0);
    start = 87400;
    Production::update_daily_resume_status_(state, false, 0);
    assert(strcmp(state.daily_resume_status, "none") == 0);
  }
}

// Before the boot journal loads, pause and context changes must leave their own
// cause for the later discard decision instead of an unconfirmed restart.
void test_daily_resume_cause_before_journal_load() {
  Production production;
  for (int entry : {0, 1, 2, 3}) {
    RuntimeStorage state;
    production.storage_ = &state;
    state.journal.available = true;
    state.daily_resume_status = state.daily_resume_reason = "none";
    if (entry == 0)
      production.pause();
    else if (entry == 1)
      production.pause(LearningStatus::MIXED_CONTEXT);
    else
      Production::context_change(state);
    if (entry == 3) production.pause();  // The first cause wins.
    assert(!state.daily_restore_allowed && strcmp(state.daily_resume_status, "none") == 0);
    assert(strcmp(state.daily_resume_reason, entry == 0 ? "paused" : "context_changed") == 0);
    Production::reset(state);  // An explicit reset still clears the cause.
    assert(strcmp(state.daily_resume_reason, "none") == 0);
  }
  // Without persistence no journal decision follows; a shown result is cleared.
  RuntimeStorage state;
  production.storage_ = &state;
  state.daily_resume_status = "discarded";
  state.daily_resume_reason = "unexpected_restart";
  production.pause();
  assert(strcmp(state.daily_resume_status, "none") == 0 && strcmp(state.daily_resume_reason, "none") == 0);
}

#ifdef OQ_TEST_EXPORT_JSON
int main() {
  RuntimeStorage state;
  state.learner.record_count = kMaxSegmentRecords;
  for (size_t i = 0; i < kMaxSegmentRecords; ++i) {
    auto& r = state.learner.records[i];
    r.start_epoch_s = UINT32_MAX - 86400;
    r.end_epoch_s = UINT32_MAX;
    r.duration_s = 86400;
    r.context_revision = UINT32_MAX;
    r.mean_room_c = r.mean_setpoint_c = 20;
    r.mean_outside_c = 5;
    r.mean_heat_w = 2000;
    r.room_trend_k_per_h = 0;
    if (i % 4 == 0) r.daily_quality = {10000, 10000, 0};
    if (i % 4 == 1) r.daily_quality = {10000, 9999, 100};
    if (i % 4 == 2) r.daily_quality = {10000, 10000, kDailyUncertaintyOverflow};
  }
  state.row_count = kMaxExportDiagnosticRows;
  Production::publish_export_(state, true, UINT32_MAX);
  std::cout << oq_ph_learning_status_endpoint.exported << '\n';
}
#else
int main() {
  test_daily_resume_notice_lifecycle();
  test_daily_resume_cause_before_journal_load();
  RuntimeStorage state;
  state.daily_restart = {1, UINT32_MAX, kDailyMaximumRestartGapMs, true, "restore", "operational_context_unknown"};
  auto& learner = state.learner;
  auto& summary = state.summary;
  learner.initialized = learner.opted_in = learner.blocked = true;
  learner.status = PassiveRuntimeStatus::BLOCKED;
  learner.record_count = summary.record_count = kMaxSegmentRecords;
  for (auto& record : learner.records) {
    record.duration_s = 86400;
    record.start_epoch_s = UINT32_MAX - 86400;
    record.end_epoch_s = UINT32_MAX;
  }
  learner.batch_accumulator.active = true;
  learner.batch_accumulator.context_revision = UINT32_MAX;
  learner.batch_accumulator.start_monotonic_ms = 1;
  learner.batch_accumulator.last_monotonic_ms = 86399001;
  learner.batch_accumulator.last_epoch_s = UINT32_MAX;
  learner.batch_accumulator.missing_energy_uncertainty_ws = std::numeric_limits<float>::max();
  learner.thermal_accumulator.active = true;
  learner.thermal_accumulator.first.monotonic_ms = 1;
  learner.thermal_accumulator.last.monotonic_ms = UINT64_MAX;
  learner.thermal_accumulator.last.epoch_s = UINT32_MAX;
  learner.diagnostics.accepted_thermal_intervals = UINT32_MAX;
  summary.batch_advice_ready = summary.thermal_model_ready = true;
  summary.batch.candidate_available = true;
  summary.batch.train_segments = summary.batch.train_days = 18;
  summary.batch.holdout_segments = summary.batch.holdout_days = 3;
  summary.batch.train_min_c = 7.55f;
  summary.batch.train_max_c = 12.34f;
  summary.batch.train_p10_c = 8.28f;
  summary.batch.train_p90_c = 11.88f;
  summary.batch.candidate.heat_loss_w_per_k = summary.batch.candidate.zero_power_temp_c =
      -std::numeric_limits<float>::max();
  summary.thermal.heat_loss_w_per_k = summary.thermal.thermal_capacity_wh_per_k = -std::numeric_limits<float>::max();
  summary.thermal.accepted_samples = summary.thermal.readiness_reasons = UINT32_MAX;
  state.config.thermal_window.target_duration_ms = UINT64_MAX;
  state.config.quality.max_daily_missing_heat_w = 18344.30078125f;
  state.input.context_revision = UINT32_MAX;
  state.source_diagnostics.invalid_reasons = UINT32_MAX;
  state.journal.status = "persistence_unavailable";
  // Enumerate names so later enum additions cannot silently defeat the budget.
  for (unsigned status = 0; status <= static_cast<unsigned>(LearningStatus::UNSTABLE_FIT); ++status) {
    summary.batch.status = learner.diagnostics.last_batch_rejection = static_cast<LearningStatus>(status);
    for (unsigned source = 0; source <= static_cast<unsigned>(SnapshotSourceStatus::HEAT_OUT_OF_RANGE); ++source) {
      state.source_diagnostics.status = state.batch_source_status = learner.diagnostics.last_batch_source_status =
          static_cast<SnapshotSourceStatus>(source);
      state.daily_restart.source_status = snapshot_source_status_name(static_cast<SnapshotSourceStatus>(source));
      for (const auto validation :
           {ModelValidationStatus::CONTEXT_MISMATCH, ModelValidationStatus::INSUFFICIENT_SHARED_RANGE,
            ModelValidationStatus::MODEL_DISAGREEMENT}) {
        summary.validation.status = validation;
        for (const bool pending : {false, true}) {
          state.daily_restore_pending = pending;
          Production::publish_status_(state, true, UINT32_MAX);
          learner.blocked = false;
          Production::publish_status_(state, true, UINT32_MAX);
          learner.blocked = true;
        }
      }
    }
  }
  Production::publish_status_(state, false, UINT32_MAX);
  assert(oq_ph_learning_status_endpoint.publications > 100);
  std::cout << oq_ph_learning_status_endpoint.largest << '\n';
}

#endif
