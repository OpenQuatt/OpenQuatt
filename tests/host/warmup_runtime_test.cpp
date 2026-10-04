#include <cassert>
#include <cmath>
#include <cstring>
#include "openquatt/includes/control/oq_warmup_runtime.h"

int main() {
  oq_warmup_runtime::Runtime runtime;
  oq_warmup::Input input{1000, true, true, true, 1, 1, 3, 17.0f, 17.0f, 0.2f};
  oq_warmup::Settings settings;
  runtime.update(input, settings);
  input.requested_c = 20.0f;
  input.now_ms += 1000;
  runtime.update(input, settings);
  assert(runtime.active());
  const float target = runtime.effective_target();
  assert(runtime.effective_target(16.0f) == 16.0f);
  assert(runtime.active() && runtime.effective_target() == target);
  // Timers, room values and edges cannot be processed by reads.
  input.now_ms += settings.step_ms;
  input.room_c = target;
  for (int i = 0; i < 100; ++i) {
    assert(runtime.active() && runtime.effective_target() == target);
    assert(runtime.offset() == settings.step_c);
    assert(std::strcmp(runtime.status(), "Warming") == 0);
  }
  runtime.update(input, settings);
  assert(runtime.effective_target() > target);
  runtime.invalidate(oq_warmup::Status::SOURCE_CHANGED);
  assert(!runtime.active() && runtime.effective_target() == 20.0f && runtime.offset() == 0.0f);
  runtime.update(input, settings);
  assert(!runtime.active());  // A reset establishes a baseline, never replays a raise.
  input.requested_c = 17.0f;
  runtime.update(input, settings);
  input.requested_c = 20.0f;
  runtime.update(input, settings);
  assert(runtime.active());
  input.fresh = false;
  runtime.update(input, settings);
  assert(!runtime.active());
  input.fresh = true;
  runtime.update(input, settings);
  assert(!runtime.active());
}
