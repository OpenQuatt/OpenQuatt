#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#ifndef OQ_TOPOLOGY_DUO
#define OQ_TOPOLOGY_DUO 0
#endif

namespace oq_diagnostics {

class InstallationMonitor {
  static constexpr uint32_t kMinuteMs = 60U * 1000U;
  static constexpr size_t kHistoryMinutes = 72U * 60U;
  static constexpr uint8_t kBucketMax = 0x0F;
  static constexpr size_t kPackedHistoryBytes = (kHistoryMinutes + 1U) / 2U;

 public:
  static constexpr uint32_t kHourMs = 60U * 60U * 1000U;
  static constexpr uint32_t kSixHoursMs = 6U * kHourMs;
  static constexpr uint32_t kDayMs = 24U * kHourMs;
  static constexpr uint32_t kTwoHoursMs = 2U * kHourMs;
  static constexpr uint32_t kSeventyTwoHoursMs = 72U * kHourMs;

  // Two four-bit minute buckets per byte hold 72 hours of starts per unit.
  using PackedBuckets = std::array<uint8_t, kPackedHistoryBytes>;
  using HistoryStorage = std::array<PackedBuckets, OQ_TOPOLOGY_DUO ? 2U : 1U>;

  explicit InstallationMonitor(HistoryStorage* storage) {
    if (storage != nullptr) {
      hp1_.history.packed_buckets = &(*storage)[0];
#if OQ_TOPOLOGY_DUO
      hp2_.history.packed_buckets = &(*storage)[1];
#endif
    }
  }

  InstallationMonitor(const InstallationMonitor&) = delete;
  InstallationMonitor& operator=(const InstallationMonitor&) = delete;

  bool observe_compressor_frequency(int hp_index, float frequency_hz, uint32_t now_ms) {
    UnitState* unit_ptr = unit_for(hp_index);
    if (unit_ptr == nullptr) {
      return false;
    }

    UnitState& unit = *unit_ptr;
    if (!std::isfinite(frequency_hz)) {
      unit.observation_valid = false;
      unit.stop_candidate_active = false;
      return false;
    }

    const bool active = frequency_hz > 0.0f;
    advance_history(unit, now_ms);

    if (!unit.observation_valid || elapsed_ms(now_ms, unit.last_observation_ms) > kMaxObservationGapMs) {
      unit.observation_valid = true;
      unit.last_observation_ms = now_ms;
      // After startup or a communications gap, require a fully observed stop
      // before accepting a subsequent start.
      unit.running = true;
      unit.stop_candidate_active = !active;
      if (!active) {
        unit.stop_candidate_ms = now_ms;
      }
      return false;
    }
    unit.last_observation_ms = now_ms;

    if (active) {
      unit.stop_candidate_active = false;
      if (unit.running) {
        return false;
      }

      unit.running = true;
      record_start(hp_index, unit, now_ms);
      return true;
    }

    if (!unit.running) {
      unit.stop_candidate_active = false;
      return false;
    }

    if (!unit.stop_candidate_active) {
      unit.stop_candidate_active = true;
      unit.stop_candidate_ms = now_ms;
      return false;
    }

    if (elapsed_ms(now_ms, unit.stop_candidate_ms) >= kStopConfirmMs) {
      unit.running = false;
      unit.stop_candidate_active = false;
    }
    return false;
  }

  size_t start_count(int hp_index, uint32_t now_ms, uint32_t window_ms) {
    UnitState* unit = unit_for(hp_index);
    if (unit == nullptr || unit->history.packed_buckets == nullptr) {
      return 0;
    }
    advance_history(*unit, now_ms);
    return window_count(unit->history, window_ms);
  }

  float published_start_count(int hp_index, uint32_t now_ms, uint32_t window_ms) {
    const UnitState* unit = unit_for(hp_index);
    if (unit != nullptr && unit->history.packed_buckets == nullptr) {
      return NAN;
    }
    return static_cast<float>(start_count(hp_index, now_ms, window_ms));
  }

  float last_start_age_minutes(int hp_index, uint32_t now_ms) const {
    const UnitState* unit = unit_for(hp_index);
    if (unit == nullptr || !unit->has_last_start) {
      return -1.0f;
    }
    return static_cast<float>(elapsed_ms(now_ms, unit->last_start_ms)) / 60000.0f;
  }

  bool compressor_cycling_warning_2h(uint32_t now_ms, size_t warning_limit) {
    return start_count(1, now_ms, kTwoHoursMs) > warning_limit || start_count(2, now_ms, kTwoHoursMs) > warning_limit;
  }

  bool compressor_cycling_warning_72h(uint32_t now_ms, size_t warning_limit) {
    return start_count(1, now_ms, kSeventyTwoHoursMs) > warning_limit ||
           start_count(2, now_ms, kSeventyTwoHoursMs) > warning_limit;
  }

  bool compressor_cycling_warning(uint32_t now_ms, size_t short_warning_limit, size_t long_warning_limit) {
    return compressor_cycling_warning_2h(now_ms, short_warning_limit) ||
           compressor_cycling_warning_72h(now_ms, long_warning_limit);
  }

  bool alternating_cycling_warning(uint32_t now_ms, size_t warning_limit) {
    prune_expired_events(now_ms);
    const size_t required_events = std::max<size_t>(warning_limit + 1U, 4U);
    if (events_count_ < required_events) {
      return false;
    }

    uint8_t previous_hp = 0;
    for (size_t offset = 0; offset < required_events; ++offset) {
      const StartEvent& event = events_[reverse_event_index(offset)];
      if (elapsed_ms(now_ms, event.at_ms) > kTwoHoursMs) {
        return false;
      }
      if (previous_hp != 0 && previous_hp == event.hp_index) {
        return false;
      }
      previous_hp = event.hp_index;
    }
    return true;
  }

 private:
  static constexpr uint32_t kStopConfirmMs = 20U * 1000U;
  static constexpr uint32_t kMaxObservationGapMs = 30U * 1000U;
  static constexpr size_t kEventsCapacity = 32;
  static_assert(kHistoryMinutes * kBucketMax <= std::numeric_limits<uint16_t>::max(),
                "Rolling compressor start counters must fit in uint16_t.");

  struct StartHistory {
    bool initialized = false;
    uint32_t last_minute_tick_ms = 0;
    size_t current_minute = 0;
    uint16_t starts_2h = 0;
    uint16_t starts_6h = 0;
    uint16_t starts_24h = 0;
    uint16_t starts_72h = 0;
    PackedBuckets* packed_buckets = nullptr;
  };

  struct UnitState {
    bool observation_valid = false;
    bool running = false;
    bool stop_candidate_active = false;
    bool has_last_start = false;
    uint32_t last_observation_ms = 0;
    uint32_t stop_candidate_ms = 0;
    uint32_t last_start_ms = 0;
    StartHistory history;
  };

  struct StartEvent {
    uint32_t at_ms = 0;
    uint8_t hp_index = 0;
  };

  static uint32_t elapsed_ms(uint32_t now_ms, uint32_t then_ms) { return static_cast<uint32_t>(now_ms - then_ms); }

  static uint8_t bucket_at(const StartHistory& history, size_t minute) {
    const uint8_t packed = (*history.packed_buckets)[minute / 2U];
    return minute % 2U == 0U ? packed & kBucketMax : packed >> 4U;
  }

  static void set_bucket(StartHistory& history, size_t minute, uint8_t value) {
    uint8_t& packed = (*history.packed_buckets)[minute / 2U];
    if (minute % 2U == 0U) {
      packed = static_cast<uint8_t>((packed & 0xF0U) | (value & kBucketMax));
    } else {
      packed = static_cast<uint8_t>((packed & 0x0FU) | ((value & kBucketMax) << 4U));
    }
  }

  static size_t history_index(size_t current_minute, size_t minutes_ago) {
    return (current_minute + kHistoryMinutes - (minutes_ago % kHistoryMinutes)) % kHistoryMinutes;
  }

  static void clear_history(StartHistory& history, uint32_t now_ms) {
    history.packed_buckets->fill(0U);
    history.current_minute = 0;
    history.starts_2h = 0;
    history.starts_6h = 0;
    history.starts_24h = 0;
    history.starts_72h = 0;
    history.last_minute_tick_ms = now_ms;
  }

  static void advance_one_minute(StartHistory& history) {
    const size_t next_minute = (history.current_minute + 1U) % kHistoryMinutes;
    const uint8_t leaving_2h = bucket_at(history, history_index(next_minute, 2U * 60U));
    const uint8_t leaving_6h = bucket_at(history, history_index(next_minute, 6U * 60U));
    const uint8_t leaving_24h = bucket_at(history, history_index(next_minute, 24U * 60U));
    const uint8_t leaving_72h = bucket_at(history, next_minute);

    history.starts_2h -= leaving_2h;
    history.starts_6h -= leaving_6h;
    history.starts_24h -= leaving_24h;
    history.starts_72h -= leaving_72h;
    set_bucket(history, next_minute, 0U);
    history.current_minute = next_minute;
  }

  static void advance_history(UnitState& unit, uint32_t now_ms) {
    StartHistory& history = unit.history;
    if (history.packed_buckets == nullptr) {
      return;
    }
    if (!history.initialized) {
      history.initialized = true;
      history.last_minute_tick_ms = now_ms;
      return;
    }

    const uint32_t minutes_elapsed = elapsed_ms(now_ms, history.last_minute_tick_ms) / kMinuteMs;
    if (minutes_elapsed == 0U) {
      return;
    }
    if (minutes_elapsed >= kHistoryMinutes) {
      clear_history(history, now_ms);
      return;
    }

    for (uint32_t minute = 0; minute < minutes_elapsed; ++minute) {
      advance_one_minute(history);
    }
    history.last_minute_tick_ms += minutes_elapsed * kMinuteMs;
  }

  static size_t summed_buckets(const StartHistory& history, size_t minutes) {
    size_t count = 0;
    const size_t bounded_minutes = std::min(minutes, kHistoryMinutes);
    for (size_t minute = 0; minute < bounded_minutes; ++minute) {
      count += bucket_at(history, history_index(history.current_minute, minute));
    }
    return count;
  }

  static size_t window_count(const StartHistory& history, uint32_t window_ms) {
    switch (window_ms) {
      case kTwoHoursMs:
        return history.starts_2h;
      case kSixHoursMs:
        return history.starts_6h;
      case kDayMs:
        return history.starts_24h;
      case kSeventyTwoHoursMs:
        return history.starts_72h;
      default:
        return summed_buckets(history, (window_ms + kMinuteMs - 1U) / kMinuteMs);
    }
  }

  UnitState* unit_for(int hp_index) {
    if (hp_index == 1) {
      return &hp1_;
    }
#if OQ_TOPOLOGY_DUO
    if (hp_index == 2) {
      return &hp2_;
    }
#endif
    return nullptr;
  }

  const UnitState* unit_for(int hp_index) const {
    if (hp_index == 1) {
      return &hp1_;
    }
#if OQ_TOPOLOGY_DUO
    if (hp_index == 2) {
      return &hp2_;
    }
#endif
    return nullptr;
  }

  size_t reverse_event_index(size_t offset) const {
    return (events_next_ + kEventsCapacity - 1U - offset) % kEventsCapacity;
  }

  void prune_expired_events(uint32_t now_ms) {
    while (events_count_ > 0U) {
      const size_t oldest_index = (events_next_ + kEventsCapacity - events_count_) % kEventsCapacity;
      if (elapsed_ms(now_ms, events_[oldest_index].at_ms) <= kTwoHoursMs) {
        return;
      }
      --events_count_;
    }
  }

  void record_start(int hp_index, UnitState& unit, uint32_t now_ms) {
    advance_history(unit, now_ms);
    prune_expired_events(now_ms);
    StartHistory& history = unit.history;
    if (history.packed_buckets != nullptr) {
      const uint8_t bucket = bucket_at(history, history.current_minute);
      if (bucket < kBucketMax) {
        set_bucket(history, history.current_minute, bucket + 1U);
        ++history.starts_2h;
        ++history.starts_6h;
        ++history.starts_24h;
        ++history.starts_72h;
      }
    }
    unit.has_last_start = true;
    unit.last_start_ms = now_ms;

    events_[events_next_] = StartEvent{now_ms, static_cast<uint8_t>(hp_index == 2 ? 2 : 1)};
    events_next_ = (events_next_ + 1U) % kEventsCapacity;
    if (events_count_ < kEventsCapacity) {
      ++events_count_;
    }
  }

  UnitState hp1_;
#if OQ_TOPOLOGY_DUO
  UnitState hp2_;
#endif
  size_t events_count_ = 0;
  size_t events_next_ = 0;
  std::array<StartEvent, kEventsCapacity> events_{};
};

}  // namespace oq_diagnostics
