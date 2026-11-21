#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "OpenQuattFlashLayout.h"
#include "PsramBuffer.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esp_partition.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

namespace esphome {
namespace openquatt_energy_history {

using openquatt_common::OpenQuattFlashLayout;
using openquatt_common::PsramObjectArray;

class ChunkedTextWriter;

class OpenQuattEnergyHistory : public Component {
 public:
  static constexpr size_t DAILY_COUNTER_COUNT = 7;

  void set_enabled_switch(switch_::Switch* enabled_switch) { this->enabled_switch_ = enabled_switch; }
  void set_clock(time::RealTimeClock* clock) { this->clock_ = clock; }
  void set_hourly_retention_select(select::Select* hourly_retention_select) {
    this->hourly_retention_select_ = hourly_retention_select;
  }

  void set_daily_counters(std::array<sensor::Sensor*, DAILY_COUNTER_COUNT> counters) {
    this->daily_counters_ = counters;
  }

  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void capture_day_totals();
  bool force_flush();
  void clear_history();
  void refresh_hourly_retention();
  void write_history(httpd_req_t* req);
  void write_history_export(httpd_req_t* req);
  std::string import_history_records(const std::string& records, uint32_t clear_generation);
  bool parse_import_csrf_token(const std::string& token, uint32_t* clear_generation) const;
  std::string get_available_label() const;
  std::string get_oldest_day_label() const;
  std::string get_newest_day_label() const;
  std::string get_last_write_label() const;
  float get_storage_kib() const;
  uint32_t get_write_count() const;
  bool runtime_storage_ready() const {
    return this->hour_state_mutex_ != nullptr && static_cast<bool>(this->external_state_storage_);
  }

 protected:
  static constexpr uint32_t RECORD_MAGIC = 0x3148454F;  // "OEH1"
  static constexpr uint16_t RECORD_VERSION = 1;
  static constexpr uint32_t HOUR_DAY_RECORD_MAGIC = 0x3248454F;  // "OEH2"
  static constexpr uint16_t HOUR_DAY_RECORD_VERSION = 1;
  static constexpr uint16_t FLAG_PARTIAL = 1U << 0U;
  static constexpr size_t FLASH_SECTOR_SIZE = OpenQuattFlashLayout::SECTOR_SIZE;
  static constexpr size_t FLASH_SLOT_SIZE = 64;
  static constexpr size_t HOUR_FLASH_SLOT_SIZE = OpenQuattFlashLayout::ENERGY_HOURLY_SLOT_SIZE;
  static constexpr uint32_t BASE_OFFSET = OpenQuattFlashLayout::ENERGY_DAILY_OFFSET;
  static constexpr size_t MAX_FLASH_SECTOR_COUNT = OpenQuattFlashLayout::ENERGY_DAILY_MAX_SECTOR_COUNT;
  static constexpr uint32_t HOUR_FLASH_BASE_OFFSET = OpenQuattFlashLayout::ENERGY_HOURLY_OFFSET;
  static constexpr uint16_t MIN_DATE_YEAR = 2020;
  static constexpr uint16_t MAX_DATE_YEAR = 2099;
  static constexpr size_t DATE_BITMAP_YEAR_COUNT = (MAX_DATE_YEAR - MIN_DATE_YEAR) + 1U;
  static constexpr size_t DATE_BITMAP_BITS = DATE_BITMAP_YEAR_COUNT * 12U * 31U;
  static constexpr size_t DATE_BITMAP_BYTES = (DATE_BITMAP_BITS + 7U) / 8U;
  static constexpr uint8_t HOURLY_RETENTION_DAYS = 7;
  static constexpr size_t HOURLY_SLOT_COUNT = static_cast<size_t>(HOURLY_RETENTION_DAYS) * 24U;
  static constexpr uint16_t DEFAULT_FLASH_HOURLY_RETENTION_DAYS = 180;
  static constexpr uint16_t MAX_FLASH_HOURLY_RETENTION_DAYS = OpenQuattFlashLayout::ENERGY_HOURLY_MAX_RETENTION_DAYS;
  static constexpr size_t EXPORT_HOUR_DATE_COUNT = MAX_FLASH_HOURLY_RETENTION_DAYS + HOURLY_RETENTION_DAYS;
  static constexpr uint32_t UNKNOWN_WH = 0xFFFFFFFFU;

  struct __attribute__((packed)) EnergyHistoryValues {
    uint32_t electrical_input_wh;
    uint32_t heating_input_wh;
    uint32_t cooling_input_wh;
    uint32_t heatpump_heat_output_wh;
    uint32_t heatpump_cooling_output_wh;
    uint32_t boiler_heat_output_wh;
    uint32_t system_heat_output_wh;
  };

  struct __attribute__((packed)) EnergyHistoryRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t sequence;
    uint32_t date_key;
    uint32_t write_timestamp_s;
    EnergyHistoryValues values;
    uint32_t crc32;
  };

  struct EnergyHistoryHourRecord {
    uint32_t sequence;
    uint32_t date_key;
    uint8_t hour;
    bool valid;
    bool includes_recovered_energy;
    EnergyHistoryValues values;
  };

  struct __attribute__((packed)) EnergyHistoryHourDayRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t sequence;
    uint32_t date_key;
    uint32_t write_timestamp_s;
    uint32_t hour_mask;
    EnergyHistoryValues hours[24];
    uint32_t crc32;
    // Optional metadata in the existing slot padding. Older firmware reads through crc32.
    uint32_t recovered_hour_mask;
    uint32_t recovery_crc32;
  };

  static_assert(sizeof(EnergyHistoryValues) == 28, "EnergyHistoryValues must stay packed");
  static_assert(sizeof(EnergyHistoryRecord) == 52, "EnergyHistoryRecord must stay packed");
  static_assert(sizeof(EnergyHistoryRecord) <= FLASH_SLOT_SIZE, "Energy history record must fit in one slot");
  static_assert(offsetof(EnergyHistoryHourDayRecord, crc32) == 696, "Legacy hourly record CRC must stay in place");
  static_assert(sizeof(EnergyHistoryHourDayRecord) <= HOUR_FLASH_SLOT_SIZE,
                "Energy history hour day record must fit in one slot");

  static constexpr size_t PENDING_SAMPLE_CAPACITY = 4;
  struct PendingSample {
    uint32_t date_key;
    uint8_t hour;
    EnergyHistoryValues values;
  };

  struct ExternalState {
    PendingSample pending_samples[PENDING_SAMPLE_CAPACITY];
    EnergyHistoryHourRecord hour_records[HOURLY_SLOT_COUNT];
    EnergyHistoryValues hour_snapshot_values[24];
    EnergyHistoryValues hour_import_values[24];
    EnergyHistoryHourDayRecord http_hour_record;
    uint8_t stored_day_bitmap[DATE_BITMAP_BYTES];
    uint8_t stored_hour_day_bitmap[DATE_BITMAP_BYTES];
    uint8_t export_date_bitmap[DATE_BITMAP_BYTES];
    uint32_t export_hour_date_keys[EXPORT_HOUR_DATE_COUNT];
    uint32_t export_hour_masks[EXPORT_HOUR_DATE_COUNT];
  };

  static constexpr size_t EXTERNAL_STATE_BYTES =
      sizeof(PendingSample) * PENDING_SAMPLE_CAPACITY + sizeof(EnergyHistoryHourDayRecord) +
      (sizeof(EnergyHistoryHourRecord) * HOURLY_SLOT_COUNT) + (sizeof(EnergyHistoryValues) * 24U * 2U) +
      (DATE_BITMAP_BYTES * 3U) + (sizeof(uint32_t) * EXPORT_HOUR_DATE_COUNT * 2U);
  static_assert(sizeof(ExternalState) == EXTERNAL_STATE_BYTES, "External energy history state must stay packed");

  ExternalState* state_() { return this->external_state_storage_.data(); }
  const ExternalState* state_() const { return this->external_state_storage_.data(); }
  void setup_daily_counters_();
  uint32_t observe_daily_date_(const ESPTime& now);
  bool enabled_() const;
  bool time_is_valid_() const;
  uint64_t current_time_ms_() const;
  bool is_partition_ready_() const;
  static uint32_t encode_kwh_(float value);
  static int32_t printable_wh_(uint32_t value);
  static uint32_t fnv1a_hash_(const uint8_t* data, size_t len);
  static uint32_t record_crc_(const EnergyHistoryRecord& record);
  static uint32_t hour_day_record_crc_(const EnergyHistoryHourDayRecord& record);
  static uint32_t hour_day_recovery_crc_(const EnergyHistoryHourDayRecord& record);
  static uint32_t recovered_hour_mask_(const EnergyHistoryHourDayRecord& record);
  static bool record_has_values_(const EnergyHistoryValues& values);
  static bool date_key_valid_(uint32_t date_key);
  static bool date_bitmap_index_(uint32_t date_key, size_t* byte_index, uint8_t* bit_mask);
  static bool date_bitmap_get_(const uint8_t* bitmap, uint32_t date_key);
  static void date_bitmap_set_(uint8_t* bitmap, uint32_t date_key);
  static bool parse_import_uint32_(const std::string& value, uint32_t* out);
  static std::vector<std::string> split_import_fields_(const std::string& line);
  static uint32_t add_wh_(uint32_t base, uint32_t delta);
  static uint32_t delta_wh_(uint32_t current, uint32_t previous);
  static std::string format_date_key_(uint32_t date_key);
  static std::string format_date_key_iso_(uint32_t date_key);
  static bool write_export_wh_field_(ChunkedTextWriter* writer, const char* field, uint32_t value);
  static bool date_key_in_range_(uint32_t date_key, uint32_t from_date_key, uint32_t to_date_key);
  static uint16_t parse_hourly_retention_days_(const std::string& option);
  bool write_export_record_(ChunkedTextWriter* writer, uint32_t date_key, int16_t hour,
                            const EnergyHistoryValues& values, bool* first) const;
  void clear_export_hour_marks_();
  bool export_hour_marked_(uint32_t date_key, uint8_t hour) const;
  bool mark_export_hour_(uint32_t date_key, uint8_t hour);

  void rotate_csrf_token_();
  EnergyHistoryValues pack_values_(float electrical_input_kwh, float heating_input_kwh, float cooling_input_kwh,
                                   float heatpump_heat_output_kwh, float heatpump_cooling_output_kwh,
                                   float boiler_heat_output_kwh, float system_heat_output_kwh) const;
  bool read_record_(uint32_t slot_index, EnergyHistoryRecord* record) const;
  bool record_valid_(const EnergyHistoryRecord& record) const;
  bool read_hour_day_record_(uint32_t slot_index, EnergyHistoryHourDayRecord* record) const;
  // The shared HTTP server runs its handlers serially. Only those handlers use the PSRAM snapshot.
  bool read_http_hour_day_record_(uint32_t slot_index);
  EnergyHistoryHourRecord copy_live_hour_record_(size_t slot_index);
  bool hour_day_record_valid_(const EnergyHistoryHourDayRecord& record) const;
  bool scan_archive_();
  bool scan_hour_archive_();
  bool write_record_(uint32_t date_key, const EnergyHistoryValues& values, bool partial, bool rescan = true,
                     bool require_enabled = true);
  bool write_hour_day_record_(uint32_t date_key, bool partial);
  bool write_hour_day_import_record_locked_(uint32_t date_key, uint32_t hour_mask, const EnergyHistoryValues hours[24],
                                            bool rescan = true);
  EnergyHistoryValues delta_values_(const EnergyHistoryValues& current, const EnergyHistoryValues& previous) const;
  void restore_current_day_hours_(uint32_t date_key, uint8_t current_hour, const EnergyHistoryValues& current_values,
                                  bool mark_recovered);
  void capture_hour_delta_(uint32_t date_key, uint8_t hour, const EnergyHistoryValues& values,
                           bool includes_recovered_energy = false);
  uint16_t requested_flash_hourly_retention_days_() const;
  size_t max_hour_flash_total_bytes_() const;
  void configure_hour_flash_window_();
  bool is_hour_partition_ready_() const;
  bool snapshot_hour_day_values_(uint32_t date_key, EnergyHistoryValues hours[24], uint32_t* hour_mask,
                                 uint32_t* recovered_hour_mask) const;
  // Main-loop requests and pending samples have one owner. The helpers ending in
  // _locked_ run with the non-recursive hour state mutex already held.
  bool service_pending_history_();
  bool process_pending_history_locked_(bool require_complete_flush = false);
  bool flush_current_day_locked_(bool require_complete_flush);
  void clear_history_locked_();
  void apply_sample_locked_(const PendingSample& sample);
  uint32_t get_hour_record_count_() const;
  void clear_hour_records_();

  switch_::Switch* enabled_switch_{nullptr};
  select::Select* hourly_retention_select_{nullptr};
  time::RealTimeClock* clock_{nullptr};
  std::array<sensor::Sensor*, DAILY_COUNTER_COUNT> daily_counters_{};
  std::bitset<DAILY_COUNTER_COUNT> daily_counters_ready_{};
  uint32_t daily_counter_date_{0};
  const esp_partition_t* flash_partition_{nullptr};
  std::string csrf_token_;

  bool partition_available_{false};
  size_t flash_sector_count_{0};
  size_t flash_slot_count_{0};
  size_t flash_total_bytes_{0};
  bool hour_partition_available_{false};
  size_t hour_flash_sector_count_{0};
  size_t hour_flash_slot_count_{0};
  size_t hour_flash_total_bytes_{0};
  uint16_t hour_flash_requested_retention_days_{DEFAULT_FLASH_HOURLY_RETENTION_DAYS};
  bool has_current_day_{false};
  uint32_t active_date_key_{0};
  EnergyHistoryValues current_values_{UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH,
                                      UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH};

  bool has_last_hour_sample_{false};
  bool mark_boot_recovery_{true};
  bool stale_shutdown_marker_{false};
  uint32_t last_hour_sample_date_key_{0};
  EnergyHistoryValues last_hour_sample_values_{UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH,
                                               UNKNOWN_WH, UNKNOWN_WH, UNKNOWN_WH};
  PsramObjectArray<ExternalState, 1U> external_state_storage_{};
  StaticSemaphore_t hour_state_mutex_storage_{};
  SemaphoreHandle_t hour_state_mutex_{nullptr};
  size_t pending_sample_count_{0};
  bool clear_requested_{false};
  bool retention_refresh_requested_{false};
  bool flush_requested_{false};
  uint32_t next_hour_sequence_{0};
  uint32_t next_hour_flash_sequence_{0};
  uint32_t hour_flash_record_count_{0};
  uint32_t hour_flash_stored_day_count_{0};
  uint32_t hour_flash_oldest_date_key_{0};
  uint32_t hour_flash_newest_date_key_{0};
  uint32_t hour_flash_last_write_timestamp_s_{0};
  // These buffers are direct esp_partition read/write operands and must remain
  // in internal RAM while the flash cache can be disabled.
  EnergyHistoryHourDayRecord hour_flash_record_buffer_{};
  uint8_t hour_flash_slot_buffer_[HOUR_FLASH_SLOT_SIZE]{};

  // Protected by hour_state_mutex_. Cancels an in-flight import after a clear.
  uint32_t archive_clear_generation_{0};
  uint32_t next_sequence_{0};
  uint32_t record_count_{0};
  uint32_t stored_day_count_{0};
  uint32_t oldest_date_key_{0};
  uint32_t newest_date_key_{0};
  uint32_t last_write_timestamp_s_{0};
};

static_assert(sizeof(OpenQuattEnergyHistory) <= 2304U,
              "Large energy history state must remain outside the static internal-RAM component");

}  // namespace openquatt_energy_history
}  // namespace esphome
