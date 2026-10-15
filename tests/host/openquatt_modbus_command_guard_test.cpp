#include <cassert>
#include <cstdint>
#include <deque>
#include <functional>

#include "components/openquatt_modbus/OpenQuattModbusCommandGuard.h"

namespace {

enum class State { READY, WAITING, TIMED_OUT };
struct Command {
  int id;
  uint16_t value;
  bool protected_write;
  State state;
};

struct Queue {
  std::deque<Command> commands;
  unsigned not_sent{0};
  unsigned offline{0};
  bool allowed{true};
  std::function<void()> on_not_sent;

  Command* select() {
    for (auto& command : commands) {
      if (command.state == State::READY) return &command;
    }
    return nullptr;
  }
  bool guard() {
    return esphome::openquatt_modbus::detail::guard_next_command(
        commands.size(), [this]() { return select(); },
        [this](const Command& command) { return !command.protected_write || command.value == 0 || allowed; },
        [this](Command& command) {
          const int id = command.id;
          for (auto it = commands.begin(); it != commands.end(); ++it) {
            if (it->id == id) {
              commands.erase(it);
              break;
            }
          }
          ++not_sent;
          if (on_not_sent) on_not_sent();
        });
  }
};

}  // namespace

int main() {
  // Initially valid, then revoked while queued. A stop behind it still dispatches.
  Queue queue;
  queue.commands = {{1, 17, true, State::READY}, {2, 0, true, State::READY}};
  assert(queue.guard() && queue.select()->id == 1);
  queue.allowed = false;
  assert(queue.guard() && queue.select()->id == 2);
  assert(queue.not_sent == 1 && queue.offline == 0);

  // Unrelated reads/writes continue; the helper cannot retire an in-flight frame.
  queue.commands = {{3, 17, true, State::WAITING}, {4, 100, false, State::READY}};
  assert(queue.guard() && queue.select()->id == 4);
  assert(queue.commands.front().state == State::WAITING);
  assert(queue.not_sent == 1);

  // Lost ACK grants a retry. Requeue does not preserve earlier permission.
  queue.commands = {{5, 17, true, State::TIMED_OUT}};
  assert(!queue.guard());
  queue.commands.front().state = State::READY;
  assert(!queue.guard() && queue.commands.empty());
  assert(queue.not_sent == 2 && queue.offline == 0);

  // Terminal callback changes policy and queue: reselect/revalidate afterwards.
  queue.commands = {{6, 17, true, State::READY}, {7, 10, true, State::READY}};
  queue.on_not_sent = [&]() { queue.allowed = true; };
  assert(queue.guard() && queue.select()->id == 7);
  assert(queue.not_sent == 3);

  // A callback that resubmits denied work must not monopolize the main loop.
  queue.allowed = false;
  queue.commands = {{8, 17, true, State::READY}};
  queue.on_not_sent = [&]() { queue.commands.push_back({8, 17, true, State::READY}); };
  assert(!queue.guard());
  assert(queue.not_sent == 4 && queue.commands.size() == 1);

  // Multiple stale writes cannot block a later safe stop.
  queue.on_not_sent = {};
  queue.commands = {{9, 17, true, State::READY}, {10, 18, true, State::READY}, {11, 0, true, State::READY}};
  assert(queue.guard() && queue.select()->id == 11);
  assert(queue.not_sent == 6);
  return 0;
}
