#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "esphome/components/globals/globals_component.h"
#include "esphome/components/openquatt_decision_log/OpenQuattDecisionLog.h"
#include "esphome/components/openquatt_web_auth/OpenQuattWebAuth.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "OpenQuattIncidentPolicy.h"
#include "PsramBuffer.h"
#include "includes/diagnostics/oq_pump_ipwm_feedback.h"
#include "includes/incidents/oq_hp_incident_engine.h"
#include "includes/incidents/oq_manual_reset_latch_policy.h"

#ifndef OQ_TOPOLOGY_DUO
#define OQ_TOPOLOGY_DUO 0
#endif

namespace esphome {
namespace openquatt_incident_manager {

class OpenQuattIncidentManager : public Component {
 public:
  using IntGlobal = globals::GlobalsComponent<int>;
  enum class DeferredActionKind : uint8_t {
    NONE = 0U,
    START_FAILURE_RETRY = 1U,
    CONFIRM_ODU_POWER_CYCLE = 2U,
  };
  enum class DeferredActionQueueResult : uint8_t {
    ACCEPTED = 0U,
    DUPLICATE = 1U,
    BUSY = 2U,
    INVALID = 3U,
  };

  void set_clock(time::RealTimeClock* clock) { this->clock_ = clock; }
  void set_control_mode_code(IntGlobal* value) { this->control_mode_code_ = value; }
  void set_decision_log(openquatt_decision_log::OpenQuattDecisionLog* value) { this->decision_log_ = value; }
  void set_web_auth(openquatt_web_auth::OpenQuattWebAuth* value) { this->web_auth_ = value; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void observe_transport(uint8_t hp_index, bool online, uint32_t now_ms);
  void observe_working_mode(uint8_t hp_index, float working_mode, uint32_t now_ms);
  void observe_compressor_frequency(uint8_t hp_index, float frequency_hz, uint32_t now_ms);
  void observe_fault_word(uint8_t hp_index, uint16_t register_address, uint16_t word, uint32_t now_ms);
  void observe_pump_context(uint8_t hp_index, bool request_valid, bool request_on, bool relay_valid, bool relay_on,
                            bool flow_switch_valid, bool flow_switch_on, bool feedback_valid, uint16_t feedback_raw,
                            bool flow_valid, float flow_lph);

  bool request_start(uint8_t hp_index, uint8_t expected_mode, uint32_t now_ms);
  void request_stop(uint8_t hp_index, uint32_t now_ms);
  oq_incidents::StartFailureResetResult retry_start_failure(uint8_t hp_index, uint32_t now_ms,
                                                            uint32_t request_id = 0U);
  DeferredActionQueueResult defer_start_failure_retry(uint8_t hp_index, uint32_t request_id);
  bool acknowledge(uint8_t hp_index, oq_incidents::IncidentId incident_id);
  uint8_t acknowledge_all_cleared();
  bool confirm_odu_power_cycle(uint8_t hp_index, uint32_t now_ms, uint32_t request_id = 0U);
  DeferredActionQueueResult defer_odu_power_cycle_confirmation(uint8_t hp_index, uint32_t request_id);

  oq_incidents::DerivedOutputs get_outputs(uint8_t hp_index) const;
  bool hp_configured(uint8_t hp_index) const;
  uint8_t configured_hp_count() const;
  uint8_t available_hp_count() const;
  bool availability_complete() const;
  bool all_unavailable_hps_allow_fallback() const;
  bool all_fallback_outputs_safe() const;

  void set_fallback_status(bool requested, bool active, uint8_t block_reason);
  void set_boiler_status(uint8_t role, bool command_active, bool output_continuous);

  void write_snapshot(httpd_req_t* req) const;
  bool storage_ready() const { return this->storage_ready_(); }
  const char* get_action_csrf_token() const { return this->action_csrf_token_.data(); }
  bool request_is_authenticated(AsyncWebServerRequest* request) const {
    return this->web_auth_ != nullptr && this->web_auth_->request_is_authenticated(request);
  }

 protected:
  static constexpr uint32_t LINK_ROUND_TIMEOUT_MS = 15000U;
  static constexpr uint32_t PARTIAL_FAULT_SNAPSHOT_TIMEOUT_MS = 15000U;
  static constexpr uint32_t MANUAL_RESET_PERSIST_RETRY_MS = 60000U;
  static constexpr uint8_t INITIALIZATION_FAULT_SNAPSHOT_COUNT = 2U;
  static constexpr size_t SYNTHETIC_INCIDENT_COUNT = 4U;
  static constexpr size_t ACTION_RESULT_HISTORY_SIZE = 4U;
  static constexpr oq_incidents::IncidentId LINK_LOSS_INCIDENT_ID = oq_incidents::kLinkLossIncidentId;
  static constexpr oq_incidents::IncidentId START_FAILED_INCIDENT_ID = oq_incidents::kStartFailedIncidentId;
  static constexpr oq_incidents::IncidentId STOP_UNCONFIRMED_INCIDENT_ID = oq_incidents::kStopUnconfirmedIncidentId;
  static constexpr oq_incidents::IncidentId PERSISTENCE_FAILURE_INCIDENT_ID =
      oq_incidents::kPersistenceFailureIncidentId;

  struct ActionResultRecord {
    const char* action{"none"};
    const char* result{"none"};
    bool ok{false};
    uint32_t sequence{0U};
    uint32_t request_id{0U};
    uint32_t at_ms{0U};
  };

  struct PumpContextState {
    bool request_valid{false};
    bool request_on{false};
    bool relay_valid{false};
    bool relay_on{false};
    bool flow_switch_valid{false};
    bool flow_switch_on{false};
    bool feedback_valid{false};
    uint16_t feedback_raw{0U};
    oq_pump_ipwm::Status status{oq_pump_ipwm::Status::UNKNOWN};
    bool power_valid{false};
    float power_w{0.0F};
    bool flow_valid{false};
    float flow_lph{0.0F};
  };

  struct UnitState {
    oq_incidents::HpIncidentEngine engine{};
    bool configured{false};
    bool transport_online{false};
    bool transport_seen{false};
    float working_mode{0.0F};
    bool working_mode_valid{false};
    uint32_t working_mode_generation{0U};
    float compressor_frequency_hz{0.0F};
    bool compressor_frequency_valid{false};
    uint32_t compressor_frequency_generation{0U};
    uint8_t expected_mode{0U};
    uint8_t active_command_mode{0U};
    uint8_t stop_confirmation_reason{0U};
    uint32_t command_mode_generation{0U};
    uint32_t command_frequency_generation{0U};
    bool start_feedback_armed{false};
    bool stop_feedback_armed{false};
    bool run_seen_since_last_confirmed_stop{false};
    std::array<uint16_t, oq_incidents::kFaultRegisterCount> fault_words{};
    std::array<bool, oq_incidents::kFaultRegisterCount> fault_pending{};
    uint32_t fault_pending_since_ms{0U};
    uint32_t fault_snapshot_generation{0U};
    uint8_t initialization_fault_snapshot_count{0U};
    uint32_t last_link_working_generation{0U};
    uint32_t last_link_frequency_generation{0U};
    uint32_t last_link_fault_generation{0U};
    uint32_t last_link_round_ms{0U};
    std::array<oq_incidents::IncidentRuntime, oq_incidents::kRawIncidentSlotCount> previous_incidents{};
    oq_incidents::DerivedOutputs previous_outputs{};
    uint8_t last_reported_availability{openquatt_decision_log::STATE_UNKNOWN};
    bool availability_reporting_initialized{false};
    std::array<oq_incidents::IncidentRuntime, SYNTHETIC_INCIDENT_COUNT> synthetic_incidents{};
    bool restored_manual_reset_pending{false};
    const char* last_action{"none"};
    const char* last_action_result{"none"};
    bool last_action_ok{false};
    uint32_t last_action_seq{0U};
    uint32_t last_action_request_id{0U};
    uint32_t last_action_at_ms{0U};
    std::array<ActionResultRecord, ACTION_RESULT_HISTORY_SIZE> action_results{};
    size_t action_result_head{0U};
    size_t action_result_count{0U};
    uint32_t pending_action_request_id{0U};
    DeferredActionKind pending_action_kind{DeferredActionKind::NONE};
    PumpContextState pump_context{};
  };

  struct PublishedUnit {
    oq_incidents::DerivedOutputs outputs{};
    std::array<oq_incidents::IncidentRuntime, oq_incidents::kRawIncidentSlotCount> incidents{};
    std::array<oq_incidents::IncidentRuntime, SYNTHETIC_INCIDENT_COUNT> synthetic_incidents{};
    const char* last_action{"none"};
    const char* last_action_result{"none"};
    bool last_action_ok{false};
    uint32_t last_action_seq{0U};
    uint32_t last_action_request_id{0U};
    uint32_t last_action_at_ms{0U};
    std::array<ActionResultRecord, ACTION_RESULT_HISTORY_SIZE> action_results{};
    size_t action_result_count{0U};
    PumpContextState pump_context{};
  };

  struct PublishedSnapshot {
    std::array<PublishedUnit, 2U> units{};
    uint8_t control_mode{0U};
    uint8_t boiler_role{0U};
    uint8_t previous_boiler_role{0U};
    uint8_t fallback_block_reason{0U};
    bool fallback_requested{false};
    bool fallback_active{false};
    bool boiler_command_active{false};
    bool boiler_output_continuous{false};
    uint32_t generated_at_ms{0U};
    uint32_t generated_at_epoch_s{0U};
  };

  struct ExternalState {
    std::array<UnitState, 2U> units{};
    std::array<PublishedSnapshot, 3U> snapshots{};
  };

  static bool valid_hp_index_(uint8_t hp_index);
  static size_t hp_slot_(uint8_t hp_index);
  static uint32_t elapsed_ms_(uint32_t now_ms, uint32_t since_ms);
  static uint8_t availability_state_(const oq_incidents::DerivedOutputs& outputs);
  static uint8_t incident_reason_(const oq_incidents::IncidentDefinition& definition);
  static uint8_t incident_severity_(const oq_incidents::IncidentDefinition& definition);
  static uint8_t event_subject_(size_t slot);
  static uint8_t incident_flags_(const oq_incidents::IncidentDefinition& definition,
                                 const oq_incidents::IncidentRuntime& runtime);
  static uint16_t duration_seconds_(uint32_t from_ms, uint32_t to_ms);
  static size_t synthetic_slot_(oq_incidents::IncidentId incident_id);
  static oq_incidents::IncidentDefinition synthetic_definition_(size_t slot);
  static uint8_t reason_for_incident_id_(oq_incidents::IncidentId incident_id);
  static uint32_t epoch_for_runtime_ms_(uint32_t generated_epoch_s, uint32_t generated_at_ms, uint32_t runtime_ms);

  bool storage_ready_() const;
  std::array<UnitState, 2U>& units_();
  const std::array<UnitState, 2U>& units_() const;
  PublishedSnapshot& published_snapshot_();
  const PublishedSnapshot& published_snapshot_() const;
  PublishedSnapshot& staging_snapshot_();
  PublishedSnapshot& response_snapshot_() const;
  UnitState* unit_(uint8_t hp_index);
  const UnitState* unit_(uint8_t hp_index) const;
  void process_fault_snapshot_(UnitState& unit, size_t slot, uint32_t now_ms, bool force_partial);
  void observe_complete_link_round_(UnitState& unit, uint32_t now_ms);
  void publish_transitions_(UnitState& unit, size_t slot, uint32_t now_ms);
  void publish_incident_transition_(size_t slot, const oq_incidents::IncidentDefinition& definition,
                                    const oq_incidents::IncidentRuntime& previous,
                                    const oq_incidents::IncidentRuntime& current, uint32_t now_ms);
  void publish_synthetic_incident_(UnitState& unit, size_t hp_slot, size_t synthetic_slot, bool next_active,
                                   uint32_t now_ms);
  void publish_synthetic_transition_(size_t hp_slot, const oq_incidents::IncidentDefinition& definition, uint8_t reason,
                                     const oq_incidents::IncidentRuntime& previous,
                                     const oq_incidents::IncidentRuntime& current, uint32_t now_ms);
  void setup_manual_reset_persistence_(uint32_t now_ms);
  void rotate_action_csrf_token_();
  DeferredActionQueueResult queue_deferred_action_(UnitState& unit, DeferredActionKind kind, uint32_t request_id);
  void record_action_result_(UnitState& unit, const char* action, const char* result, bool ok, uint32_t now_ms,
                             uint32_t request_id = 0U);
  void reconcile_manual_reset_persistence_(uint32_t now_ms);
  bool persist_manual_reset_mask_(uint8_t latch_mask);
  uint8_t runtime_manual_reset_mask_() const;
  oq_incidents::DerivedOutputs outputs_for_slot_(size_t slot) const;
  void publish_snapshot_(uint32_t now_ms);
  uint32_t current_epoch_s_() const;

  openquatt_common::PsramObjectArray<ExternalState, 1U> external_state_{};
  time::RealTimeClock* clock_{nullptr};
  IntGlobal* control_mode_code_{nullptr};
  openquatt_decision_log::OpenQuattDecisionLog* decision_log_{nullptr};
  openquatt_web_auth::OpenQuattWebAuth* web_auth_{nullptr};
  bool fallback_requested_{false};
  bool fallback_active_{false};
  uint8_t fallback_block_reason_{0U};
  uint8_t boiler_role_{0U};
  uint8_t previous_boiler_role_{0U};
  bool boiler_command_active_{false};
  bool boiler_output_continuous_{false};
  uint32_t last_loop_ms_{0U};
  oq_incidents::ManualResetLatchPersistencePolicy manual_reset_persistence_{};
  uint8_t initialization_manual_reset_mask_{0U};
  ESPPreferenceObject manual_reset_pref_a_{};
  ESPPreferenceObject manual_reset_pref_b_{};
  ESPPreferenceObject manual_reset_marker_pref_{};
  std::array<char, 33U> action_csrf_token_{};
  StaticSemaphore_t action_mutex_storage_{};
  StaticSemaphore_t snapshot_mutex_storage_{};
  SemaphoreHandle_t action_mutex_{nullptr};
  mutable SemaphoreHandle_t snapshot_mutex_{nullptr};
  uint8_t published_snapshot_index_{0U};
  uint8_t staging_snapshot_index_{1U};
  mutable std::atomic<bool> response_snapshot_in_use_{false};
};

static_assert(sizeof(OpenQuattIncidentManager) <= 512U, "Large incident state must remain in the external PSRAM arena");

}  // namespace openquatt_incident_manager
}  // namespace esphome
