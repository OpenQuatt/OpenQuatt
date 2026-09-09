#include <assert.h>
#include <math.h>
#include <stdint.h>

#include "../../openquatt/includes/sources/oq_raw_receipt.h"

int main() {
  oq_sources::RawFloatReceipt receipt;
  assert(!receipt.received && !receipt.valid);
  receipt.observe(12.5f, 100U, true);
  assert(receipt.received && receipt.valid && receipt.value == 12.5f);
  assert(receipt.fresh(200U, 100U));
  assert(!receipt.fresh(201U, 100U));
  receipt.invalidate();
  assert(receipt.received && !receipt.valid && receipt.value == 12.5f && receipt.received_ms == 100U);
  receipt.revoke(250U);
  assert(receipt.received && !receipt.valid);
  assert(!receipt.fresh(251U, 1000U));
  receipt.observe(20.0f, static_cast<uint64_t>(UINT32_MAX) + 100U, true);
  assert(receipt.fresh(static_cast<uint64_t>(UINT32_MAX) + 120U, 20U));
  assert(!receipt.fresh(static_cast<uint64_t>(UINT32_MAX) + 121U, 20U));
  assert(!receipt.fresh(static_cast<uint64_t>(UINT32_MAX) + 99U, 100U));
  assert(!receipt.fresh(static_cast<uint64_t>(UINT32_MAX) + 120U, 0U));
  receipt.observe(NAN, 30U, false);
  assert(!receipt.valid && receipt.value != 20.0f);
  return 0;
}
