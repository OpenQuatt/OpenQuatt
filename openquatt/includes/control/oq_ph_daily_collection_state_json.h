#pragma once

#include "oq_ph_daily_collection_json.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

namespace oq_ph_learning {

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
  json.add(",\"batch_missing_energy_budget_wh\":");
  json.number(kDailyMissingHeatBudgetW * 24.0);
  json.add(",\"batch_maximum_gap_s\":%u}", kDailyMaximumGapMs / 1000U);
}

}  // namespace oq_ph_learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
