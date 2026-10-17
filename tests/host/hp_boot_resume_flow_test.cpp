#include "openquatt/includes/control/oq_hp_boot_resume_flow.h"

#include <cassert>
#include <initializer_list>

int main() {
  using namespace oq_input_source;
  oq_sources::RawFloatReceipt hp1, hp2, controller;
  hp1.observe(650, 10000, true);
  hp2.observe(650, 10000, true);
  controller.observe(800, 10000, true);
  oq_boot_resume::FlowReceipts receipts{hp1, hp2, controller, false, true};
  FlowSelection selected{650, FlowRoute::HP1, true};
  const auto fresh = [&](uint64_t now) { return oq_boot_resume::flow_fresh(selected, receipts, now, 15000); };
  assert(fresh(25000));
  assert(!fresh(25001));  // A cached selected value must not renew a physical receipt.
  hp2.revoke(11000);
  assert(fresh(11000));  // The unrelated ODU does not invalidate a selected HP1 flowmeter.
  selected.route = FlowRoute::HP2;
  assert(!fresh(11000));
  selected.route = FlowRoute::AGGREGATE;
  assert(!fresh(11000));  // Both physical contributors must be fresh in Duo.
  hp2.observe(650, 11000, true);
  assert(fresh(11000));
  hp1.revoke(12000);
  assert(!fresh(12000));
  receipts.primary_is_controller = true;
  assert(fresh(12000));  // Q V1 aggregate uses its controller flowmeter.
  selected.route = FlowRoute::HP1;
  assert(!fresh(12000));  // A controller receipt cannot validate the ODU register route.
  selected.route = FlowRoute::CONTROLLER;
  assert(fresh(12000));
  controller.revoke(13000);
  assert(!fresh(13000));
  for (auto route : {FlowRoute::CIC, FlowRoute::PUMPS_STOPPED, FlowRoute::NONE}) {
    selected.route = route;
    assert(!fresh(13000));
  }
  receipts.duo = false;
  receipts.primary_is_controller = false;
  hp1.observe(650, 14000, true);
  hp2.revoke(14000);
  selected.route = FlowRoute::AGGREGATE;
  assert(fresh(14000));
  selected.valid = false;
  assert(!fresh(14000));
}
