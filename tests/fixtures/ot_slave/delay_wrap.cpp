// Production process()/RX-state transition bodies run verbatim. Hardware,
// decoder and clock boundaries are deterministic probes, not a second DELAY
// predicate. The decoder itself has its own existing host regression suite.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>

#define OQ_OT_RMT_SUPPORTED 1
#include "production_ot_enums.inc"

namespace {
constexpr uint32_t kCyclesPerUs = 240;
constexpr uint32_t OT_FRAME_DELAY_US = 100000;
constexpr uint32_t kDelayCycles = OT_FRAME_DELAY_US * kCyclesPerUs;
uint32_t clock_cycles = 0;
uint64_t clock_us = 0;
uint32_t now_cycles() { return clock_cycles; }
int64_t esp_timer_get_time() { return static_cast<int64_t>(clock_us); }
}  // namespace

class OpenTherm {
 public:
  struct RxFrame {
    uint32_t done_ts_cycles;
    uint8_t symbol_count;
    unsigned long decoded;
    bool valid = true;
  };
  OpenThermStatus status = OpenThermStatus::READY;
  unsigned long response = 0, responseTimestamp = 0;
  OpenThermResponseStatus responseStatus = OpenThermResponseStatus::NONE;
  uint32_t cycles_per_us_ = kCyclesPerUs;
  uint32_t delay_until_ts_ = 0;
  bool edge_overflow_ = false, tx_in_progress_ = false, tx_complete_ = false, isSlave = true;
  void (*processResponseCallback)(unsigned long, OpenThermResponseStatus, void*) = nullptr;
  void* pCallbackUser = nullptr;
  std::deque<RxFrame> queued;
  unsigned long last_tx = 0;
  unsigned idle_calls = 0;
  static constexpr size_t PROCESS_MAX_RX_FRAMES = 2;
  static constexpr uint32_t PROCESS_BUDGET_US = 250;
  bool process();
  bool sendResponse(unsigned long);
  void process_rmt_frame_(const RxFrame&);
  bool pop_rx_frame_(RxFrame& frame) {
    if (queued.empty()) return false;
    frame = queued.front();
    queued.pop_front();
    return true;
  }
  bool decode_rmt_frame_(const RxFrame& frame, unsigned long& decoded, OpenThermDriverError& error) {
    decoded = frame.decoded;
    error = frame.valid ? OpenThermDriverError::DRIVER_ERROR_NONE : OpenThermDriverError::DRIVER_ERROR_TIMING_INVALID;
    return frame.valid;
  }
  bool isValidParity(unsigned long) { return true; }
  bool isValidRequest(unsigned long) { return true; }
  bool isValidResponse(unsigned long) { return true; }
  bool queue_frame_tx_(unsigned long frame) {
    last_tx = frame;
    tx_in_progress_ = true;
    tx_complete_ = false;
    return true;
  }
  void setIdleState() { ++idle_calls; }
};

// The production process() also includes a fixed critical section around the
// overflow flag. No concurrent ISR runs in this clock/state-machine fixture.
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
#include "production_ot_process.inc"

namespace {
void poll(OpenTherm& ot, uint32_t start, uint32_t elapsed) {
  clock_cycles = start + elapsed;
  clock_us += 1000;
  ot.process();
}
OpenTherm delayed(uint32_t start) {
  OpenTherm ot;
  clock_cycles = start;
  clock_us = 1000000;
  ot.status = OpenThermStatus::RESPONSE_READY;
  ot.responseTimestamp = start;
  ot.response = 0x1014500UL;
  ot.process();
  assert(ot.status == OpenThermStatus::DELAY);
  return ot;
}
void boundary(uint32_t start) {
  auto ot = delayed(start);
  for (uint32_t elapsed : {0U, 1U, kDelayCycles - 1U, kDelayCycles}) {
    poll(ot, start, elapsed);
    assert(ot.status == OpenThermStatus::DELAY && "DELAY released before its existing strict 100ms boundary");
  }
  poll(ot, start, kDelayCycles + 1U);
  assert(ot.status == OpenThermStatus::READY && "completed DELAY did not release");
}
struct Callback {
  OpenTherm* ot;
  unsigned calls = 0;
  unsigned long request = 0;
  bool transmit = false;
};
void received(unsigned long request, OpenThermResponseStatus status, void* context) {
  auto& capture = *static_cast<Callback*>(context);
  assert(status == OpenThermResponseStatus::SUCCESS);
  ++capture.calls;
  capture.request = request;
  if (capture.transmit) capture.ot->sendResponse(0x4014500UL);
}
void rx_overwrite(bool transmit) {
  constexpr uint32_t start = std::numeric_limits<uint32_t>::max() - 36000000U + 1U;
  auto ot = delayed(start);
  Callback capture{&ot, 0, 0, transmit};
  ot.processResponseCallback = received;
  ot.pCallbackUser = &capture;
  constexpr uint32_t rx_at = start + 48000000U;
  constexpr unsigned long request = 0x1015200UL;
  ot.queued.push_back({rx_at, 1, request});
  clock_cycles = rx_at;
  clock_us += 200000;
  assert(ot.process());
  assert(capture.calls == 1 && capture.request == request && ot.queued.empty());
  if (transmit) {
    assert(ot.status == OpenThermStatus::REQUEST_SENDING && ot.last_tx == 0x4014500UL);
    ot.tx_complete_ = true;
    poll(ot, rx_at, 10000U * kCyclesPerUs);
    assert(ot.status == OpenThermStatus::READY && ot.idle_calls == 1);
  } else {
    assert(ot.status == OpenThermStatus::DELAY);
    poll(ot, rx_at, kDelayCycles);
    assert(ot.status == OpenThermStatus::DELAY);
    poll(ot, rx_at, kDelayCycles + 1U);
    assert(ot.status == OpenThermStatus::READY);
    assert(capture.calls == 1);
  }
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string scenario = argv[1];
  constexpr uint32_t maximum = std::numeric_limits<uint32_t>::max();
  if (scenario == "normal")
    boundary(120000000U);
  else if (scenario == "deadline_wrap")
    boundary(maximum - 12000000U + 1U);
  else if (scenario == "poll_wrap") {
    constexpr uint32_t start = maximum - 36000000U + 1U;
    auto ot = delayed(start);
    poll(ot, start, 48000000U);
    assert(ot.status == OpenThermStatus::READY && "completed DELAY stuck after cycle wrap");
  } else if (scenario == "zero_start")
    boundary(0);
  else if (scenario == "zero_boundary")
    boundary(maximum - kDelayCycles + 1U);
  else if (scenario == "rx_overwrite")
    rx_overwrite(false);
  else if (scenario == "callback_tx")
    rx_overwrite(true);
  else
    assert(false);
}
