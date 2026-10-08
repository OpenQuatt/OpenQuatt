#include <array>
#include <cassert>
#include <functional>
#include <span>
#include "includes/control/oq_defrost_logic.h"

#define ESP_LOGI(...) ((void)0)
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)

namespace esphome {
uint32_t clock_now{1000};
uint32_t millis() { return clock_now; }
namespace openquatt_web_auth {
bool allowed{true};
bool normal_web_access_allowed() { return allowed; }
}  // namespace openquatt_web_auth
namespace modbus {
enum class ExceptionCode { ILLEGAL_DATA_ADDRESS };
class ModbusClientDevice {
 public:
  int responses{0};
  void on_response(std::span<const uint8_t>, std::span<const uint8_t>) { ++responses; }
};
}  // namespace modbus
namespace openquatt_odu_defrost {
struct Auth {
  void* context{nullptr};
  bool (*callback)(void*){nullptr};
  bool add_restart_blocker(void* owner, bool (*check)(void*)) {
    context = owner;
    callback = check;
    return true;
  }
  bool blocked() { return callback(context); }
};
class OpenQuattOduDefrost : public modbus::ModbusClientDevice {
 public:
  Auth auth;
  Auth* auth_{&auth};
  bool action_pending_{false}, loading_{false}, trigger_ready_{false}, save_ready_{false};
  bool save_writing_{false}, save_verifying_{false}, forced_write_pending_{false};
  uint32_t forced_write_ms_{0};
  bool queue_ok{true}, failed{false};
  unsigned writes{0}, queue_clears{0};
  oq_defrost::Cycle cycle;
  struct Parameters {
    int mode() { return 2; }
  } parameters_;
  std::function<void()> before_write;
  void mark_failed() { failed = true; }
  void register_blocker() {
    // PRODUCTION_REGISTRATION
  }
  void tick_timeout() {
    // PRODUCTION_TIMEOUT
  }
  void clear_tx_queue_for_address() { ++queue_clears; }
  void clear_tx_queue_for_device() { ++queue_clears; }
  bool write_single_register(uint16_t address, uint16_t value) {
    assert(address == 3999 && value == 4);
    if (before_write) before_write();
    ++writes;
    return queue_ok;
  }
  void reject(const char* reason) { cycle.result = reason; }
  void fail_(const char* reason) { cycle.result = reason; }
  bool send_forced_once(uint32_t now);
  void on_response(std::span<const uint8_t> request, std::span<const uint8_t> response);
  void on_not_sent(std::span<const uint8_t> request);
  bool on_no_response(std::span<const uint8_t> request);
  void on_error(std::span<const uint8_t> request, modbus::ExceptionCode error);
};
// PRODUCTION_METHODS
}  // namespace openquatt_odu_defrost
}  // namespace esphome

int main() {
  using namespace esphome;
  using namespace esphome::openquatt_odu_defrost;
  const std::array<uint8_t, 5> forced{0x06, 0x0f, 0x9f, 0, 4};
  const std::array<uint8_t, 5> unrelated{0x06, 0x0f, 0x9e, 0, 4};
  const std::array<uint8_t, 5> read{0x03, 0x0f, 0x9f, 0, 1};
  const std::array<uint8_t, 4> short_request{0x06, 0x0f, 0x9f, 0};
  for (int terminal = 0; terminal < 5; ++terminal) {
    OpenQuattOduDefrost target;
    target.register_blocker();
    assert(!target.failed && !target.auth.blocked());
    target.before_write = [&]() { assert(target.auth.blocked()); };
    assert(target.send_forced_once(1000));
    assert(target.auth.blocked() && target.writes == 1);
    target.on_response(unrelated, unrelated);
    target.on_response(read, read);
    target.on_response(short_request, short_request);
    target.on_not_sent(unrelated);
    target.on_error(unrelated, modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
    assert(!target.on_no_response(unrelated));
    assert(target.auth.blocked());
    if (terminal == 0)
      target.on_response(forced, forced);
    else if (terminal == 1)
      target.on_not_sent(forced);
    else if (terminal == 2)
      assert(!target.on_no_response(forced));
    else if (terminal == 3)
      target.on_error(forced, modbus::ExceptionCode::ILLEGAL_DATA_ADDRESS);
    else {
      clock_now = 30999;
      target.tick_timeout();
      assert(target.auth.blocked());
      const unsigned clears = target.queue_clears;
      clock_now = 31000;
      target.tick_timeout();
      assert(target.queue_clears == clears + 1 && std::strcmp(target.cycle.result, "WRITE_UNCERTAIN") == 0);
    }
    assert(!target.auth.blocked() && target.writes == 1);
  }
  OpenQuattOduDefrost queue_failed;
  queue_failed.register_blocker();
  queue_failed.queue_ok = false;
  assert(!queue_failed.send_forced_once(1000));
  assert(!queue_failed.auth.blocked() && std::strcmp(queue_failed.cycle.result, "WRITE_FAILED") == 0);
  OpenQuattOduDefrost closed;
  closed.register_blocker();
  openquatt_web_auth::allowed = false;
  assert(!closed.send_forced_once(1000));
  assert(closed.writes == 0 && !closed.auth.blocked());
}
