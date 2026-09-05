#include <assert.h>
#include <string.h>

#include "../../openquatt/includes/control/oq_daily_window_logic.h"

int main() {
  using namespace oq_daily_window;

  auto output = evaluate(true, true, true, 9 * 60, 9 * 60, 22 * 60);
  assert(output.valid && output.active && output.status == Status::IN_WINDOW);
  output = evaluate(true, true, true, 22 * 60, 9 * 60, 22 * 60);
  assert(output.valid && !output.active && output.status == Status::OUT_OF_WINDOW);
  output = evaluate(true, true, true, 23 * 60, 22 * 60, 7 * 60);
  assert(output.valid && output.active);
  output = evaluate(true, true, true, 7 * 60, 22 * 60, 7 * 60);
  assert(output.valid && !output.active);
  output = evaluate(true, true, true, 12 * 60, 12 * 60, 12 * 60);
  assert(output.valid && !output.active && output.status == Status::WINDOW_DISABLED);

  output = evaluate(false, true, true, 12 * 60, 9 * 60, 22 * 60);
  assert(!output.valid && !output.active && output.status == Status::TIME_INVALID);
  output = evaluate(true, false, true, 12 * 60, 9 * 60, 22 * 60);
  assert(!output.valid && !output.active && output.status == Status::CONFIG_INVALID);
  output = evaluate(true, true, true, 1440, 9 * 60, 22 * 60);
  assert(!output.valid && !output.active && output.status == Status::CONFIG_INVALID);

  assert(strcmp(status_name(Status::TIME_INVALID), "time_invalid") == 0);
  assert(strcmp(status_name(Status::CONFIG_INVALID), "configuration_invalid") == 0);
  assert(strcmp(status_name(Status::WINDOW_DISABLED), "window_disabled") == 0);
  assert(strcmp(status_name(Status::OUT_OF_WINDOW), "out_of_window") == 0);
  assert(strcmp(status_name(Status::IN_WINDOW), "in_window") == 0);
  return 0;
}
