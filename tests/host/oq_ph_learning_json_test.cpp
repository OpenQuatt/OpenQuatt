#include <assert.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "../../openquatt/includes/control/oq_ph_learning_json.h"
#include "../../openquatt/includes/control/oq_ph_daily_collection_json.h"
#include "../../openquatt/includes/control/oq_ph_daily_collection_state_json.h"

namespace {
using namespace oq_ph_learning;

void add_record(JsonWriter& json, bool comma) {
  static constexpr char kFloat[] = "-3.40282e+38";
  static constexpr char kUint[] = "4294967295";
  json.add("%s[%s,%s,%s,%s,%s,%s,%s,%s,%s,1]", comma ? "," : "", kUint, kUint, kFloat, kFloat, kFloat, kFloat, kFloat,
           kUint, kFloat);
}

void add_diagnostic(JsonWriter& json, bool comma) {
  static constexpr char kFloat[] = "-3.40282e+38";
  static constexpr char kUint[] = "4294967295";
  static constexpr char kInt[] = "-2147483648";
  static constexpr char kBool[] = "false";
  json.add("%s[%s,%s,%s", comma ? "," : "", kUint, kUint, kInt);
  for (size_t index = 0; index < 4U; ++index) json.add(",%s", kFloat);
  json.add(",%s,%s,%s,%s,%s,%s,%s,%s", kBool, kBool, kFloat, kFloat, kBool, kFloat, kFloat, kUint);
  for (size_t index = 0; index < 10U; ++index) json.add(",%s", kBool);
  json.add(",%s]", kUint);
}

void test_worst_case_export_fits_fixed_psram_cache() {
  char buffer[kExportJsonBufferSize]{};
  JsonWriter json(buffer, sizeof(buffer));
  json.add("%s", kExportRecordsPrefix);
  for (size_t index = 0; index < kMaxExportRecordRows; ++index) add_record(json, index != 0U);
  json.add("%s", kExportDiagnosticsPrefix);
  for (size_t index = 0; index < kMaxExportDiagnosticRows; ++index) add_diagnostic(json, index != 0U);
  json.add("%s", kExportSuffix);

  assert(json.ok());
  assert(json.size() == kMaxExportBaseJsonBytes);
  assert(json.size() < sizeof(buffer));
  assert(buffer[json.size()] == '\0');
}

void test_writer_bounds_numbers_and_fails_closed_on_overflow() {
  char number_buffer[32]{};
  JsonWriter number(number_buffer, sizeof(number_buffer));
  number.number(-std::numeric_limits<float>::max());
  assert(number.ok());
  assert(number.size() <= kMaxSerializedFloatChars);

  char unavailable_buffer[8]{};
  JsonWriter unavailable(unavailable_buffer, sizeof(unavailable_buffer));
  unavailable.number(std::numeric_limits<float>::quiet_NaN());
  assert(std::strcmp(unavailable_buffer, "null") == 0);

  char short_buffer[4]{};
  JsonWriter overflow(short_buffer, sizeof(short_buffer));
  overflow.add("1234");
  assert(!overflow.ok());
  assert(overflow.size() == 0U);
}

void test_worst_case_daily_diagnostics_and_all_export_rows_fit() {
  using namespace oq_power_house::learning;
  DailyCollectionDiagnostics diagnostics;
  diagnostics.interrupted_periods = diagnostics.completed_quality_rejections = diagnostics.append_rejections =
      diagnostics.bridged_gaps = diagnostics.rejected_next_day_remainders = UINT32_MAX;
  diagnostics.last_remainder_rejection.epoch_s = UINT32_MAX;
  diagnostics.last_remainder_rejection.duration_ms = UINT64_MAX - 1;
  diagnostics.last_remainder_rejection.uncertainty_ws =
      diagnostics.last_remainder_rejection.completed_day_uncertainty_ws = -std::numeric_limits<float>::max();
  diagnostics.discarded_collected_ms = UINT64_MAX;
  for (auto& value : diagnostics.cause_counts) value = UINT32_MAX;
  diagnostics.pending_invalid_reasons = UINT32_MAX;
  diagnostics.last_loss.epoch_s = diagnostics.last_loss.context_revision = diagnostics.last_loss.invalid_reasons =
      UINT32_MAX;
  diagnostics.last_loss.collected_ms = diagnostics.last_loss.observed_gap_ms = UINT64_MAX - 1;
  diagnostics.last_loss.charged_uncertainty_ws = diagnostics.last_loss.attempted_day_uncertainty_ws =
      diagnostics.last_loss.attempted_next_day_uncertainty_ws = -std::numeric_limits<float>::max();
  // Exhaust every name rather than assume which enum value serializes longest.
  size_t longest_cause = 0, longest_status = 0, longest_source = 0;
  for (size_t index = 1; index < kDailyCollectionCauseCount; ++index) {
    if (strlen(daily_collection_cause_name(static_cast<DailyCollectionCause>(index))) > longest_cause) {
      diagnostics.last_loss.cause = static_cast<DailyCollectionCause>(index);
      longest_cause = strlen(daily_collection_cause_name(diagnostics.last_loss.cause));
    }
  }
  for (unsigned index = 0; index <= static_cast<unsigned>(LearningStatus::UNSTABLE_FIT); ++index) {
    if (strlen(learning_status_name(static_cast<LearningStatus>(index))) > longest_status) {
      diagnostics.last_loss.status = static_cast<LearningStatus>(index);
      longest_status = strlen(learning_status_name(diagnostics.last_loss.status));
    }
  }
  for (unsigned index = 0; index <= static_cast<unsigned>(SnapshotSourceStatus::BOILER_COMMAND_ACTIVE); ++index) {
    if (strlen(snapshot_source_status_name(static_cast<SnapshotSourceStatus>(index))) > longest_source) {
      diagnostics.last_loss.source_status = diagnostics.pending_source_status =
          static_cast<SnapshotSourceStatus>(index);
      longest_source = strlen(snapshot_source_status_name(diagnostics.last_loss.source_status));
    }
  }
  char diagnostics_buffer[kMaxDailyCollectionJsonBytes];
  JsonWriter body(diagnostics_buffer, sizeof(diagnostics_buffer));
  write_daily_collection_json(body, diagnostics);
  assert(body.ok() && body.size() < kMaxDailyCollectionJsonBytes);

  char buffer[kExportJsonBufferSize];
  JsonWriter json(buffer, sizeof(buffer));
  json.add("%s", kExportRecordsPrefix);
  for (size_t index = 0; index < kMaxExportRecordRows; ++index) add_record(json, index != 0U);
  json.add("%s", kExportDiagnosticsPrefix);
  for (size_t index = 0; index < kMaxExportDiagnosticRows; ++index) add_diagnostic(json, index != 0U);
  json.add("%s", kExportDailyCollectionPrefix);
  write_daily_collection_json(json, diagnostics);
  DailyCollectionState state;
  state.tick_epoch = state.context_revision = UINT32_MAX;
  state.uptime_s = state.elapsed_s = UINT64_MAX;
  state.reset_reason = INT32_MIN;
  state.resume_status = "waiting_for_sources";
  state.resume_reason = "checkpoint_context_unavailable";
  state.source_status = state.last_source_status = diagnostics.pending_source_status;
  state.last_rejection = diagnostics.last_loss.status;
  state.uncertainty_wh = -std::numeric_limits<float>::max();
  char state_buffer[kMaxDailyCollectionStateJsonBytes];
  JsonWriter state_body(state_buffer, sizeof(state_buffer));
  write_daily_collection_state_json(state_body, state);
  assert(state_body.ok() && state_body.size() < sizeof(state_buffer));
  json.add("%s", kExportDailyCollectionStatePrefix);
  write_daily_collection_state_json(json, state);
  json.add("}");
  assert(json.ok() && json.size() <= kMaxExportJsonBytes && json.size() < sizeof(buffer));
  assert(buffer[json.size()] == '\0');
}

void test_current_collection_unknown_values_are_not_fabricated_measurements() {
  char buffer[kMaxDailyCollectionStateJsonBytes];
  JsonWriter json(buffer, sizeof(buffer));
  write_daily_collection_state_json(json, DailyCollectionState{});
  assert(json.ok());
  assert(strstr(buffer, "\"batch_last_rejection\":null") != nullptr);
  assert(strstr(buffer, "\"batch_missing_energy_uncertainty_wh\":null") != nullptr);
  assert(strstr(buffer, "\"batch_missing_energy_budget_wh\":2400") != nullptr);
  assert(strstr(buffer, "\"batch_maximum_gap_s\":120") != nullptr);
}
}  // namespace

int main() {
  test_worst_case_export_fits_fixed_psram_cache();
  test_writer_bounds_numbers_and_fails_closed_on_overflow();
  test_worst_case_daily_diagnostics_and_all_export_rows_fit();
  test_current_collection_unknown_values_are_not_fabricated_measurements();
  return 0;
}
