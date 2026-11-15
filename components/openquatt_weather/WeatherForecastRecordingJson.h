#pragma once

#include "WeatherForecastJson.h"
#include "WeatherForecastRecording.h"

namespace esphome::openquatt_weather {

template <typename Sink>
bool write_recording_quality(const ForecastQuality& quality, ForecastJsonWriter<Sink>& out) {
  if (!out.literal(",\"quality\":{\"schema\":2,\"model_context\":") || !out.integer(quality.model_context) ||
      !out.literal(",\"started_epoch\":") || !out.integer(quality.started_epoch) ||
      !out.literal(",\"evaluated_epoch\":") || !out.integer(quality.evaluated_epoch) || !out.literal(",\"issued\":") ||
      !out.integer(quality.issued) || !out.literal(",\"horizons\":["))
    return false;
  for (size_t i = 0; i < 3; ++i) {
    if ((i && !out.byte(',')) || !out.literal("{\"horizon_s\":") ||
        !out.integer(i == 0   ? 1800
                     : i == 1 ? 3600
                              : 10800) ||
        !out.literal(",\"samples\":") || !out.integer(quality.samples[i]) || !out.literal(",\"mae_c\":") ||
        !out.number(quality.samples[i] ? quality.mae_c[i] : NAN) || !out.literal(",\"pending\":") ||
        !out.integer(quality.pending[i]) || !out.literal(",\"discarded\":") || !out.integer(quality.discarded[i]) ||
        !out.literal(",\"unchanged_room_mae_c\":") ||
        !out.number(quality.samples[i] ? quality.unchanged_room_mae_c[i] : NAN) || !out.literal(",\"bias_c\":") ||
        !out.number(quality.samples[i] ? quality.bias_c[i] : NAN) || !out.literal(",\"model_bias_c\":") ||
        !out.number(quality.samples[i] ? quality.model_bias_c[i] : NAN) || !out.literal(",\"reference_mae_c\":") ||
        !out.number(quality.samples[i] ? quality.reference_mae_c[i] : NAN) || !out.literal(",\"model_mae_c\":") ||
        !out.number(quality.samples[i] ? quality.model_mae_c[i] : NAN) || !out.literal(",\"weather_effect_c\":") ||
        !out.number(quality.samples[i] ? quality.weather_effect_c[i] : NAN) || !out.literal(",\"heat_effect_c\":") ||
        !out.number(quality.samples[i] ? quality.heat_effect_c[i] : NAN) || !out.byte('}'))
      return false;
  }
  return out.literal("]}");
}

template <typename Sink>
bool write_forecast_recording(const ForecastRecording::Snapshot& snapshot, uint32_t boot_id, Sink sink) {
  ForecastJsonWriter<Sink> out(sink);
  if (!out.literal(
          R"({"schema":2,"kind":"openquatt-forecast-measurements","storage":"controller_psram","heat_scope":"learner_selected_heat_to_water","boot_id":)") ||
      !out.integer(boot_id) || !out.literal(R"(,"max_age_s":)") || !out.integer(ForecastRecording::kRetentionS) ||
      !out.literal(
          R"(,"horizons_s":[1800,3600,10800],"observation_columns":["monotonic_ms","epoch","segment","context","room_c","outside_c","heat_w"],"observations":[)"))
    return false;
  for (size_t i = 0; i < snapshot.observation_count; ++i) {
    const auto& row = snapshot.observations[i];
    if ((i && !out.byte(',')) || !out.byte('[') || !out.integer64(row.monotonic_ms) || !out.byte(',') ||
        !out.integer(row.epoch) || !out.byte(',') || !out.integer(row.segment) || !out.byte(',') ||
        !out.integer(row.context) || !out.byte(',') || !out.number(row.room_c) || !out.byte(',') ||
        !out.number(row.outside_c) || !out.byte(',') || !out.number(row.heat_w) || !out.byte(']'))
      return false;
  }
  if (!out.literal(R"(],"issues_truncated":)") || !out.literal(snapshot.issues_truncated ? "true" : "false") ||
      !out.literal(R"(,"issues":[)"))
    return false;
  for (size_t i = 0; i < snapshot.issue_count; ++i) {
    const auto& row = snapshot.issues[i];
    ScenarioSnapshot scenario;
    scenario.issued = row.scenario;
    scenario.valid = true;
    scenario.horizon_valid_mask = 7U;
    scenario.reason = ScenarioReason::READY;
    if ((i && !out.byte(',')) || !out.literal(R"({"monotonic_ms":)") || !out.integer64(row.monotonic_ms) ||
        !out.literal(R"(,"scenario":)") || !write_scenario_json(scenario, out) ||
        !out.literal(R"(,"outside_step_s":900,"forecast_outside_c":[)"))
      return false;
    for (size_t j = 0; j < 13; ++j)
      if ((j && !out.byte(',')) || !out.number(row.outside_c[j])) return false;
    if (!out.literal("]}")) return false;
  }
  return out.byte(']') && out.literal(R"(,"quality_scope":"current_evaluation_since_reset")") &&
         write_recording_quality(snapshot.quality, out) && out.byte('}') && out.flush();
}

}  // namespace esphome::openquatt_weather
