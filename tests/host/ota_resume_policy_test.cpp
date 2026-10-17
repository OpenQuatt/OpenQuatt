#include <cassert>
#include <cstdint>
#include "components/openquatt_incident_manager/OpenQuattRestartHandoffPolicy.h"
using esphome::openquatt_incident_manager::restart_handoff::OtaResumePolicy;
int main() {
  OtaResumePolicy p;
  assert(!p.holding(1, 1, 3));
  assert(!p.owns(42));
  p.begin(42, 100, 3);
  assert(p.owns(42) && !p.owns(43));
  assert(p.holding(1, 45099, 3));
  assert(p.holding(2, 45099, 3));
  assert(!p.holding(0, 45099, 3));
  assert(!p.holding(3, 45099, 3));
  assert(p.complete(42, 45099) == 3);
  assert(p.holding(1, 45099, 3));
  assert(!p.holding(1, 45100, 3));
  assert(p.complete(42, 45100) == 0);
  p.reset();
  p.begin(42, 100, 3);
  assert(p.complete(42, 45100) == 0);
  assert(!p.holding(1, 45101, 3));
  p.reset();
  p.begin(42, UINT32_MAX - 100, 1);
  assert(p.holding(1, 200, 1));
  assert(!p.holding(2, 200, 1));
  assert(p.complete(42, 300) == 1);
  assert(!p.holding(1, 400, 0));
  assert(!p.holding(1, 401, 1));  // Permission recovery cannot re-arm.
  p.reset();
  p.begin(42, 100, 3);
  assert(!p.holding(2, 101, 1));  // Loss of one captured HP cancels the session.
  assert(p.complete(42, 102) == 0);
  p.reset();
  p.begin(42, 100, 1);
  assert(p.complete(43, 101) == 0);  // Different transport cannot finish this transfer.
  p.reset();
  p.begin(42, 100, 1);
  p.begin(43, 200, 1);
  assert(p.complete(42, 300) == 0);  // Overlap cannot extend the deadline.
  assert(p.complete(43, 300) == 0);
  p.reset();
  p.begin(42, 100, 1);
  p.cancel();
  assert(!p.holding(1, 101, 1));
  assert(p.complete(42, 102) == 0);
  p.reset();
  p.begin(43, 300, 2);
  assert(p.complete(43, 301) == 2);  // A fresh, explicit transaction after abort works.
  assert(p.complete(43, 302) == 0);  // Completion is one-shot.
  assert(!p.holding(2, 303, 3));
  p.reset();
  p.begin(42, 100, 0);
  assert(p.complete(42, 101) == 0);
}
