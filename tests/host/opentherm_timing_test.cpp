#include <assert.h>
#include <stdint.h>

#include "../../components/opentherm/opentherm_timing.h"

int main() {
  using esphome::opentherm::timing::conversation_end_us;
  using esphome::opentherm::timing::delay_elapsed;
  using esphome::opentherm::timing::elapsed_us;

  assert(elapsed_us(25U, UINT32_MAX - 24U) == 50U);
  assert(!delay_elapsed(99U, 0U, 100U));
  assert(delay_elapsed(100U, 0U, 100U));
  assert(delay_elapsed(25U, UINT32_MAX - 24U, 50U));
  assert(!delay_elapsed(49999U, UINT32_MAX - 49999U, 100000U));
  assert(delay_elapsed(50000U, UINT32_MAX - 49999U, 100000U));

  constexpr uint32_t response_captured_us = 735000U;
  constexpr uint32_t response_deadline_us = 834000U;
  constexpr uint32_t processed_us = 1250000U;
  assert(conversation_end_us(true, response_captured_us, false, response_deadline_us, processed_us) ==
         response_captured_us);
  assert(conversation_end_us(true, response_captured_us, true, response_deadline_us, processed_us) ==
         response_captured_us);
  assert(conversation_end_us(false, 0U, true, response_deadline_us, processed_us) == response_deadline_us);
  assert(conversation_end_us(false, 0U, false, response_deadline_us, processed_us) == processed_us);
  assert(delay_elapsed(processed_us,
                       conversation_end_us(true, response_captured_us, false, response_deadline_us, processed_us),
                       100000U));

  return 0;
}
