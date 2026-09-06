#pragma once

#include "oq_ph_learning_platform.h"

#if OQ_PH_LEARNING_CORE_AVAILABLE

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "../control/oq_house_model_logic.h"

namespace oq_power_house::learning {

constexpr size_t kMaxSegmentRecords = 64;
constexpr size_t kMaxPassiveContextBytes = 1024;
// A 42 * 24 hour inclusive retention window can touch 43 UTC calendar days.
constexpr size_t kMaxCalendarDays = 43;
constexpr uint64_t kSegmentDurationMs = 4ULL * 60ULL * 60ULL * 1000ULL;
constexpr uint32_t kMaxRecordAgeS = 42U * 24U * 60U * 60U;
constexpr uint8_t kMaxHuberPasses = 6;
constexpr uint64_t kSetpointRecoveryMs = 60ULL * 60ULL * 1000ULL;
constexpr uint16_t kLearningAlgorithmVersion = 2;

enum class LearningStatus : uint8_t {
  OK = 0,
  COLLECTING,
  SEGMENT_READY,
  FIT_IN_PROGRESS,
  ADVICE_READY,
  INSUFFICIENT_DATA,
  INVALID_CONFIGURATION,
  INVALID_MEASUREMENT,
  MIXED_CONTEXT,
  TIME_DISCONTINUITY,
  SEGMENT_INELIGIBLE,
  NONPOSITIVE_HEAT,
  SETPOINT_CHANGED,
  ROOM_UNSTABLE,
  WATER_STORAGE_UNSTABLE,
  MEASUREMENT_UNCERTAIN,
  DATASET_FULL,
  STALE_DATA,
  INSUFFICIENT_SPREAD,
  DAY_DOMINANCE,
  INVALID_ACTIVE_MODEL,
  FIT_FAILED,
  CANDIDATE_IMPLAUSIBLE,
  NO_HOLDOUT_IMPROVEMENT,
  UNSTABLE_FIT,
};

enum InvalidReason : uint32_t {
  INVALID_NONE = 0,
  INVALID_ESSENTIAL_SOURCE = 1U << 0,
  INVALID_SOURCE_STALE = 1U << 1,
  INVALID_NOT_HEATING = 1U << 2,
  INVALID_BOILER_HEAT = 1U << 3,
  INVALID_DEFROST_OR_OIL_RETURN = 1U << 4,
  INVALID_CONTROL_MODE = 1U << 5,
  INVALID_ACTIVE_LIMIT = 1U << 6,
  INVALID_SERVICE_OR_OTA = 1U << 7,
  INVALID_COOLING = 1U << 8,
  INVALID_SOURCE_UNCERTAIN = 1U << 9,
  // The source keeps this asserted for the full post-setpoint recovery period.
  INVALID_SETPOINT_RECOVERY = 1U << 10,
};

struct LearningSnapshot {
  uint64_t monotonic_ms = 0;
  uint32_t epoch_s = 0;
  uint32_t context_revision = 0;
  uint32_t invalid_reasons = INVALID_NONE;
  float room_c = NAN;
  float setpoint_c = NAN;
  float outside_c = NAN;
  float heat_to_water_w = NAN;  // Signed calorimetry; validity is separate.
  float heat_uncertainty_w = NAN;
  float mean_water_c = NAN;
};

struct SegmentRecord {
  uint32_t start_epoch_s = 0;
  uint32_t end_epoch_s = 0;
  uint32_t duration_s = 0;
  uint32_t context_revision = 0;
  float mean_room_c = NAN;
  float mean_setpoint_c = NAN;
  float mean_outside_c = NAN;
  float mean_heat_w = NAN;
  float mean_heat_uncertainty_w = NAN;
  float room_trend_k_per_h = NAN;
  float room_range_k = NAN;
  float setpoint_range_c = NAN;
  float water_start_c = NAN;
  float water_end_c = NAN;
};

struct RecordBuffer {
  SegmentRecord* records = nullptr;
  size_t count = 0;
  size_t capacity = 0;
};

// Durable measurement configuration plus the revision that binds one coherent
// learning cohort. A changed setting or source starts a new cohort.
struct PassiveContextView {
  const uint8_t* bytes = nullptr;
  size_t size = 0;
  uint32_t context_revision = 0;
};

struct LearningDatasetView {
  const SegmentRecord* records = nullptr;
  size_t record_count = 0;
  PassiveContextView context;
};

inline const char* learning_status_name(LearningStatus status) {
  switch (status) {
    case LearningStatus::OK:
      return "ok";
    case LearningStatus::COLLECTING:
      return "collecting";
    case LearningStatus::SEGMENT_READY:
      return "segment_ready";
    case LearningStatus::FIT_IN_PROGRESS:
      return "fit_in_progress";
    case LearningStatus::ADVICE_READY:
      return "advice_ready";
    case LearningStatus::INSUFFICIENT_DATA:
      return "insufficient_data";
    case LearningStatus::INVALID_CONFIGURATION:
      return "invalid_configuration";
    case LearningStatus::INVALID_MEASUREMENT:
      return "invalid_measurement";
    case LearningStatus::MIXED_CONTEXT:
      return "mixed_context";
    case LearningStatus::TIME_DISCONTINUITY:
      return "time_discontinuity";
    case LearningStatus::SEGMENT_INELIGIBLE:
      return "segment_ineligible";
    case LearningStatus::NONPOSITIVE_HEAT:
      return "nonpositive_heat";
    case LearningStatus::SETPOINT_CHANGED:
      return "setpoint_changed";
    case LearningStatus::ROOM_UNSTABLE:
      return "room_unstable";
    case LearningStatus::WATER_STORAGE_UNSTABLE:
      return "water_storage_unstable";
    case LearningStatus::MEASUREMENT_UNCERTAIN:
      return "measurement_uncertain";
    case LearningStatus::DATASET_FULL:
      return "dataset_full";
    case LearningStatus::STALE_DATA:
      return "stale_data";
    case LearningStatus::INSUFFICIENT_SPREAD:
      return "insufficient_spread";
    case LearningStatus::DAY_DOMINANCE:
      return "day_dominance";
    case LearningStatus::INVALID_ACTIVE_MODEL:
      return "invalid_active_model";
    case LearningStatus::FIT_FAILED:
      return "fit_failed";
    case LearningStatus::CANDIDATE_IMPLAUSIBLE:
      return "candidate_implausible";
    case LearningStatus::NO_HOLDOUT_IMPROVEMENT:
      return "no_holdout_improvement";
    case LearningStatus::UNSTABLE_FIT:
      return "unstable_fit";
  }
  return "unknown";
}

}  // namespace oq_power_house::learning

#endif  // OQ_PH_LEARNING_CORE_AVAILABLE
