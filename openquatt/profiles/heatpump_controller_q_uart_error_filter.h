#pragma once

#ifdef USE_ESP_IDF

#include "driver/uart.h"
#include "soc/uart_reg.h"
#include "esphome/components/uart/uart_component_esp_idf.h"
#include "esphome/core/log.h"

namespace oq_controller_q_uart {

inline bool error_character_discard_enabled(uint8_t uart_num) {
  return (REG_READ(UART_CONF0_REG(uart_num)) & UART_ERR_WR_MASK) != 0;
}

inline bool error_character_discard_enabled(esphome::uart::IDFUARTComponent* uart) {
  return error_character_discard_enabled(uart->get_hw_serial_number());
}

inline void enable_error_character_discard_all() {
  for (uint8_t uart_num = 0; uart_num < UART_NUM_MAX; uart_num++) {
    REG_SET_BIT(UART_CONF0_REG(uart_num), UART_ERR_WR_MASK);
    if (!error_character_discard_enabled(uart_num)) {
      ESP_LOGW("oq.uart_filter", "Controller Q UART%u: parity/framing error discard did not enable", uart_num);
      continue;
    }

    ESP_LOGI("oq.uart_filter", "Controller Q UART%u: parity/framing error discard enabled", uart_num);
  }
}

}  // namespace oq_controller_q_uart

#endif  // USE_ESP_IDF
