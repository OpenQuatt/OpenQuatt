#include <assert.h>

#include "../../openquatt/includes/control/oq_incident_actuator_logic.h"

int main() {
  using oq_incident_actuator::Action;
  using oq_incident_actuator::apply_start_gate_before_active_write;
  using oq_incident_actuator::apply_stop_notification_before_safe_write;
  using oq_incident_actuator::decide;
  using oq_incident_actuator::Inputs;
  using oq_incident_actuator::requires_stop_notification;
  using oq_incident_actuator::safe_stop_write_retry_due;

  auto decision = decide(Inputs{5, 0, true, false});
  assert(decision.action == Action::FOLLOW_REQUEST);
  assert(decision.guarded_level == 5);

  decision = decide(Inputs{5, 0, false, false});
  assert(decision.action == Action::BLOCK_NEW_START);
  assert(decision.guarded_level == 0);
  assert(!decision.bypass_runtime_and_defrost_holds);

  // A short link suspect may block a new start, but must not stop an already
  // running unit unless the incident manager explicitly asserts must_stop.
  decision = decide(Inputs{5, 5, false, false});
  assert(decision.action == Action::FOLLOW_REQUEST);
  assert(decision.guarded_level == 5);

  decision = decide(Inputs{0, 5, false, true});
  assert(decision.action == Action::FORCE_STOP);
  assert(decision.guarded_level == 0);
  assert(decision.bypass_runtime_and_defrost_holds);

  decision = decide(Inputs{5, 5, true, true});
  assert(decision.action == Action::FORCE_STOP);
  assert(decision.guarded_level == 0);

  int start_gate_calls = 0;
  int active_writes = 0;
  int safe_writes = 0;
  const bool denied = apply_start_gate_before_active_write(
      true,
      [&]() {
        ++start_gate_calls;
        return false;
      },
      [&]() { ++active_writes; }, [&]() { ++safe_writes; });
  assert(!denied);
  assert(start_gate_calls == 1);
  assert(active_writes == 0);
  assert(safe_writes == 1);

  int sequence = 0;
  int stop_notification_order = 0;
  int safe_write_order = 0;
  apply_stop_notification_before_safe_write(
      true, [&]() { stop_notification_order = ++sequence; }, [&]() { safe_write_order = ++sequence; });
  assert(stop_notification_order == 1);
  assert(safe_write_order == 2);

  assert(!safe_stop_write_retry_due(false, 10000U, 0U, 10000U));
  assert(safe_stop_write_retry_due(true, 10000U, 0U, 10000U));
  assert(!safe_stop_write_retry_due(true, 19999U, 10000U, 10000U));
  assert(safe_stop_write_retry_due(true, 20000U, 10000U, 10000U));
  assert(safe_stop_write_retry_due(true, 5U, UINT32_MAX - 10U, 10U));

  assert(!requires_stop_notification(false, false, true, false));
  assert(requires_stop_notification(true, false, true, false));
  assert(requires_stop_notification(false, true, false, false));
  assert(requires_stop_notification(false, false, false, true));
  return 0;
}
