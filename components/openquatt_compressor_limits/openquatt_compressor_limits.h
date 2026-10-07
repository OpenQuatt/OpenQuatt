#pragma once

#include "esphome/core/component.h"
#include "esphome/components/number/number.h"
#include "limits_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esphome {
namespace openquatt_compressor_limits {

class CompressorLimits;

class WarningLimitNumber : public number::Number {
 public:
  void set_parent(CompressorLimits* parent, uint8_t index) {
    parent_ = parent;
    index_ = index;
  }

 protected:
  void control(float value) override;
  CompressorLimits* parent_{nullptr};
  uint8_t index_{0U};
};

class NvsLimitsStorage : public LimitsStorage {
 public:
  void set_legacy_key(uint8_t index, uint32_t key) { legacy_keys_[index] = key; }
  ReadResult read_bundle(LimitsRecord& record) override;
  ReadResult read_legacy(uint8_t index, float& value) override;
  bool write_bundle(const LimitsRecord& record) override;
  bool erase_legacy() override;

 protected:
  ReadResult read_blob_(const char* key, void* data, size_t expected_size);
  uint32_t legacy_keys_[2]{};
};

class CompressorLimits : public Component {
 public:
  void set_limit(uint8_t index, WarningLimitNumber* number) {
    limits_[index] = number;
    number->set_parent(this, index);
  }
  void set_write_interval(uint32_t interval) { write_interval_ms_ = interval; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  void request(uint8_t index, float value);

 protected:
  void publish_();
  void persist_();
  void drain_requests_(bool close = false);
  WarningLimitNumber* limits_[2]{};
  NvsLimitsStorage storage_{};
  LimitsState state_{};
  uint32_t write_interval_ms_{60000U};
  uint32_t last_attempt_ms_{0U};
  // Only these fixed mailbox slots cross task boundaries. NVS/live state
  // remain main-loop-owned; the critical section only copies scalar values.
  portMUX_TYPE request_lock_ = portMUX_INITIALIZER_UNLOCKED;
  float requested_[2]{};
  uint8_t request_mask_{0U};
  bool accepting_requests_{true};
  TaskHandle_t owner_task_{};
};

}  // namespace openquatt_compressor_limits
}  // namespace esphome
