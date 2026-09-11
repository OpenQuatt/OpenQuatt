#pragma once

#ifdef USE_ESP32

#include "driver/uart.h"
#include "driver/uart_select.h"
#include "hal/uart_ll.h"
#ifdef USE_ESP_IDF
#include "soc/uart_reg.h"
#endif
#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace oq_modbus_uart_diag {

static volatile uint32_t parity_error_count = 0;
static volatile uint32_t frame_error_count = 0;
static volatile uint32_t other_error_count = 0;
static volatile uint32_t last_error_status = 0;
#ifdef USE_ESP_IDF
static volatile uint32_t last_error_conf0 = 0;
static volatile uint8_t last_error_uart_num = 0;
static volatile bool last_error_conf0_valid = false;
#endif

#ifdef USE_ESP_IDF
inline uint32_t read_conf0(uint8_t uart_num) { return REG_READ(UART_CONF0_REG(uart_num)); }

inline bool err_wr_mask_enabled(uint32_t conf0) { return (conf0 & UART_ERR_WR_MASK) != 0; }

inline void log_conf0_info(uint8_t uart_num, const char* label, uint32_t conf0) {
  ESP_LOGI("oq.modbus_uart_err", "UART%u %s: 0x%08X ERR_WR_MASK=%u", uart_num, label, static_cast<unsigned>(conf0),
           err_wr_mask_enabled(conf0));
}

inline void log_conf0_warn(uint8_t uart_num, const char* label, uint32_t conf0) {
  ESP_LOGW("oq.modbus_uart_err", "UART%u %s: 0x%08X ERR_WR_MASK=%u", uart_num, label, static_cast<unsigned>(conf0),
           err_wr_mask_enabled(conf0));
}
#endif

static void IRAM_ATTR uart_cb(uart_port_t uart_num, uart_select_notif_t notification, BaseType_t* task_woken) {
  if (notification == UART_SELECT_READ_NOTIF) {
    esphome::Application::wake_loop_isrsafe(task_woken);
    return;
  }

  if (notification != UART_SELECT_ERROR_NOTIF) {
    return;
  }

#ifdef USE_ESP_IDF
  // Preserve the peripheral state at the error interrupt; report it later from the main loop.
  last_error_conf0 = read_conf0(static_cast<uint8_t>(uart_num));
  last_error_uart_num = static_cast<uint8_t>(uart_num);
  last_error_conf0_valid = true;
#endif
  const uint32_t status = uart_ll_get_intsts_mask(UART_LL_GET_HW(uart_num));
  last_error_status = status;

  bool classified = false;
  if ((status & UART_INTR_PARITY_ERR) != 0) {
    parity_error_count++;
    classified = true;
  }
  if ((status & UART_INTR_FRAM_ERR) != 0) {
    frame_error_count++;
    classified = true;
  }
  if (!classified) {
    other_error_count++;
  }

  esphome::Application::wake_loop_isrsafe(task_woken);
}

inline void set_discard_bad_bytes(uint8_t uart_num, bool discard = true) {
#ifdef USE_ESP_IDF
  const uint32_t conf0_before = read_conf0(uart_num);
  log_conf0_info(uart_num, "CONF0 before filter", conf0_before);

  if (discard) {
    REG_SET_BIT(UART_CONF0_REG(uart_num), UART_ERR_WR_MASK);
  } else {
    REG_CLR_BIT(UART_CONF0_REG(uart_num), UART_ERR_WR_MASK);
  }

  const uint32_t conf0_after = read_conf0(uart_num);
  log_conf0_info(uart_num, "CONF0 after filter", conf0_after);
#else
  (void)uart_num;
  (void)discard;
  ESP_LOGW("oq.modbus_uart_err", "UART parity/framing discard filter requires ESP-IDF");
#endif
}

inline void install(uint8_t uart_num) {
  uart_set_select_notif_callback(static_cast<uart_port_t>(uart_num), uart_cb);
  ESP_LOGI("oq.modbus_uart_err", "UART%u selected for uart_bus; parity/frame error probe installed", uart_num);
}

inline void report_startup_filter_state(uint8_t uart_num) {
#ifdef USE_ESP_IDF
  const uint32_t conf0 = read_conf0(uart_num);
  log_conf0_info(uart_num, "filter state after startup", conf0);
#else
  (void)uart_num;
#endif
}

inline void report_if_changed() {
  static uint32_t reported_parity = 0;
  static uint32_t reported_frame = 0;
  static uint32_t reported_other = 0;

  const uint32_t parity = parity_error_count;
  const uint32_t frame = frame_error_count;
  const uint32_t other = other_error_count;
  if (parity == reported_parity && frame == reported_frame && other == reported_other) {
    return;
  }

  ESP_LOGW("oq.modbus_uart_err", "UART RX errors: parity=%u frame=%u other=%u", parity, frame, other);
  ESP_LOGW("oq.modbus_uart_err", "Last UART status=0x%08X", static_cast<unsigned>(last_error_status));
#ifdef USE_ESP_IDF
  if (last_error_conf0_valid) {
    const uint32_t conf0_at_error = last_error_conf0;
    const uint8_t uart_num = last_error_uart_num;
    const uint32_t conf0_now = read_conf0(uart_num);
    log_conf0_warn(uart_num, "CONF0 at error", conf0_at_error);
    log_conf0_warn(uart_num, "CONF0 now", conf0_now);
  }
#endif

  reported_parity = parity;
  reported_frame = frame;
  reported_other = other;
}

}  // namespace oq_modbus_uart_diag

#endif  // USE_ESP32
