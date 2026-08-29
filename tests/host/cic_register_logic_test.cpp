#include <assert.h>
#include <math.h>
#include <stddef.h>

#include "../../openquatt/includes/protocol/oq_cic_register_logic.h"

int main() {
  assert(oq_cic::gated(false, 42u) == 0u);
  assert(oq_cic::gated(true, 42u) == 42u);
  assert(oq_cic::FIRMWARE_FALLBACK == 0x011Eu);
  assert(oq_cic::scaled(NAN) == 0u);
  assert(oq_cic::scaled(NAN, 1.0f, oq_cic::FIRMWARE_FALLBACK) == 0x011Eu);
  assert(oq_cic::scaled(12.49f) == 12u);
  assert(oq_cic::scaled(12.5f) == 13u);
  assert(oq_cic::scaled(-0.5f) == oq_cic::UNAVAILABLE);
  assert(oq_cic::scaled(4.25f, 10.0f) == 43u);
  assert(oq_cic::temperature(NAN) == 0u);
  assert(oq_cic::temperature(-30.0f) == 0u);
  assert(oq_cic::temperature(0.0f) == 3000u);
  assert(oq_cic::temperature(21.25f) == 5125u);
  assert(oq_cic::flow(NAN) == 0u);
  assert(oq_cic::flow(0.618f) == 1u);
  assert(oq_cic::flow(0.926f) == 1u);
  assert(oq_cic::flow(0.927f) == 2u);
  assert(oq_cic::flow(12.36f) == 20u);

  assert(oq_cic::on_option("Off") == 0u);
  assert(oq_cic::on_option("On") == 1u);
  assert(oq_cic::on_option(nullptr) == 0u);
  assert(oq_cic::on_option("On", 0x1000u) == 0x1000u);
  assert(oq_cic::boolean(false) == 0u);
  assert(oq_cic::boolean(true) == 1u);
  assert(oq_cic::working_mode("Cooling") == 1u);
  assert(oq_cic::working_mode("Heating") == 2u);
  assert(oq_cic::working_mode("Standby") == 0u);
  assert(oq_cic::working_mode(nullptr) == 0u);
  assert(oq_cic::compressor_level("") == 0u);
  assert(oq_cic::compressor_level(nullptr) == 0u);
  assert(oq_cic::compressor_level("3") == 3u);
  assert(oq_cic::compressor_level("invalid") == 0u);
  assert(oq_cic::compressor_level("-1") == oq_cic::UNAVAILABLE);

  constexpr uint16_t masks[] = {0x0001u, 0x0004u, 0x0008u, 0x0010u, 0x0020u, 0x0040u, 0x0800u};
  for (size_t active = 0; active < 7; ++active) {
    bool flags[7] = {};
    flags[active] = true;
    assert(oq_cic::status_flags(flags[0], flags[1], flags[2], flags[3], flags[4], flags[5], flags[6]) == masks[active]);
  }
  assert(oq_cic::status_flags(true, true, true, true, true, true, true) == 0x087Du);
  return 0;
}
