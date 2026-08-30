#include "hub.h"
#include "opentherm_timing.h"
#include "esphome/core/helpers.h"

#include <string>

namespace esphome::opentherm {

static const char* const TAG = "opentherm";
static constexpr uint32_t MIN_CONVERSATION_GAP_US = 100000;
static constexpr uint32_t MAX_CONVERSATION_CADENCE_US = 1150000;
static constexpr uint32_t SLOW_PHASE_US = 50000;
namespace message_data {
bool parse_flag8_lb_0(OpenthermData& data) { return read_bit(data.valueLB, 0); }
bool parse_flag8_lb_1(OpenthermData& data) { return read_bit(data.valueLB, 1); }
bool parse_flag8_lb_2(OpenthermData& data) { return read_bit(data.valueLB, 2); }
bool parse_flag8_lb_3(OpenthermData& data) { return read_bit(data.valueLB, 3); }
bool parse_flag8_lb_4(OpenthermData& data) { return read_bit(data.valueLB, 4); }
bool parse_flag8_lb_5(OpenthermData& data) { return read_bit(data.valueLB, 5); }
bool parse_flag8_lb_6(OpenthermData& data) { return read_bit(data.valueLB, 6); }
bool parse_flag8_lb_7(OpenthermData& data) { return read_bit(data.valueLB, 7); }
bool parse_flag8_hb_0(OpenthermData& data) { return read_bit(data.valueHB, 0); }
bool parse_flag8_hb_1(OpenthermData& data) { return read_bit(data.valueHB, 1); }
bool parse_flag8_hb_2(OpenthermData& data) { return read_bit(data.valueHB, 2); }
bool parse_flag8_hb_3(OpenthermData& data) { return read_bit(data.valueHB, 3); }
bool parse_flag8_hb_4(OpenthermData& data) { return read_bit(data.valueHB, 4); }
bool parse_flag8_hb_5(OpenthermData& data) { return read_bit(data.valueHB, 5); }
bool parse_flag8_hb_6(OpenthermData& data) { return read_bit(data.valueHB, 6); }
bool parse_flag8_hb_7(OpenthermData& data) { return read_bit(data.valueHB, 7); }
uint8_t parse_u8_lb(OpenthermData& data) { return data.valueLB; }
uint8_t parse_u8_hb(OpenthermData& data) { return data.valueHB; }
int8_t parse_s8_lb(OpenthermData& data) { return (int8_t)data.valueLB; }
int8_t parse_s8_hb(OpenthermData& data) { return (int8_t)data.valueHB; }
uint16_t parse_u16(OpenthermData& data) { return data.get_u16(); }
uint16_t parse_u8_lb_60(OpenthermData& data) { return data.valueLB * 60; }
uint16_t parse_u8_hb_60(OpenthermData& data) { return data.valueHB * 60; }
int16_t parse_s16(OpenthermData& data) { return data.get_s16(); }
float parse_f88(OpenthermData& data) { return data.get_f88(); }

void write_flag8_lb_0(const bool value, OpenthermData& data) { data.valueLB = write_bit(data.valueLB, 0, value); }
void write_flag8_lb_1(const bool value, OpenthermData& data) { data.valueLB = write_bit(data.valueLB, 1, value); }
void write_flag8_lb_2(const bool value, OpenthermData& data) { data.valueLB = write_bit(data.valueLB, 2, value); }
void write_flag8_lb_3(const bool value, OpenthermData& data) { data.valueLB = write_bit(data.valueLB, 3, value); }
void write_flag8_lb_4(const bool value, OpenthermData& data) { data.valueLB = write_bit(data.valueLB, 4, value); }
void write_flag8_lb_5(const bool value, OpenthermData& data) { data.valueLB = write_bit(data.valueLB, 5, value); }
void write_flag8_lb_6(const bool value, OpenthermData& data) { data.valueLB = write_bit(data.valueLB, 6, value); }
void write_flag8_lb_7(const bool value, OpenthermData& data) { data.valueLB = write_bit(data.valueLB, 7, value); }
void write_flag8_hb_0(const bool value, OpenthermData& data) { data.valueHB = write_bit(data.valueHB, 0, value); }
void write_flag8_hb_1(const bool value, OpenthermData& data) { data.valueHB = write_bit(data.valueHB, 1, value); }
void write_flag8_hb_2(const bool value, OpenthermData& data) { data.valueHB = write_bit(data.valueHB, 2, value); }
void write_flag8_hb_3(const bool value, OpenthermData& data) { data.valueHB = write_bit(data.valueHB, 3, value); }
void write_flag8_hb_4(const bool value, OpenthermData& data) { data.valueHB = write_bit(data.valueHB, 4, value); }
void write_flag8_hb_5(const bool value, OpenthermData& data) { data.valueHB = write_bit(data.valueHB, 5, value); }
void write_flag8_hb_6(const bool value, OpenthermData& data) { data.valueHB = write_bit(data.valueHB, 6, value); }
void write_flag8_hb_7(const bool value, OpenthermData& data) { data.valueHB = write_bit(data.valueHB, 7, value); }
void write_u8_lb(const uint8_t value, OpenthermData& data) { data.valueLB = value; }
void write_u8_hb(const uint8_t value, OpenthermData& data) { data.valueHB = value; }
void write_s8_lb(const int8_t value, OpenthermData& data) { data.valueLB = (uint8_t)value; }
void write_s8_hb(const int8_t value, OpenthermData& data) { data.valueHB = (uint8_t)value; }
void write_u16(const uint16_t value, OpenthermData& data) { data.set_u16(value); }
void write_s16(const int16_t value, OpenthermData& data) { data.set_s16(value); }
void write_f88(const float value, OpenthermData& data) { data.set_f88(value); }

}  // namespace message_data

OpenthermData OpenthermHub::build_request_(MessageId request_id) const {
  OpenthermData data;
  data.type = 0;
  data.id = request_id;
  data.valueHB = 0;
  data.valueLB = 0;

  // We need this special logic for STATUS message because we have two options for specifying boiler modes:
  // with static config values in the hub, or with separate switches.
  if (request_id == MessageId::STATUS) {
    // NOLINTBEGIN
    bool const ch_enabled = this->ch_enable && OPENTHERM_READ_ch_enable && OPENTHERM_READ_t_set > 0.0;
    bool const dhw_enabled = this->dhw_enable && OPENTHERM_READ_dhw_enable;
    bool const cooling_enabled =
        this->cooling_enable && OPENTHERM_READ_cooling_enable && OPENTHERM_READ_cooling_control > 0.0;
    bool const otc_enabled = this->otc_active && OPENTHERM_READ_otc_active;
    bool const ch2_enabled = this->ch2_active && OPENTHERM_READ_ch2_active && OPENTHERM_READ_t_set_ch2 > 0.0;
    bool const summer_mode_is_active = this->summer_mode_active && OPENTHERM_READ_summer_mode_active;
    bool const dhw_blocked = this->dhw_block && OPENTHERM_READ_dhw_block;
    // NOLINTEND

    data.type = MessageType::READ_DATA;
    data.valueHB = ch_enabled | (dhw_enabled << 1) | (cooling_enabled << 2) | (otc_enabled << 3) | (ch2_enabled << 4) |
                   (summer_mode_is_active << 5) | (dhw_blocked << 6);

    return data;
  }

  // Next, we start with write requests from switches and other inputs,
  // because we would want to write that data if it is available, rather than
  // request a read for that type (in the case that both read and write are
  // supported).
  switch (request_id) {
    OPENTHERM_SWITCH_MESSAGE_HANDLERS(OPENTHERM_MESSAGE_WRITE_MESSAGE, OPENTHERM_MESSAGE_WRITE_ENTITY, ,
                                      OPENTHERM_MESSAGE_WRITE_POSTSCRIPT, )
    OPENTHERM_NUMBER_MESSAGE_HANDLERS(OPENTHERM_MESSAGE_WRITE_MESSAGE, OPENTHERM_MESSAGE_WRITE_ENTITY, ,
                                      OPENTHERM_MESSAGE_WRITE_POSTSCRIPT, )
    OPENTHERM_OUTPUT_MESSAGE_HANDLERS(OPENTHERM_MESSAGE_WRITE_MESSAGE, OPENTHERM_MESSAGE_WRITE_ENTITY, ,
                                      OPENTHERM_MESSAGE_WRITE_POSTSCRIPT, )
    OPENTHERM_INPUT_SENSOR_MESSAGE_HANDLERS(OPENTHERM_MESSAGE_WRITE_MESSAGE, OPENTHERM_MESSAGE_WRITE_ENTITY, ,
                                            OPENTHERM_MESSAGE_WRITE_POSTSCRIPT, )
    OPENTHERM_SETTING_MESSAGE_HANDLERS(OPENTHERM_MESSAGE_WRITE_MESSAGE, OPENTHERM_MESSAGE_WRITE_SETTING, ,
                                       OPENTHERM_MESSAGE_WRITE_POSTSCRIPT, )
    default:
      break;
  }

  // Finally, handle the simple read requests, which only change with the message id.
  switch (request_id) {
    OPENTHERM_SENSOR_MESSAGE_HANDLERS(OPENTHERM_MESSAGE_READ_MESSAGE, OPENTHERM_IGNORE, , , )
    default:
      break;
  }
  switch (request_id) {
    OPENTHERM_BINARY_SENSOR_MESSAGE_HANDLERS(OPENTHERM_MESSAGE_READ_MESSAGE, OPENTHERM_IGNORE, , , )
    default:
      break;
  }

  // And if we get here, a message was requested which somehow wasn't handled.
  // This shouldn't happen due to the way the defines are configured, so we
  // log an error and just return a 0 message.
  ESP_LOGE(TAG, "Tried to create a request with unknown id %d. This should never happen, so please open an issue.",
           request_id);
  return {};
}

OpenthermHub::OpenthermHub() : Component(), in_pin_{}, out_pin_{} {}

void OpenthermHub::process_response(OpenthermData& data) {
  ESP_LOGD(TAG, "Received OpenTherm response with id %d (%s)", data.id,
           this->opentherm_->message_id_to_str((MessageId)data.id));
  this->opentherm_->debug_data(data);

  switch (data.id) {
    OPENTHERM_SENSOR_MESSAGE_HANDLERS(OPENTHERM_MESSAGE_RESPONSE_MESSAGE, OPENTHERM_MESSAGE_RESPONSE_ENTITY, ,
                                      OPENTHERM_MESSAGE_RESPONSE_POSTSCRIPT, )
  }
  switch (data.id) {
    OPENTHERM_BINARY_SENSOR_MESSAGE_HANDLERS(OPENTHERM_MESSAGE_RESPONSE_MESSAGE, OPENTHERM_MESSAGE_RESPONSE_ENTITY, ,
                                             OPENTHERM_MESSAGE_RESPONSE_POSTSCRIPT, )
  }
}

void OpenthermHub::setup() {
  this->opentherm_ = make_unique<OpenTherm>(this->in_pin_, this->out_pin_);
  if (!this->opentherm_->initialize()) {
    ESP_LOGE(TAG, "Failed to initialize OpenTherm protocol. See previous log messages for details.");
    this->mark_failed();
    return;
  }

  // Ensure that, once the runtime owner starts polling, there is at least one
  // request. Sending the status request once per second is good practice.
  this->add_repeating_message(MessageId::STATUS);
  this->write_initial_messages_(this->messages_);
  this->message_iterator_ = this->messages_.begin();
}

void OpenthermHub::on_shutdown() { this->suspend_polling(); }

void OpenthermHub::prioritize_messages(MessageId first, MessageId second) {
  // Urgent priority is reserved for fail-safe lifecycle transitions. It
  // always supersedes a deferred runtime start, but it must not truncate an
  // active request/response exchange: a slow but valid boiler response may
  // still be on the bus. start_conversation_() installs the sequence as soon
  // as the current exchange reaches IDLE.
  this->deferred_priority_pending_ = false;
  this->deferred_priority_activated_ = false;
  if (!this->polling_enabled_) {
    this->urgent_priority_pending_ = false;
    return;
  }
  this->urgent_priority_first_ = first;
  this->urgent_priority_second_ = second;
  this->urgent_priority_pending_ = true;
}

void OpenthermHub::defer_priority_messages(MessageId first, MessageId second) {
  // Do not stop an active request/response exchange for a normal runtime
  // command. The sequence is installed from start_conversation_() once the
  // current conversation, and any already active fail-safe priority sequence,
  // has completed.
  if (!this->polling_enabled_) {
    this->deferred_priority_pending_ = false;
    this->deferred_priority_activated_ = false;
    return;
  }
  this->deferred_priority_activated_ = false;
  this->deferred_priority_first_ = first;
  this->deferred_priority_second_ = second;
  this->deferred_priority_pending_ = true;
}

bool OpenthermHub::consume_deferred_priority_activation(MessageId first, MessageId second) {
  const bool matches = this->deferred_priority_activated_ && this->deferred_priority_first_ == first &&
                       this->deferred_priority_second_ == second;
  this->deferred_priority_activated_ = false;
  return matches;
}

void OpenthermHub::start_priority_polling(MessageId first, MessageId second) {
  this->polling_enabled_ = true;
  this->prioritize_messages(first, second);
  this->enable_loop();
}

void OpenthermHub::resume_polling() {
  this->polling_enabled_ = true;
  this->urgent_priority_pending_ = false;
  this->deferred_priority_pending_ = false;
  this->deferred_priority_activated_ = false;
  this->opentherm_->stop();
  this->sending_initial_ = true;
  this->priority_sequence_active_ = false;
  this->write_initial_messages_(this->messages_);
  this->message_iterator_ = this->messages_.begin();
  this->has_last_conversation_start_ = false;
  this->has_last_conversation_end_ = false;
  this->has_last_wire_response_ = false;
  this->enable_loop();
}

void OpenthermHub::suspend_polling() {
  this->polling_enabled_ = false;
  this->urgent_priority_pending_ = false;
  this->deferred_priority_pending_ = false;
  this->deferred_priority_activated_ = false;
  if (this->opentherm_ != nullptr) {
    this->opentherm_->stop();
  }
  this->disable_loop();
}

// Disabling clang-tidy for this particular line since it keeps removing the trailing underscore (bug?)
void OpenthermHub::write_initial_messages_(std::vector<MessageId>& target) {  // NOLINT
  std::vector<std::pair<MessageId, uint8_t>> sorted;
  std::copy_if(this->configured_messages_.begin(), this->configured_messages_.end(), std::back_inserter(sorted),
               [](const std::pair<MessageId, uint8_t>& pair) { return pair.second < REPEATING_MESSAGE_ORDER; });
  std::sort(sorted.begin(), sorted.end(),
            [](const std::pair<MessageId, uint8_t>& a, const std::pair<MessageId, uint8_t>& b) {
              return a.second < b.second;
            });

  target.clear();
  std::transform(sorted.begin(), sorted.end(), std::back_inserter(target),
                 [](const std::pair<MessageId, uint8_t>& pair) { return pair.first; });
}

// Disabling clang-tidy for this particular line since it keeps removing the trailing underscore (bug?)
void OpenthermHub::write_repeating_messages_(std::vector<MessageId>& target) {  // NOLINT
  target.clear();
  for (auto const& pair : this->configured_messages_) {
    if (pair.second == REPEATING_MESSAGE_ORDER) {
      target.push_back(pair.first);
    }
  }
}

void OpenthermHub::loop() {
  if (!this->polling_enabled_) {
    return;
  }
  const OperationMode transport_mode_before = this->opentherm_->get_mode();
  const uint32_t transport_started_us = micros();
  const transport_diagnostics::PollResult transport_result = this->opentherm_->process();
  const uint32_t transport_finished_us = micros();
  const OperationMode transport_mode_after = this->opentherm_->get_mode();
  this->record_transport_poll_(transport_result, transport_mode_before, transport_mode_after,
                               timing::elapsed_us(transport_finished_us, transport_started_us));
  if (this->sync_mode_) {
    this->sync_loop_();
    return;
  }

  auto cur_time_us = micros();
  auto const cur_mode = this->opentherm_->get_mode();

  if (this->handle_error_(cur_mode)) {
    return;
  }

  switch (cur_mode) {
    case OperationMode::WRITE:
    case OperationMode::READ:
    case OperationMode::LISTEN:
      break;
    case OperationMode::IDLE:
      if (this->should_skip_loop_(cur_time_us)) {
        break;
      }
      this->start_conversation_();
      break;
    case OperationMode::SENT:
      // Message sent, now listen for the response.
      this->opentherm_->listen();
      break;
    case OperationMode::RECEIVED:
      this->read_response_();
      break;
    default:
      break;
  }
  this->last_mode_ = cur_mode;
}

bool OpenthermHub::handle_error_(OperationMode mode) {
  switch (mode) {
    case OperationMode::ERROR_PROTOCOL:
      // Protocol error can happen only while reading boiler response.
      this->handle_protocol_error_();
      return true;
    case OperationMode::ERROR_TIMEOUT:
      // Timeout error might happen while we wait for device to respond.
      this->handle_timeout_error_();
      return true;
    case OperationMode::ERROR_TIMER:
      // Timer error can happen only on ESP32.
      this->handle_timer_error_();
      return true;
    default:
      return false;
  }
}

void OpenthermHub::sync_loop_() {
  if (!this->opentherm_->is_idle()) {
    ESP_LOGE(TAG, "OpenTherm is not idle at the start of the loop");
    return;
  }

  auto cur_time_us = micros();

  if (this->should_skip_loop_(cur_time_us)) {
    return;
  }

  this->start_conversation_();
  // There may be a timer error at this point
  if (this->handle_error_(this->opentherm_->get_mode())) {
    return;
  }

  // Spin while message is being sent to device
  if (!this->spin_wait_(1150, [&] {
        this->opentherm_->process();
        return this->opentherm_->is_active();
      })) {
    ESP_LOGE(TAG, "Hub timeout triggered during send");
    this->stop_opentherm_();
    return;
  }

  // ESP32 RMT closes the TX-to-RX handover in the TX-complete ISR, so the
  // first spin can cover the complete request/response exchange. Other
  // platforms still stop at SENT and enter the existing listen phase below.
  if (this->handle_error_(this->opentherm_->get_mode())) {
    return;
  } else if (this->opentherm_->has_message()) {
    this->read_response_();
    return;
  } else if (!this->opentherm_->is_sent()) {
    ESP_LOGW(TAG, "Unexpected state after sending request: %s",
             this->opentherm_->operation_mode_to_str(this->opentherm_->get_mode()));
    this->stop_opentherm_();
    return;
  }

  // Listen for the response
  this->opentherm_->listen();
  // There may be a timer error at this point
  if (this->handle_error_(this->opentherm_->get_mode())) {
    return;
  }

  // Spin while response is being received
  if (!this->spin_wait_(1150, [&] {
        this->opentherm_->process();
        return this->opentherm_->is_active();
      })) {
    ESP_LOGE(TAG, "Hub timeout triggered during receive");
    this->stop_opentherm_();
    return;
  }

  // Check for errors and ensure we are in the right state (message received successfully)
  if (this->handle_error_(this->opentherm_->get_mode())) {
    return;
  } else if (!this->opentherm_->has_message()) {
    ESP_LOGW(TAG, "Unexpected state after receiving response: %s",
             this->opentherm_->operation_mode_to_str(this->opentherm_->get_mode()));
    this->stop_opentherm_();
    return;
  }

  this->read_response_();
}

void OpenthermHub::check_cadence_(uint32_t started_us) const {
  if (!this->has_last_conversation_start_) {
    return;
  }

  const uint32_t cadence_us = timing::elapsed_us(started_us, this->last_conversation_start_us_);
  if (cadence_us <= MAX_CONVERSATION_CADENCE_US) {
    return;
  }

  if (this->has_last_wire_response_) {
    ESP_LOGW(TAG,
             "OpenTherm request cadence delayed to %u ms: previous wire response %u ms, main-loop processing "
             "latency %u ms. Response timeouts are reported separately.",
             static_cast<unsigned>(cadence_us / 1000U), static_cast<unsigned>(this->last_wire_response_us_ / 1000U),
             static_cast<unsigned>(this->last_processing_latency_us_ / 1000U));
  } else {
    ESP_LOGW(TAG,
             "OpenTherm request cadence delayed to %u ms; the previous conversation had no captured response. "
             "Response timeouts are reported separately.",
             static_cast<unsigned>(cadence_us / 1000U));
  }
  this->log_transport_diagnostics_();
}

bool OpenthermHub::should_skip_loop_(uint32_t cur_time_us) const {
  if (this->has_last_conversation_end_ &&
      !timing::delay_elapsed(cur_time_us, this->last_conversation_end_us_, MIN_CONVERSATION_GAP_US)) {
    ESP_LOGV(TAG, "Less than 100 ms elapsed since last convo, skipping this iteration");
    return true;
  }

  return false;
}

void OpenthermHub::warn_if_slow_(const char* phase, uint32_t started_us) const {
  const uint32_t elapsed = timing::elapsed_us(micros(), started_us);
  if (elapsed >= SLOW_PHASE_US) {
    ESP_LOGW(TAG, "%s took %u ms in the main loop; this is not OpenTherm wire wait time", phase,
             static_cast<unsigned>(elapsed / 1000U));
  }
}

void OpenthermHub::record_transport_poll_(transport_diagnostics::PollResult result, OperationMode mode_before,
                                          OperationMode mode_after, uint32_t elapsed_us) {
  if (!transport_diagnostics::record_slow_poll(this->slow_transport_poll_stats_, result, elapsed_us)) {
    return;
  }
  this->last_slow_transport_mode_before_ = mode_before;
  this->last_slow_transport_mode_after_ = mode_after;
}

void OpenthermHub::log_transport_diagnostics_() const {
  ESP_LOGD(TAG,
           "OpenTherm transport: requests=%u tx_completed=%u rx_captured=%u rx_accepted=%u rx_rejected=%u "
           "tx_timeouts=%u response_timeouts=%u late_timeouts=%u max_wire_response=%u ms "
           "max_processing_latency=%u ms slow_polls=%u max_poll_wall=%u ms "
           "slow_outcomes(no_work=%u tx_timeout=%u rx_timeout_no_frame=%u rx_frame_after_deadline=%u "
           "rx_frame_accepted=%u rx_frame_rejected=%u) last_slow=%s mode=%s->%s; slow poll wall time may "
           "include task preemption and is not OpenTherm wire wait time",
           static_cast<unsigned>(this->requests_started_), static_cast<unsigned>(this->tx_completed_),
           static_cast<unsigned>(this->rx_captured_), static_cast<unsigned>(this->rx_accepted_),
           static_cast<unsigned>(this->rx_rejected_), static_cast<unsigned>(this->tx_timeouts_),
           static_cast<unsigned>(this->response_timeouts_), static_cast<unsigned>(this->late_response_timeouts_),
           static_cast<unsigned>(this->max_wire_response_us_ / 1000U),
           static_cast<unsigned>(this->max_processing_latency_us_ / 1000U),
           static_cast<unsigned>(this->slow_transport_poll_stats_.count),
           static_cast<unsigned>(this->slow_transport_poll_stats_.max_elapsed_us / 1000U),
           static_cast<unsigned>(this->slow_transport_poll_stats_.no_work),
           static_cast<unsigned>(this->slow_transport_poll_stats_.tx_timeout),
           static_cast<unsigned>(this->slow_transport_poll_stats_.rx_timeout_no_frame),
           static_cast<unsigned>(this->slow_transport_poll_stats_.rx_frame_after_deadline),
           static_cast<unsigned>(this->slow_transport_poll_stats_.rx_frame_accepted),
           static_cast<unsigned>(this->slow_transport_poll_stats_.rx_frame_rejected),
           this->slow_transport_poll_stats_.count > 0
               ? transport_diagnostics::poll_result_to_str(this->slow_transport_poll_stats_.last_result)
               : "none",
           this->opentherm_->operation_mode_to_str(this->last_slow_transport_mode_before_),
           this->opentherm_->operation_mode_to_str(this->last_slow_transport_mode_after_));
}

void OpenthermHub::activate_priority_sequence_(MessageId first, MessageId second) {
  this->messages_ = {first, second};
  this->message_iterator_ = this->messages_.begin();
  this->sending_initial_ = false;
  this->priority_sequence_active_ = true;
}

void OpenthermHub::apply_urgent_priority_() {
  if (!this->urgent_priority_pending_) {
    return;
  }
  this->activate_priority_sequence_(this->urgent_priority_first_, this->urgent_priority_second_);
  this->urgent_priority_pending_ = false;
}

void OpenthermHub::apply_deferred_priority_() {
  if (!this->deferred_priority_pending_) {
    return;
  }
  // A fail-safe priority sequence must finish before a queued start can take
  // ownership. message_iterator_ reaches end only after both responses were
  // processed successfully.
  if (this->priority_sequence_active_ && this->message_iterator_ != this->messages_.end()) {
    return;
  }

  const MessageId first = this->deferred_priority_first_;
  const MessageId second = this->deferred_priority_second_;
  this->activate_priority_sequence_(first, second);
  this->deferred_priority_pending_ = false;
  this->deferred_priority_activated_ = true;
}

void OpenthermHub::start_conversation_() {
  this->apply_urgent_priority_();
  this->apply_deferred_priority_();
  if (this->message_iterator_ == this->messages_.end()) {
    if (this->priority_sequence_active_) {
      this->priority_sequence_active_ = false;
      this->sending_initial_ = false;
      this->write_repeating_messages_(this->messages_);
    } else if (this->sending_initial_) {
      this->sending_initial_ = false;
      this->write_repeating_messages_(this->messages_);
    }
    this->message_iterator_ = this->messages_.begin();
  }

  auto request = this->build_request_(*this->message_iterator_);

  const uint32_t request_processing_started_us = micros();
  this->before_send_callback_.call(request);

  ESP_LOGD(TAG, "Sending request with id %d (%s)", request.id,
           this->opentherm_->message_id_to_str((MessageId)request.id));
  this->opentherm_->debug_data(request);
  uint32_t fallback_started_us = micros();
#ifndef USE_ESP32
  // Avoid diagnostics while the timer-driven Manchester transmission is active.
  this->check_cadence_(fallback_started_us);
  this->warn_if_slow_("request preparation", request_processing_started_us);
  fallback_started_us = micros();
  this->last_conversation_start_us_ = fallback_started_us;
  this->has_last_conversation_start_ = true;
#endif
  // Send the request
  this->opentherm_->send(request);

#ifdef USE_ESP32
  ConversationTiming conversation_timing;
  const bool has_wire_timing = this->opentherm_->get_conversation_timing(conversation_timing);
  if (has_wire_timing) {
    this->requests_started_++;
  }
  const uint32_t started_us = has_wire_timing ? conversation_timing.request_started_us : fallback_started_us;
  this->check_cadence_(started_us);
  this->last_conversation_start_us_ = started_us;
  this->has_last_conversation_start_ = true;
  this->warn_if_slow_("request preparation and send", request_processing_started_us);
#else
  if (!this->opentherm_->is_error()) {
    this->requests_started_++;
  }
#endif
}

void OpenthermHub::read_response_() {
  const uint32_t response_processing_started_us = micros();
  OpenthermData response;
  if (!this->opentherm_->get_message(response)) {
    ESP_LOGW(TAG, "Couldn't get the response, but flags indicated success. This is a bug.");
    this->stop_opentherm_();
    return;
  }

  this->stop_opentherm_();

  this->before_process_response_callback_.call(response);
  this->process_response(response);
  this->warn_if_slow_("response callbacks and entity updates", response_processing_started_us);

  this->message_iterator_++;
}

void OpenthermHub::stop_opentherm_() {
  const OperationMode completed_mode = this->opentherm_->get_mode();
  ConversationTiming conversation_timing;
  const bool has_wire_timing = this->opentherm_->stop(conversation_timing);
  const uint32_t processed_us = micros();

  this->has_last_wire_response_ = has_wire_timing && conversation_timing.response_captured;
  const bool receive_timed_out = completed_mode == OperationMode::ERROR_TIMEOUT && has_wire_timing &&
                                 conversation_timing.request_completed && !conversation_timing.response_captured;
  this->last_conversation_end_us_ =
      timing::conversation_end_us(this->has_last_wire_response_, conversation_timing.response_captured_us,
                                  receive_timed_out, conversation_timing.response_deadline_us, processed_us);
  if (this->has_last_wire_response_) {
    this->last_processing_latency_us_ = timing::elapsed_us(processed_us, conversation_timing.response_captured_us);
    if (conversation_timing.request_completed) {
      this->last_wire_response_us_ =
          timing::elapsed_us(conversation_timing.response_captured_us, conversation_timing.request_completed_us);
    } else {
      this->last_wire_response_us_ =
          timing::elapsed_us(conversation_timing.response_captured_us, conversation_timing.request_started_us);
    }
  } else if (receive_timed_out) {
    // No response occupied the wire. Use the existing receive deadline as the
    // protocol end so scheduler latency does not extend the 100 ms gap.
    this->last_processing_latency_us_ = 0;
    this->last_wire_response_us_ = 0;
  } else {
    this->last_processing_latency_us_ = 0;
    this->last_wire_response_us_ = 0;
  }
  this->has_last_conversation_end_ = true;

  if (has_wire_timing && conversation_timing.request_completed) {
    this->tx_completed_++;
  }
  if (this->has_last_wire_response_) {
    this->rx_captured_++;
    if (this->last_wire_response_us_ > this->max_wire_response_us_) {
      this->max_wire_response_us_ = this->last_wire_response_us_;
    }
    if (this->last_processing_latency_us_ > this->max_processing_latency_us_) {
      this->max_processing_latency_us_ = this->last_processing_latency_us_;
    }
  }
  if (completed_mode == OperationMode::RECEIVED) {
    this->rx_accepted_++;
  } else if (completed_mode == OperationMode::ERROR_PROTOCOL) {
    this->rx_rejected_++;
  } else if (completed_mode == OperationMode::ERROR_TIMEOUT) {
    if (has_wire_timing && !conversation_timing.request_completed) {
      this->tx_timeouts_++;
    } else {
      this->response_timeouts_++;
      if (this->has_last_wire_response_) {
        this->late_response_timeouts_++;
      }
    }
  }

  // Keep the counter summary useful for soak tests without logging every conversation.
  if (this->requests_started_ > 0 && (this->requests_started_ % 128U) == 0U) {
    this->log_transport_diagnostics_();
  }
}

void OpenthermHub::handle_protocol_error_() {
  OpenThermError error;
  this->opentherm_->get_protocol_error(error);
  ESP_LOGW(TAG, "Protocol error occured while receiving response: %s",
           this->opentherm_->protocol_error_to_str(error.error_type));
  this->opentherm_->debug_error(error);
  this->stop_opentherm_();
}

void OpenthermHub::handle_timeout_error_() {
  ConversationTiming conversation_timing;
  const bool has_wire_timing = this->opentherm_->get_conversation_timing(conversation_timing);
  if (has_wire_timing && !conversation_timing.request_completed) {
    ESP_LOGW(TAG, "Timeout while waiting for response from device: RMT TX did not complete");
  } else if (has_wire_timing && conversation_timing.response_captured) {
    ESP_LOGW(TAG, "Timeout while waiting for response from device: frame was captured after the receive deadline");
  } else if (has_wire_timing) {
    ESP_LOGW(TAG, "Timeout while waiting for response from device: no frame captured before the receive deadline");
  } else {
    ESP_LOGW(TAG, "Timeout while waiting for response from device");
  }
  this->stop_opentherm_();
}

void OpenthermHub::handle_timer_error_() {
  this->urgent_priority_pending_ = false;
  this->deferred_priority_pending_ = false;
  this->deferred_priority_activated_ = false;
  this->opentherm_->report_and_reset_timer_error();
  this->stop_opentherm_();
  // Timer error is critical, there is no point in retrying.
  this->mark_failed();
}

void OpenthermHub::dump_config() {
  std::vector<MessageId> initial_messages;
  std::vector<MessageId> repeating_messages;
  this->write_initial_messages_(initial_messages);
  this->write_repeating_messages_(repeating_messages);

  ESP_LOGCONFIG(TAG,
                "OpenTherm:\n"
                "  Sync mode: %s\n"
                "  Sensors: %s\n"
                "  Binary sensors: %s\n"
                "  Switches: %s\n"
                "  Input sensors: %s\n"
                "  Outputs: %s\n"
                "  Numbers: %s",
                YESNO(this->sync_mode_), SHOW(OPENTHERM_SENSOR_LIST(ID, )), SHOW(OPENTHERM_BINARY_SENSOR_LIST(ID, )),
                SHOW(OPENTHERM_SWITCH_LIST(ID, )), SHOW(OPENTHERM_INPUT_SENSOR_LIST(ID, )),
                SHOW(OPENTHERM_OUTPUT_LIST(ID, )), SHOW(OPENTHERM_NUMBER_LIST(ID, )));
  LOG_PIN("  In: ", this->in_pin_);
  LOG_PIN("  Out: ", this->out_pin_);
  ESP_LOGCONFIG(TAG, "  Initial requests:");
  for (auto type : initial_messages) {
    ESP_LOGCONFIG(TAG, "  - %d (%s)", type, this->opentherm_->message_id_to_str(type));
  }
  ESP_LOGCONFIG(TAG, "  Repeating requests:");
  for (auto type : repeating_messages) {
    ESP_LOGCONFIG(TAG, "  - %d (%s)", type, this->opentherm_->message_id_to_str(type));
  }
}

}  // namespace esphome::opentherm
