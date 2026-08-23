/*
 * OpenTherm protocol implementation. Originally taken from https://github.com/jpraus/arduino-opentherm, but
 * heavily modified to comply with ESPHome coding standards and provide better logging.
 * Original code is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International
 * Public License, which is compatible with GPLv3 license, which covers C++ part of ESPHome project.
 */

#pragma once

#include <string>
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "opentherm_rmt_encoder.h"

#ifdef USE_ESP32
#include "driver/gptimer.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#endif

namespace esphome::opentherm {

template <class T>
constexpr T read_bit(T value, uint8_t bit) {
  return (value >> bit) & 0x01;
}

template <class T>
constexpr T set_bit(T value, uint8_t bit) {
  return value |= (1UL << bit);
}

template <class T>
constexpr T clear_bit(T value, uint8_t bit) {
  return value &= ~(1UL << bit);
}

template <class T>
constexpr T write_bit(T value, uint8_t bit, uint8_t bit_value) {
  return bit_value ? set_bit(value, bit) : clear_bit(value, bit);
}

enum OperationMode {
  IDLE = 0,  // no operation

  LISTEN = 1,    // waiting for transmission to start
  READ = 2,      // reading 32-bit data frame
  RECEIVED = 3,  // data frame received with valid start and stop bit

  WRITE = 4,        // writing data to output
  SENT = 5,         // all data written to output
  RMT_PENDING = 6,  // ESP32 hardware captured a frame; main-loop decode pending

  ERROR_PROTOCOL = 8,  // protocol error, can happed only during READ
  ERROR_TIMEOUT = 9,   // timeout while sending or waiting for a response
  ERROR_TIMER = 10     // error operating the ESP32 timer
};

enum ProtocolErrorType {
  NO_ERROR = 0,            // No error
  NO_TRANSITION = 1,       // No transition in the middle of the bit
  INVALID_STOP_BIT = 2,    // Stop bit wasn't present when expected
  PARITY_ERROR = 3,        // Parity check didn't pass
  NO_CHANGE_TOO_LONG = 4,  // No level change for too much timer ticks
};

enum TimerErrorType {
  NO_TIMER_ERROR = 0,           // No error
  SET_ALARM_VALUE_ERROR = 1,    // No transition in the middle of the bit
  TIMER_START_ERROR = 2,        // Stop bit wasn't present when expected
  TIMER_PAUSE_ERROR = 3,        // Parity check didn't pass
  SET_COUNTER_VALUE_ERROR = 4,  // No level change for too much timer ticks
};

enum MessageType {
  READ_DATA = 0,
  READ_ACK = 4,
  WRITE_DATA = 1,
  WRITE_ACK = 5,
  INVALID_DATA = 2,
  DATA_INVALID = 6,
  UNKNOWN_DATAID = 7
};

enum MessageId {
  STATUS = 0,
  CH_SETPOINT = 1,
  CONTROLLER_CONFIG = 2,
  DEVICE_CONFIG = 3,
  COMMAND_CODE = 4,
  FAULT_FLAGS = 5,
  REMOTE = 6,
  COOLING_CONTROL = 7,
  CH2_SETPOINT = 8,
  CH_SETPOINT_OVERRIDE = 9,
  TSP_COUNT = 10,
  TSP_COMMAND = 11,
  FHB_SIZE = 12,
  FHB_COMMAND = 13,
  MAX_MODULATION_LEVEL = 14,
  MAX_BOILER_CAPACITY = 15,  // u8_hb - u8_lb gives min modulation level
  ROOM_SETPOINT = 16,
  MODULATION_LEVEL = 17,
  CH_WATER_PRESSURE = 18,
  DHW_FLOW_RATE = 19,
  DAY_TIME = 20,
  DATE = 21,
  YEAR = 22,
  ROOM_SETPOINT_CH2 = 23,
  ROOM_TEMP = 24,
  FEED_TEMP = 25,
  DHW_TEMP = 26,
  OUTSIDE_TEMP = 27,
  RETURN_WATER_TEMP = 28,
  SOLAR_STORE_TEMP = 29,
  SOLAR_COLLECT_TEMP = 30,
  FEED_TEMP_CH2 = 31,
  DHW2_TEMP = 32,
  EXHAUST_TEMP = 33,
  FAN_SPEED = 35,
  FLAME_CURRENT = 36,
  ROOM_TEMP_CH2 = 37,
  REL_HUMIDITY = 38,
  DHW_BOUNDS = 48,
  CH_BOUNDS = 49,
  OTC_CURVE_BOUNDS = 50,
  DHW_SETPOINT = 56,
  MAX_CH_SETPOINT = 57,
  OTC_CURVE_RATIO = 58,

  // HVAC Specific Message IDs
  HVAC_STATUS = 70,
  REL_VENT_SETPOINT = 71,
  DEVICE_VENT = 74,
  HVAC_VER_ID = 75,
  REL_VENTILATION = 77,
  REL_HUMID_EXHAUST = 78,
  EXHAUST_CO2 = 79,
  SUPPLY_INLET_TEMP = 80,
  SUPPLY_OUTLET_TEMP = 81,
  EXHAUST_INLET_TEMP = 82,
  EXHAUST_OUTLET_TEMP = 83,
  EXHAUST_FAN_SPEED = 84,
  SUPPLY_FAN_SPEED = 85,
  REMOTE_VENTILATION_PARAM = 86,
  NOM_REL_VENTILATION = 87,
  HVAC_NUM_TSP = 88,
  HVAC_IDX_TSP = 89,
  HVAC_FHB_SIZE = 90,
  HVAC_FHB_IDX = 91,

  RF_SIGNAL = 98,
  DHW_MODE = 99,
  OVERRIDE_FUNC = 100,

  // Solar Specific Message IDs
  SOLAR_MODE_FLAGS = 101,  // hb0-2 Controller storage mode
                           // lb0   Device fault
                           // lb1-3 Device mode status
                           // lb4-5 Device status
  SOLAR_ASF = 102,
  SOLAR_VERSION_ID = 103,
  SOLAR_PRODUCT_ID = 104,
  SOLAR_NUM_TSP = 105,
  SOLAR_IDX_TSP = 106,
  SOLAR_FHB_SIZE = 107,
  SOLAR_FHB_IDX = 108,
  SOLAR_STARTS = 109,
  SOLAR_HOURS = 110,
  SOLAR_ENERGY = 111,
  SOLAR_TOTAL_ENERGY = 112,

  FAILED_BURNER_STARTS = 113,
  BURNER_FLAME_LOW = 114,
  OEM_DIAGNOSTIC = 115,
  BURNER_STARTS = 116,
  CH_PUMP_STARTS = 117,
  DHW_PUMP_STARTS = 118,
  DHW_BURNER_STARTS = 119,
  BURNER_HOURS = 120,
  CH_PUMP_HOURS = 121,
  DHW_PUMP_HOURS = 122,
  DHW_BURNER_HOURS = 123,
  OT_VERSION_CONTROLLER = 124,
  OT_VERSION_DEVICE = 125,
  VERSION_CONTROLLER = 126,
  VERSION_DEVICE = 127
};

enum BitPositions { STOP_BIT = 33 };

/**
 * Structure to hold Opentherm data packet content.
 * Use get_f88(), get_u16() or get_s16() functions to get appropriate value of data packet according to id of message.
 */
struct OpenthermData {
  uint8_t type;
  uint8_t id;
  uint8_t valueHB;
  uint8_t valueLB;

  OpenthermData() : type(0), id(0), valueHB(0), valueLB(0) {}

  /**
   * @return float representation of data packet value
   */
  float get_f88();

  /**
   * @param float number to set as value of this data packet
   */
  void set_f88(float value);

  /**
   * @return unsigned 16b integer representation of data packet value
   */
  uint16_t get_u16();

  /**
   * @param unsigned 16b integer number to set as value of this data packet
   */
  void set_u16(uint16_t value);

  /**
   * @return signed 16b integer representation of data packet value
   */
  int16_t get_s16();

  /**
   * @param signed 16b integer number to set as value of this data packet
   */
  void set_s16(int16_t value);
};

struct OpenThermError {
  ProtocolErrorType error_type;
  uint32_t capture;
  uint8_t clock;
  uint32_t data;
  uint8_t bit_pos;
};

struct ConversationTiming {
  bool request_started{false};
  bool request_completed{false};
  bool response_captured{false};
  uint32_t request_started_us{0};
  uint32_t request_completed_us{0};
  uint32_t response_captured_us{0};
  uint32_t response_deadline_us{0};
};

/**
 * Opentherm static class that supports either listening or sending Opentherm data packets in the same time
 */
class OpenTherm {
 public:
  OpenTherm(InternalGPIOPin* in_pin, InternalGPIOPin* out_pin, int32_t device_timeout = 800);

  /**
   * Setup pins.
   */
  bool initialize();

  /**
   * Start listening for Opentherm data packet comming from line connected to given pin.
   * If data packet is received then has_message() function returns true and data packet can be retrieved by calling
   * get_message() function. If timeout > 0 then this function waits for incomming data package for timeout millis and
   * if no data packet is recevived, error state is indicated by is_error() function. If either data packet is received
   * or timeout is reached listening is stopped.
   */
  void listen();

  /**
   * Use this function to check whether listen() function already captured a valid data packet.
   *
   * @return true if data packet has been captured from line by listen() function.
   */
  bool has_message() { return mode_ == OperationMode::RECEIVED; }

  /**
   * Use this to retrive data packed captured by listen() function. Data packet is ready when has_message() function
   * returns true. This function can be called multiple times until stop() is called.
   *
   * @param data reference to data structure to which fill the data packet data.
   * @return true if packet was ready and was filled into data structure passed, false otherwise.
   */
  bool get_message(OpenthermData& data);

  /**
   * Immediately send out Opentherm data packet to line connected on given pin.
   * Completed data transfer is indicated by is_sent() function.
   * Error state is indicated by is_error() function.
   *
   * @param data Opentherm data packet.
   */
  void send(OpenthermData& data);

  /**
   * Process a completed hardware receive capture. This is a no-op on ESP8266.
   */
  void process();

  /**
   * Stops listening for data packet or sending out data packet and resets internal state of this class.
   * Stops all timers and unattaches all interrupts.
   */
  void stop();

  /**
   * Atomically claim the final ESP32 RMT timing snapshot while stopping the conversation.
   * Returns false on platforms without RMT timing data.
   */
  bool stop(ConversationTiming& timing);

  /**
   * Get protocol error details in case a protocol error occured.
   * @param error reference to data structure to which fill the error details
   * @return true if protocol error occured during last conversation, false otherwise.
   */
  bool get_protocol_error(OpenThermError& error);

  /**
   * Return wire-level timestamps for the current or most recently completed ESP32 RMT conversation.
   * Returns false on platforms without RMT timing data.
   */
  bool get_conversation_timing(ConversationTiming& timing);

  /**
   * Use this function to check whether send() function already finished sending data packed to line.
   *
   * @return true if data packet has been sent, false otherwise.
   */
  bool is_sent() { return mode_ == OperationMode::SENT; }

  /**
   * Indicates whether listinig or sending is not in progress.
   * That also means that no timers are running and no interrupts are attached.
   *
   * @return true if listening nor sending is in progress.
   */
  bool is_idle() { return mode_ == OperationMode::IDLE; }

  /**
   * Indicates whether last listen() or send() operation ends up with an error. Includes both timeout and
   * protocol errors.
   *
   * @return true if last listen() or send() operation ends up with an error.
   */
  bool is_error() {
    return mode_ == OperationMode::ERROR_TIMEOUT || mode_ == OperationMode::ERROR_PROTOCOL || mode_ == ERROR_TIMER;
  }

  /**
   * Indicates whether last listen() or send() operation ends up with a *timeout* error
   * @return true if last listen() or send() operation ends up with a *timeout* error.
   */
  bool is_timeout() { return mode_ == OperationMode::ERROR_TIMEOUT; }

  /**
   * Indicates whether last listen() or send() operation ends up with a *protocol* error
   * @return true if last listen() or send() operation ends up with a *protocol* error.
   */
  bool is_protocol_error() { return mode_ == OperationMode::ERROR_PROTOCOL; }

  /**
   * Indicates whether start_esp32_timer_() or stop_timer_() had an error. Only relevant when used on ESP32.
   * @return true if there was an error.
   */
  bool is_timer_error() { return mode_ == OperationMode::ERROR_TIMER; }

  bool is_active() { return mode_ == LISTEN || mode_ == READ || mode_ == WRITE || mode_ == RMT_PENDING; }

  OperationMode get_mode() { return mode_; }

  void debug_data(OpenthermData& data);
  void debug_error(OpenThermError& error) const;
  void report_and_reset_timer_error();

  const char* protocol_error_to_str(ProtocolErrorType error_type);
  const char* timer_error_to_str(TimerErrorType error_type);
  const char* message_type_to_str(MessageType message_type);
  const char* operation_mode_to_str(OperationMode mode);
  const char* message_id_to_str(MessageId id);

#ifdef USE_ESP32
  static bool timer_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* user_ctx);
  static bool IRAM_ATTR rmt_rx_done_callback_(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t* event,
                                              void* user_ctx);
  static bool IRAM_ATTR rmt_tx_done_callback_(rmt_channel_handle_t channel, const rmt_tx_done_event_data_t* event,
                                              void* user_ctx);
#else
  static bool timer_isr(OpenTherm* arg);
#endif

#ifdef ESP8266
  static void esp8266_timer_isr();
#endif

 private:
  InternalGPIOPin* in_pin_;
  InternalGPIOPin* out_pin_;
  ISRInternalGPIOPin isr_in_pin_;
  ISRInternalGPIOPin isr_out_pin_;

#ifdef USE_ESP32
  static constexpr size_t RMT_CAPTURE_SYMBOLS = 96;
  static constexpr size_t RMT_TX_SYMBOLS = rmt_encoder::FRAME_SYMBOLS;
  static constexpr uint32_t RMT_TX_TIMEOUT_US = 100000;
  gptimer_handle_t timer_handle_{nullptr};
  gptimer_alarm_config_t alarm_config_{
      .alarm_count = 0,
      .reload_count = 0,
      .flags = {.auto_reload_on_alarm = true},
  };
  rmt_channel_handle_t rmt_rx_channel_{nullptr};
  rmt_receive_config_t rmt_rx_config_{};
  rmt_symbol_word_t rmt_rx_symbols_[RMT_CAPTURE_SYMBOLS]{};
  rmt_channel_handle_t rmt_tx_channel_{nullptr};
  rmt_encoder_handle_t rmt_tx_encoder_{nullptr};
  rmt_symbol_word_t rmt_tx_symbols_[RMT_TX_SYMBOLS]{};
  portMUX_TYPE rmt_mux_ = portMUX_INITIALIZER_UNLOCKED;
  volatile size_t rmt_symbol_count_{0};
  volatile bool rmt_armed_{false};
  volatile bool rmt_frame_ready_{false};
  volatile uint32_t rmt_frame_completed_us_{0};
  volatile bool rmt_tx_active_{false};
  volatile bool rmt_request_started_{false};
  volatile bool rmt_request_completed_{false};
  volatile bool rmt_response_captured_{false};
  volatile uint32_t rmt_request_started_us_{0};
  volatile uint32_t rmt_request_completed_us_{0};
  volatile uint32_t rmt_response_captured_us_{0};
  uint32_t rmt_tx_deadline_us_{0};
  uint32_t receive_deadline_us_{0};
#endif

  OperationMode mode_;
  ProtocolErrorType error_type_;
  uint32_t capture_;
  uint8_t clock_;
  uint32_t data_;
  uint8_t bit_pos_;
  int32_t timeout_counter_;  // <0 no timeout
  int32_t device_timeout_;

#ifdef USE_ESP32
  esp_err_t timer_error_ = ESP_OK;
  TimerErrorType timer_error_type_ = TimerErrorType::NO_TIMER_ERROR;

  bool init_esp32_timer_();
  bool init_esp32_rmt_();
  bool init_esp32_rmt_tx_();
  bool arm_esp32_rmt_();
  bool start_esp32_rmt_tx_();
  bool restore_esp32_rmt_tx_idle_();
  bool reset_esp32_rmt_tx_();
  void cancel_esp32_rmt_();
  void cancel_esp32_rmt_tx_();
  void process_esp32_rmt_();
  void start_esp32_timer_(uint64_t alarm_value, bool auto_reload);
#endif

  void stop_timer_();

  void read_();               // data detected start reading
  void start_read_timer_();   // ESP32 response timeout; ESP8266 reads at 1/5 bit length (5kHz)
  void start_write_timer_();  // writing timer_ to send manchester code (at 2kHz)
  bool check_parity_(uint32_t val);

  void bit_read_(uint8_t value);
  ProtocolErrorType verify_stop_bit_(uint8_t value);
  void write_bit_(uint8_t high, uint8_t clock);

#ifdef ESP8266
  // ESP8266 timer can accept callback with no parameters, so we have this hack to save a static instance of OpenTherm
  static OpenTherm* instance;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
#endif
};

}  // namespace esphome::opentherm
