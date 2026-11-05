#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>
#ifdef USE_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
#include <mutex>
#endif
#include "OpenQuattWeatherLogic.h"
#include "WeatherScenarioSnapshot.h"

namespace esphome::openquatt_weather {

// Both copies are caller-owned strict PSRAM. No std::string, vector, pointers
// into entities, or JSON allocation may cross the main-loop/HTTP boundary.
struct ForecastSnapshot {
  uint64_t published_ms = 0, fetched_ms = 0;
  uint32_t now_epoch = 0, fetched_epoch = 0, generation = 0, configuration = 0, start_epoch = 0;
  oq_weather::Current current;
  oq_weather::CurrentWind current_wind;
  ScenarioSnapshot scenario;
  float temperature_c[oq_weather::kHours]{}, wind_mps[oq_weather::kHours]{};
  float wind_direction_deg[oq_weather::kHours]{};
  uint64_t wind_direction_valid_mask = 0;
  float latitude = NAN, longitude = NAN;
  char location[128]{}, configured_location[65]{};
  bool enabled = false, valid = false;
};
static_assert(std::is_trivially_copyable_v<ForecastSnapshot>);
static_assert(sizeof(ForecastSnapshot) <= 808, "forecast endpoint copies must stay compact");

inline void age_forecast_snapshot(ForecastSnapshot& snapshot, uint64_t now_ms) {
  // Only lower validity. HTTP may extrapolate a recently published clock from
  // monotonic time, but cannot revive a stale/disabled/location-invalid cache.
  const bool fresh = now_ms >= snapshot.published_ms && now_ms - snapshot.published_ms <= 15000;
  const uint64_t epoch =
      static_cast<uint64_t>(snapshot.now_epoch) + (fresh ? (now_ms - snapshot.published_ms) / 1000U : 0U);
  // The main-loop epoch has no subsecond phase. Round age checks upwards so
  // extrapolation cannot grant an extra second at a model/coverage boundary.
  const uint64_t expiry_epoch =
      static_cast<uint64_t>(snapshot.now_epoch) + (fresh ? (now_ms - snapshot.published_ms + 999U) / 1000U : 0U);
  if (!fresh || snapshot.now_epoch == 0 || expiry_epoch > UINT32_MAX) {
    snapshot.valid = false;
    snapshot.current.valid = false;
    snapshot.current_wind.valid = false;
    snapshot.current_wind.direction_valid = false;
    snapshot.now_epoch = 0;
    invalidate_scenario(snapshot.scenario, ScenarioReason::SNAPSHOT_STALE);
    return;
  }
  snapshot.now_epoch = static_cast<uint32_t>(epoch);
  const uint64_t last = static_cast<uint64_t>(snapshot.start_epoch) + (oq_weather::kHours - 1U) * 3600U;
  snapshot.valid &= snapshot.enabled && snapshot.generation != 0 && snapshot.fetched_epoch != 0 &&
                    snapshot.fetched_epoch <= expiry_epoch &&
                    expiry_epoch - snapshot.fetched_epoch <= oq_weather::kMaxAgeS && now_ms >= snapshot.fetched_ms &&
                    now_ms - snapshot.fetched_ms <= static_cast<uint64_t>(oq_weather::kMaxAgeS) * 1000U &&
                    snapshot.start_epoch <= epoch && last >= expiry_epoch + oq_weather::kMinimumCoverageS;
  snapshot.current.valid &=
      snapshot.valid && oq_weather::valid_current(snapshot.current, static_cast<uint32_t>(expiry_epoch));
  snapshot.current_wind.valid &=
      snapshot.valid && oq_weather::valid_current_wind(snapshot.current_wind, static_cast<uint32_t>(expiry_epoch));
  snapshot.current_wind.direction_valid &=
      snapshot.current_wind.valid && oq_weather::valid_wind_direction(snapshot.current_wind.direction_deg);
  age_scenario_snapshot(snapshot.scenario, static_cast<uint32_t>(expiry_epoch), now_ms, snapshot.valid,
                        snapshot.configuration);
}

class ForecastSnapshotAccess {
 public:
  // Bind once before the server accepts requests; pointers never change again.
  void bind(ForecastSnapshot* cache, ForecastSnapshot* request) {
    cache_ = cache;
    request_ = request;
    ready_.store(cache && request, std::memory_order_release);
  }
  bool available() const { return ready_.load(std::memory_order_acquire); }
  template <typename Publish>
  void publish(Publish publish) {
    if (!available()) return;
    cache_mutex_.lock();
    publish(*cache_);
    cache_mutex_.unlock();
  }
  bool begin_request(uint64_t now_ms) {
    if (!available() || !request_mutex_.try_lock()) return false;
    cache_mutex_.lock();
    *request_ = *cache_;
    cache_mutex_.unlock();
    age_forecast_snapshot(*request_, now_ms);
    return true;
  }
  const ForecastSnapshot& request() const { return *request_; }
  void end_request() { request_mutex_.unlock(); }

 private:
  class Mutex {
   public:
    void lock() {
#ifdef USE_ESP32
      xSemaphoreTake(mutex_, portMAX_DELAY);
#else
      mutex_.lock();
#endif
    }
    bool try_lock() {
#ifdef USE_ESP32
      return xSemaphoreTake(mutex_, 0) == pdTRUE;
#else
      return mutex_.try_lock();
#endif
    }
    void unlock() {
#ifdef USE_ESP32
      xSemaphoreGive(mutex_);
#else
      mutex_.unlock();
#endif
    }

   private:
#ifdef USE_ESP32
    StaticSemaphore_t storage_{};
    SemaphoreHandle_t mutex_ = xSemaphoreCreateMutexStatic(&storage_);
#else
    std::mutex mutex_;
#endif
  } cache_mutex_, request_mutex_;
  ForecastSnapshot* cache_ = nullptr;
  ForecastSnapshot* request_ = nullptr;
  std::atomic<bool> ready_{false};
};

}  // namespace esphome::openquatt_weather
