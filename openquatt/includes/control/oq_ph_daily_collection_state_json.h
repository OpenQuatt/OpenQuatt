#pragma once

#include "oq_ph_daily_collection_json.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

namespace oq_ph_learning {

struct DailyRestartDiagnostics {
  uint32_t checkpoint_epoch = 0, checked_epoch = 0;
  uint32_t limit_ms = oq_power_house::learning::kDailyMaximumGapMs;
  bool verified_ticket = false;
  const char* time_decision = "discard";
  const char* source_status = nullptr;
};

inline void write_daily_restart_json(JsonWriter& json, const DailyRestartDiagnostics& state) {
  if (state.checkpoint_epoch == 0) {
    json.add("null");
    return;
  }
  json.add("{\"checkpoint_epoch\":%u,\"checked_epoch\":%u,\"gap_s\":", state.checkpoint_epoch, state.checked_epoch);
  if (state.checked_epoch >= state.checkpoint_epoch)
    json.add("%u", state.checked_epoch - state.checkpoint_epoch);
  else
    json.add("null");
  json.add(",\"limit_s\":%u,\"verified_ticket\":%s,\"time_decision\":\"%s\",\"source_status\":", state.limit_ms / 1000U,
           state.verified_ticket ? "true" : "false", state.time_decision);
  if (state.source_status == nullptr)
    json.add("null");
  else
    json.add("\"%s\"", state.source_status);
  json.add("}");
}

// Main-loop scalars only. HTTP readers still receive the immutable JSON cache.
struct DailyCollectionState {
  uint32_t tick_epoch = 0, context_revision = 0;
  uint64_t uptime_s = 0, elapsed_s = 0;
  int reset_reason = 0;
  bool enabled = false, active = false, gap_pending = false, restore_pending = false;
  const char* resume_status = "none";
  const char* resume_reason = "none";
  oq_power_house::learning::LearningStatus last_rejection = oq_power_house::learning::LearningStatus::OK;
  oq_power_house::learning::SnapshotSourceStatus source_status = oq_power_house::learning::SnapshotSourceStatus::OK;
  oq_power_house::learning::SnapshotSourceStatus last_source_status =
      oq_power_house::learning::SnapshotSourceStatus::OK;
  float uncertainty_wh = NAN;
  float missing_heat_bound_w = oq_power_house::learning::QualityConfig{}.max_abs_heat_w;
  DailyRestartDiagnostics restart;
};

inline void write_daily_collection_state_json(JsonWriter& json, const DailyCollectionState& state) {
  using namespace oq_power_house::learning;
  json.add(
      "{\"tick_epoch\":%u,\"context_revision\":%u,\"uptime_s\":%llu,\"boot_reset_reason\":%d,"
      "\"enabled\":%s,\"batch_active\":%s,\"batch_gap_pending\":%s,\"batch_restore_pending\":%s,"
      "\"batch_elapsed_s\":%llu,\"batch_target_s\":%llu,\"batch_resume_status\":\"%s\","
      "\"batch_resume_reason\":\"%s\",\"batch_source_status\":\"%s\",\"batch_last_source_status\":\"%s\","
      "\"batch_last_rejection\":",
      state.tick_epoch, state.context_revision, static_cast<unsigned long long>(state.uptime_s), state.reset_reason,
      state.enabled ? "true" : "false", state.active ? "true" : "false", state.gap_pending ? "true" : "false",
      state.restore_pending ? "true" : "false", static_cast<unsigned long long>(state.elapsed_s),
      kSegmentDurationMs / 1000ULL, state.resume_status, state.resume_reason,
      snapshot_source_status_name(state.source_status), snapshot_source_status_name(state.last_source_status));
  if (state.last_rejection == LearningStatus::OK)
    json.add("null");
  else
    json.add("\"%s\"", learning_status_name(state.last_rejection));
  json.add(",\"batch_missing_energy_uncertainty_wh\":");
  json.number(state.uncertainty_wh);
  json.add(",\"batch_missing_heat_bound_w\":");
  json.number(state.missing_heat_bound_w);
  json.add(",\"batch_missing_energy_budget_wh\":");
  json.number(kDailyMissingHeatBudgetW * 24.0);
  json.add(",\"batch_budget_scope\":\"model_advice_only\"");
  json.add(",\"batch_maximum_gap_s\":%u,\"restart\":", kDailyMaximumGapMs / 1000U);
  write_daily_restart_json(json, state.restart);
  json.add("}");
}

}  // namespace oq_ph_learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
