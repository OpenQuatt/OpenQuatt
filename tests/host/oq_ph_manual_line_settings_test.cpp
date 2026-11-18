#include <cassert>

#include "../../openquatt/includes/control/oq_ph_manual_line_settings.h"

using namespace oq_power_house::manual_line;

int main() {
  const Settings original{-10, 7020, 16};
  const Settings proposal{-10, 6570, 16.5};
  const Settings later_manual_edit{-10, 6800, 16.5};
  Receipt durable;
  Settings flash = original;
  Receipt receipt;
  auto save_receipt = [&](const Receipt& value) {
    durable = value;
    return true;
  };
  auto save_pair = [&](const Settings& value) {
    flash = value;
    return true;
  };
  auto fail = [](const auto&) { return false; };

  assert(apply(receipt, flash, proposal, fail, save_pair) == Result::STORE_FAILED);
  assert(flash == original && !durable.pending() && !receipt.pending());
  assert(apply(receipt, flash, proposal, save_receipt, [&](const Settings& value) {
           flash.rated_w = value.rated_w;  // Power lost between the two preference writes.
           return false;
         }) == Result::PENDING);
  assert(durable.phase == Phase::APPLYING);
  assert(flash.rated_w == proposal.rated_w && flash.zero_c == original.zero_c);

  auto after_restart = durable;
  assert(finish_pending(after_restart, flash, save_receipt, save_pair) == Result::DONE);
  assert(flash == proposal && durable.can_restore(flash));
  assert(restore(after_restart, later_manual_edit, save_receipt, save_pair) == Result::REFUSED);
  assert(flash == proposal && durable.can_restore(flash));

  assert(restore(
             after_restart, flash,
             [&](const Receipt& value) {
               if (value.phase == Phase::RESTORED) return false;
               return save_receipt(value);
             },
             save_pair) == Result::CONFIRM_FAILED);
  assert(flash == original && durable.phase == Phase::RESTORING);
  after_restart = durable;
  assert(finish_pending(after_restart, flash, save_receipt, save_pair) == Result::DONE);
  assert(flash == original && durable.phase == Phase::RESTORED && !durable.can_restore(flash));
  assert(apply(after_restart, flash, {-8, 6500, 16.5}, save_receipt, save_pair) == Result::REFUSED);

  assert(apply(after_restart, flash, proposal, save_receipt, fail) == Result::PENDING);
  flash = {-9, proposal.rated_w, original.zero_c};
  auto superseded = durable;
  const Settings original_at_new_reference{-9, original.rated_w, original.zero_c};
  assert(finish_pending(superseded, flash, fail, save_pair) == Result::CONFIRM_FAILED);
  assert(flash == original_at_new_reference && durable.phase == Phase::APPLYING);
  assert(finish_pending(superseded, flash, save_receipt, save_pair) == Result::DONE);
  assert(flash == original_at_new_reference && durable.phase == Phase::EMPTY && !superseded.pending());

  flash = proposal;
  Receipt restoring{Receipt::VERSION, Phase::APPLIED, original, proposal};
  assert(restore(restoring, flash, save_receipt, [&](const Settings& value) {
           flash.rated_w = value.rated_w;
           return false;
         }) == Result::PENDING);
  // A normal edit by the corrected adapter first settles the coherent live pair.
  assert(cancel_pending(restoring, proposal, save_receipt, save_pair) == Result::DONE);
  flash.zero_c = 18;
  assert(flash.rated_w == proposal.rated_w && !durable.pending());

  flash = proposal;
  Receipt retry{Receipt::VERSION, Phase::APPLIED, original, proposal};
  assert(restore(
             retry, flash, [&](const Receipt& value) { return value.phase != Phase::RESTORED && save_receipt(value); },
             save_pair) == Result::CONFIRM_FAILED);
  assert(flash == original && retry.pending());
  assert(!prepare_manual_edit(retry, original, true, fail, save_pair));
  assert(prepare_manual_edit(retry, original, true, save_receipt, save_pair));
  assert(flash == original && durable.phase == Phase::EMPTY && !retry.pending());
}
