// Platform boundary for extracted production MQTT methods. No sockets or sleeps
// proportional to the device watchdog: waits are released by the test thread.
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cinttypes>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "components/openquatt_mqtt_config/OpenQuattMqttConfigPolicy.h"

template <class... Arguments>
void host_log(Arguments&&...) {}
#define ESP_LOGE(...) host_log(__VA_ARGS__)
#define ESP_LOGW(...) host_log(__VA_ARGS__)
#define ESP_LOGI(...) host_log(__VA_ARGS__)
#define ESP_LOGD(...) host_log(__VA_ARGS__)
using namespace std::chrono_literals;
using SemaphoreHandle_t = std::mutex*;
using BaseType_t = int;
using esp_err_t = int;
using esp_event_base_t = const char*;
constexpr int pdTRUE = 1, pdPASS = 1, pdFAIL = 0, ESP_OK = 0, ESP_FAIL = -1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
constexpr int MQTT_TRANSPORT_OVER_TCP = 1;
enum { MQTT_EVENT_ANY, MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED, MQTT_EVENT_DATA, MQTT_EVENT_ERROR };
enum eTaskState { eRunning, eSuspended, eDeleted };

template <class Predicate>
void until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!predicate()) {
    assert(std::chrono::steady_clock::now() < deadline && "controlled host wait timed out");
    std::this_thread::sleep_for(1ms);
  }
}

struct WorkerExit {};
struct FakeTask {
  std::mutex mutex;
  std::condition_variable changed;
  unsigned notifications = 0;
  bool stop = false, external = true, suspended = false;
  std::thread thread;
};
using TaskHandle_t = FakeTask*;
static thread_local TaskHandle_t current_task = nullptr;
static std::atomic<uint32_t> now_ms{10000};
static std::atomic<unsigned> worker_creations{0};
static bool wrong_worker_region = false;
static std::function<void(const char*)> log_hook;

int xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t) {
  mutex->lock();
  return pdTRUE;
}
void xSemaphoreGive(SemaphoreHandle_t mutex) { mutex->unlock(); }
uint32_t pdMS_TO_TICKS(uint32_t value) { return value; }
uint32_t millis() { return now_ms.load(); }
void vTaskDelay(uint32_t value) {
  now_ms.fetch_add(value);
  std::this_thread::yield();
}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) { return 8192; }
void* pxTaskGetStackStart(TaskHandle_t task) { return task; }
bool esp_ptr_external_ram(void* stack) { return static_cast<FakeTask*>(stack)->external; }
eTaskState eTaskGetState(TaskHandle_t task) { return task->suspended ? eSuspended : eRunning; }
void vTaskSuspend(TaskHandle_t task) {
  assert(task);
  task->suspended = true;
}
void vTaskResume(TaskHandle_t task) { task->suspended = false; }
BaseType_t xTaskNotifyGive(TaskHandle_t task) {
  std::lock_guard<std::mutex> lock(task->mutex);
  ++task->notifications;
  task->changed.notify_all();
  return pdPASS;
}
unsigned ulTaskNotifyTake(int clear, uint32_t timeout) {
  assert(current_task && clear == pdTRUE && timeout == portMAX_DELAY);
  auto& task = *current_task;
  std::unique_lock<std::mutex> lock(task.mutex);
  task.changed.wait(lock, [&] { return task.stop || task.notifications; });
  if (task.stop) throw WorkerExit{};
  const unsigned count = task.notifications;
  task.notifications = 0;
  return count;
}
struct StaticTask {
  std::unique_ptr<FakeTask> task;
  ~StaticTask() { deallocate(); }
  bool is_created() const { return bool(task); }
  TaskHandle_t get_handle() { return task.get(); }
  bool create(void (*entry)(void*), const char*, uint32_t, void* argument, int, bool external) {
    assert(!task);
    ++worker_creations;
    task = std::make_unique<FakeTask>();
    task->external = wrong_worker_region ? !external : external;
    auto* handle = task.get();
    task->thread = std::thread([=] {
      current_task = handle;
      try {
        entry(argument);
      } catch (const WorkerExit&) {
      }
      current_task = nullptr;
    });
    return true;
  }
  void deallocate() {
    if (!task) return;
    {
      std::lock_guard<std::mutex> lock(task->mutex);
      task->stop = true;
      task->changed.notify_all();
    }
    task->thread.join();
    task.reset();
  }
};

struct BlockingPoint {
  std::mutex mutex;
  std::condition_variable changed;
  bool enabled = false, entered = false, released = false;
  void hold() {
    std::unique_lock<std::mutex> lock(mutex);
    if (!enabled) return;
    entered = true;
    changed.notify_all();
    assert(changed.wait_for(lock, 5s, [&] { return released; }));
  }
  void wait() {
    std::unique_lock<std::mutex> lock(mutex);
    assert(changed.wait_for(lock, 5s, [&] { return entered; }));
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};
struct FakeClient {
  unsigned id = 0;
};
using esp_mqtt_client_handle_t = FakeClient*;
struct esp_mqtt_event_t {
  esp_mqtt_client_handle_t client = nullptr;
  const char *topic = "offline/input", *data = "12";
  int topic_len = 13, data_len = 2, total_data_len = 2, current_data_offset = 0;
  bool retain = false;
};
using esp_mqtt_event_handle_t = esp_mqtt_event_t*;
struct esp_mqtt_client_config_t {
  struct {
    struct {
      const char* hostname = nullptr;
      uint16_t port = 0;
      int transport = 0;
    } address;
  } broker;
  struct {
    const char *client_id = nullptr, *username = nullptr;
    struct {
      const char* password = nullptr;
    } authentication;
  } credentials;
  struct {
    int keepalive = 0;
    bool disable_clean_session = false;
  } session;
  struct {
    int stack_size = 0;
  } task;
};
static BlockingPoint subscribe_wait, init_wait, stop_wait, idle_wait, pending_clear_wait;
// Pause immediately before the worker clears its advisory pending bit. The
// underlying atomic operation retains its real ordering after the test resumes.
struct PendingFlag : std::atomic<bool> {
  using std::atomic<bool>::atomic;
  void store(bool value, std::memory_order order = std::memory_order_seq_cst) {
    if (!value && current_task) pending_clear_wait.hold();
    std::atomic<bool>::store(value, order);
  }
};
static std::atomic<bool> network_connected{true};
static std::atomic<unsigned> subscribe_calls{0}, stop_calls{0}, init_calls{0}, destroy_calls{0};
static std::atomic<int> stop_failures{0};
static std::array<FakeClient, 8> clients{};
static void worker_only() { assert(current_task && "SDK operation executed on the main loop"); }
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t* config) {
  assert(config->task.stack_size == EXPECTED_MQTT_TASK_STACK);
  worker_only();
  const unsigned index = init_calls.fetch_add(1);
  assert(index < clients.size());
  init_wait.hold();
  clients[index].id = index + 1;
  return &clients[index];
}
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t, int,
                                         void (*)(void*, esp_event_base_t, int32_t, void*), void*) {
  worker_only();
  return ESP_OK;
}
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t) {
  worker_only();
  return ESP_OK;
}
int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t, const char*, int) {
  worker_only();
  ++subscribe_calls;
  subscribe_wait.hold();
  return 1;
}
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t) {
  worker_only();
  ++stop_calls;
  stop_wait.hold();
  if (stop_failures.load() > 0) {
    --stop_failures;
    return ESP_FAIL;
  }
  return ESP_OK;
}
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t) {
  worker_only();
  ++destroy_calls;
  return ESP_OK;
}
esp_err_t esp_mqtt_client_disconnect(esp_mqtt_client_handle_t) {
  worker_only();
  return ESP_OK;
}
const char* esp_err_to_name(int) { return "controlled error"; }

namespace esphome {
struct Application {
  void wake_loop_threadsafe() {}
  std::string get_name() { return "offline-test"; }
} App;
namespace network {
bool is_connected() { return network_connected.load(); }
}  // namespace network
namespace openquatt_mqtt_config {
static const char* const TAG = "offline.mqtt";
static bool time_reached_(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }
static void log_heap_state_(const char* phase) {
  if (log_hook) log_hook(phase);
}
class OpenQuattMqttConfig {
 public:
  ~OpenQuattMqttConfig() { this->client_worker_task_state_.deallocate(); }
  static constexpr size_t BROKER_MAX_LEN = 128, USERNAME_MAX_LEN = 64, PASSWORD_MAX_LEN = 128;
  static constexpr size_t MQTT_CLIENT_ID_MAX_LEN = 96, NUMERIC_INPUT_COUNT = 1;
  // PRODUCTION_MQTT_TASK_STACK
  static constexpr uint32_t MQTT_WORKER_TASK_STACK_SIZE = 24576, MQTT_RECONCILE_RETRY_MS = 5000;
  static constexpr bool MQTT_WORKER_STACK_IN_PSRAM = true;
  static constexpr uint8_t INPUT_MASK_ALL = 0xff, STATEFUL_INPUT_MASK = 0xff;
  struct Input {
    std::string topic = "offline/input";
    const char* log_name = "offline";
  };
  std::array<Input, 1> numeric_inputs_{};
  std::array<Input, 0> binary_inputs_{};
  struct Storage {
    uint8_t enabled = 1, input_disabled_mask = 0, retained_disabled_mask = 0;
    uint16_t port = 1883;
    char broker[BROKER_MAX_LEN + 1] = "offline-broker", username[USERNAME_MAX_LEN + 1]{},
                                 password[PASSWORD_MAX_LEN + 1]{};
  };
  struct ClientConfig {
    bool enabled = true;
    uint16_t port = 1883;
    std::array<char, BROKER_MAX_LEN + 1> broker{};
    std::array<char, USERNAME_MAX_LEN + 1> username{};
    std::array<char, PASSWORD_MAX_LEN + 1> password{};
  };
  enum class ClientReconcileResult : uint8_t { APPLIED, WAIT_FOR_NETWORK, RETRY };
  enum class StorageApplyResult : uint8_t { APPLIED, RUNTIME_PENDING, FAILED };
  std::string broker_ = "offline-broker", username_, password_, config_source_;
  uint16_t port_ = 1883;
  std::mutex runtime_mutex, config_mutex, worker_mutex;
  SemaphoreHandle_t runtime_lock_ = &runtime_mutex, config_lock_ = &config_mutex, worker_lock_ = &worker_mutex;
  StaticTask client_worker_task_state_{};
  bool client_worker_region_valid_ = false, mqtt_client_started_ = true, disconnect_requested_ = false;
  FakeClient initial_client;
  esp_mqtt_client_handle_t mqtt_client_ = &initial_client;
  std::atomic<bool> enabled_{true}, connected_{true}, resubscribe_inputs_{true}, client_events_enabled_{true};
  std::atomic<bool> mqtt_client_present_{true}, mqtt_transport_connected_{true}, mqtt_connected_seen_{true};
  std::atomic<bool> mqtt_disconnected_seen_{false}, permanent_failure_cleanup_pending_{false};
  std::atomic<bool> client_worker_active_{false}, force_publish_{false};
  PendingFlag client_reconcile_pending_{false};
  std::atomic<bool> clear_session_scoped_inputs_pending_{false}, marked_failed{false};
  std::atomic<uint8_t> clear_input_mask_pending_{0}, input_disabled_mask_{0}, retained_disabled_mask_{0};
  std::atomic<uint32_t> active_client_generation_{1}, client_requested_generation_{1}, client_applied_generation_{1};
  std::atomic<uint32_t> callback_session_generation_{1}, mqtt_session_generation_{1}, next_reconcile_attempt_ms_{0};
  std::atomic<esp_mqtt_client_handle_t> callback_client_{&initial_client};
  std::atomic<unsigned> accepted_payloads{0};
  static uint8_t numeric_input_bit_(size_t index) { return 1U << index; }
  static uint8_t binary_input_bit_(size_t index) { return 1U << (NUMERIC_INPUT_COUNT + index); }
  bool is_numeric_input_accept_retained_(size_t) const { return false; }
  bool is_binary_input_accept_retained_(size_t) const { return false; }
  void process_storage_transaction_() {}
  void clear_session_scoped_inputs_() {}
  void clear_input_(uint8_t) {}
  void consume_pending_numeric_payloads_() {}
  void consume_pending_binary_payloads_() {}
  void publish_runtime_state_(bool) {}
  void clear_all_inputs_() {}
  void clear_disabled_inputs_() {}
  bool is_failed() { return marked_failed.load(); }
  void mark_failed() {
    assert(!current_task && "mark_failed must stay on main");
    marked_failed.store(true);
  }
  void queue_numeric_payload_(size_t, const char*, int, bool, uint32_t) { ++accepted_payloads; }
  void queue_binary_payload_(size_t, const char*, int, bool, uint32_t) { ++accepted_payloads; }
  // PRODUCTION_DECLARATIONS
};

// PRODUCTION_METHODS

}  // namespace openquatt_mqtt_config
}  // namespace esphome

using esphome::openquatt_mqtt_config::OpenQuattMqttConfig;
static void idle(OpenQuattMqttConfig& component) {
  until([&] { return !component.client_worker_active_.load(); });
}
static void data(OpenQuattMqttConfig& component, esp_mqtt_client_handle_t client) {
  esp_mqtt_event_t event;
  event.client = client;
  component.mqtt_event_handler_(&component, nullptr, MQTT_EVENT_DATA, &event);
}
static void blocked_subscribe() {
  OpenQuattMqttConfig component;
  subscribe_wait.enabled = true;
  component.loop();
  subscribe_wait.wait();
  // Worker owns runtime_lock_ while waiting in the SDK. Main must still return.
  component.loop();
  assert(component.client_worker_active_.load() && subscribe_calls == 1);
  subscribe_wait.release();
  idle(component);
}
static void reconnect_at_uptime(uint32_t reconnect_ms, bool retry_pending) {
  OpenQuattMqttConfig component;
  component.loop();
  idle(component);
  assert(subscribe_calls == 1 && component.next_reconcile_attempt_ms_.load() == 0U);
  now_ms.store(reconnect_ms);
  const uint32_t retry_ms = reconnect_ms + OpenQuattMqttConfig::MQTT_RECONCILE_RETRY_MS;
  if (retry_pending) component.next_reconcile_attempt_ms_.store(retry_ms);

  esp_mqtt_event_t event;
  event.client = component.mqtt_client_;
  component.mqtt_event_handler_(&component, nullptr, MQTT_EVENT_DISCONNECTED, &event);
  component.mqtt_event_handler_(&component, nullptr, MQTT_EVENT_CONNECTED, &event);
  assert(component.resubscribe_inputs_.load());
  if (retry_pending) {
    component.loop();
    assert(subscribe_calls == 1 && !component.client_worker_active_.load());
    now_ms.store(retry_ms - 1U);
    component.loop();
    assert(subscribe_calls == 1 && !component.client_worker_active_.load());
    now_ms.store(retry_ms);
  }
  component.loop();
  assert(component.client_worker_active_.load() || subscribe_calls == 2);
  until([&] { return subscribe_calls == 2 && !component.client_worker_active_.load(); });
  assert(!component.resubscribe_inputs_.load() && component.next_reconcile_attempt_ms_.load() == 0U);
  assert(init_calls == 0 && stop_calls == 0 && destroy_calls == 0);
}
static void cleanup_at_uptime(uint32_t cleanup_ms) {
  OpenQuattMqttConfig component;
  component.loop();
  idle(component);
  now_ms.store(cleanup_ms);
  component.start_permanent_failure_cleanup_();
  assert(component.client_worker_active_.load() || !component.mqtt_client_present_.load());
  until([&] { return !component.mqtt_client_present_.load() && !component.client_worker_active_.load(); });
  now_ms.store(cleanup_ms + OpenQuattMqttConfig::MQTT_RECONCILE_RETRY_MS);
  component.loop();
  assert(component.marked_failed.load() && component.permanent_failure_cleanup_pending_.load());
  assert(!component.client_events_enabled_.load() && stop_calls == 1 && destroy_calls == 1 && init_calls == 0);
}
static void idle_handoff() {
  OpenQuattMqttConfig component;
  idle_wait.enabled = true;
  log_hook = [](const char* phase) {
    if (std::string(phase) == "MQTT ingress reconcile complete") idle_wait.hold();
  };
  component.loop();
  idle_wait.wait();
  assert(component.client_worker_active_.load() && subscribe_calls == 1);
  // Real input-only apply arrives after subscriptions, just before worker idle.
  OpenQuattMqttConfig::Storage storage;
  storage.input_disabled_mask = 1;
  component.apply_storage_(storage, "test");
  storage.input_disabled_mask = 0;
  component.apply_storage_(storage, "test");
  component.loop();
  assert(subscribe_calls == 1 && component.resubscribe_inputs_.load());
  idle_wait.release();
  idle(component);
  component.loop();
  until([&] { return subscribe_calls == 2 && !component.client_worker_active_.load(); });
}
static void offline_handoff() {
  OpenQuattMqttConfig component;
  network_connected.store(false);
  pending_clear_wait.enabled = true;
  OpenQuattMqttConfig::Storage storage;
  storage.enabled = 0;
  component.apply_storage_(storage, "disable");
  pending_clear_wait.wait();
  assert(!component.mqtt_client_present_.load());
  storage.enabled = 1;
  std::strcpy(storage.broker, "offline-new-broker");
  component.apply_storage_(storage, "offline-enable");
  pending_clear_wait.release();
  idle(component);
  assert(!component.client_reconcile_pending_.load() && !component.resubscribe_inputs_.load());
  assert(component.client_requested_generation_.load() != component.client_applied_generation_.load());
  network_connected.store(true);
  component.loop();
  assert(component.client_worker_active_.load() || init_calls == 1);
  until([&] { return init_calls == 1 && !component.client_worker_active_.load(); });
  assert(component.client_requested_generation_.load() == component.client_applied_generation_.load());
}
static void reconfigure() {
  OpenQuattMqttConfig component;
  auto* old = component.mqtt_client_;
  subscribe_wait.enabled = true;
  component.loop();
  subscribe_wait.wait();
  data(component, old);
  assert(component.accepted_payloads == 1);
  OpenQuattMqttConfig::Storage storage;
  std::strcpy(storage.broker, "new-broker");
  component.apply_storage_(storage, "test");
  data(component, old);
  assert(component.accepted_payloads == 1);
  component.loop();  // No runtime-lock wait during reconfiguration either.
  subscribe_wait.release();
  until([&] { return init_calls == 1 && !component.client_worker_active_.load(); });
  assert(stop_calls == 1 && component.mqtt_client_ != old);
  data(component, old);
  assert(component.accepted_payloads == 1);
  data(component, component.mqtt_client_);
  assert(component.accepted_payloads == 2);
}
static void terminal_cleanup() {
  OpenQuattMqttConfig component;
  subscribe_wait.enabled = true;
  stop_wait.enabled = true;
  stop_failures.store(2);
  component.loop();
  subscribe_wait.wait();
  component.start_permanent_failure_cleanup_();
  // Queue an additional wake before the worker reaches terminal cleanup.
  assert(component.notify_client_worker_());
  subscribe_wait.release();
  stop_wait.wait();
  component.loop();
  assert(!component.marked_failed.load() && component.mqtt_client_present_.load());
  stop_wait.release();
  until([&] { return !component.mqtt_client_present_.load() && !component.client_worker_active_.load(); });
  component.loop();
  assert(component.marked_failed.load() && component.permanent_failure_cleanup_pending_.load());
  assert(stop_calls == 3 && destroy_calls == 1 && init_calls == 0);
  assert(component.notify_client_worker_());
  idle(component);
  assert(init_calls == 0 && component.mqtt_client_ == nullptr);
}
static void invalid_region() {
  OpenQuattMqttConfig component;
  wrong_worker_region = true;
  component.loop();
  for (int i = 0; i < 3; ++i) {
    now_ms.fetch_add(5001);
    component.loop();
  }
  assert(worker_creations == 1 && !component.client_worker_task_state_.is_created());
  assert(component.permanent_failure_cleanup_pending_.load() && !component.client_events_enabled_.load());
  assert(component.mqtt_client_present_.load() && !component.marked_failed.load());
  assert(subscribe_calls == 0 && stop_calls == 0 && destroy_calls == 0 && init_calls == 0);
}
static void cold_start() {
  OpenQuattMqttConfig component;
  component.mqtt_client_ = nullptr;
  component.mqtt_client_present_.store(false);
  component.callback_client_.store(nullptr);
  component.mqtt_client_started_ = false;
  component.client_events_enabled_.store(false);
  component.connected_.store(false);
  component.client_reconcile_pending_.store(true);
  component.loop();
  until([&] { return init_calls == 1 && !component.client_worker_active_.load(); });
  assert(component.mqtt_client_present_.load() && component.mqtt_client_started_);
  assert(component.mqtt_client_ != nullptr && component.mqtt_client_ != &component.initial_client);
  assert(component.client_events_enabled_.load() && component.callback_client_.load() == component.mqtt_client_);
}
static void partial_init() {
  OpenQuattMqttConfig component;
  component.mqtt_client_ = nullptr;
  component.mqtt_client_present_.store(false);
  component.callback_client_.store(nullptr);
  component.mqtt_client_started_ = false;
  component.client_events_enabled_.store(false);
  component.connected_.store(false);
  component.client_reconcile_pending_.store(true);
  init_wait.enabled = true;
  component.loop();
  init_wait.wait();
  component.start_permanent_failure_cleanup_();
  assert(!component.marked_failed.load());
  init_wait.release();
  until([&] { return !component.client_worker_active_.load(); });
  component.loop();
  assert(component.marked_failed.load() && !component.client_events_enabled_.load());
  assert(!component.mqtt_client_present_.load() && init_calls == 1 && destroy_calls == 1);
  assert(stop_calls == 0 && component.permanent_failure_cleanup_pending_.load());
}
int main(int argc, char** argv) {
  assert(argc == 2 || argc == 3);
  const std::string name = argv[1];
  const uint32_t test_ms = argc == 3 ? static_cast<uint32_t>(std::stoul(argv[2], nullptr, 0)) : 0U;
  if (name == "blocked_subscribe")
    blocked_subscribe();
  else if (name == "reconnect_at_uptime")
    reconnect_at_uptime(test_ms, false);
  else if (name == "reconnect_with_retry_at_uptime")
    reconnect_at_uptime(test_ms, true);
  else if (name == "cleanup_at_uptime")
    cleanup_at_uptime(test_ms);
  else if (name == "idle_handoff")
    idle_handoff();
  else if (name == "offline_handoff")
    offline_handoff();
  else if (name == "reconfigure")
    reconfigure();
  else if (name == "terminal_cleanup")
    terminal_cleanup();
  else if (name == "invalid_region")
    invalid_region();
  else if (name == "cold_start")
    cold_start();
  else if (name == "partial_init")
    partial_init();
  else
    assert(false && "unknown case");
  std::cout << "PASS " << name << '\n';
}
