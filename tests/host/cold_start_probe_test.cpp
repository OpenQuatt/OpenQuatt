#include <assert.h>

#include <cstdint>
#include <vector>

#include "../../openquatt/includes/control/oq_cold_start_probe.h"

namespace {
using Bytes = const std::vector<uint8_t>&;
const std::vector<uint8_t> request{3, 0x08, 0x56, 0, 1};
const std::vector<uint8_t> response{3, 2, 0x14, 0x50};  // 5200 -> 22 C through the sensor filters

struct Device {
  virtual ~Device() = default;
  virtual void on_response(Bytes, Bytes) = 0;
  virtual void on_error(Bytes, int) = 0;
  virtual void on_not_sent(Bytes) = 0;
  virtual bool on_no_response(Bytes) = 0;
  void set_parent(void*) {}
  void set_address(uint8_t value) { address = value; }
  void clear_tx_queue_for_device() {
    ++cancels;
    detached = true;
  }
  bool read_holding_registers(uint16_t reg, uint16_t count) {
    assert(reg == 2134 && count == 1);
    ++reads;
    detached = false;
    return accept;
  }
  bool accept = true;
  bool detached = false;
  uint8_t address = 0;
  int reads = 0;
  int cancels = 0;
};
struct Controller {
  void* hub() { return this; }
  uint8_t device_address() { return 1; }
};
struct Sensor {
  int publications = 0;
  float last_raw = 0;
  void publish_state(float value) {
    ++publications;
    last_raw = value;
  }
};
using Probe = oq_cold_start::WaterProbeBase<Device, Controller, Sensor, Bytes, int>;

void test_read_is_bounded_and_uses_existing_sensor_pipeline() {
  Probe probe;
  Controller controller;
  Sensor sensor;
  probe.poll(100, 0, true, &controller, &sensor);
  probe.poll(100, 100, false, &controller, &sensor);
  assert(probe.reads == 0);
  probe.poll(100, 100, true, &controller, &sensor);
  assert(probe.reads == 1 && probe.address == 1);
  // Even a stalled queue cannot accumulate a second frame or owner.
  probe.poll(20000, 100, true, &controller, &sensor);
  assert(probe.reads == 1);
  probe.on_response(request, response);
  assert(sensor.publications == 1 && sensor.last_raw == 5200);
  probe.poll(20001, 100, true, &controller, &sensor);
  assert(probe.reads == 2);
  probe.on_response(request, response);
  probe.poll(20002, 100, true, &controller, &sensor);
  assert(probe.reads == 2);
}

void test_failures_remain_closed_and_retry_is_rate_limited() {
  Probe probe;
  Controller controller;
  Sensor sensor;
  probe.accept = false;
  probe.poll(100, 100, true, &controller, &sensor);
  assert(probe.reads == 1);
  probe.poll(10099, 100, true, &controller, &sensor);
  assert(probe.reads == 1 && sensor.publications == 0);
  probe.accept = true;
  probe.poll(10100, 100, true, &controller, &sensor);
  assert(probe.reads == 2);
  assert(!probe.on_no_response(request));
  probe.poll(20100, 100, true, &controller, &sensor);
  probe.on_error(request, 2);
  probe.poll(30100, 100, true, &controller, &sensor);
  probe.on_not_sent(request);
  assert(sensor.publications == 0);
  probe.poll(40100, 100, true, &controller, &sensor);
  probe.on_response(request, response);
  assert(sensor.publications == 1);
}

void test_invalid_responses_never_refresh_temperature() {
  Controller controller;
  const std::vector<std::vector<uint8_t>> invalid{{},
                                                  {3},
                                                  {3, 2, 0},
                                                  {3, 4, 0x14, 0x50},
                                                  {4, 2, 0x14, 0x50},
                                                  {3, 2, 0x14, 0x50, 0},
                                                  {3, 2, 0, 0},
                                                  {3, 2, 0xFF, 0xFF}};
  for (const auto& bytes : invalid) {
    Probe probe;
    Sensor sensor;
    probe.poll(100, 100, true, &controller, &sensor);
    probe.on_response(request, bytes);
    assert(sensor.publications == 0);
  }
  Probe probe;
  Sensor sensor;
  probe.poll(100, 100, true, &controller, &sensor);
  probe.on_response({3, 0x08, 0x55, 0, 1}, response);
  assert(sensor.publications == 0);
}

void test_cancellation_and_new_session() {
  Probe probe;
  Controller controller;
  Sensor sensor;
  probe.poll(100, 100, true, &controller, &sensor);
  probe.poll(200, 100, false, &controller, &sensor);
  assert(probe.cancels == 1 && probe.detached);
  probe.on_response(request, response);
  assert(sensor.publications == 0);
  probe.poll(300, 300, true, &controller, &sensor);
  assert(probe.reads == 2);
  probe.poll(400, 400, true, &controller, &sensor);
  assert(probe.cancels == 2 && probe.reads == 3);
  probe.on_response(request, response);
  assert(sensor.publications == 1);
  probe.cancel();
  assert(probe.cancels == 2);
}

void test_retry_across_millis_rollover() {
  Probe probe;
  Controller controller;
  Sensor sensor;
  probe.poll(UINT32_MAX - 20, UINT32_MAX - 20, true, &controller, &sensor);
  probe.on_error(request, 2);
  probe.poll(9978, UINT32_MAX - 20, true, &controller, &sensor);
  assert(probe.reads == 1);
  probe.poll(9979, UINT32_MAX - 20, true, &controller, &sensor);
  assert(probe.reads == 2);
}
}  // namespace

int main() {
  test_read_is_bounded_and_uses_existing_sensor_pipeline();
  test_failures_remain_closed_and_retry_is_rate_limited();
  test_invalid_responses_never_refresh_temperature();
  test_cancellation_and_new_session();
  test_retry_across_millis_rollover();
}
