// ESPHome 2026.9.0, modified 2026-10-20: RTD settling and optional diagnostics. See LICENSE.
#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/spi/spi.h"

namespace esphome::max31865 {

enum MAX31865RegisterMasks { SPI_WRITE_M = 0x80 };
enum MAX31865Registers {
  CONFIGURATION_REG = 0x00,
  RTD_RESISTANCE_MSB_REG = 0x01,
  RTD_RESISTANCE_LSB_REG = 0x02,
  FAULT_THRESHOLD_H_MSB_REG = 0x03,
  FAULT_THRESHOLD_H_LSB_REG = 0x04,
  FAULT_THRESHOLD_L_MSB_REG = 0x05,
  FAULT_THRESHOLD_L_LSB_REG = 0x06,
  FAULT_STATUS_REG = 0x07,
};
enum MAX31865ConfigFilter {
  FILTER_60HZ = 0,
  FILTER_50HZ = 1,
};

class MAX31865Sensor final : public sensor::Sensor,
                             public PollingComponent,
                             public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                                   spi::CLOCK_PHASE_TRAILING, spi::DATA_RATE_4MHZ> {
 public:
  void set_reference_resistance(float reference_resistance) { reference_resistance_ = reference_resistance; }
  void set_nominal_resistance(float nominal_resistance) { rtd_nominal_resistance_ = nominal_resistance; }
  void set_filter(MAX31865ConfigFilter filter) { filter_ = filter; }
  void set_num_rtd_wires(uint8_t rtd_wires) { rtd_wires_ = rtd_wires; }
  void setup() override;
  void dump_config() override;

  void update() override;
  void set_probe_sequence(bool separate_clear, uint32_t bias_ms, uint32_t manual_ms, uint32_t conversion_ms) {
    separate_clear_ = separate_clear;
    bias_ms_ = bias_ms;
    manual_ms_ = manual_ms;
    conversion_ms_ = conversion_ms;
  }
  void set_conversion_read_margin(uint32_t margin_ms) { read_margin_ms_ = margin_ms; }
  void set_diagnostics(bool diagnostics) { diagnostics_ = diagnostics; }

 protected:
  bool separate_clear_ = false;
  bool measurement_active_ = false;
  bool diagnostics_ = false;
  uint32_t bias_ms_ = 0;
  uint32_t manual_ms_ = 0;
  uint32_t conversion_ms_ = 0;
  uint32_t read_margin_ms_ = 0;
  uint32_t sample_count_ = 0;
  uint32_t fault_count_ = 0;
  uint32_t measurement_started_us_ = 0;
  uint32_t conversion_started_us_ = 0;
  uint32_t fault_detect_us_ = 0;
  struct DiagnosticSnapshot {
    uint16_t rtd[2];
    uint8_t faults[2];
    uint8_t config[2];
  };
  struct FaultSnapshot {
    uint8_t config = 0;
    uint8_t faults = 0;
    uint32_t wait_us = 0;
  };
  FaultSnapshot after_clear_{};
  FaultSnapshot manual_wait_{};
  FaultSnapshot after_detection_{};
  uint32_t phase_started_us_ = 0;
  uint8_t diagnostic_phases_ = 0;
  void start_fault_detection_();
  void finish_fault_detection_();
  void start_conversion_();
  DiagnosticSnapshot read_diagnostic_snapshot_(uint16_t rtd, uint8_t faults);
  FaultSnapshot read_fault_snapshot_();
  void diagnose_fault_detection_();
  void diagnose_(uint16_t rtd, uint8_t faults, const DiagnosticSnapshot& before, uint32_t read_started_us,
                 uint32_t bias_off_us);
  float reference_resistance_;
  float rtd_nominal_resistance_;
  MAX31865ConfigFilter filter_;
  uint8_t rtd_wires_;
  uint8_t base_config_;
  bool has_fault_ = false;
  bool has_warn_ = false;
  void read_data_();
  void write_config_(uint8_t mask, uint8_t bits, uint8_t start_position = 0);
  void write_register_(uint8_t reg, uint8_t value);
  uint8_t read_register_(uint8_t reg);
  uint16_t read_register_16_(uint8_t reg);
  float calc_temperature_(float rtd_ratio);
};

}  // namespace esphome::max31865
