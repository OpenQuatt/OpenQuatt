#include <cassert>
#include <algorithm>

#include "../../../components/max31865/max31865.h"

using esphome::max31865::MAX31865Sensor;

void initialize(MAX31865Sensor& sensor) {
  chip = {};
  diagnostic_logs.clear();
  diagnostic_log_bias_states.clear();
  diagnostic_bias_on = false;
  clock_us = 1000;
  sensor.set_reference_resistance(1500);
  sensor.set_nominal_resistance(1000);
  sensor.set_num_rtd_wires(2);
  sensor.set_filter(esphome::max31865::FILTER_50HZ);
  sensor.setup();
  chip.writes.clear();
}

void complete(MAX31865Sensor& sensor) {
  unsigned steps = 0;
  while (sensor.pending) {
    assert(++steps <= 4);
    const auto before = chip.writes.size();
    sensor.update();
    assert(chip.writes.size() == before);  // All wait phases reject overlaps.
    sensor.next();
  }
}

int main() {
  MAX31865Sensor baseline;
  initialize(baseline);
  baseline.update();
  assert(chip.writes.size() == 2);
  assert(chip.writes[0].second == 0x87);
  assert(chip.writes[1].second == 0xA1);
  assert(baseline.due - clock_us == 66000);
  const auto writes = chip.writes.size();
  baseline.update();  // A retry while the conversion is pending cannot overlap it.
  assert(chip.writes.size() == writes);
  baseline.next();
  assert(std::isfinite(baseline.state) && !baseline.error);
  assert(chip.config == 1);                                          // BIAS and test bits always off after the read.
  assert(std::count(chip.reads.begin(), chip.reads.end(), 7) == 2);  // No diagnostic SPI traffic by default.

  for (const uint8_t fault : {4, 8, 16, 32, 24}) {
    MAX31865Sensor faulty;
    initialize(faulty);
    chip.injected_faults = fault;
    faulty.update();
    faulty.next();
    assert(std::isnan(faulty.state) && faulty.error);
    chip.injected_faults = 0;
    faulty.update();
    faulty.next();
    assert(std::isfinite(faulty.state) && !faulty.error);
  }

  for (const uint8_t warning : {64, 128}) {
    MAX31865Sensor threshold;
    initialize(threshold);
    chip.injected_faults = warning;
    threshold.update();
    complete(threshold);
    assert(std::isfinite(threshold.state) && !threshold.error && threshold.warning);
  }
  MAX31865Sensor data_fault_only;
  initialize(data_fault_only);
  chip.rtd_fault_only = true;
  data_fault_only.update();
  complete(data_fault_only);
  assert(std::isfinite(data_fault_only.state) && !data_fault_only.error && data_fault_only.warning);

  // A diagnostic second read cannot override the original read's decision.
  for (const uint8_t first_fault : {0, 8}) {
    MAX31865Sensor inconsistent;
    initialize(inconsistent);
    chip.injected_faults = first_fault;
    chip.contradict_repeat = true;
    inconsistent.set_diagnostics(true);
    inconsistent.update();
    complete(inconsistent);
    assert(inconsistent.error == (first_fault != 0));
    assert(std::isnan(inconsistent.state) == (first_fault != 0));
    assert(std::count(chip.reads.begin(), chip.reads.end(), 7) == 6);
    assert(diagnostic_logs.size() == 3);
    assert(diagnostic_logs[0].find(" pre rtd=BB80/BB80 fault=00/00 cfg=81/81") != std::string::npos || first_fault);
    // Deliberately bad post-off RTD reads cannot replace the original valid RTD.
    assert(diagnostic_logs[1].find(" post rtd=0000/0000") != std::string::npos);
  }

  MAX31865Sensor diagnostic_sequence;
  initialize(diagnostic_sequence);
  diagnostic_sequence.set_probe_sequence(true, 10, 10, 10);
  diagnostic_sequence.set_conversion_read_margin(1);
  diagnostic_sequence.set_diagnostics(true);
  diagnostic_sequence.update();
  diagnostic_sequence.next();
  diagnostic_sequence.next();
  diagnostic_sequence.next();
  const auto reads_before_value = chip.reads.size();
  diagnostic_sequence.next();
  const std::vector<uint8_t> snapshot_reads(chip.reads.begin() + reads_before_value, chip.reads.end());
  assert((snapshot_reads == std::vector<uint8_t>{1, 2, 7, 0, 1, 2, 7, 0, 7, 1, 2, 0, 1, 2, 7, 0}));
  for (size_t i = 0; i < snapshot_reads.size(); i++) {
    assert(chip.read_bias_states[reads_before_value + i] == (i < 8));
  }
  assert(diagnostic_logs.size() == 3);
  assert(diagnostic_logs[0].find("pre rtd=BB80/BB80 fault=00/00 cfg=81/81 conv_us=67000 pre_us=120") !=
         std::string::npos);
  assert(diagnostic_logs[1].find("post rtd=BB80/BB80 fault=00/00 cfg=01/01") != std::string::npos);
  assert(diagnostic_logs[1].find("post_us=140") != std::string::npos);
  assert(diagnostic_logs[2].find(
             "stages=7 clear_cfg=81 clear_fault=00 manual_cfg=89 manual_fault=00 detect_cfg=81 detect_fault=00") !=
         std::string::npos);
  assert(diagnostic_logs[2].find("bias_wait_us=10040 manual_wait_us=10040 settle_us=10040") != std::string::npos);
  assert(
      std::none_of(diagnostic_log_bias_states.begin(), diagnostic_log_bias_states.end(), [](bool on) { return on; }));
  assert(std::count(chip.reads.begin(), chip.reads.end(), 7) == 8);
  assert(std::isfinite(diagnostic_sequence.state) && !diagnostic_sequence.error && chip.config == 1);

  // The manual wait snapshot exposes a lost first command without repairing or
  // suppressing the resulting fault. The same write sequence is still attempted.
  MAX31865Sensor lost_manual;
  initialize(lost_manual);
  lost_manual.set_probe_sequence(true, 10, 10, 10);
  lost_manual.set_diagnostics(true);
  chip.drop_manual_start = true;
  chip.injected_faults = 8;
  lost_manual.update();
  complete(lost_manual);
  assert(lost_manual.error && std::isnan(lost_manual.state) && chip.config == 1);
  assert(chip.writes.size() == 5);
  assert(diagnostic_logs[2].find("manual_cfg=81 manual_fault=00 detect_cfg=81 detect_fault=08") != std::string::npos);

  MAX31865Sensor uncleared_fault;
  initialize(uncleared_fault);
  uncleared_fault.set_probe_sequence(true, 10, 10, 10);
  uncleared_fault.set_diagnostics(true);
  chip.ignore_clear = true;
  chip.faults = 8;
  uncleared_fault.update();
  complete(uncleared_fault);
  assert(uncleared_fault.error && std::isnan(uncleared_fault.state));
  assert(diagnostic_logs[2].find("clear_cfg=81 clear_fault=08 manual_cfg=89 manual_fault=08") != std::string::npos);
  chip.ignore_clear = false;
  uncleared_fault.update();
  complete(uncleared_fault);
  assert(!uncleared_fault.error && std::isfinite(uncleared_fault.state));
  assert(diagnostic_logs[5].find("clear_cfg=81 clear_fault=00 manual_cfg=89 manual_fault=00") != std::string::npos);

  MAX31865Sensor diagnostic_timeout;
  initialize(diagnostic_timeout);
  diagnostic_timeout.set_probe_sequence(true, 10, 10, 10);
  diagnostic_timeout.set_diagnostics(true);
  chip.stuck = true;
  diagnostic_timeout.update();
  complete(diagnostic_timeout);
  assert(diagnostic_timeout.error && std::isnan(diagnostic_timeout.state) && chip.config == 1);
  assert(diagnostic_logs.size() == 2 && diagnostic_logs[0].find("detector_timeout") != std::string::npos);
  assert(diagnostic_logs[1].find("stages=3") != std::string::npos);
  assert(
      std::none_of(diagnostic_log_bias_states.begin(), diagnostic_log_bias_states.end(), [](bool on) { return on; }));
  chip.stuck = false;
  diagnostic_timeout.update();
  complete(diagnostic_timeout);
  assert(!diagnostic_timeout.error && std::isfinite(diagnostic_timeout.state) && chip.config == 1);
  assert(diagnostic_logs.size() == 5 && diagnostic_logs[4].find("stages=7") != std::string::npos);
  chip.stuck = true;
  diagnostic_timeout.update();
  complete(diagnostic_timeout);
  assert(diagnostic_timeout.error && std::isnan(diagnostic_timeout.state) && chip.config == 1);
  assert(diagnostic_logs.size() == 7 && diagnostic_logs[6].find("stages=3") != std::string::npos);
  assert(diagnostic_logs[6].find("detect_cfg=00 detect_fault=00") != std::string::npos);
  assert(diagnostic_logs[6].find("settle_us=0") != std::string::npos);

  // Faults newly latched during power-off still follow the existing error path.
  MAX31865Sensor power_off_fault;
  initialize(power_off_fault);
  power_off_fault.set_diagnostics(true);
  chip.faults_on_power_off = 4;
  power_off_fault.update();
  complete(power_off_fault);
  assert(std::isnan(power_off_fault.state) && power_off_fault.error && chip.config == 1);
  assert(diagnostic_logs[0].find("fault=00/00") != std::string::npos);
  assert(diagnostic_logs[1].find("fault=04/04") != std::string::npos);

  // Consecutive real faults and recovery stay visible on the separate log tag.
  for (const uint8_t fault : {4, 8, 16, 32}) {
    MAX31865Sensor diagnostic_fault;
    initialize(diagnostic_fault);
    diagnostic_fault.set_probe_sequence(true, 10, 10, 10);
    diagnostic_fault.set_conversion_read_margin(1);
    diagnostic_fault.set_diagnostics(true);
    chip.strict_clear = true;
    chip.injected_faults = fault;
    for (unsigned cycle = 0; cycle < 3; cycle++) {
      diagnostic_fault.update();
      complete(diagnostic_fault);
      assert(diagnostic_fault.error && std::isnan(diagnostic_fault.state) && chip.config == 1);
    }
    chip.injected_faults = 0;
    diagnostic_fault.update();
    complete(diagnostic_fault);
    assert(!diagnostic_fault.error && std::isfinite(diagnostic_fault.state) && chip.config == 1);
    assert(diagnostic_logs.size() == 12);
    assert(diagnostic_logs[10].find("faults=3") != std::string::npos);
  }

  // Strict D1 semantics expose the baseline's combined clear/test assumption.
  MAX31865Sensor strict_baseline;
  initialize(strict_baseline);
  chip.strict_clear = true;
  chip.injected_faults = 8;
  strict_baseline.update();
  complete(strict_baseline);
  chip.injected_faults = 0;
  strict_baseline.update();
  complete(strict_baseline);
  assert(strict_baseline.error);

  for (const uint32_t manual_ms : {0, 10}) {
    MAX31865Sensor persistent;
    initialize(persistent);
    persistent.set_probe_sequence(true, manual_ms, manual_ms, manual_ms);
    chip.strict_clear = true;
    chip.injected_faults = 24;
    for (unsigned cycle = 0; cycle < 3; cycle++) {
      persistent.update();
      complete(persistent);
      assert(persistent.error && std::isnan(persistent.state) && chip.config == 1);
    }
    chip.injected_faults = 0;
    persistent.update();
    complete(persistent);
    assert(!persistent.error && std::isfinite(persistent.state) && chip.config == 1);
  }

  MAX31865Sensor clear_only;
  initialize(clear_only);
  clear_only.set_probe_sequence(true, 0, 0, 0);
  clear_only.update();
  assert(chip.writes[0].second == 0x83);
  assert(chip.writes[1].second == 0x85);
  clear_only.next();

  MAX31865Sensor manual;
  initialize(manual);
  manual.set_probe_sequence(true, 10, 10, 10);
  manual.update();
  assert(chip.writes.size() == 1 && chip.writes[0].second == 0x83);
  manual.next();
  assert(chip.writes[1].second == 0x89);
  assert(chip.writes[1].first - chip.writes[0].first >= 10000);
  manual.next();
  assert(chip.writes[2].second == 0x8D);
  assert(chip.writes[2].first - chip.writes[1].first >= 10000);
  manual.next();
  assert(chip.writes[3].second == 0xA1);
  assert(chip.writes[3].first - chip.writes[2].first >= 10000);
  manual.next();
  assert(std::isfinite(manual.state) && !manual.error && chip.config == 1);

  MAX31865Sensor q_profile;
  initialize(q_profile);
  q_profile.set_probe_sequence(true, 10, 10, 10);
  q_profile.set_conversion_read_margin(1);
  q_profile.update();
  q_profile.next();
  q_profile.next();
  q_profile.next();
  assert(q_profile.due - clock_us == 67000);
  complete(q_profile);
  assert(std::isfinite(q_profile.state) && !q_profile.error && chip.config == 1);

  MAX31865Sensor sixty_hz;
  initialize(sixty_hz);
  sixty_hz.set_filter(esphome::max31865::FILTER_60HZ);
  sixty_hz.setup();
  sixty_hz.set_conversion_read_margin(1);
  sixty_hz.update();
  assert(sixty_hz.due - clock_us == 56000);
  complete(sixty_hz);
  assert(std::isfinite(sixty_hz.state) && !sixty_hz.error && chip.config == 0);

  MAX31865Sensor disconnected;
  initialize(disconnected);
  chip.rtd = 0;
  disconnected.update();
  disconnected.next();
  assert(std::isnan(disconnected.state) && disconnected.error && chip.config == 1);
  assert(std::count(chip.reads.begin(), chip.reads.end(), 7) == 1);

  MAX31865Sensor all_ones;
  initialize(all_ones);
  chip.rtd = 0xFFFF;
  chip.rtd_fault_only = true;
  all_ones.update();
  complete(all_ones);
  assert(std::isnan(all_ones.state) && all_ones.error && chip.config == 1);
  assert(std::count(chip.reads.begin(), chip.reads.end(), 7) == 1);

  MAX31865Sensor invalid_diagnostic;
  initialize(invalid_diagnostic);
  invalid_diagnostic.set_diagnostics(true);
  chip.rtd = 0;
  invalid_diagnostic.update();
  complete(invalid_diagnostic);
  assert(std::isnan(invalid_diagnostic.state) && invalid_diagnostic.error);
  assert(std::count(chip.reads.begin(), chip.reads.end(), 7) == 6);

  // A reboot can find the external chip in a manual test left by the old MCU.
  MAX31865Sensor restarted;
  initialize(restarted);
  chip.config = 0x89;
  chip.faults = 24;
  chip.strict_clear = true;
  restarted.setup();
  assert(chip.config == 3 && chip.faults == 0);
  restarted.set_probe_sequence(true, 10, 10, 10);
  restarted.set_conversion_read_margin(1);
  restarted.update();
  complete(restarted);
  assert(std::isfinite(restarted.state) && !restarted.error && chip.config == 1);

  MAX31865Sensor stuck;
  initialize(stuck);
  chip.stuck = true;
  stuck.update();
  assert(std::isnan(stuck.state) && stuck.error && chip.config == 1 && !stuck.pending);
  chip.stuck = false;
  stuck.update();
  stuck.next();
  assert(std::isfinite(stuck.state) && !stuck.error);
}
