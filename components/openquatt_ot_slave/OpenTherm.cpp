/*
OpenTherm.cpp - OpenTherm Communication Library For Arduino, ESP8266
Copyright 2018, Ihor Melnyk
*/

#include "OpenTherm.h"
#include "OpenThermRmtDecoder.h"
#include "esphome/core/log.h"
#include "esp_err.h"
#include "esp_private/esp_clk.h"
#include "esp_timer.h"
static const char* TAG = "OpenThermV2";
using namespace esphome;

namespace {
uint32_t IRAM_ATTR now_cycles() { return static_cast<uint32_t>(esp_cpu_get_cycle_count()); }

bool read_bit(unsigned long value, int index) { return ((value >> index) & 0x1U) != 0; }
}  // namespace

#if OQ_OT_RMT_SUPPORTED
static constexpr uint32_t OT_TX_RESOLUTION_HZ = 1000000;
static constexpr uint32_t OT_RX_RESOLUTION_HZ = 1000000;
static constexpr uint16_t OT_HALF_BIT_US = 500;
static constexpr uint32_t OT_RMT_CLK_FREQ_HZ = 80000000;
static constexpr uint32_t OT_RX_SIGNAL_MAX_US = 2500;
#endif
static constexpr uint32_t OT_FRAME_DELAY_US = 100000;

OpenTherm::OpenTherm(int inPin, int outPin, bool isSlave)
    : status(OpenThermStatus::NOT_INITIALIZED),
      inPin(inPin),
      outPin(outPin),
      isSlave(isSlave),
      response(0),
      responseStatus(OpenThermResponseStatus::NONE),
      responseTimestamp(0),
      processResponseCallback(NULL),
      pCallbackUser(NULL) {}

bool OpenTherm::begin(void (*handleInterruptCallback)(void),
                      void (*processResponseCallback)(unsigned long, OpenThermResponseStatus, void*),
                      void* pCallbackUser) {
  gpio_config_t input_config{};
  input_config.pin_bit_mask = 1ULL << inPin;
  input_config.mode = GPIO_MODE_INPUT;
  input_config.pull_up_en = GPIO_PULLUP_DISABLE;
  input_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  input_config.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&input_config));

  gpio_config_t output_config{};
  output_config.pin_bit_mask = 1ULL << outPin;
  output_config.mode = GPIO_MODE_OUTPUT;
  output_config.pull_up_en = GPIO_PULLUP_DISABLE;
  output_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  output_config.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&output_config));

  const bool wants_rx = (handleInterruptCallback != NULL) || (processResponseCallback != NULL);
  this->pCallbackUser = pCallbackUser;
  activateBoiler();
  status = OpenThermStatus::NOT_INITIALIZED;
  cycles_per_us_ = static_cast<uint32_t>(esp_clk_cpu_freq() / 1000000ULL);
  if (cycles_per_us_ == 0) {
    cycles_per_us_ = 240;
  }
  reset_receive_state_();
  responseTimestamp = now_cycles();
  this->processResponseCallback = processResponseCallback;
  init_rx_glitch_filter_();
  if (wants_rx && !init_rx_channel_()) {
    ESP_LOGE(TAG, "Failed to start RMT RX on GPIO%d", inPin);
    deinit_rx_glitch_filter_();
    return false;
  }
  if (!init_tx_channel_()) {
    ESP_LOGE(TAG, "Failed to start RMT TX on GPIO%d", outPin);
    deinit_rx_channel_();
    deinit_rx_glitch_filter_();
    return false;
  }

  status = OpenThermStatus::READY;
  return true;
}

bool OpenTherm::begin(void (*handleInterruptCallback)(void)) { return begin(handleInterruptCallback, NULL, NULL); }

bool IRAM_ATTR OpenTherm::isReady() { return status == OpenThermStatus::READY; }

void OpenTherm::setIdleState() { gpio_set_level(static_cast<gpio_num_t>(outPin), 1); }

void OpenTherm::activateBoiler() {
  setIdleState();
  // As an OT slave we should not block the main loop for a full second when the
  // runtime starts; that can make the thermostat report a temporary comms loss.
  if (!isSlave) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void OpenTherm::reset_receive_state_() {
  portENTER_CRITICAL(&mux_);
#if OQ_OT_RMT_SUPPORTED
  rx_frame_head_ = 0;
  rx_frame_tail_ = 0;
#endif
  edge_overflow_ = false;
  portEXIT_CRITICAL(&mux_);
  response = 0;
  responseStatus = OpenThermResponseStatus::NONE;
  delay_until_ts_ = 0;
}

bool OpenTherm::init_tx_channel_() {
#if !OQ_OT_RMT_SUPPORTED
  ESP_LOGW(TAG, "OpenTherm TX unavailable on this target: newer RMT driver headers missing");
  tx_rmt_ready_ = false;
  return false;
#else
  if (tx_rmt_ready_) {
    return true;
  }

  rmt_tx_channel_config_t tx_config{};
  tx_config.gpio_num = static_cast<gpio_num_t>(outPin);
  tx_config.clk_src = RMT_CLK_SRC_DEFAULT;
  tx_config.resolution_hz = OT_TX_RESOLUTION_HZ;
  tx_config.mem_block_symbols = 64;
  tx_config.trans_queue_depth = 2;
  tx_config.intr_priority = 0;
  tx_config.flags.invert_out = false;
  tx_config.flags.with_dma = false;
  tx_config.flags.io_loop_back = false;
  tx_config.flags.io_od_mode = false;
  tx_config.flags.allow_pd = false;
  tx_config.flags.init_level = 1;

  esp_err_t err = rmt_new_tx_channel(&tx_config, &tx_channel_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to create TX channel on GPIO%d: %s", outPin, esp_err_to_name(err));
    tx_channel_ = nullptr;
    return false;
  }

  rmt_copy_encoder_config_t encoder_config{};
  err = rmt_new_copy_encoder(&encoder_config, &tx_encoder_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to create TX encoder: %s", esp_err_to_name(err));
    rmt_del_channel(tx_channel_);
    tx_channel_ = nullptr;
    return false;
  }

  rmt_tx_event_callbacks_t callbacks{};
  callbacks.on_trans_done = tx_done_callback_;
  err = rmt_tx_register_event_callbacks(tx_channel_, &callbacks, this);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to register TX callbacks: %s", esp_err_to_name(err));
    rmt_del_encoder(tx_encoder_);
    tx_encoder_ = nullptr;
    rmt_del_channel(tx_channel_);
    tx_channel_ = nullptr;
    return false;
  }

  err = rmt_enable(tx_channel_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to enable TX channel: %s", esp_err_to_name(err));
    rmt_del_encoder(tx_encoder_);
    tx_encoder_ = nullptr;
    rmt_del_channel(tx_channel_);
    tx_channel_ = nullptr;
    return false;
  }

  tx_complete_ = false;
  tx_in_progress_ = false;
  tx_rmt_ready_ = true;
  return true;
#endif
}

bool OpenTherm::init_rx_glitch_filter_() {
#if !OQ_OT_RMT_SUPPORTED || !OQ_OT_GPIO_GLITCH_FILTER_SUPPORTED
  rx_glitch_filter_ = nullptr;
  return false;
#else
  if (rx_glitch_filter_ != nullptr) {
    return true;
  }

  gpio_pin_glitch_filter_config_t config{};
#ifdef GLITCH_FILTER_CLK_SRC_DEFAULT
  config.clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT;
#endif
  config.gpio_num = static_cast<gpio_num_t>(inPin);

  esp_err_t err = gpio_new_pin_glitch_filter(&config, &rx_glitch_filter_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to create RX pin glitch filter on GPIO%d: %s", inPin, esp_err_to_name(err));
    rx_glitch_filter_ = nullptr;
    return false;
  }

  err = gpio_glitch_filter_enable(rx_glitch_filter_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to enable RX pin glitch filter on GPIO%d: %s", inPin, esp_err_to_name(err));
    gpio_del_glitch_filter(rx_glitch_filter_);
    rx_glitch_filter_ = nullptr;
    return false;
  }

  ESP_LOGI(TAG, "Enabled RX pin glitch filter on GPIO%d", inPin);
  return true;
#endif
}

void OpenTherm::deinit_rx_glitch_filter_() {
#if !OQ_OT_RMT_SUPPORTED || !OQ_OT_GPIO_GLITCH_FILTER_SUPPORTED
  rx_glitch_filter_ = nullptr;
  return;
#else
  if (rx_glitch_filter_ == nullptr) {
    return;
  }
  gpio_glitch_filter_disable(rx_glitch_filter_);
  gpio_del_glitch_filter(rx_glitch_filter_);
  rx_glitch_filter_ = nullptr;
#endif
}

bool OpenTherm::init_rx_channel_() {
#if !OQ_OT_RMT_SUPPORTED
  ESP_LOGW(TAG, "OpenTherm RX unavailable on this target: newer RMT driver headers missing");
  rx_rmt_ready_ = false;
  return false;
#else
  if (rx_rmt_ready_) {
    return true;
  }

  rmt_rx_channel_config_t rx_config{};
  rx_config.gpio_num = static_cast<gpio_num_t>(inPin);
  rx_config.clk_src = RMT_CLK_SRC_DEFAULT;
  rx_config.resolution_hz = OT_RX_RESOLUTION_HZ;
  rx_config.mem_block_symbols = RX_CAPTURE_SYMBOLS;
  rx_config.intr_priority = 0;
  rx_config.flags.invert_in = false;
  rx_config.flags.with_dma = false;
  rx_config.flags.io_loop_back = false;
  rx_config.flags.allow_pd = false;

  esp_err_t err = rmt_new_rx_channel(&rx_config, &rx_channel_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to create RX channel on GPIO%d: %s", inPin, esp_err_to_name(err));
    rx_channel_ = nullptr;
    return false;
  }

  rmt_rx_event_callbacks_t callbacks{};
  callbacks.on_recv_done = rx_done_callback_;
  err = rmt_rx_register_event_callbacks(rx_channel_, &callbacks, this);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to register RX callbacks: %s", esp_err_to_name(err));
    rmt_del_channel(rx_channel_);
    rx_channel_ = nullptr;
    return false;
  }

  err = rmt_enable(rx_channel_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to enable RX channel: %s", esp_err_to_name(err));
    rmt_del_channel(rx_channel_);
    rx_channel_ = nullptr;
    return false;
  }

  rx_rmt_ready_ = true;
  if (!arm_rmt_receive_()) {
    ESP_LOGW(TAG, "Failed to start initial RMT RX receive");
    deinit_rx_channel_();
    return false;
  }

  ESP_LOGI(TAG, "Enabled RMT RX capture on GPIO%d", inPin);
  return true;
#endif
}

void OpenTherm::deinit_rx_channel_() {
#if !OQ_OT_RMT_SUPPORTED
  rx_rmt_ready_ = false;
  return;
#else
  if (rx_channel_ == nullptr) {
    rx_rmt_ready_ = false;
    return;
  }
  rmt_disable(rx_channel_);
  rmt_del_channel(rx_channel_);
  rx_channel_ = nullptr;
  rx_rmt_ready_ = false;
#endif
}

#if OQ_OT_RMT_SUPPORTED
bool IRAM_ATTR OpenTherm::queue_rx_frame_(const rmt_symbol_word_t* symbols, size_t num_symbols,
                                          uint32_t done_ts_cycles) {
  if (symbols == nullptr || num_symbols == 0) {
    return true;
  }
  if (num_symbols > RX_CAPTURE_SYMBOLS) {
    num_symbols = RX_CAPTURE_SYMBOLS;
  }

  portENTER_CRITICAL_ISR(&mux_);
  const uint8_t next_head = static_cast<uint8_t>((rx_frame_head_ + 1U) % RX_FRAME_QUEUE_SIZE);
  if (next_head == rx_frame_tail_) {
    edge_overflow_ = true;
    portEXIT_CRITICAL_ISR(&mux_);
    return false;
  }
  RxFrame& frame = rx_frame_queue_[rx_frame_head_];
  frame.done_ts_cycles = done_ts_cycles;
  frame.symbol_count = static_cast<uint8_t>(num_symbols);
  for (size_t i = 0; i < num_symbols; ++i) {
    frame.symbols[i] = symbols[i];
  }
  rx_frame_head_ = next_head;
  portEXIT_CRITICAL_ISR(&mux_);
  return true;
}

bool OpenTherm::pop_rx_frame_(RxFrame& frame) {
  bool has_frame = false;
  portENTER_CRITICAL(&mux_);
  if (rx_frame_tail_ != rx_frame_head_) {
    frame = rx_frame_queue_[rx_frame_tail_];
    rx_frame_tail_ = static_cast<uint8_t>((rx_frame_tail_ + 1U) % RX_FRAME_QUEUE_SIZE);
    has_frame = true;
  }
  portEXIT_CRITICAL(&mux_);
  return has_frame;
}

bool OpenTherm::decode_rmt_frame_(const RxFrame& frame, unsigned long& decoded_frame, OpenThermDriverError& error) {
  using openquatt_ot_slave::rmt_decoder::DecodeError;
  using openquatt_ot_slave::rmt_decoder::Decoder;
  using openquatt_ot_slave::rmt_decoder::InputPolarity;
  using openquatt_ot_slave::rmt_decoder::Pulse;

  // The thermostat input front-end maps an active OpenTherm level to GPIO
  // high. Keep this hardware mapping explicit and outside the protocol state
  // machine.
  Decoder decoder(InputPolarity::ACTIVE_HIGH);
  bool accepting_pulses = true;

  for (size_t i = 0; i < frame.symbol_count; ++i) {
    const uint32_t durations[2] = {frame.symbols[i].duration0, frame.symbols[i].duration1};
    const bool levels[2] = {frame.symbols[i].level0 != 0, frame.symbols[i].level1 != 0};
    for (int part = 0; part < 2; ++part) {
      const uint32_t duration = durations[part];
      if (duration == 0) {
        continue;
      }
      if (accepting_pulses) {
        accepting_pulses = decoder.consume(Pulse{static_cast<uint16_t>(duration), levels[part]});
      }
    }
  }

  const auto result = decoder.finish();
  decoded_frame = result.data;
  if (result.error == DecodeError::NONE || result.error == DecodeError::PARITY) {
    // Preserve the existing callback contract: parity is classified later as
    // INVALID_PARITY instead of collapsing into a generic decoder failure.
    error = OpenThermDriverError::DRIVER_ERROR_NONE;
    return true;
  }

  if (result.error == DecodeError::GLITCH) {
    error = OpenThermDriverError::DRIVER_ERROR_GLITCH_REJECT;
  } else if (result.error == DecodeError::START_BIT) {
    error = OpenThermDriverError::DRIVER_ERROR_START_BIT_INVALID;
  } else {
    error = OpenThermDriverError::DRIVER_ERROR_TIMING_INVALID;
  }
  return false;
}

void OpenTherm::process_rmt_frame_(const RxFrame& frame) {
  if (frame.symbol_count == 0) {
    return;
  }
  unsigned long decoded_frame = 0;
  OpenThermDriverError error = OpenThermDriverError::DRIVER_ERROR_NONE;
  if (!decode_rmt_frame_(frame, decoded_frame, error)) {
    // The RX channel remains armed while the slave transmits. Ignore any
    // rejected self-capture without disturbing TX completion state.
    if (status == OpenThermStatus::REQUEST_SENDING) {
      return;
    }
    response = 0;
    responseTimestamp = frame.done_ts_cycles;
    responseStatus = OpenThermResponseStatus::INVALID;
    status = OpenThermStatus::RESPONSE_INVALID;
    return;
  }

  response = decoded_frame;
  responseTimestamp = frame.done_ts_cycles;
  status = OpenThermStatus::RESPONSE_READY;
  responseStatus = OpenThermResponseStatus::NONE;
}

bool IRAM_ATTR OpenTherm::arm_rmt_receive_() {
#if !OQ_OT_RMT_SUPPORTED
  return false;
#else
  if (!rx_rmt_ready_ || rx_channel_ == nullptr) {
    return false;
  }

  rmt_receive_config_t rx_receive_config{};
  // ESP-IDF/ESPHome caps the hardware min-range filter to a very small value.
  // Larger values make rmt_receive() fail on ESP32-S3, so keep hardware filtering
  // minimal here and classify OT glitches in the duration decoder instead.
  const uint32_t hw_filter_max_ns = 255U * 1000U / (OT_RMT_CLK_FREQ_HZ / 1000000U);
  rx_receive_config.signal_range_min_ns = hw_filter_max_ns;
  rx_receive_config.signal_range_max_ns = OT_RX_SIGNAL_MAX_US * 1000U;
  rx_receive_config.flags.en_partial_rx = false;
  return rmt_receive(rx_channel_, rx_symbols_, sizeof(rx_symbols_), &rx_receive_config) == ESP_OK;
#endif
}

bool IRAM_ATTR OpenTherm::rx_done_callback_(rmt_channel_handle_t, const rmt_rx_done_event_data_t* edata,
                                            void* user_ctx) {
  auto* instance = static_cast<OpenTherm*>(user_ctx);
  if (instance == nullptr || edata == nullptr) {
    return false;
  }
  instance->queue_rx_frame_(edata->received_symbols, edata->num_symbols, now_cycles());
  instance->arm_rmt_receive_();
  return false;
}

bool IRAM_ATTR OpenTherm::tx_done_callback_(rmt_channel_handle_t, const rmt_tx_done_event_data_t*, void* user_ctx) {
  auto* instance = static_cast<OpenTherm*>(user_ctx);
  if (instance != nullptr) {
    instance->tx_complete_ = true;
  }
  return false;
}
#endif

bool OpenTherm::queue_frame_tx_(unsigned long frame) {
#if !OQ_OT_RMT_SUPPORTED
  (void)frame;
  return false;
#else
  if (!tx_rmt_ready_ || tx_channel_ == nullptr || tx_encoder_ == nullptr) {
    return false;
  }

  tx_symbols_[0] = {
      .duration0 = OT_HALF_BIT_US,
      .level0 = 0,
      .duration1 = OT_HALF_BIT_US,
      .level1 = 1,
  };

  for (int i = 31; i >= 0; i--) {
    const bool bit = read_bit(frame, i);
    const size_t symbol_index = 1U + static_cast<size_t>(31 - i);
    tx_symbols_[symbol_index] =
        bit ? rmt_symbol_word_t{.duration0 = OT_HALF_BIT_US, .level0 = 0, .duration1 = OT_HALF_BIT_US, .level1 = 1}
            : rmt_symbol_word_t{.duration0 = OT_HALF_BIT_US, .level0 = 1, .duration1 = OT_HALF_BIT_US, .level1 = 0};
  }

  tx_symbols_[33] = {
      .duration0 = OT_HALF_BIT_US,
      .level0 = 0,
      .duration1 = OT_HALF_BIT_US,
      .level1 = 1,
  };

  rmt_transmit_config_t tx_config{};
  tx_config.loop_count = 0;
  tx_config.flags.eot_level = 1;
  tx_config.flags.queue_nonblocking = 1;

  tx_complete_ = false;
  tx_in_progress_ = true;
  const esp_err_t err = rmt_transmit(tx_channel_, tx_encoder_, tx_symbols_, sizeof(tx_symbols_), &tx_config);
  if (err != ESP_OK) {
    tx_in_progress_ = false;
    tx_complete_ = false;
    ESP_LOGW(TAG, "Failed to queue TX frame: %s", esp_err_to_name(err));
    return false;
  }
  return true;
#endif
}

bool OpenTherm::sendResponse(unsigned long request) {
  status = OpenThermStatus::REQUEST_SENDING;
  response = 0;
  responseStatus = OpenThermResponseStatus::NONE;
  if (queue_frame_tx_(request)) {
    responseTimestamp = now_cycles();
    return true;
  }
  status = OpenThermStatus::READY;
  responseTimestamp = now_cycles();
  return false;
}

bool OpenTherm::process() {
#if OQ_OT_RMT_SUPPORTED
  const uint64_t process_start_us = static_cast<uint64_t>(esp_timer_get_time());
  size_t processed_frames = 0;
  RxFrame frame{};
  while (pop_rx_frame_(frame)) {
    process_rmt_frame_(frame);
    ++processed_frames;
    if (status == OpenThermStatus::RESPONSE_READY || status == OpenThermStatus::RESPONSE_INVALID) {
      break;
    }
    if (processed_frames >= PROCESS_MAX_RX_FRAMES) {
      break;
    }
    if ((static_cast<uint64_t>(esp_timer_get_time()) - process_start_us) >= PROCESS_BUDGET_US) {
      break;
    }
  }

  if (edge_overflow_) {
    portENTER_CRITICAL(&mux_);
    edge_overflow_ = false;
    portEXIT_CRITICAL(&mux_);
    status = OpenThermStatus::RESPONSE_INVALID;
    responseTimestamp = now_cycles();
  }

  OpenThermStatus st = status;
  unsigned long ts = responseTimestamp;
  unsigned long current_response = response;

  bool bDidProcessMessage = false;

  if (st == OpenThermStatus::REQUEST_SENDING && tx_in_progress_ && tx_complete_) {
    tx_in_progress_ = false;
    tx_complete_ = false;
    setIdleState();
    status = OpenThermStatus::READY;
    responseTimestamp = now_cycles();
    st = status;
    ts = responseTimestamp;
  }

  if (st == OpenThermStatus::READY) return bDidProcessMessage;

  const uint32_t newTs = now_cycles();
  const uint32_t timeout_cycles = 1000000U * cycles_per_us_;
  if (st != OpenThermStatus::NOT_INITIALIZED && st != OpenThermStatus::DELAY &&
      (newTs - static_cast<uint32_t>(ts)) > timeout_cycles) {
    status = OpenThermStatus::READY;
    responseStatus = OpenThermResponseStatus::TIMEOUT;
    if (processResponseCallback != NULL) {
      processResponseCallback(current_response, OpenThermResponseStatus::TIMEOUT, pCallbackUser);
      bDidProcessMessage = true;
    }
  } else if (st == OpenThermStatus::RESPONSE_INVALID) {
    responseStatus = OpenThermResponseStatus::INVALID;
    if (processResponseCallback != NULL) {
      processResponseCallback(current_response, OpenThermResponseStatus::INVALID, pCallbackUser);
      bDidProcessMessage = true;
    }
    status = OpenThermStatus::READY;
    delay_until_ts_ = 0;
  } else if (st == OpenThermStatus::RESPONSE_READY) {
    OpenThermResponseStatus current_status = OpenThermResponseStatus::INVALID_MESSAGE;
    if (!isValidParity(current_response)) {
      current_status = OpenThermResponseStatus::INVALID_PARITY;
    } else {
      current_status = (isSlave ? isValidRequest(current_response) : isValidResponse(current_response))
                           ? OpenThermResponseStatus::SUCCESS
                           : OpenThermResponseStatus::INVALID_MESSAGE;
    }
    status = OpenThermStatus::DELAY;
    responseStatus = current_status;
    delay_until_ts_ = now_cycles() + (OT_FRAME_DELAY_US * cycles_per_us_);

    if (processResponseCallback != NULL) {
      processResponseCallback(current_response, current_status, pCallbackUser);
      bDidProcessMessage = true;
    }
  } else if (st == OpenThermStatus::DELAY) {
    if (delay_until_ts_ == 0) {
      delay_until_ts_ = static_cast<uint32_t>(ts) + (OT_FRAME_DELAY_US * cycles_per_us_);
    }
    if (newTs > delay_until_ts_) {
      status = OpenThermStatus::READY;
      delay_until_ts_ = 0;
    }
  }
  return bDidProcessMessage;
#else
  const OpenThermStatus st = status;
  const unsigned long current_response = response;
  bool bDidProcessMessage = false;

  if (st == OpenThermStatus::REQUEST_SENDING) {
    status = OpenThermStatus::READY;
    responseTimestamp = now_cycles();
  }

  if (st != OpenThermStatus::NOT_INITIALIZED && st != OpenThermStatus::READY && st != OpenThermStatus::DELAY) {
    status = OpenThermStatus::READY;
    responseStatus = OpenThermResponseStatus::TIMEOUT;
    if (processResponseCallback != NULL) {
      processResponseCallback(current_response, OpenThermResponseStatus::TIMEOUT, pCallbackUser);
      bDidProcessMessage = true;
    }
  }
  return bDidProcessMessage;
#endif
}

bool OpenTherm::parity(unsigned long frame)  // odd parity
{
  uint8_t p = 0;
  unsigned long tmp = frame;
  while (tmp > 0) {
    if (tmp & 1) p++;
    tmp = tmp >> 1;
  }
  return (p & 1);
}

OpenThermMessageType OpenTherm::getMessageType(unsigned long message) {
  OpenThermMessageType msg_type = static_cast<OpenThermMessageType>((message >> 28) & 7);
  return msg_type;
}

OpenThermMessageID OpenTherm::getDataID(unsigned long frame) { return (OpenThermMessageID)((frame >> 16) & 0xFF); }

unsigned long OpenTherm::buildRequest(OpenThermMessageType type, OpenThermMessageID id, unsigned int data) {
  unsigned long request = data;
  if (type == OpenThermMessageType::WRITE_DATA) {
    request |= 1ul << 28;
  }
  request |= ((unsigned long)id) << 16;
  if (parity(request)) request |= (1ul << 31);
  return request;
}

unsigned long OpenTherm::buildResponse(OpenThermMessageType type, OpenThermMessageID id, unsigned int data) {
  unsigned long response = data;
  response |= ((unsigned long)type) << 28;
  response |= ((unsigned long)id) << 16;
  if (parity(response)) response |= (1ul << 31);
  return response;
}

bool OpenTherm::isValidParity(unsigned long message) { return (parity(message) == 0); }

bool OpenTherm::isValidResponse(unsigned long response) {
  uint8_t msgType = (response << 1) >> 29;
  return msgType == READ_ACK || msgType == WRITE_ACK;
}

bool OpenTherm::isValidRequest(unsigned long request) {
  uint8_t msgType = (request << 1) >> 29;
  return msgType == READ_DATA || msgType == WRITE_DATA;
}

void OpenTherm::end() {
  deinit_rx_channel_();
#if OQ_OT_RMT_SUPPORTED
  if (tx_channel_ != nullptr) {
    if (tx_in_progress_) {
      rmt_tx_wait_all_done(tx_channel_, 50);
      tx_in_progress_ = false;
      tx_complete_ = false;
    }
    rmt_disable(tx_channel_);
  }
  if (tx_encoder_ != nullptr) {
    rmt_del_encoder(tx_encoder_);
    tx_encoder_ = nullptr;
  }
  if (tx_channel_ != nullptr) {
    rmt_del_channel(tx_channel_);
    tx_channel_ = nullptr;
  }
#endif
  deinit_rx_glitch_filter_();
  tx_rmt_ready_ = false;
  reset_receive_state_();
  status = OpenThermStatus::NOT_INITIALIZED;
}

// parsing responses
bool OpenTherm::isFault(unsigned long response) { return response & 0x1; }

bool OpenTherm::isCentralHeatingActive(unsigned long response) { return response & 0x2; }

bool OpenTherm::isHotWaterActive(unsigned long response) { return response & 0x4; }

bool OpenTherm::isFlameOn(unsigned long response) { return response & 0x8; }

bool OpenTherm::isCoolingActive(unsigned long response) { return response & 0x10; }

bool OpenTherm::isDiagnostic(unsigned long response) { return response & 0x40; }

uint16_t OpenTherm::getUInt(const unsigned long response) const {
  const uint16_t u88 = response & 0xffff;
  return u88;
}

float OpenTherm::getFloat(const unsigned long response) const {
  const uint16_t u88 = getUInt(response);
  const float f = (u88 & 0x8000) ? -(0x10000L - u88) / 256.0f : u88 / 256.0f;
  return f;
}
