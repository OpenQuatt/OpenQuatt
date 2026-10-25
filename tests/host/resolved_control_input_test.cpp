#include <cassert>
#include "openquatt/includes/sources/oq_resolved_control_input.h"

int main() {
  using namespace oq_sources;
  RawFloatReceipt receipt;
  receipt.observe(19.0f, 1000, true);
  assert(current_receipt_matches(receipt, 19.0f));
  receipt.observe(19.0005f, 2000, true);
  assert(current_receipt_matches(receipt, 19.0f));
  assert(!current_receipt_matches(receipt, 20.0f));
  auto selected = selected_source(18.0f, true, LearningSourceRoute::CIC_ROOM, 7);
  // Valid new producer publications need not equal the slower selected sensor.
  auto input = control_input(selected, 18.0f, current_receipt_matches(receipt, 19.0f));
  assert(input.valid && input.fresh && !input.held && input.value == 18.0f);
  assert(input.configuration_generation == 7);
  receipt.observe(NAN, 3000, false);
  input = control_input(selected, 18.0f, current_receipt_matches(receipt, 19.0f));
  assert(input.valid && !input.fresh);  // Public entity retained; current field missing.
  receipt.observe(19.0f, 4000, true);
  assert(control_input(selected, 18.0f, current_receipt_matches(receipt, 19.0f)).fresh);
  receipt.received_ms = 0;
  assert(!current_receipt_matches(receipt, 19.0f));
  assert(!control_input(selected, 17.0f, true).valid);
  assert(!control_input({}, 18.0f, true).valid);
  selected.provenance = LearningSourceProvenance::HELD;
  input = control_input(selected, 18.0f, true);
  assert(input.valid && input.held && !input.fresh);
}
