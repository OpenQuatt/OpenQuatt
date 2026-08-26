#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <esp_http_server.h>
#include "OpenQuattFlashLayout.h"
#include "OpenQuattTrendsStoragePolicy.h"
#include "esp_partition.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include "PsramBuffer.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"

namespace esphome {
namespace openquatt_trends {

using openquatt_common::OpenQuattFlashLayout;
using openquatt_common::PsramBuffer;

class ChunkedTextWriter;

class OpenQuattTrends : public Component {
 public:
  void set_capture_switch(switch_::Switch* capture_switch) { this->capture_switch_ = capture_switch; }
  void set_flash_switch(switch_::Switch* flash_switch) { this->flash_switch_ = flash_switch; }
  void set_clock(time::RealTimeClock* clock) { this->clock_ = clock; }

  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void capture_sample(float outside_c, float supply_c, float room_c, float room_setpoint_c, float flow_lph,
                      float input_w, float output_w, bool force = false);
  void set_flash_enabled(bool enabled);
  bool force_flush();
  void clear_history();
  void write_history(httpd_req_t* req, uint32_t window_hours);
  void write_metadata(httpd_req_t* req);
  std::string get_flash_available_label() const;
  std::string get_flash_oldest_point_label() const;
  std::string get_flash_newest_point_label() const;
  std::string get_flash_last_flush_label() const;
  float get_flash_storage_kib() const;
  uint32_t get_flash_write_count() const;

 protected:
  static constexpr uint32_t TAG_MAGIC = 0x4F545247;  // "OTRG"
  static constexpr uint16_t TAG_VERSION = 1;
  static constexpr uint32_t SAMPLE_INTERVAL_MS = 5UL * 60UL * 1000UL;
  static constexpr float INTERVAL_DEVIATION_RATIO = 0.05f;
  static constexpr uint32_t RAM_WINDOW_MS = 7UL * 24UL * 60UL * 60UL * 1000UL;
  static constexpr uint32_t ARCHIVE_WINDOW_MS = 30UL * 24UL * 60UL * 60UL * 1000UL;
  static constexpr size_t RAM_CAPACITY = 7UL * 24UL * 12UL;
  static constexpr size_t FLASH_SECTOR_SIZE = OpenQuattFlashLayout::SECTOR_SIZE;
  static constexpr size_t FLASH_SLOT_SIZE = 512;
  static constexpr size_t FLASH_SAMPLES_PER_BLOCK = 12;
  static constexpr size_t FLASH_SLOTS_PER_SECTOR = 8;
  static constexpr size_t FLASH_SECTOR_COUNT = OpenQuattFlashLayout::TRENDS_SECTOR_COUNT;
  static constexpr size_t FLASH_SLOT_COUNT = FLASH_SLOTS_PER_SECTOR * FLASH_SECTOR_COUNT;
  static constexpr size_t FLASH_TOTAL_BYTES = FLASH_SECTOR_SIZE * FLASH_SECTOR_COUNT;

  static_assert(FLASH_SLOT_COUNT == 720, "The flash ring must cover 30 days at 1-hour checkpoints");
  static_assert(FLASH_SLOT_SIZE * FLASH_SLOTS_PER_SECTOR == FLASH_SECTOR_SIZE, "Flash slots must fit in sectors");

  struct __attribute__((packed)) TrendValues {
    int16_t outside_c_x10;
    int16_t supply_c_x10;
    int16_t room_c_x10;
    int16_t room_setpoint_c_x10;
    uint16_t flow_lph;
    uint16_t input_w;
    uint16_t output_w;
  };

  struct __attribute__((packed)) TrendSample {
    uint64_t timestamp_ms;
    TrendValues values;
  };

  struct __attribute__((packed)) TrendBlockHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t sample_count;
    uint32_t sequence;
    uint64_t start_timestamp_ms;
    uint32_t payload_bytes;
    uint32_t crc32;
    uint32_t reserved;
  };

  struct FlashBlockInfo {
    uint32_t sequence{0};
    uint64_t start_timestamp_ms{0};
    uint64_t end_timestamp_ms{0};
    uint64_t flush_timestamp_ms{0};
    uint16_t sample_count{0};
    uint32_t slot_index{0};
  };

  struct IntervalMetricState {
    float sum{0.0f};
    uint32_t count{0};
    float min{0.0f};
    float max{0.0f};
    float last_saved{0.0f};
  };

  struct FlashBlockBuilder {
    bool active{false};
    uint64_t start_timestamp_ms{0};
    uint16_t sample_count{0};
    uint32_t sequence{0};
    std::array<TrendSample, FLASH_SAMPLES_PER_BLOCK> samples{};
  };

  struct FlashIOMetrics {
    uint32_t erase_count{0};
    uint32_t erase_failure_count{0};
    uint32_t last_erase_duration_ms{0};
    uint32_t max_erase_duration_ms{0};
    uint32_t write_failure_count{0};
    uint32_t last_write_duration_ms{0};
    uint32_t max_write_duration_ms{0};
    uint32_t last_flush_duration_ms{0};
    uint32_t max_flush_duration_ms{0};
    uint32_t last_index_update_duration_ms{0};
    uint32_t max_index_update_duration_ms{0};
  };

  static_assert(sizeof(TrendValues) == 14, "TrendValues must stay packed");
  static_assert(sizeof(TrendSample) == 22, "TrendSample must stay packed");
  static_assert(sizeof(TrendBlockHeader) == 32, "TrendBlockHeader must stay packed");
  static_assert(sizeof(TrendBlockHeader) + (FLASH_SAMPLES_PER_BLOCK * sizeof(TrendSample)) <= FLASH_SLOT_SIZE,
                "Trend block must fit in one flash slot");

  bool capture_enabled_() const;
  bool flash_switch_enabled_() const;
  TrendStorageCapabilities storage_capabilities_() const;
  bool flash_archive_available_() const;
  bool time_is_valid_() const;
  uint64_t current_time_ms_() const;
  uint64_t current_epoch_offset_ms_() const;

  static int16_t encode_temp_(float value);
  static uint16_t encode_unsigned_(float value);
  static float decode_temp_(int16_t value);
  static float decode_unsigned_(uint16_t value);
  static uint32_t fnv1a_hash_(const uint8_t* data, size_t len);
  static bool valid_unsigned_metric_(float value);
  static void reset_interval_metric_samples_(IntervalMetricState& state);
  static void update_interval_metric_(IntervalMetricState& state, float value);
  static float select_interval_metric_value_(const IntervalMetricState& state, float fallback);
  static void update_last_saved_metric_(IntervalMetricState& state, float value);

  TrendValues pack_values_(float outside_c, float supply_c, float room_c, float room_setpoint_c, float flow_lph,
                           float input_w, float output_w) const;
  TrendSample make_sample_(uint64_t timestamp_ms, const TrendValues& values) const;

  void sync_time_state_();
  void load_archive_if_needed_();
  void rebase_ram_history_(uint64_t offset_ms);

  void push_ram_sample_(const TrendSample& sample);
  bool get_ram_sample_at_(size_t ordered_index, TrendSample* sample) const;

  bool scan_flash_archive_();
  bool clear_flash_archive_();
  bool merge_flash_history_into_ram_();
  bool append_sample_to_flash_(const TrendSample& sample);
  bool flush_flash_builder_(bool force);
  bool write_flash_block_(const FlashBlockBuilder& builder);
  void record_flash_erase_(uint32_t duration_ms, bool success);
  void record_flash_write_(uint32_t duration_ms, bool success);
  void record_flash_flush_(uint32_t duration_ms);
  void record_flash_index_update_(uint32_t duration_ms);
  FlashIOMetrics snapshot_flash_io_metrics_();
  bool read_flash_block_(uint32_t slot_index, uint32_t expected_sequence, FlashBlockInfo* info,
                         std::array<TrendSample, FLASH_SAMPLES_PER_BLOCK>* samples) const;
  bool update_flash_index_after_write_(const FlashBlockInfo& info, bool erased_sector);
  void rebuild_flash_metadata_from_index_();
  void invalidate_flash_index_();
  void reset_flash_builder_();

  bool write_sample_line_(ChunkedTextWriter* writer, const TrendSample& sample) const;
  void write_samples_for_history_(ChunkedTextWriter* writer, uint32_t window_hours);
  uint64_t get_window_cutoff_ms_(uint32_t window_hours) const;
  uint32_t get_window_stride_(uint32_t window_hours) const;
  uint64_t get_ram_oldest_timestamp_ms_() const;
  uint64_t get_latest_archive_timestamp_ms_() const;
  void update_flash_metadata_(uint64_t latest_timestamp_ms);
  void reset_flash_metadata_();
  void reset_interval_samples_();
  void reset_interval_filters_();
  uint64_t get_flash_oldest_timestamp_ms_() const;
  uint64_t get_flash_newest_timestamp_ms_() const;
  uint64_t get_flash_last_flush_timestamp_ms_() const;
  std::string format_flash_absolute_time_(uint64_t timestamp_ms) const;
  std::string format_flash_relative_age_(uint64_t timestamp_ms) const;
  std::string format_flash_available_span_(uint64_t oldest_timestamp_ms, uint64_t newest_timestamp_ms) const;

  switch_::Switch* capture_switch_{nullptr};
  switch_::Switch* flash_switch_{nullptr};
  time::RealTimeClock* clock_{nullptr};
  const esp_partition_t* flash_partition_{nullptr};

  bool time_rebased_{false};
  bool flash_enabled_{false};
  bool flash_archive_scanned_{false};
  bool flash_archive_seeded_{false};
  bool flash_dirty_{false};

  uint32_t next_flash_sequence_{0};
  uint32_t last_capture_ms_{0};
  uint32_t last_flash_flush_ms_{0};
  uint64_t flash_latest_timestamp_ms_{0};
  uint64_t flash_oldest_timestamp_ms_{0};
  uint64_t flash_last_flush_timestamp_ms_{0};
  uint16_t flash_valid_block_count_{0};
  portMUX_TYPE flash_io_metrics_lock_ = portMUX_INITIALIZER_UNLOCKED;
  FlashIOMetrics flash_io_metrics_{};

  PsramBuffer<TrendSample> ram_history_{};
  size_t ram_head_{0};
  size_t ram_count_{0};

  PsramBuffer<FlashBlockInfo> flash_index_{};
  size_t flash_index_count_{0};

  FlashBlockBuilder flash_builder_{};
  IntervalMetricState flow_interval_{};
  IntervalMetricState input_w_interval_{};
  IntervalMetricState output_w_interval_{};
};

}  // namespace openquatt_trends
}  // namespace esphome
