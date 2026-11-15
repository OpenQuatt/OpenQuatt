#pragma once

#include <algorithm>
#include <cstring>
#include <mutex>
#include "PsramBuffer.h"
#include "WeatherForecastSnapshot.h"
#include "includes/learning/oq_ph_forecast_logic.h"

namespace esphome::openquatt_weather {

struct ForecastQuality {
  uint32_t model_context = 0, samples[3]{};
  uint32_t started_epoch = 0, evaluated_epoch = 0, issued = 0, pending[3]{}, discarded[3]{};
  float mae_c[3]{}, reference_mae_c[3]{}, model_mae_c[3]{}, weather_effect_c[3]{}, heat_effect_c[3]{};
  float unchanged_room_mae_c[3]{}, bias_c[3]{}, model_bias_c[3]{};
};

struct RecordedObservation {
  uint64_t monotonic_ms;
  uint32_t epoch, context;
  float room_c, outside_c, heat_w;
  uint32_t segment = 0;
};

struct RecordedIssue {
  uint64_t monotonic_ms;
  IssuedScenario scenario;
  float outside_c[13];
};

class ForecastRecording {
 public:
  static constexpr uint32_t kRetentionS = 12U * 3600U;
  static constexpr size_t kSampleCapacity = 4608, kIssueCapacity = 128;

  struct Snapshot {
    openquatt_common::PsramBuffer<RecordedObservation> observations;
    openquatt_common::PsramBuffer<RecordedIssue> issues;
    size_t observation_count = 0, issue_count = 0;
    uint32_t revision = 0;
    bool issues_truncated = false;
    ForecastQuality quality;
  };

  enum class Result { READY, EMPTY, UNAVAILABLE };

  struct Summary {
    size_t count = 0;
    uint32_t duration_s = 0;
    bool available = true;
  };

  Summary summary(uint64_t now_ms) {
    const std::lock_guard lock(mutex_);
    if (allocation_attempted_ && !samples_) return {0, 0, false};
    if (!sample_count_) return {};
    const auto window = window_(samples_, sample_next_, sample_count_, cutoff_(now_ms));
    if (!window.count) return {};
    const size_t last = (sample_next_ + kSampleCapacity - 1) % kSampleCapacity;
    return {window.count,
            static_cast<uint32_t>((samples_[last].monotonic_ms - samples_[window.start].monotonic_ms) / 1000U)};
  }

  void clear(bool release_buffers = false) {
    const std::lock_guard lock(mutex_);
    sample_count_ = issue_count_ = sample_next_ = issue_next_ = 0;
    dropped_issue_ms_ = 0;
    if (release_buffers) {
      samples_.release();
      issues_.release();
    }
    allocation_attempted_ = bool(samples_);
    segment_ = 0;
    gap_ = true;
    quality_ = {};
    if (++revision_ == 0) ++revision_;
  }

  void gap(const oq_power_house::forecast::State& forecast) {
    const std::lock_guard lock(mutex_);
    gap_ = true;
    if (sample_count_) update_quality_(forecast);
  }

  void observe(RecordedObservation sample, const oq_power_house::forecast::State& forecast,
               const RecordedIssue* issue) {
    const std::lock_guard lock(mutex_);
    if (!allocation_attempted_) {
      allocation_attempted_ = true;
      if (!samples_.allocate_external(kSampleCapacity) || !issues_.allocate_external(kIssueCapacity)) {
        samples_.release();
        issues_.release();
      }
    }
    if (!samples_) return;
    update_quality_(forecast);
    if (sample_count_) {
      const auto& previous = samples_[(sample_next_ + kSampleCapacity - 1) % kSampleCapacity];
      if (sample.monotonic_ms <= previous.monotonic_ms) {
        if (issue && sample.monotonic_ms == previous.monotonic_ms) append_issue_(*issue);
        return;
      }
      const uint64_t dt = sample.monotonic_ms - previous.monotonic_ms;
      const int64_t clock_dt = (static_cast<int64_t>(sample.epoch) - previous.epoch) * 1000;
      gap_ |= dt > 30000 || std::abs(clock_dt - static_cast<int64_t>(dt)) > 2000;
    }
    if (gap_ || evaluation_id_ != forecast.evaluation_id) ++segment_;
    gap_ = false;
    sample.segment = segment_;
    evaluation_id_ = forecast.evaluation_id;
    samples_[sample_next_] = sample;
    sample_next_ = (sample_next_ + 1) % kSampleCapacity;
    if (sample_count_ < kSampleCapacity) ++sample_count_;
    if (issue) append_issue_(*issue);
  }

  Result capture(uint64_t now_ms, Snapshot& snapshot) {
    const uint64_t cutoff = cutoff_(now_ms);
    std::unique_lock lock(mutex_);
    const uint32_t revision = revision_;
    // Retry sizing once if the recorder grows while PSRAM is being allocated.
    for (unsigned attempt = 0; attempt <= 2; ++attempt) {
      if (revision_ != revision) return Result::EMPTY;
      if (allocation_attempted_ && !samples_) return Result::UNAVAILABLE;
      if (!sample_count_) return Result::EMPTY;
      const auto observations = window_(samples_, sample_next_, sample_count_, cutoff);
      if (!observations.count) return Result::EMPTY;
      const uint64_t first = samples_[observations.start].monotonic_ms;
      const auto issues = issue_count_ ? window_(issues_, issue_next_, issue_count_, first) : Window{};
      if (observations.count <= snapshot.observations.size() && issues.count <= snapshot.issues.size()) {
        copy_window_(samples_, observations, snapshot.observations);
        copy_window_(issues_, issues, snapshot.issues);
        snapshot.observation_count = observations.count;
        snapshot.issue_count = issues.count;
        snapshot.issues_truncated = dropped_issue_ms_ >= first;
        snapshot.revision = revision;
        snapshot.quality = quality_;
        return Result::READY;
      }
      if (attempt == 2) break;
      lock.unlock();
      if ((observations.count > snapshot.observations.size() &&
           !snapshot.observations.allocate_external(observations.count)) ||
          (issues.count > snapshot.issues.size() && !snapshot.issues.allocate_external(issues.count)))
        return Result::UNAVAILABLE;
      lock.lock();
    }
    return Result::UNAVAILABLE;
  }

  bool current(uint32_t revision) {
    const std::lock_guard lock(mutex_);
    return revision == revision_;
  }

 private:
  void update_quality_(const oq_power_house::forecast::State& forecast) {
    quality_ = {};
    quality_.model_context = forecast.context;
    quality_.started_epoch = forecast.started_epoch;
    quality_.evaluated_epoch = forecast.evaluated_epoch;
    quality_.issued = forecast.issued_count;
    for (size_t i = 0; i < 3; ++i) {
      const auto& errors = forecast.errors[i];
      quality_.discarded[i] = errors.discarded;
      for (const auto& ticket : forecast.pending)
        if (ticket.active && ticket.next_horizon <= i) ++quality_.pending[i];
      quality_.samples[i] = errors.issued.count;
      quality_.mae_c[i] = errors.issued.mae();
      quality_.reference_mae_c[i] = errors.persistence.mae();
      quality_.model_mae_c[i] = errors.actual_inputs.mae();
      quality_.unchanged_room_mae_c[i] = errors.unchanged_room.mae();
      quality_.bias_c[i] = errors.issued.bias();
      quality_.model_bias_c[i] = errors.actual_inputs.bias();
      quality_.weather_effect_c[i] = errors.weather_contribution.mae();
      quality_.heat_effect_c[i] = errors.heat_assumption.mae();
    }
  }

  void append_issue_(const RecordedIssue& issue) {
    if (issue_count_ && issues_[(issue_next_ + kIssueCapacity - 1) % kIssueCapacity].monotonic_ms >= issue.monotonic_ms)
      return;
    if (issue_count_ == kIssueCapacity)
      dropped_issue_ms_ = issues_[issue_next_].monotonic_ms;
    else
      ++issue_count_;
    issues_[issue_next_] = issue;
    issue_next_ = (issue_next_ + 1) % kIssueCapacity;
  }

  struct Window {
    size_t start, count;
  };

  static uint64_t cutoff_(uint64_t now_ms) {
    const uint64_t retention_ms = uint64_t{kRetentionS} * 1000U;
    return now_ms > retention_ms ? now_ms - retention_ms : 0U;
  }

  template <typename Row>
  static Window window_(const openquatt_common::PsramBuffer<Row>& ring, size_t next, size_t count, uint64_t cutoff) {
    const size_t capacity = ring.size();
    size_t start = (next + capacity - count) % capacity;
    while (count && ring[start].monotonic_ms < cutoff) {
      start = (start + 1) % capacity;
      --count;
    }
    return {start, count};
  }

  template <typename Row>
  static void copy_window_(const openquatt_common::PsramBuffer<Row>& ring, Window window,
                           openquatt_common::PsramBuffer<Row>& out) {
    if (!window.count) return;
    const size_t first = std::min(window.count, ring.size() - window.start);
    std::memcpy(out.data(), ring.data() + window.start, first * sizeof(Row));
    std::memcpy(out.data() + first, ring.data(), (window.count - first) * sizeof(Row));
  }

  ForecastMutex mutex_;
  ForecastQuality quality_;
  openquatt_common::PsramBuffer<RecordedObservation> samples_;
  openquatt_common::PsramBuffer<RecordedIssue> issues_;
  size_t sample_next_ = 0, issue_next_ = 0, sample_count_ = 0, issue_count_ = 0;
  uint64_t dropped_issue_ms_ = 0;
  uint32_t revision_ = 1, evaluation_id_ = 0;
  uint32_t segment_ = 0;
  bool allocation_attempted_ = false, gap_ = true;
};

static_assert(ForecastRecording::kSampleCapacity * sizeof(RecordedObservation) +
                      ForecastRecording::kIssueCapacity * sizeof(RecordedIssue) <=
                  160U * 1024U,
              "forecast recording and its export copy each need at most 160 KiB of PSRAM");

}  // namespace esphome::openquatt_weather
