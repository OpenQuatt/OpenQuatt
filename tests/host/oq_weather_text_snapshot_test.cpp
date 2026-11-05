#include <atomic>
#include <cassert>
#include <cstring>
#include <string>
#include <thread>
#include "components/openquatt_weather/WeatherTextSnapshot.h"

int main() {
  using namespace esphome::openquatt_weather;
  WeatherTextSnapshots store;
  int location = 0, status = 0, foreign = 0, resolved = 0, issued = 0;
  store.bind(0, &location);
  store.bind(1, &status);
  store.bind(2, &resolved);
  store.bind(3, &issued);
  store.bind(4, &foreign);  // No hidden fifth entry.
  published_weather_snapshots.store(&store, std::memory_order_release);
  TextSnapshotValue value;
  assert(!copy_weather_text_snapshot(&foreign, value));
  assert(!copy_weather_text_snapshot(nullptr, value));
  assert(copy_weather_text_snapshot(&location, value) && value.text[0] == '\0');
  store.publish(&location, "Brummen");
  assert(copy_weather_text_snapshot(&location, value) && std::strcmp(value.text, "Brummen") == 0);
  const std::string maximum(127, 'A');
  constexpr const char* first_token = "0123456789abcdef0123456789abcdef";
  constexpr const char* second_token = "fedcba9876543210fedcba9876543210";
  const std::string oversized(512, 'B');
  store.publish(&location, oversized.c_str());
  assert(copy_weather_text_snapshot(&location, value) && std::strlen(value.text) == 127);
  store.publish(&location, "");
  assert(copy_weather_text_snapshot(&location, value) && value.text[0] == '\0');
  store.publish(&location, maximum.c_str(), first_token, SaveState::PENDING);
  store.publish(&status, "disabled");
  store.publish(&resolved, "Blocked");
  store.publish(&issued, "Ready");
  std::atomic<bool> finished{false};
  std::atomic<bool> start{false};
  std::atomic<unsigned> ready{0}, concurrent_reads{0};
  const auto wait_start = [&]() {
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
  };
  std::thread writer([&]() {
    wait_start();
    for (unsigned i = 0; i < 20000; ++i) {
      if (i % 2)
        store.publish(&location, maximum.c_str(), first_token, SaveState::PENDING);
      else
        store.publish(&location, "Brummen", second_token, SaveState::STORED);
      store.publish(&resolved, i % 2 ? "Blocked" : "Brummen (52.09000, 6.15556)");
      store.publish(&issued, i % 2 ? "Ready" : "1793770000");
      if (i % 32 == 0) std::this_thread::yield();
    }
    finished.store(true, std::memory_order_release);
  });
  const auto read = [&]() {
    TextSnapshotValue snapshot;
    ready.fetch_add(1, std::memory_order_release);
    wait_start();
    do {
      assert(copy_weather_text_snapshot(&location, snapshot));
      if (std::strcmp(snapshot.text, "Brummen") == 0) {
        assert(std::strcmp(snapshot.save_request, second_token) == 0);
        assert(snapshot.save_state == SaveState::STORED);
      } else {
        assert(std::strcmp(snapshot.text, maximum.c_str()) == 0);
        assert(std::strcmp(snapshot.save_request, first_token) == 0);
        assert(snapshot.save_state == SaveState::PENDING);
      }
      assert(copy_weather_text_snapshot(&status, snapshot) && std::strcmp(snapshot.text, "disabled") == 0);
      assert(snapshot.save_request[0] == '\0' && snapshot.save_state == SaveState::NONE);
      assert(copy_weather_text_snapshot(&resolved, snapshot));
      assert(std::strcmp(snapshot.text, "Blocked") == 0 ||
             std::strcmp(snapshot.text, "Brummen (52.09000, 6.15556)") == 0);
      assert(copy_weather_text_snapshot(&issued, snapshot));
      assert(std::strcmp(snapshot.text, "Ready") == 0 || std::strcmp(snapshot.text, "1793770000") == 0);
      if (!finished.load(std::memory_order_acquire)) concurrent_reads.fetch_add(1, std::memory_order_relaxed);
    } while (!finished.load(std::memory_order_acquire));
  };
  std::thread first(read), second(read);
  while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
  start.store(true, std::memory_order_release);
  writer.join();
  first.join();
  second.join();
  assert(concurrent_reads.load() >= 2);  // A completed writer alone is not evidence of an interleaving.
  published_weather_snapshots.store(nullptr, std::memory_order_release);
  assert(!copy_weather_text_snapshot(&location, value));
}
