#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

#include "../../../openquatt/includes/control/oq_ph_daily_collection_state_json.h"
#include "../../../openquatt/includes/learning/oq_ph_learning_persistence_logic.h"
#include "../../../openquatt/includes/learning/oq_ph_passive_runtime_logic.h"

using namespace oq_ph_learning;
using namespace oq_power_house::learning;

// Only SDK reads/cache publication are stubbed; the complete production status
// body and real pure learner types below are compiled and executed verbatim.
constexpr unsigned MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2;
unsigned heap_caps_get_free_size(unsigned) { return UINT32_MAX; }
unsigned heap_caps_get_minimum_free_size(unsigned) { return UINT32_MAX; }
unsigned heap_caps_get_largest_free_block(unsigned) { return UINT32_MAX; }
unsigned uxTaskGetStackHighWaterMark(void*) { return UINT32_MAX; }
int oq_control_mode_code = INT32_MIN;
struct Endpoint {
  std::string largest;
  unsigned publications = 0;
  bool publish_status_json(const char* text, size_t length) {
    assert(length != 0 && length < kStatusJsonBufferSize && text[length] == '\0');
    if (length > largest.size()) largest.assign(text, length);
    ++publications;
    return true;
  }
} oq_ph_learning_status_endpoint;
#define id(value) value

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
  bool daily_restore_pending = false;
  const char* daily_resume_status = "waiting_for_sources";
  const char* daily_resume_reason = "checkpoint_context_unavailable";
  SnapshotSourceStatus batch_source_status = SnapshotSourceStatus::OK;
  uint64_t max_tick_us = UINT64_MAX;
  char json[kExportJsonBufferSize]{};
};
struct Production {
  static const char* route_name_(int) { return "selected_outside_temperature"; }
  static void publish_status_error_() { assert(false && "production status exceeded its fixed cache"); }
#include "production_learning_status.inc"
};

int main() {
  RuntimeStorage state;
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
  summary.batch.candidate.heat_loss_w_per_k = summary.batch.candidate.zero_power_temp_c =
      -std::numeric_limits<float>::max();
  summary.thermal.heat_loss_w_per_k = summary.thermal.thermal_capacity_wh_per_k = -std::numeric_limits<float>::max();
  summary.thermal.accepted_samples = summary.thermal.readiness_reasons = UINT32_MAX;
  state.config.thermal_window.target_duration_ms = UINT64_MAX;
  state.input.context_revision = UINT32_MAX;
  state.source_diagnostics.invalid_reasons = UINT32_MAX;
  state.journal.status = "persistence_unavailable";
  // Enumerate names so later enum additions cannot silently defeat the budget.
  for (unsigned status = 0; status <= static_cast<unsigned>(LearningStatus::UNSTABLE_FIT); ++status) {
    summary.batch.status = learner.diagnostics.last_batch_rejection = static_cast<LearningStatus>(status);
    for (unsigned source = 0; source <= static_cast<unsigned>(SnapshotSourceStatus::BOILER_COMMAND_ACTIVE); ++source) {
      state.source_diagnostics.status = state.batch_source_status = learner.diagnostics.last_batch_source_status =
          static_cast<SnapshotSourceStatus>(source);
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
