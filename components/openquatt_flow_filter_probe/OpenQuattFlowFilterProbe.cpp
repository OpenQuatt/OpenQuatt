#include "OpenQuattFlowFilterProbe.h"

#include <cmath>

#include "esphome/core/log.h"

namespace esphome::openquatt_flow_filter_probe {

static const char* const TAG = "oq.flow_probe";
static constexpr uint32_t EDGE_FILTER_US = 100;
static constexpr uint32_t PULSE_13_FILTER_US = 13;
static constexpr uint32_t PULSE_20_FILTER_US = 20;
static constexpr uint32_t PULSE_50_FILTER_US = 50;
static constexpr uint32_t PULSE_100_FILTER_US = 100;
static constexpr uint32_t TIMEOUT_US = 5000000UL;
static constexpr uint32_t DIAGNOSTICS_WINDOW_US = 10000000UL;

void OpenQuattFlowFilterProbe::setup() {
  this->pin_->setup();
  this->isr_pin_ = this->pin_->to_isr();
  this->last_pin_val_ = this->pin_->digital_read();
  this->pulse_13_filter_.latched = this->last_pin_val_;
  this->pulse_20_filter_.latched = this->last_pin_val_;
  this->pulse_50_filter_.latched = this->last_pin_val_;
  this->pulse_100_filter_.latched = this->last_pin_val_;

  const uint32_t now = micros();
  this->edge_100_runtime_.last_processed_edge_us = now;
  this->pulse_13_runtime_.last_processed_edge_us = now;
  this->pulse_20_runtime_.last_processed_edge_us = now;
  this->pulse_50_runtime_.last_processed_edge_us = now;
  this->pulse_100_runtime_.last_processed_edge_us = now;
  this->diagnostics_window_started_us_ = now;

  this->pin_->attach_interrupt(OpenQuattFlowFilterProbe::gpio_intr, this, gpio::INTERRUPT_ANY_EDGE);
}

void IRAM_ATTR OpenQuattFlowFilterProbe::apply_pulse_filter_(volatile ChannelState& channel,
                                                             volatile PulseFilterState& filter, uint32_t filter_us,
                                                             uint32_t now_us, bool pin_val, bool previous_pin_val) {
  const bool length = now_us - filter.last_intr_us >= filter_us;

  if (length && filter.latched && !previous_pin_val) {
    filter.latched = false;
  } else if (length && !filter.latched && previous_pin_val) {
    filter.latched = true;
    channel.last_detected_edge_us = filter.last_intr_us;
    channel.count += 1;
  }

  channel.last_rising_edge_us = !filter.latched && pin_val ? now_us : channel.last_detected_edge_us;
  filter.last_intr_us = now_us;
}

void IRAM_ATTR OpenQuattFlowFilterProbe::gpio_intr(OpenQuattFlowFilterProbe* probe) {
  const uint32_t now = micros();
  const bool pin_val = probe->isr_pin_.digital_read();
  const bool previous_pin_val = probe->last_pin_val_;

  if (pin_val && !previous_pin_val) {
    probe->raw_rising_count_window_ += 1;
    probe->raw_high_started_us_ = now;
    if ((now - probe->edge_100_last_sent_us_) >= EDGE_FILTER_US) {
      probe->edge_100_last_sent_us_ = now;
      probe->edge_100_state_.last_detected_edge_us = now;
      probe->edge_100_state_.last_rising_edge_us = now;
      probe->edge_100_state_.count += 1;
    }
  } else if (!pin_val && previous_pin_val && probe->raw_high_started_us_ != 0) {
    const uint32_t width_us = now - probe->raw_high_started_us_;
    probe->raw_high_started_us_ = 0;
    probe->width_stats_.count += 1;
    probe->width_stats_.sum_us += width_us;
    if (width_us < probe->width_stats_.min_us) probe->width_stats_.min_us = width_us;
    if (width_us > probe->width_stats_.max_us) probe->width_stats_.max_us = width_us;
    if (width_us < 20) {
      probe->width_stats_.lt20 += 1;
    } else if (width_us < 50) {
      probe->width_stats_.from20_to50 += 1;
    } else if (width_us < 100) {
      probe->width_stats_.from50_to100 += 1;
    } else {
      probe->width_stats_.ge100 += 1;
    }
  }

  apply_pulse_filter_(probe->pulse_13_state_, probe->pulse_13_filter_, PULSE_13_FILTER_US, now, pin_val,
                      previous_pin_val);
  apply_pulse_filter_(probe->pulse_20_state_, probe->pulse_20_filter_, PULSE_20_FILTER_US, now, pin_val,
                      previous_pin_val);
  apply_pulse_filter_(probe->pulse_50_state_, probe->pulse_50_filter_, PULSE_50_FILTER_US, now, pin_val,
                      previous_pin_val);
  apply_pulse_filter_(probe->pulse_100_state_, probe->pulse_100_filter_, PULSE_100_FILTER_US, now, pin_val,
                      previous_pin_val);

  probe->last_pin_val_ = pin_val;
}

float OpenQuattFlowFilterProbe::pulses_per_minute_to_lph_(float ppm) const {
  const float hz = ppm / 60.0f;
  const float lpm = this->kf_lpm_per_hz_ * hz + this->q0_lpm_;
  if (std::isnan(lpm)) return NAN;
  const float lph = lpm * 60.0f;
  return lph > 0.0f ? lph : 0.0f;
}

void OpenQuattFlowFilterProbe::process_channel_(sensor::Sensor* sensor, ChannelState state, RuntimeState& runtime,
                                                uint32_t filter_us, bool pulse_mode, uint32_t now_us) {
  if (runtime.peeked_edge && state.count > 0) {
    runtime.peeked_edge = false;
    state.count--;
  }

  if (pulse_mode && !runtime.peeked_edge && state.last_rising_edge_us != state.last_detected_edge_us &&
      now_us - state.last_rising_edge_us >= filter_us) {
    runtime.peeked_edge = true;
    state.last_detected_edge_us = state.last_rising_edge_us;
    state.count++;
  }

  if (state.count > 0) {
    if (runtime.meter_state != 1) {
      runtime.meter_state = 1;
    } else {
      const uint32_t delta_us = state.last_detected_edge_us - runtime.last_processed_edge_us;
      const float pulse_width_us = delta_us / static_cast<float>(state.count);
      if (pulse_width_us > 0.0f) {
        const float ppm = (60.0f * 1000000.0f) / pulse_width_us;
        sensor->publish_state(this->pulses_per_minute_to_lph_(ppm));
      }
    }
    runtime.last_processed_edge_us = state.last_detected_edge_us;
    return;
  }

  if (now_us - runtime.last_processed_edge_us > TIMEOUT_US && runtime.meter_state != 2) {
    runtime.meter_state = 2;
    sensor->publish_state(0.0f);
  }
}

void OpenQuattFlowFilterProbe::publish_raw_diagnostics_(uint32_t now_us) {
  if (now_us - this->diagnostics_window_started_us_ < DIAGNOSTICS_WINDOW_US) return;

  uint32_t raw_count = 0;
  WidthStats stats{};
  {
    InterruptLock lock;
    raw_count = this->raw_rising_count_window_;
    this->raw_rising_count_window_ = 0;

    stats.count = this->width_stats_.count;
    stats.sum_us = this->width_stats_.sum_us;
    stats.min_us = this->width_stats_.min_us;
    stats.max_us = this->width_stats_.max_us;
    stats.lt20 = this->width_stats_.lt20;
    stats.from20_to50 = this->width_stats_.from20_to50;
    stats.from50_to100 = this->width_stats_.from50_to100;
    stats.ge100 = this->width_stats_.ge100;

    this->width_stats_.count = 0;
    this->width_stats_.sum_us = 0;
    this->width_stats_.min_us = UINT32_MAX;
    this->width_stats_.max_us = 0;
    this->width_stats_.lt20 = 0;
    this->width_stats_.from20_to50 = 0;
    this->width_stats_.from50_to100 = 0;
    this->width_stats_.ge100 = 0;
  }

  const uint32_t elapsed_us = now_us - this->diagnostics_window_started_us_;
  this->diagnostics_window_started_us_ = now_us;
  const float elapsed_s = elapsed_us / 1000000.0f;
  const float raw_hz = elapsed_s > 0.0f ? raw_count / elapsed_s : 0.0f;

  if (this->raw_rising_hz_sensor_ != nullptr) this->raw_rising_hz_sensor_->publish_state(raw_hz);
  if (this->raw_rising_count_sensor_ != nullptr) this->raw_rising_count_sensor_->publish_state(raw_count);

  if (stats.count == 0) {
    if (this->pulse_width_min_sensor_ != nullptr) this->pulse_width_min_sensor_->publish_state(NAN);
    if (this->pulse_width_avg_sensor_ != nullptr) this->pulse_width_avg_sensor_->publish_state(NAN);
    if (this->pulse_width_max_sensor_ != nullptr) this->pulse_width_max_sensor_->publish_state(NAN);
    if (this->pulse_width_lt20_sensor_ != nullptr) this->pulse_width_lt20_sensor_->publish_state(0.0f);
    if (this->pulse_width_20_50_sensor_ != nullptr) this->pulse_width_20_50_sensor_->publish_state(0.0f);
    if (this->pulse_width_50_100_sensor_ != nullptr) this->pulse_width_50_100_sensor_->publish_state(0.0f);
    if (this->pulse_width_ge100_sensor_ != nullptr) this->pulse_width_ge100_sensor_->publish_state(0.0f);
    return;
  }

  const float count = static_cast<float>(stats.count);
  if (this->pulse_width_min_sensor_ != nullptr) this->pulse_width_min_sensor_->publish_state(stats.min_us);
  if (this->pulse_width_avg_sensor_ != nullptr)
    this->pulse_width_avg_sensor_->publish_state(static_cast<float>(stats.sum_us) / count);
  if (this->pulse_width_max_sensor_ != nullptr) this->pulse_width_max_sensor_->publish_state(stats.max_us);
  if (this->pulse_width_lt20_sensor_ != nullptr)
    this->pulse_width_lt20_sensor_->publish_state(100.0f * stats.lt20 / count);
  if (this->pulse_width_20_50_sensor_ != nullptr)
    this->pulse_width_20_50_sensor_->publish_state(100.0f * stats.from20_to50 / count);
  if (this->pulse_width_50_100_sensor_ != nullptr)
    this->pulse_width_50_100_sensor_->publish_state(100.0f * stats.from50_to100 / count);
  if (this->pulse_width_ge100_sensor_ != nullptr)
    this->pulse_width_ge100_sensor_->publish_state(100.0f * stats.ge100 / count);
}

void OpenQuattFlowFilterProbe::loop() {
  ChannelState edge_100{};
  ChannelState pulse_13{};
  ChannelState pulse_20{};
  ChannelState pulse_50{};
  ChannelState pulse_100{};

  {
    InterruptLock lock;
    const bool current = this->pin_->digital_read();
    if (current != this->last_pin_val_) gpio_intr(this);

    edge_100.last_detected_edge_us = this->edge_100_state_.last_detected_edge_us;
    edge_100.last_rising_edge_us = this->edge_100_state_.last_rising_edge_us;
    edge_100.count = this->edge_100_state_.count;
    this->edge_100_state_.count = 0;

    pulse_13.last_detected_edge_us = this->pulse_13_state_.last_detected_edge_us;
    pulse_13.last_rising_edge_us = this->pulse_13_state_.last_rising_edge_us;
    pulse_13.count = this->pulse_13_state_.count;
    this->pulse_13_state_.count = 0;

    pulse_20.last_detected_edge_us = this->pulse_20_state_.last_detected_edge_us;
    pulse_20.last_rising_edge_us = this->pulse_20_state_.last_rising_edge_us;
    pulse_20.count = this->pulse_20_state_.count;
    this->pulse_20_state_.count = 0;

    pulse_50.last_detected_edge_us = this->pulse_50_state_.last_detected_edge_us;
    pulse_50.last_rising_edge_us = this->pulse_50_state_.last_rising_edge_us;
    pulse_50.count = this->pulse_50_state_.count;
    this->pulse_50_state_.count = 0;

    pulse_100.last_detected_edge_us = this->pulse_100_state_.last_detected_edge_us;
    pulse_100.last_rising_edge_us = this->pulse_100_state_.last_rising_edge_us;
    pulse_100.count = this->pulse_100_state_.count;
    this->pulse_100_state_.count = 0;
  }

  const uint32_t now = micros();
  this->process_channel_(this->edge_100_sensor_, edge_100, this->edge_100_runtime_, EDGE_FILTER_US, false, now);
  this->process_channel_(this->pulse_13_sensor_, pulse_13, this->pulse_13_runtime_, PULSE_13_FILTER_US, true, now);
  this->process_channel_(this->pulse_20_sensor_, pulse_20, this->pulse_20_runtime_, PULSE_20_FILTER_US, true, now);
  this->process_channel_(this->pulse_50_sensor_, pulse_50, this->pulse_50_runtime_, PULSE_50_FILTER_US, true, now);
  this->process_channel_(this, pulse_100, this->pulse_100_runtime_, PULSE_100_FILTER_US, true, now);
  this->publish_raw_diagnostics_(now);
}

void OpenQuattFlowFilterProbe::dump_config() {
  ESP_LOGCONFIG(TAG, "Q flow filter comparison probe");
  LOG_PIN("  Pin: ", this->pin_);
  ESP_LOGCONFIG(TAG, "  Primary/control: PULSE 100us");
  ESP_LOGCONFIG(TAG, "  Comparison: PULSE 13us / 20us / 50us and EDGE 100us");
  ESP_LOGCONFIG(TAG, "  Raw diagnostics: rising-edge Hz/count and pulse-width distribution");
}

}  // namespace esphome::openquatt_flow_filter_probe
