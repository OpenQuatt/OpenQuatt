#pragma once

#include "oq_raw_receipt.h"

namespace oq_sources {

// Main-loop-owned receipt storage. The callbacks and all current readers run
// on the ESPHome loop task, so these small POD records need no synchronization.
struct HeatPumpReceipts {
  RawFloatReceipt working_mode;
  RawFloatReceipt compressor_frequency;
  RawFloatReceipt status_2108;
  RawFloatReceipt defrost;
  RawFloatReceipt status_2119;
  RawFloatReceipt water_in;
  RawFloatReceipt water_out;
  RawFloatReceipt flow;
  RawFloatReceipt outside;
};

inline HeatPumpReceipts hp1{};

#if OQ_TOPOLOGY_DUO
inline HeatPumpReceipts hp2{};
#endif

#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
inline RawFloatReceipt controller_flow{};
#endif

}  // namespace oq_sources
