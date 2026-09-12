#pragma once

#ifdef USE_ESP_IDF

#include "driver/uart.h"
#include "soc/uart_reg.h"
#include "esphome/core/log.h"

namespace oq_controller_q_uart {

inline void enable_error_character_discard_all() {
  for (uint8_t uart_num = 0; uart_num < UART_NUM_MAX; uart_num++) {
    REG_SET_BIT(UART_CONF0_REG(uart_num), UART_ERR_WR_MASK);

    const uint32_t conf0 = REG_READ(UART_CONF0_REG(uart_num));
    if ((conf0 & UART_ERR_WR_MASK) == 0) {
      ESP_LOGW("oq.uart_filter", "Controller Q UART%u: parity/framing error discard did not enable", uart_num);
      continue;
    }

    ESP_LOGI("oq.uart_filter", "Controller Q UART%u: parity/framing error discard enabled", uart_num);
  }
}

}  // namespace oq_controller_q_uart

#endif  // USE_ESP_IDF
