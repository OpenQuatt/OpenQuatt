#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace oq_incidents {

using IncidentId = uint16_t;
using EffectMask = uint16_t;

static constexpr IncidentId kNoIncident = 0U;
static constexpr IncidentId kLinkLossIncidentId = 1001U;
static constexpr IncidentId kStartFailedIncidentId = 1002U;
static constexpr IncidentId kStopUnconfirmedIncidentId = 1003U;
static constexpr IncidentId kPersistenceFailureIncidentId = 1004U;
static constexpr uint16_t kFirstFaultRegister = 2119U;
static constexpr uint16_t kLastFaultRegister = 2121U;
static constexpr size_t kFaultRegisterCount = 3U;
static constexpr size_t kBitsPerFaultRegister = 16U;
static constexpr size_t kRawIncidentSlotCount = kFaultRegisterCount * kBitsPerFaultRegister;

enum class IncidentCategory : uint8_t {
  STATUS = 0,
  PROTECTION = 1,
  WARNING = 2,
  FAULT = 3,
};

enum class IncidentSeverity : uint8_t {
  INFO = 0,
  WARNING = 1,
  FAULT = 2,
};

enum class IncidentEffect : EffectMask {
  NONE = 0U,
  DISPLAY = 1U << 0U,
  LIMIT_CAPACITY = 1U << 1U,
  BLOCK_START = 1U << 2U,
  STOP_COMPRESSOR = 1U << 3U,
  MARK_HP_UNAVAILABLE = 1U << 4U,
  PUMP_UNAVAILABLE = 1U << 5U,
  ALLOW_CM4 = 1U << 6U,
  BLOCK_BOILER = 1U << 7U,
  REQUIRE_CONFIRMED_ODU_POWER_CYCLE = 1U << 8U,
};

constexpr EffectMask effect_mask(IncidentEffect effect) { return static_cast<EffectMask>(effect); }

constexpr EffectMask operator|(IncidentEffect lhs, IncidentEffect rhs) { return effect_mask(lhs) | effect_mask(rhs); }

constexpr EffectMask operator|(EffectMask lhs, IncidentEffect rhs) { return lhs | effect_mask(rhs); }

constexpr bool has_effect(EffectMask effects, IncidentEffect effect) { return (effects & effect_mask(effect)) != 0U; }

enum class ClearPolicy : uint8_t {
  AFTER_STABLE_READS = 0,
  AFTER_CONFIRMED_ODU_POWER_CYCLE = 1,
};

enum class FallbackPolicy : uint8_t {
  NEVER = 0,
  AFTER_SYSTEM_GUARDS = 1,
};

enum class DocumentationConfidence : uint8_t {
  DESCRIBED = 0,
  NAME_ONLY = 1,
  REVIEW_REQUIRED = 2,
};

enum class UserAction : uint8_t {
  NONE = 0,
  WAIT_FOR_AUTOMATIC_RECOVERY = 1,
  CHECK_INSTALLATION = 2,
  CONTACT_INSTALLER = 3,
};

enum class RecoveryCondition : uint8_t {
  WHEN_BIT_CLEARS = 0,
  STABLE_READS_AND_RECOVERY_WINDOW = 1,
  PREHEAT_COMPLETE = 2,
  CONFIRMED_ODU_POWER_CYCLE = 3,
  STABLE_TELEMETRY = 4,
  EXPLICIT_RETRY_AFTER_SAFE_STOP = 5,
  FRESH_STOP_CONFIRMATION = 6,
  REVIEW_REQUIRED = 7,
  AFTER_STABLE_READS = 8,
};

enum class LinkState : uint8_t {
  BOOTSTRAP = 0,
  HEALTHY = 1,
  SUSPECT = 2,
  LOST = 3,
  RECOVERING = 4,
};

enum class ProtectionState : uint8_t {
  CLEAR = 0,
  LIMITED = 1,
  START_BLOCKED = 2,
  FAULT_ACTIVE = 3,
  FAULT_RECOVERY = 4,
};

enum class RunState : uint8_t {
  UNKNOWN = 0,
  STOPPED = 1,
  START_REQUESTED = 2,
  WAIT_MODE = 3,
  WAIT_COMPRESSOR = 4,
  RUNNING = 5,
  STOPPING = 6,
  STOP_UNCONFIRMED = 7,
};

enum class StartFailureResetResult : uint8_t {
  READY = 0,
  CLEARED = 1,
  NO_START_FAILURE = 2,
  STOP_NOT_CONFIRMED = 3,
  LINK_NOT_HEALTHY = 4,
  HARD_FAULT_ACTIVE = 5,
  FAULT_RECOVERY_PENDING = 6,
  HP_NOT_CONFIGURED = 7,
};

struct IncidentDefinition {
  IncidentId id = kNoIncident;
  uint16_t register_address = 0U;
  uint8_t bit = 0U;
  const char* key = "";
  const char* presentation_key = "";
  IncidentCategory category = IncidentCategory::FAULT;
  IncidentSeverity severity = IncidentSeverity::FAULT;
  EffectMask effects = effect_mask(IncidentEffect::NONE);
  uint8_t trip_reads = 1U;
  uint8_t clear_reads = 1U;
  ClearPolicy clear_policy = ClearPolicy::AFTER_STABLE_READS;
  FallbackPolicy fallback_policy = FallbackPolicy::NEVER;
  DocumentationConfidence documentation_confidence = DocumentationConfidence::REVIEW_REQUIRED;
  UserAction user_action = UserAction::CONTACT_INSTALLER;
  RecoveryCondition recovery_condition = RecoveryCondition::REVIEW_REQUIRED;
};

struct IncidentRuntime {
  bool raw_active = false;
  bool confirmed_active = false;
  bool latched = false;
  bool acknowledged = false;
  uint8_t trip_streak = 0U;
  uint8_t clear_streak = 0U;
  uint32_t first_seen_ms = 0U;
  uint32_t last_seen_ms = 0U;
  uint32_t cleared_at_ms = 0U;
  uint32_t occurrence_count = 0U;
};

struct EngineTuning {
  uint32_t link_lost_ms = 30000U;
  uint8_t link_lost_rounds = 3U;
  uint32_t link_recovery_ms = 60000U;
  uint8_t link_recovery_rounds = 3U;
  uint32_t fault_recovery_ms = 60000U;
  uint8_t fault_recovery_rounds = 3U;
  float compressor_running_threshold_hz = 0.5F;
  uint8_t stop_confirm_reads = 2U;
  uint32_t mode_ack_timeout_ms = 30000U;
  uint32_t start_timeout_ms = 120000U;
  uint32_t stop_confirm_timeout_ms = 60000U;
};

struct FaultWordsObservation {
  uint32_t now_ms = 0U;
  std::array<uint16_t, kFaultRegisterCount> words{};
  std::array<bool, kFaultRegisterCount> fresh{};
};

struct RunObservation {
  uint32_t now_ms = 0U;
  bool fresh = false;
  bool mode_matches_request = false;
  bool stop_mode_confirmed = false;
  bool compressor_frequency_valid = false;
  float compressor_frequency_hz = 0.0F;
};

struct DerivedOutputs {
  LinkState link_state = LinkState::BOOTSTRAP;
  ProtectionState protection_state = ProtectionState::CLEAR;
  RunState run_state = RunState::UNKNOWN;
  EffectMask active_effects = effect_mask(IncidentEffect::NONE);
  IncidentId primary_incident_id = kNoIncident;
  uint8_t active_incident_count = 0U;
  bool available_for_start = false;
  bool must_stop = false;
  bool fault_active = false;
  bool protection_active = false;
  bool running_confirmed = false;
  bool stop_confirmed = false;
  bool stop_confirmation_pending = false;
  bool stop_unconfirmed = false;
  bool start_mode_ack_timed_out = false;
  bool start_timed_out = false;
  // These are HP-local facts only. CM4 still requires supervisory heating,
  // flow, temperature, boiler and topology guards.
  bool fallback_cause_present = false;
  bool fallback_eligible = false;
};

constexpr bool valid_fault_location(uint16_t register_address, uint8_t bit) {
  return register_address >= kFirstFaultRegister && register_address <= kLastFaultRegister &&
         bit < kBitsPerFaultRegister;
}

constexpr size_t incident_slot(uint16_t register_address, uint8_t bit) {
  return static_cast<size_t>(register_address - kFirstFaultRegister) * kBitsPerFaultRegister + bit;
}

constexpr IncidentId incident_id(uint16_t register_address, uint8_t bit) {
  return valid_fault_location(register_address, bit)
             ? static_cast<IncidentId>(incident_slot(register_address, bit) + 1U)
             : kNoIncident;
}

constexpr uint16_t register_for_slot(size_t slot) {
  return static_cast<uint16_t>(kFirstFaultRegister + slot / kBitsPerFaultRegister);
}

constexpr uint8_t bit_for_slot(size_t slot) { return static_cast<uint8_t>(slot % kBitsPerFaultRegister); }

}  // namespace oq_incidents
