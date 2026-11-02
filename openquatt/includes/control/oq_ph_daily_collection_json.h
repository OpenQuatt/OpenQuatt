#pragma once

#include "../learning/oq_ph_daily_collection_diagnostics.h"
#include "../learning/oq_ph_learning_live_logic.h"
#include "oq_ph_learning_json.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

namespace oq_ph_learning {

inline void write_daily_collection_json(JsonWriter& json,
                                        const oq_power_house::learning::DailyCollectionDiagnostics& diagnostics) {
  using namespace oq_power_house::learning;
  const auto duration = [&json](uint64_t value) {
    if (value == kUnknownDailyDurationMs)
      json.add("null");
    else
      json.add("%llu", static_cast<unsigned long long>(value));
  };
  json.add(
      "{\"scope\":\"active_collection_since_initialize_or_reset\","
      "\"interrupted_periods\":%u,\"completed_quality_rejections\":%u,\"append_rejections\":%u,"
      "\"bridged_gaps\":%u,\"rejected_next_day_remainders\":%u,\"discarded_collected_ms\":%llu,\"cause_counts\":{",
      diagnostics.interrupted_periods, diagnostics.completed_quality_rejections, diagnostics.append_rejections,
      diagnostics.bridged_gaps, diagnostics.rejected_next_day_remainders,
      static_cast<unsigned long long>(diagnostics.discarded_collected_ms));
  bool comma = false;
  for (size_t index = 1; index < kDailyCollectionCauseCount; ++index) {
    if (diagnostics.cause_counts[index] == 0) continue;
    json.add("%s\"%s\":%u", comma ? "," : "", daily_collection_cause_name(static_cast<DailyCollectionCause>(index)),
             diagnostics.cause_counts[index]);
    comma = true;
  }
  json.add("},\"pending_source_status\":\"%s\",\"pending_invalid_reasons\":%u,\"last_remainder_rejection\":",
           snapshot_source_status_name(diagnostics.pending_source_status), diagnostics.pending_invalid_reasons);
  const auto& remainder = diagnostics.last_remainder_rejection;
  if (remainder.epoch_s == 0) {
    json.add("null");
  } else {
    json.add("{\"epoch_s\":%u,\"duration_ms\":", remainder.epoch_s);
    duration(remainder.duration_ms);
    json.add(",\"uncertainty_wh\":");
    json.number(remainder.uncertainty_ws / 3600.0);
    json.add(",\"completed_day_uncertainty_wh\":");
    json.number(remainder.completed_day_uncertainty_ws / 3600.0);
    json.add("}");
  }
  json.add(",\"last_loss\":");
  const auto& loss = diagnostics.last_loss;
  if (loss.cause == DailyCollectionCause::NONE) {
    json.add("null}");
    return;
  }
  json.add("{\"cause\":\"%s\",\"status\":\"%s\",\"source_status\":\"%s\",\"epoch_s\":",
           daily_collection_cause_name(loss.cause), learning_status_name(loss.status),
           snapshot_source_status_name(loss.source_status));
  if (loss.epoch_s == 0)
    json.add("null");
  else
    json.add("%u", loss.epoch_s);
  json.add(",\"context_revision\":%u,\"invalid_reasons\":%u,\"collected_ms\":", loss.context_revision,
           loss.invalid_reasons);
  duration(loss.collected_ms);
  json.add(",\"observed_gap_ms\":");
  duration(loss.observed_gap_ms);
  json.add(",\"charged_uncertainty_wh\":");
  json.number(loss.charged_uncertainty_ws / 3600.0);
  json.add(",\"attempted_day_uncertainty_wh\":");
  json.number(loss.attempted_day_uncertainty_ws / 3600.0);
  json.add(",\"attempted_next_day_uncertainty_wh\":");
  json.number(loss.attempted_next_day_uncertainty_ws / 3600.0);
  json.add("}}");
}

}  // namespace oq_ph_learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
