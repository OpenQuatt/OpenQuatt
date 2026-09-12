#pragma once

#ifdef USE_ESP_IDF

#include "soc/uart_reg.h"
#include "esphome/components/uart/uart_component_esp_idf.h"
#include "esphome/core/log.h"

namespace oq_controller_q_uart {

inline void enable_error_character_discard(esphome::uart::IDFUARTComponent* uart, const char* port_name) {
  const uint8_t uart_num = uart->get_hw_serial_number();
  REG_SET_BIT(UART_CONF0_REG(uart_num), UART_ERR_WR_MASK);

  if ((REG_READ(UART_CONF0_REG(uart_num)) & UART_ERR_WR_MASK) == 0) {
    ESP_LOGW("oq.uart_filter", "Controller Q %s UART%u: parity/framing error discard did not enable", port_name,
             uart_num);
    return;
  }

  ESP_LOGI("oq.uart_filter", "Controller Q %s UART%u: parity/framing error discard enabled", port_name, uart_num);
}

}  // namespace oq_controller_q_uart

#endif  // USE_ESP_IDF
