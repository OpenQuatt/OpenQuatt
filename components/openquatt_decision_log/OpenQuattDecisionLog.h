#pragma once

#include <cstddef>
#include <cstdint>

#include <esp_http_server.h>
#include "OpenQuattFlashLayout.h"
#include "OpenQuattUrgentFlushPolicy.h"
#include "esp_partition.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include "esphome/components/switch/switch.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

namespace esphome {
namespace openquatt_decision_log {

using openquatt_common::OpenQuattFlashLayout;

enum EventType : uint8_t {
  EVENT_UNKNOWN = 0,
  EVENT_BOOT_MARKER = 1,
  EVENT_SOURCE_START = 2,
  EVENT_SOURCE_STOP = 3,
  EVENT_TOPOLOGY_CHANGE = 4,
  EVENT_DECISION_HOLD = 5,
  EVENT_DECISION_BLOCKED = 6,
  EVENT_DEFROST_SEEN_START = 7,
  EVENT_DEFROST_SEEN_CLEAR = 8,
  EVENT_COOLING_LIMITED = 9,
  EVENT_COOLING_RELEASED = 10,
  EVENT_STICKY_PUMP_RUN = 11,
  EVENT_BOILER_ASSIST_START = 12,
  EVENT_BOILER_ASSIST_STOP = 13,
  EVENT_ATTENTION_PATTERN = 14,
  EVENT_FROST_PROTECTION_START = 15,
  EVENT_FROST_PROTECTION_CLEAR = 16,
  EVENT_CANDIDATE_BLOCKED = 17,
  EVENT_FLOW_HOLD_START = 18,
  EVENT_FLOW_HOLD_CLEAR = 19,
  EVENT_STARTUP_INHIBIT_START = 20,
  EVENT_STARTUP_INHIBIT_CLEAR = 21,
  EVENT_STARTUP_INHIBIT_REFRESH = 22,
  EVENT_INCIDENT_START = 23,
  EVENT_INCIDENT_CLEAR = 24,
  EVENT_INCIDENT_ACKNOWLEDGED = 25,
  EVENT_HP_AVAILABILITY_CHANGE = 26,
  EVENT_CONTROL_MODE_CHANGE = 27,
  EVENT_BOILER_FALLBACK_START = 28,
  EVENT_BOILER_FALLBACK_STOP = 29,
  EVENT_HP_START_CONFIRMED = 30,
  EVENT_HP_STOP_CONFIRMED = 31,
};

enum Subject : uint8_t {
  SUBJECT_UNKNOWN = 0,
  SUBJECT_SYSTEM = 1,
  SUBJECT_HP1 = 2,
  SUBJECT_HP2 = 3,
  SUBJECT_BOTH = 4,
  SUBJECT_CV = 5,
  SUBJECT_COOLING = 6,
  SUBJECT_PUMP = 7,
  SUBJECT_CONTROLLER = 8,
};

enum ReasonCode : uint8_t {
  REASON_UNKNOWN = 0,
  REASON_KEEP_CURRENT = 5,
  REASON_HOLD_ACTIVE = 6,
  REASON_DEFROST_HOLD = 7,
  REASON_BETTER_HEAT = 8,
  REASON_SOFT_GUARD = 9,
  REASON_LESS_POWER = 10,
  REASON_NO_CANDIDATE = 11,
  REASON_DEFROST_BOOST = 12,
  REASON_RUNTIME_LEAD = 13,
  REASON_SINGLE_TOPOLOGY = 14,
  REASON_OIL_RETURN_HOLD = 15,
  REASON_MIN_REST_ACTIVE = 30,
  REASON_START_STOP_RATE_HIGH = 31,
  REASON_STICKY_PROTECTION = 32,
  REASON_BOILER_ASSIST = 33,
  REASON_DEW_STOP = 34,
  REASON_COOLING_LIMITER = 35,
  REASON_SENSOR_FALLBACK = 36,
  REASON_FROST_PROTECTION = 37,
  REASON_FLOW_PREFLOW = 38,
  REASON_FLOW_POSTFLOW = 39,
  REASON_FLOW_TOO_LOW = 40,
  REASON_CANDIDATE_IN_REST = 41,
  REASON_CANDIDATE_IN_DEFROST = 42,
  REASON_CANDIDATE_UNAVAILABLE = 43,
  REASON_COOLING_REQUEST_CLEARED = 44,
  REASON_HEATING_REQUEST_CLEARED = 45,
  REASON_PROJECTED_FLOOR = 46,
  REASON_SIMMER = 47,
  REASON_FALLING_GAP = 48,
  REASON_BUFFER_STOP = 49,
  REASON_RESTART_WAIT = 50,
  REASON_ROOM_CAP = 51,
  REASON_LEVEL1_HOLD = 52,
  REASON_OIL_RETURN_RECOVERY = 53,
  REASON_CAPACITY_CAP = 54,
  REASON_STARTUP_INHIBIT = 55,
  REASON_HP_FAULT = 56,
  REASON_HP_PROTECTION = 57,
  REASON_HP_PREHEAT = 58,
  REASON_HP_LINK_LOSS = 59,
  REASON_HP_START_FAILED = 60,
  REASON_HP_STOP_UNCONFIRMED = 61,
  REASON_HP_RECOVERY_WAIT = 62,
  REASON_BOILER_FALLBACK = 64,
  REASON_FALLBACK_BLOCKED = 65,
  REASON_HEATING_REQUEST = 66,
  REASON_COOLING_REQUEST = 67,
  REASON_COMMISSIONING = 68,
  REASON_SUPERVISORY_OVERRIDE = 69,
  REASON_HP_PERSISTENCE_FAILURE = 70,
  REASON_HP_RECOVERED = 71,
  REASON_ROOM_DEMAND = 72,
  REASON_SETPOINT_RAISE = 73,
};

enum Severity : uint8_t {
  SEVERITY_UNKNOWN = 0,
  SEVERITY_NORMAL = 1,
  SEVERITY_LIMITED = 2,
  SEVERITY_ATTENTION = 3,
  SEVERITY_FAULT = 4,
};

enum DecisionState : uint8_t {
  STATE_UNKNOWN = 0,
  STATE_IDLE = 1,
  STATE_STANDBY = 2,
  STATE_ACTIVE = 3,
  STATE_SINGLE = 4,
  STATE_DUO = 5,
  STATE_BLOCKED = 6,
  STATE_LIMITED = 7,
  STATE_AVAILABLE = 8,
  STATE_SUSPECT = 9,
  STATE_OFFLINE = 10,
  STATE_FAULTED = 11,
  STATE_RECOVERING = 12,
  STATE_STARTING = 13,
  STATE_PREHEAT = 14,
  STATE_FALLBACK = 15,
};

enum EventFlags : uint8_t {
  FLAG_NONE = 0x00,
  FLAG_LATCHED = 0x01,
  FLAG_AUTO_CLEARABLE = 0x02,
  FLAG_STOP_UNCONFIRMED = 0x04,
  FLAG_FALLBACK_REQUESTED = 0x08,
  FLAG_FALLBACK_BLOCKED = 0x10,
  FLAG_RAW_VALUE_VALID = 0x20,
  FLAG_MANUAL_RESET_REQUIRED = 0x40,
  FLAG_RESTORED_AFTER_BOOT = 0x80,
};

struct DecisionEvent {
  uint32_t seq{0};
  uint32_t epoch_s{0};
  uint64_t uptime_s{0};
  uint16_t duration_s{0};
  int16_t value_a{0};
  int16_t value_b{0};
  int16_t threshold_a{0};
  uint8_t event_type{EVENT_UNKNOWN};
  uint8_t subject{SUBJECT_UNKNOWN};
  uint8_t reason_code{REASON_UNKNOWN};
  uint8_t severity{SEVERITY_UNKNOWN};
  uint8_t control_mode_code{0};
  uint8_t from_state{STATE_UNKNOWN};
  uint8_t to_state{STATE_UNKNOWN};
  uint8_t flags{0};
};

struct HourBucket {
  uint64_t hour_start_uptime_s{0};
  uint32_t hour_start_epoch_s{0};
  uint16_t starts_hp1{0};
  uint16_t starts_hp2{0};
  uint16_t stops_hp1{0};
  uint16_t stops_hp2{0};
  uint16_t topology_single_count{0};
  uint16_t topology_duo_count{0};
  uint16_t cv_assist_start_count{0};
  uint16_t cv_assist_stop_count{0};
  uint16_t cooling_limited_count{0};
  uint16_t cooling_released_count{0};
  uint16_t dewpoint_stop_count{0};
  uint16_t sticky_run_count{0};
  uint16_t defrost_seen_count_hp1{0};
  uint16_t defrost_seen_count_hp2{0};
  uint16_t defrost_hold_count_hp1{0};
  uint16_t defrost_hold_count_hp2{0};
  uint16_t defrost_boost_count_hp1{0};
  uint16_t defrost_boost_count_hp2{0};
  uint16_t attention_count{0};
  bool valid{false};
};

static_assert(sizeof(DecisionEvent) <= 32, "DecisionEvent must stay compact");
static_assert(sizeof(HourBucket) <= 64, "HourBucket must stay compact");

class OpenQuattDecisionLog : public Component {
 public:
  ~OpenQuattDecisionLog();

  void set_clock(time::RealTimeClock* clock) { this->clock_ = clock; }
  void set_flash_switch(switch_::Switch* flash_switch) { this->flash_switch_ = flash_switch; }
  void set_event_capacity(size_t capacity) { this->event_capacity_requested_ = capacity; }
  void set_event_fallback_capacity(size_t capacity) { this->event_capacity_fallback_ = capacity; }
  void set_hour_bucket_capacity(size_t capacity) { this->bucket_capacity_requested_ = capacity; }
  void set_hour_bucket_fallback_capacity(size_t capacity) { this->bucket_capacity_fallback_ = capacity; }

  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void emit(uint8_t event_type, uint8_t subject, uint8_t reason_code, uint8_t severity, uint8_t control_mode_code,
            uint8_t from_state, uint8_t to_state, int16_t value_a = 0, int16_t value_b = 0, int16_t threshold_a = 0,
            uint16_t duration_s = 0, uint8_t flags = 0);

  void write_decision_log(httpd_req_t* req) const;
  void write_metadata(httpd_req_t* req) const;
  void set_flash_enabled(bool enabled);
  bool force_flush();
  bool clear_flash_history();

 protected:
  static constexpr uint32_t FLASH_TAG_MAGIC = 0x4F444C47;  // "ODLG"
  static constexpr uint16_t FLASH_TAG_VERSION = 2;
  static constexpr size_t FLASH_PARTITION_OFFSET = OpenQuattFlashLayout::DECISION_LOG_OFFSET;
  static constexpr size_t FLASH_SECTOR_SIZE = OpenQuattFlashLayout::SECTOR_SIZE;
  static constexpr size_t FLASH_SLOT_SIZE = 512;
  static constexpr size_t FLASH_SLOTS_PER_SECTOR = 8;
  static constexpr size_t FLASH_SECTOR_COUNT = OpenQuattFlashLayout::DECISION_LOG_SECTOR_COUNT;
  static constexpr size_t FLASH_SLOT_COUNT = FLASH_SLOTS_PER_SECTOR * FLASH_SECTOR_COUNT;
  static constexpr size_t FLASH_TOTAL_BYTES = FLASH_SECTOR_SIZE * FLASH_SECTOR_COUNT;
  static constexpr size_t FLASH_EVENTS_PER_SLOT = 20;
  static constexpr size_t FLASH_EVENT_CAPACITY = FLASH_SLOT_COUNT * FLASH_EVENTS_PER_SLOT;
  static constexpr uint32_t RETENTION_SECONDS = 7UL * 24UL * 60UL * 60UL;
  static constexpr uint64_t URGENT_FLUSH_COALESCE_US = 2ULL * 1000ULL * 1000ULL;
  static constexpr uint64_t URGENT_FLUSH_MIN_INTERVAL_US = 15ULL * 1000ULL * 1000ULL;
  static constexpr uint64_t URGENT_FLUSH_RETRY_US = 30ULL * 1000ULL * 1000ULL;
  static constexpr size_t URGENT_FLUSH_MAX_BATCHES = 4U;

  static_assert(FLASH_EVENT_CAPACITY == 5120, "Decision-log flash ring capacity changed unexpectedly");
  static_assert(FLASH_PARTITION_OFFSET % FLASH_SECTOR_SIZE == 0,
                "Decision-log flash region must start on a sector boundary");
  static_assert(FLASH_SLOT_SIZE * FLASH_SLOTS_PER_SECTOR == FLASH_SECTOR_SIZE,
                "Decision-log flash slots must fit in sectors");

  struct __attribute__((packed)) FlashEventRecord {
    uint32_t seq;
    uint32_t epoch_s;
    int16_t value_a;
    int16_t value_b;
    int16_t threshold_a;
    uint16_t duration_s;
    uint8_t event_type;
    uint8_t subject;
    uint8_t reason_code;
    uint8_t severity;
    uint8_t control_mode_code;
    uint8_t from_state;
    uint8_t to_state;
    uint8_t flags;
  };

  struct __attribute__((packed)) FlashBlockHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t event_count;
    uint32_t block_sequence;
    uint32_t write_epoch_s;
    uint32_t first_event_seq;
    uint32_t first_epoch_s;
    uint32_t last_epoch_s;
    uint32_t crc32;
  };

  struct FlashBlockInfo {
    uint32_t block_sequence{0};
    uint32_t write_epoch_s{0};
    uint32_t first_event_seq{0};
    uint32_t first_epoch_s{0};
    uint32_t last_epoch_s{0};
    uint16_t event_count{0};
    uint32_t slot_index{0};
  };

  static_assert(sizeof(FlashEventRecord) == 24, "Decision-log flash event must stay compact");
  static_assert(sizeof(FlashBlockHeader) == 32, "Decision-log flash header must stay packed");
  static_assert(sizeof(FlashBlockHeader) + (sizeof(FlashEventRecord) * FLASH_EVENTS_PER_SLOT) == FLASH_SLOT_SIZE,
                "Decision-log event block must fill one flash slot");

  time::RealTimeClock* clock_{nullptr};
  switch_::Switch* flash_switch_{nullptr};
  const esp_partition_t* flash_partition_{nullptr};
  DecisionEvent* events_{nullptr};
  HourBucket* buckets_{nullptr};
  FlashBlockInfo* flash_index_{nullptr};
  size_t event_capacity_requested_{FLASH_EVENT_CAPACITY};
  size_t event_capacity_fallback_{128};
  size_t bucket_capacity_requested_{168};
  size_t bucket_capacity_fallback_{24};
  size_t event_capacity_{0};
  size_t bucket_capacity_{0};
  size_t event_head_{0};
  size_t event_count_{0};
  uint32_t next_seq_{1};
  uint32_t dropped_count_{0};
  bool events_external_{false};
  bool buckets_external_{false};
  bool flash_enabled_{false};
  bool flash_archive_scanned_{false};
  size_t flash_index_count_{0};
  uint32_t next_flash_sequence_{0};
  uint32_t last_persisted_event_seq_{0};
  uint32_t flash_oldest_epoch_s_{0};
  uint32_t flash_newest_epoch_s_{0};
  uint32_t flash_last_flush_epoch_s_{0};
  uint32_t flash_stored_event_count_{0};
  uint32_t tracked_hour_start_epoch_s_{0};
  UrgentFlushPolicy urgent_flush_{};
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

  bool time_is_valid_() const;
  uint32_t current_epoch_s_() const;
  uint64_t monotonic_uptime_s_() const;
  uint64_t boot_epoch_s_() const;
  void release_buffers_();
  void allocate_buffers_();
  bool push_event_locked_(const DecisionEvent& event);
  void update_bucket_locked_(const DecisionEvent& event);
  HourBucket* current_bucket_locked_(uint64_t uptime_s, uint32_t epoch_s, bool* created = nullptr);
  bool copy_event_(size_t index, DecisionEvent* out) const;
  bool copy_bucket_(size_t index, HourBucket* out) const;
  bool copy_flash_info_(size_t index, FlashBlockInfo* out) const;
  bool flash_switch_enabled_() const;
  bool flash_partition_available_() const;
  bool flash_archive_available_() const;
  static bool urgent_event_(const DecisionEvent& event);
  void request_urgent_flush_(uint32_t event_seq);
  void clear_urgent_flush_(uint64_t now_us);
  void complete_urgent_flush_(uint64_t now_us, uint32_t persisted_target_seq);
  void process_urgent_flush_();
  bool scan_flash_archive_();
  bool read_flash_block_(uint32_t slot_index, uint32_t expected_sequence, FlashBlockInfo* info,
                         FlashEventRecord* events) const;
  bool write_flash_events_(const DecisionEvent* events, size_t event_count);
  bool flush_pending_events_(size_t max_batches = SIZE_MAX, bool urgent_target_active = false,
                             uint32_t urgent_target_seq = 0U, bool* urgent_target_persisted = nullptr);
  void record_flash_block_(const FlashBlockInfo& info);
  void restore_flash_events_();
  void initialize_current_hour_();
  void reset_flash_metadata_();
  void rebuild_flash_metadata_();
  static FlashEventRecord pack_flash_event_(const DecisionEvent& event);
  static DecisionEvent unpack_flash_event_(const FlashEventRecord& event);
  static uint32_t fnv1a_hash_(const uint8_t* data, size_t len);

  static const char* event_type_to_string_(uint8_t value);
  static const char* subject_to_string_(uint8_t value);
  static const char* reason_to_string_(uint8_t value);
  static const char* severity_to_string_(uint8_t value);
  static const char* state_to_string_(uint8_t value);
  static uint16_t increment_u16_(uint16_t value);
};

}  // namespace openquatt_decision_log
}  // namespace esphome
