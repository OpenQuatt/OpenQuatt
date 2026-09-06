#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../openquatt/includes/learning/oq_ph_learning_aggregate.h"
#include "../openquatt/includes/learning/oq_ph_learning_fit.h"
#include "../openquatt/includes/learning/oq_ph_model_validation.h"
#include "../openquatt/includes/learning/oq_ph_thermal_aggregate.h"

namespace learning = oq_power_house::learning;

static const char* kHeader =
    "monotonic_ms,epoch_s,source_generation,physical_context_generation,control_generation,invalid_reasons,room_c,"
    "setpoint_c,outside_c,heat_to_water_w,heat_uncertainty_w,mean_water_c";
static const size_t kFields = 12;

struct Counters {
  unsigned long long rows = 0;
  unsigned long long accepted_windows = 0;
  unsigned long long rejected_observations = 0;
  unsigned long long malformed_rows = 0;
  unsigned long long incomplete_windows = 0;
  unsigned long long discarded_cohort_records = 0;
  unsigned long long cohort_changes = 0;
  unsigned long long status_counts[static_cast<size_t>(learning::LearningStatus::UNSTABLE_FIT) + 1]{};
};

static const size_t kStatusCount = static_cast<size_t>(learning::LearningStatus::UNSTABLE_FIT) + 1;

static bool parse_u64(const char* text, uint64_t& value) {
  if (text == nullptr || *text == '\0' || *text == '-') return false;
  for (const char* digit = text; *digit != '\0'; ++digit)
    if (*digit < '0' || *digit > '9') return false;
  char* end = nullptr;
  errno = 0;
  const unsigned long long parsed = strtoull(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0') return false;
  value = static_cast<uint64_t>(parsed);
  return true;
}

static bool parse_u32(const char* text, uint32_t& value) {
  uint64_t parsed = 0;
  if (!parse_u64(text, parsed) || parsed > UINT32_MAX) return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

static bool parse_float(const char* text, float& value) {
  if (text == nullptr || *text == '\0') return false;
  char* end = nullptr;
  errno = 0;
  value = strtof(text, &end);
  return errno == 0 && end != text && *end == '\0';
}

static bool parse_snapshot(char* line, learning::LearningSnapshot& snapshot) {
  char* fields[kFields]{};
  size_t count = 0;
  char* cursor = line;
  while (cursor != nullptr && count < kFields) {
    fields[count++] = cursor;
    char* comma = strchr(cursor, ',');
    if (comma == nullptr) {
      cursor = nullptr;
    } else {
      *comma = '\0';
      cursor = comma + 1;
    }
  }
  if (cursor != nullptr || count != kFields) return false;
  uint64_t monotonic_ms = 0;
  uint32_t epoch_s = 0;
  uint32_t source_generation = 0;
  uint32_t physical_generation = 0;
  uint32_t control_generation = 0;
  uint32_t invalid_reasons = 0;
  if (!parse_u64(fields[0], monotonic_ms) || !parse_u32(fields[1], epoch_s) ||
      !parse_u32(fields[2], source_generation) || !parse_u32(fields[3], physical_generation) ||
      !parse_u32(fields[4], control_generation) || !parse_u32(fields[5], invalid_reasons) ||
      !parse_float(fields[6], snapshot.room_c) || !parse_float(fields[7], snapshot.setpoint_c) ||
      !parse_float(fields[8], snapshot.outside_c) || !parse_float(fields[9], snapshot.heat_to_water_w) ||
      !parse_float(fields[10], snapshot.heat_uncertainty_w) || !parse_float(fields[11], snapshot.mean_water_c))
    return false;
  snapshot.monotonic_ms = monotonic_ms;
  snapshot.epoch_s = epoch_s;
  snapshot.source_generation = source_generation;
  snapshot.physical_context_generation = physical_generation;
  snapshot.control_generation = control_generation;
  snapshot.invalid_reasons = invalid_reasons;
  return true;
}

static void print_float(double value) {
  if (isfinite(value))
    printf("%.9g", static_cast<double>(value));
  else
    printf("null");
}

static void print_status(learning::LearningStatus status) {
  printf("\"batch_status\":\"%s\"", learning::learning_status_name(status));
}

static void print_reasons(learning::LearningStatus status, const Counters& counters, bool input_error) {
  printf(",\"reasons\":[\"%s\"", learning::learning_status_name(status));
  if (input_error) printf(",\"input_rejected\"");
  for (size_t index = 0; index < kStatusCount; ++index)
    if (counters.status_counts[index] != 0)
      printf(",\"%s:%llu\"", learning::learning_status_name(static_cast<learning::LearningStatus>(index)),
             counters.status_counts[index]);
  printf("]");
}

static bool parse_option_float(const char* text, float& value) { return parse_float(text, value) && isfinite(value); }

// Count bytes instead of using fgets/strlen so embedded NULs cannot hide data.
static int read_line(FILE* file, char* buffer, size_t capacity) {
  size_t length = 0;
  int character = 0;
  while ((character = fgetc(file)) != EOF) {
    if (character == '\0') return -1;
    if (character == '\n') {
      buffer[length] = '\0';
      return 1;
    }
    if (length + 1 >= capacity) return -1;
    buffer[length++] = static_cast<char>(character);
  }
  if (ferror(file) || length == 0) return ferror(file) ? -1 : 0;
  buffer[length] = '\0';
  return 1;
}

int main(int argc, char** argv) {
  const char* path = nullptr;
  float active_h = NAN;
  float active_t0 = NAN;
  float reference_room_c = NAN;
  float reference_setpoint_c = NAN;
  float unmodeled_gain_bound_w = NAN;
  uint32_t now_epoch_s = static_cast<uint32_t>(time(nullptr));
  for (int index = 1; index < argc; ++index) {
    if (strcmp(argv[index], "--active-h") == 0 && index + 1 < argc) {
      if (!parse_option_float(argv[++index], active_h)) return fprintf(stderr, "invalid --active-h\n"), 2;
    } else if (strcmp(argv[index], "--active-t0") == 0 && index + 1 < argc) {
      if (!parse_option_float(argv[++index], active_t0)) return fprintf(stderr, "invalid --active-t0\n"), 2;
    } else if (strcmp(argv[index], "--reference-room-c") == 0 && index + 1 < argc) {
      if (!parse_option_float(argv[++index], reference_room_c) || reference_room_c < 0.0f || reference_room_c > 40.0f)
        return fprintf(stderr, "invalid --reference-room-c\n"), 2;
    } else if (strcmp(argv[index], "--reference-setpoint-c") == 0 && index + 1 < argc) {
      if (!parse_option_float(argv[++index], reference_setpoint_c) || reference_setpoint_c < 5.0f ||
          reference_setpoint_c > 35.0f)
        return fprintf(stderr, "invalid --reference-setpoint-c\n"), 2;
    } else if (strcmp(argv[index], "--rls-max-unmodeled-gain-w") == 0 && index + 1 < argc) {
      if (!parse_option_float(argv[++index], unmodeled_gain_bound_w) || unmodeled_gain_bound_w < 0.0f ||
          unmodeled_gain_bound_w > 50000.0f)
        return fprintf(stderr, "invalid --rls-max-unmodeled-gain-w\n"), 2;
    } else if (strcmp(argv[index], "--now-epoch") == 0 && index + 1 < argc) {
      if (!parse_u32(argv[++index], now_epoch_s) || now_epoch_s == 0)
        return fprintf(stderr, "invalid --now-epoch\n"), 2;
    } else if (argv[index][0] != '-' && path == nullptr) {
      path = argv[index];
    } else {
      return fprintf(stderr, "usage: replay CSV --active-h W/K --active-t0 C [--now-epoch S]\n"), 2;
    }
  }
  if (path == nullptr || !isfinite(active_h) || active_h <= 0.0f || !isfinite(active_t0) ||
      !isfinite(reference_room_c) || !isfinite(reference_setpoint_c))
    return fprintf(stderr, "CSV, active line and reference room/setpoint are required\n"), 2;
  FILE* file = fopen(path, "rb");
  if (file == nullptr) return fprintf(stderr, "cannot open CSV\n"), 2;

  char line[2048];
  const int header_result = read_line(file, line, sizeof(line));
  if (header_result <= 0) {
    fclose(file);
    return fprintf(stderr, "empty CSV\n"), 2;
  }
  const size_t trimmed_length = strlen(line);
  if (trimmed_length > 0 && line[trimmed_length - 1] == '\r') line[trimmed_length - 1] = '\0';
  if (strcmp(line, kHeader) != 0) {
    fclose(file);
    return fprintf(stderr, "malformed CSV header\n"), 2;
  }

  learning::QualityConfig quality;
  learning::ThermalWindowAccumulator thermal_accumulator;
  learning::ThermalWindowConfig thermal_window_config;
  thermal_window_config.unmodeled_gain_bound_valid = isfinite(unmodeled_gain_bound_w);
  thermal_window_config.unmodeled_gain_bound_w = unmodeled_gain_bound_w;
  learning::ThermalModelConfig thermal_config;
  thermal_config.initial_heat_loss_w_per_k = active_h;
  learning::ThermalModelState thermal_state;
  learning::initialize_thermal_model(thermal_state, thermal_config);
  learning::SegmentAccumulator accumulator;
  learning::SegmentRecord records[learning::kMaxSegmentRecords]{};
  learning::RecordBuffer buffer{records, 0, learning::kMaxSegmentRecords};
  Counters counters;
  uint64_t previous_monotonic = 0;
  uint32_t previous_epoch = 0;
  uint32_t cohort_source = 0;
  uint32_t cohort_physical = 0;
  uint32_t cohort_control = 0;
  bool cohort_set = false;
  bool input_error = false;
  int line_result = 0;
  while ((line_result = read_line(file, line, sizeof(line))) > 0) {
    size_t length = strlen(line);
    if (length == sizeof(line) - 1) {
      fclose(file);
      return fprintf(stderr, "CSV row exceeds 2047 bytes\n"), 2;
    }
    while (length > 0 && line[length - 1] == '\r') line[--length] = '\0';
    if (length == 0) continue;
    ++counters.rows;
    learning::LearningSnapshot snapshot;
    if (!parse_snapshot(line, snapshot) || snapshot.monotonic_ms <= previous_monotonic ||
        snapshot.epoch_s < previous_epoch || snapshot.epoch_s > now_epoch_s || snapshot.source_generation == 0 ||
        snapshot.physical_context_generation == 0 || snapshot.control_generation == 0 ||
        (cohort_set &&
         (snapshot.source_generation < cohort_source || snapshot.physical_context_generation < cohort_physical ||
          snapshot.control_generation < cohort_control))) {
      ++counters.malformed_rows;
      ++counters.rejected_observations;
      input_error = true;
      break;
    }
    previous_monotonic = snapshot.monotonic_ms;
    previous_epoch = snapshot.epoch_s;
    if (cohort_set &&
        (snapshot.source_generation != cohort_source || snapshot.physical_context_generation != cohort_physical)) {
      counters.discarded_cohort_records += buffer.count;
      buffer.count = 0;
      learning::reset_segment(accumulator);
      ++counters.cohort_changes;
    }
    if (cohort_set &&
        (snapshot.source_generation != cohort_source || snapshot.physical_context_generation != cohort_physical ||
         snapshot.control_generation != cohort_control)) {
      thermal_accumulator = {};
      learning::initialize_thermal_model(thermal_state, thermal_config);
    }
    cohort_source = snapshot.source_generation;
    cohort_physical = snapshot.physical_context_generation;
    cohort_control = snapshot.control_generation;
    cohort_set = true;
    const auto thermal_window =
        learning::observe_thermal_snapshot(thermal_accumulator, snapshot, quality, thermal_window_config);
    if (thermal_window.has_interval) {
      learning::observe_thermal_interval(thermal_state, thermal_window.interval, thermal_config);
    } else if (thermal_window.status != learning::ThermalWindowStatus::COLLECTING) {
      learning::invalidate_thermal_observation(thermal_state, snapshot.monotonic_ms, thermal_config);
    }
    const learning::ObserveResult observed = learning::observe_snapshot(accumulator, snapshot, quality);
    const size_t status_index = static_cast<size_t>(observed.status);
    if (status_index < kStatusCount) ++counters.status_counts[status_index];
    if (observed.has_record) {
      const learning::LearningStatus appended = learning::append_record(buffer, observed.record, now_epoch_s, quality);
      if (appended == learning::LearningStatus::OK)
        ++counters.accepted_windows;
      else
        ++counters.rejected_observations;
      const size_t appended_index = static_cast<size_t>(appended);
      if (appended_index < kStatusCount) ++counters.status_counts[appended_index];
    } else if (observed.status != learning::LearningStatus::COLLECTING) {
      ++counters.rejected_observations;
    }
  }
  if (line_result < 0 || ferror(file)) input_error = true;
  fclose(file);
  if (accumulator.active) ++counters.incomplete_windows;
  if (counters.rows == 0) return fprintf(stderr, "CSV has no data rows\n"), 2;

  if (input_error) {
    printf(
        "{\"rows\":%llu,\"accepted_windows\":%llu,\"rejected_observations\":%llu,\"malformed_rows\":%llu,\"incomplete_"
        "windows\":%llu,\"discarded_cohort_records\":%llu,\"cohort_changes\":%llu,\"status\":\"input_error\","
        "\"reasons\":[\"input_rejected\"],\"candidate_available\":false,"
        "\"advice_ready\":false,\"auto_apply_allowed\":false}\n",
        counters.rows, counters.accepted_windows, counters.rejected_observations, counters.malformed_rows,
        counters.incomplete_windows, counters.discarded_cohort_records, counters.cohort_changes);
    return 2;
  }

  const oq_power_house::HouseLine active_line{active_h, active_t0};
  learning::AdviceFitWorkspace workspace;
  learning::FitConfig fit_config;
  fit_config.reference_room_c = reference_room_c;
  fit_config.reference_setpoint_c = reference_setpoint_c;
  learning::LearningStatus status = learning::begin_advice_fit(buffer.records, buffer.count, now_epoch_s, active_line,
                                                               quality, fit_config, workspace);
  while (status == learning::LearningStatus::FIT_IN_PROGRESS) status = learning::advance_advice_fit(workspace);
  const learning::AdviceResult& result = learning::advice_result(workspace);
  const uint64_t analysis_age_ms = static_cast<uint64_t>(now_epoch_s - previous_epoch) * 1000ULL;
  const uint64_t analysis_now_ms =
      previous_monotonic <= UINT64_MAX - analysis_age_ms ? previous_monotonic + analysis_age_ms : 0;
  const auto validation = learning::validate_house_models(result, thermal_state, thermal_config, analysis_now_ms,
                                                          cohort_source, cohort_physical, cohort_control);
  printf(
      "{\"rows\":%llu,\"accepted_windows\":%llu,\"rejected_observations\":%llu,\"malformed_rows\":%llu,\"incomplete_"
      "windows\":%llu,\"discarded_cohort_records\":%llu,\"cohort_changes\":%llu,\"reference_room_c\":",
      counters.rows, counters.accepted_windows, counters.rejected_observations, counters.malformed_rows,
      counters.incomplete_windows, counters.discarded_cohort_records, counters.cohort_changes);
  print_float(reference_room_c);
  printf(",\"reference_setpoint_c\":");
  print_float(reference_setpoint_c);
  printf(",");
  print_status(status);
  printf(",\"status\":\"%s\"", result.advice_ready ? learning::model_validation_status_name(validation.status)
                                                   : learning::learning_status_name(status));
  print_reasons(status, counters, false);
  printf(
      ",\"source_generation\":%u,\"physical_context_generation\":%u,\"latest_control_generation\":%u,\"candidate_"
      "available\":%s,\"advice_ready\":%s,\"train_segments\":%u,\"holdout_segments\":%u,\"candidate_h\":",
      result.source_generation, result.physical_context_generation, result.latest_control_generation,
      result.candidate_available ? "true" : "false", validation.cross_validated_advice_ready ? "true" : "false",
      result.train_segments, result.holdout_segments);
  print_float(result.candidate.heat_loss_w_per_k);
  printf(",\"candidate_t0\":");
  print_float(result.candidate.zero_power_temp_c);
  printf(",\"algorithm_version\":%u,\"observed_temp_min_c\":", result.algorithm_version);
  print_float(result.observed_temp_min_c);
  printf(",\"observed_temp_max_c\":");
  print_float(result.observed_temp_max_c);
  printf(",\"validated_temp_min_c\":");
  print_float(result.validated_temp_min_c);
  printf(",\"validated_temp_max_c\":");
  print_float(result.validated_temp_max_c);
  printf(",\"holdout_candidate_mae_w\":");
  print_float(result.holdout_candidate_mae_w);
  printf(",\"holdout_active_mae_w\":");
  print_float(result.holdout_active_mae_w);
  printf(",\"holdout_candidate_signed_bias_w\":");
  print_float(result.holdout_candidate_signed_bias_w);
  printf(",\"holdout_active_signed_bias_w\":");
  print_float(result.holdout_active_signed_bias_w);
  printf(",\"holdout_mean_uncertainty_w\":");
  print_float(result.holdout_mean_uncertainty_w);
  printf(",\"holdout_candidate_signed_bias_by_temp_w\":[");
  for (size_t index = 0; index < 3; ++index) {
    if (index != 0) printf(",");
    print_float(result.holdout_candidate_signed_bias_by_temp_w[index]);
  }
  printf("],\"holdout_active_signed_bias_by_temp_w\":[");
  for (size_t index = 0; index < 3; ++index) {
    if (index != 0) printf(",");
    print_float(result.holdout_active_signed_bias_by_temp_w[index]);
  }
  printf("]");
  printf(",\"holdout_improvement_fraction\":");
  print_float(result.holdout_improvement_fraction);
  printf(",\"batch_advice_ready\":%s,\"model_validation_status\":\"%s\",\"auto_apply_allowed\":false",
         result.advice_ready ? "true" : "false", learning::model_validation_status_name(validation.status));
  printf(",\"u_rls\":");
  print_float(thermal_state.accepted_samples > 0 ? validation.thermal.heat_loss_w_per_k : NAN);
  printf(",\"c_rls_wh_per_k\":");
  print_float(thermal_state.accepted_samples > 0 ? validation.thermal.thermal_capacity_wh_per_k : NAN);
  printf(
      ",\"rls_ready\":%s,\"rls_readiness_reasons\":%u,\"rls_accepted_samples\":%u,\"rls_last_update_monotonic_ms\":%"
      "llu",
      validation.thermal.ready ? "true" : "false", validation.thermal.readiness_reasons, thermal_state.accepted_samples,
      static_cast<unsigned long long>(thermal_state.last_interval_end_monotonic_ms));
  printf(",\"rls_outside_span_c\":");
  print_float(validation.thermal.outside_span_c);
  printf(",\"rls_residual_rms_k_per_h\":");
  print_float(validation.thermal.residual_rms_k_per_h);
  printf(",\"rls_residual_bias_k_per_h\":");
  print_float(validation.thermal.residual_bias_k_per_h);
  printf(",\"rls_effective_observation_hours\":");
  print_float(validation.thermal.effective_observation_hours);
  printf(",\"max_estimated_storage_power_w\":");
  print_float(validation.max_estimated_storage_power_w);
  printf(",\"max_estimated_storage_fraction\":");
  print_float(validation.max_estimated_storage_fraction);
  printf(",\"rls_unmodeled_gain_bound_w\":");
  print_float(unmodeled_gain_bound_w);
  printf(",\"model_difference_fraction\":");
  print_float(validation.heat_loss_difference_fraction);
  printf("}\n");
  // A valid replay with insufficient or rejected learning evidence is still a
  // successful CLI operation; advice_ready in the JSON is the decision gate.
  return 0;
}
