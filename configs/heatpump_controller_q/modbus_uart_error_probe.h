#pragma once

#ifdef USE_ESP32

#include "driver/uart.h"
#include "driver/uart_select.h"
#include "hal/uart_ll.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace oq_modbus_uart_diag {

static volatile uint32_t parity_error_count = 0;
static volatile uint32_t frame_error_count = 0;
static volatile uint32_t other_error_count = 0;
static volatile uint32_t last_error_status = 0;

static void IRAM_ATTR uart_select_callback(uart_port_t uart_num, uart_select_notif_t notification,
                                           BaseType_t *task_woken) {
  if (notification == UART_SELECT_READ_NOTIF) {
    esphome::Application::wake_loop_isrsafe(task_woken);
    return;
  }

  if (notification != UART_SELECT_ERROR_NOTIF) {
    return;
  }

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

  // Make the main loop run promptly so the diagnostic log stays close to the raw UART capture.
  esphome::Application::wake_loop_isrsafe(task_woken);
}

inline void install(uint8_t uart_num) {
  uart_set_select_notif_callback(static_cast<uart_port_t>(uart_num), uart_select_callback);
  ESP_LOGI("oq.modbus_uart_err", "UART parity/frame error probe installed on UART%u", uart_num);
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

  ESP_LOGW("oq.modbus_uart_err",
           "UART RX error: parity=%u (+%u), frame=%u (+%u), other=%u (+%u), last_status=0x%08X",
           parity, parity - reported_parity, frame, frame - reported_frame, other, other - reported_other,
           static_cast<unsigned>(last_error_status));

  reported_parity = parity;
  reported_frame = frame;
  reported_other = other;
}

}  // namespace oq_modbus_uart_diag

#endif  // USE_ESP32
