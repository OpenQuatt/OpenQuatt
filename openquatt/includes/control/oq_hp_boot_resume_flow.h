#pragma once

#include "oq_input_source_logic.h"
#include "../sources/oq_raw_receipt.h"

namespace oq_boot_resume {
struct FlowReceipts {
  const oq_sources::RawFloatReceipt& hp1;
  const oq_sources::RawFloatReceipt& hp2;
  const oq_sources::RawFloatReceipt& controller;
  bool primary_is_controller;
  bool duo;
};

// The selected flow's learning metadata need not contain receipts. Check the
// physical producers of the selected route instead; never refresh cached data.
inline bool flow_fresh(const oq_input_source::FlowSelection& selected, const FlowReceipts& receipts, uint64_t now,
                       uint64_t maximum_age) {
  if (!selected.valid) return false;
  using oq_input_source::FlowRoute;
  switch (selected.route) {
    case FlowRoute::CONTROLLER:
      return receipts.controller.fresh(now, maximum_age);
    case FlowRoute::HP1:
      return receipts.hp1.fresh(now, maximum_age);
    case FlowRoute::HP2:
      return receipts.hp2.fresh(now, maximum_age);
    case FlowRoute::AGGREGATE: {
      const auto& primary = receipts.primary_is_controller ? receipts.controller : receipts.hp1;
      return primary.fresh(now, maximum_age) && (!receipts.duo || receipts.hp2.fresh(now, maximum_age));
    }
    default:
      // CIC has no physical receipt here; synthesized zero is not live flow.
      return false;
  }
}
}  // namespace oq_boot_resume
