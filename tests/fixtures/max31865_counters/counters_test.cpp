#include <algorithm>
#include <cassert>

#include "fake_esphome.h"
// Expose only the driver's test state to exercise UINT32_MAX without billions
// of SPI cycles; production access remains read-only.
#define protected public
#include "../../../components/max31865/max31865.h"
#undef protected

using esphome::max31865::MAX31865Sensor;

void initialize(MAX31865Sensor& sensor) {
  chip = {};
  sensor.set_reference_resistance(1500);
  sensor.set_nominal_resistance(1000);
  sensor.set_num_rtd_wires(2);
  sensor.set_filter(esphome::max31865::FILTER_50HZ);
  sensor.setup();
  chip.writes.clear();
  chip.reads.clear();
}

int main() {
  // All fault bytes: D3 and D4 are independent, other bits do not add counts.
  for (unsigned fault = 0; fault <= 255; fault++) {
    MAX31865Sensor sensor;
    initialize(sensor);
    chip.injected_faults = fault;
    // A stale between-measurement read is not counted again.
    chip.faults = 0x18;
    for (uint32_t cycle = 1; cycle <= 3; cycle++) {
      sensor.update();
      assert(sensor.delay_ms == 66);
      sensor.complete();
      assert(sensor.refin_low_fault_count() == ((fault & 0x10) ? cycle : 0U));
      assert(sensor.rtdin_low_fault_count() == ((fault & 0x08) ? cycle : 0U));
      assert(sensor.error == ((fault & 0x3C) != 0));
      assert(std::isnan(sensor.state) == sensor.error);
    }
    assert((chip.writes == std::vector<uint8_t>{0x87, 0xA1, 0x01, 0x87, 0xA1, 0x01, 0x87, 0xA1, 0x01}));
    // Unchanged traffic: one final fault read per conversion plus optional
    // between-measurement reads, skipped by upstream after an error.
    assert(std::count(chip.reads.begin(), chip.reads.end(), 7) == ((fault & 0x3C) ? 4 : 6));
    chip.injected_faults = 0;
    sensor.update();
    sensor.complete();
    assert(!sensor.error && std::isfinite(sensor.state));
    assert(sensor.refin_low_fault_count() == ((fault & 0x10) ? 3U : 0U));
    assert(sensor.rtdin_low_fault_count() == ((fault & 0x08) ? 3U : 0U));
  }
  for (uint16_t bad_read : {0U, 65535U}) {
    MAX31865Sensor sensor;
    initialize(sensor);
    chip.rtd = bad_read;
    chip.injected_faults = 0x18;
    sensor.update();
    sensor.complete();
    assert(sensor.error && std::isnan(sensor.state));
    assert(sensor.refin_low_fault_count() == 0 && sensor.rtdin_low_fault_count() == 0);
    assert(std::count(chip.reads.begin(), chip.reads.end(), 7) == 1);
  }
  MAX31865Sensor timeout;
  initialize(timeout);
  chip.stuck = true;
  timeout.update();
  assert(timeout.error && std::isnan(timeout.state) && !timeout.pending);
  assert(timeout.refin_low_fault_count() == 0 && timeout.rtdin_low_fault_count() == 0);
  MAX31865Sensor saturated;
  initialize(saturated);
  saturated.refin_low_fault_count_ = UINT32_MAX - 1U;
  saturated.rtdin_low_fault_count_ = UINT32_MAX - 1U;
  chip.injected_faults = 0x18;
  for (unsigned cycle = 0; cycle < 3; cycle++) {
    saturated.update();
    saturated.complete();
    assert(saturated.refin_low_fault_count() == UINT32_MAX);
    assert(saturated.rtdin_low_fault_count() == UINT32_MAX);
    assert(saturated.error && std::isnan(saturated.state));
  }
}
