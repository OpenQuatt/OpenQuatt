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

enum class LocalOutsideRoute : uint8_t { NONE = 0, HP1, HP2, COMPOSITE };
enum class LocalOutsideOperation : uint8_t { NONE = 0, MINIMUM, ARITHMETIC_MEAN };

struct LocalOutsideSelection {
  LocalOutsideRoute route = LocalOutsideRoute::NONE;
  LocalOutsideOperation operation = LocalOutsideOperation::NONE;
  bool valid = false;
  RawFloatReceipt hp1_receipt;
  RawFloatReceipt hp2_receipt;

  void observe(LocalOutsideRoute next_route, LocalOutsideOperation next_operation, bool next_valid,
               const RawFloatReceipt& next_hp1_receipt, const RawFloatReceipt& next_hp2_receipt = {}) {
    route = next_route;
    operation = next_operation;
    valid = next_valid;
    hp1_receipt = next_hp1_receipt;
    hp2_receipt = next_hp2_receipt;
  }
};

inline LocalOutsideSelection local_outside_selection{};

#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q
inline RawFloatReceipt controller_flow{};
#endif

}  // namespace oq_sources
