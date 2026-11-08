#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace oq_power_house::forecast {

constexpr size_t kHorizons = 3;
constexpr uint32_t kHorizonS[kHorizons]{1800, 3600, 10800};
constexpr uint32_t kIssueIntervalS = 900;
constexpr size_t kForcingSteps = 12;
constexpr size_t kPendingSlots = 13;
constexpr uint64_t kMaxSampleGapMs = 30000;

struct Observation {
  uint64_t monotonic_ms = 0;
  uint32_t epoch = 0, context = 0, model_samples = 0;
  bool valid = false, model_ready = false;
  double room_c = NAN, outside_c = NAN, heat_w = NAN;
  double h_w_per_k = NAN, c_wh_per_k = NAN;
};

inline bool valid_observation(const Observation& o) {
  return o.valid && o.monotonic_ms > 0 && o.epoch > 0 && o.context > 0 && std::isfinite(o.room_c) && o.room_c >= 0 &&
         o.room_c <= 40 && std::isfinite(o.outside_c) && o.outside_c >= -40 && o.outside_c <= 55 &&
         std::isfinite(o.heat_w) && std::fabs(o.heat_w) <= 50000;
}

inline bool valid_model(const Observation& o) {
  return o.model_ready && o.model_samples > 0 && std::isfinite(o.h_w_per_k) && o.h_w_per_k >= 20 &&
         o.h_w_per_k <= 1500 && std::isfinite(o.c_wh_per_k) && o.c_wh_per_k >= 500 && o.c_wh_per_k <= 100000;
}

// Exact 1R1C transition for constant inputs. No T0 correction is added: the
// dynamic learner fits Q - H*(Ti-To), independently of the structural house line.
inline double advance(double room, double outside, double heat, double h, double c, double hours) {
  const double equilibrium = outside + heat / h;
  return room + (equilibrium - room) * -std::expm1(-h * hours / c);
}

struct ErrorStats {
  uint32_t count = 0;
  double absolute_sum = 0, square_sum = 0, signed_sum = 0;
  void add(double error) {
    if (!std::isfinite(error) || count == UINT32_MAX) return;
    ++count;
    absolute_sum += std::fabs(error);
    square_sum += error * error;
    signed_sum += error;
  }
  double mae() const { return count ? absolute_sum / count : NAN; }
};

struct HorizonStats {
  ErrorStats issued, persistence, actual_inputs, weather_contribution, heat_assumption;
};

struct Ticket {
  bool active = false;
  uint8_t next_horizon = 0;
  uint32_t epoch = 0, context = 0, model_samples = 0, weather_generation = 0;
  uint64_t monotonic_ms = 0;
  double h = NAN, c = NAN, held_heat = NAN, held_outside = NAN;
  float weather[kForcingSteps + 1]{};
  double issued[kHorizons]{}, persistence[kHorizons]{};
  double actual_inputs = NAN, weather_actual_heat = NAN;
};

struct State {
  Ticket pending[kPendingSlots];
  HorizonStats errors[kHorizons];
  Observation previous;
  uint64_t last_issue_ms = 0;
  uint32_t latest_issue_epoch = 0;
  uint32_t context = 0, issued_count = 0, discarded_count = 0;
  uint32_t last_model_samples = 0;
  double latest[kHorizons]{NAN, NAN, NAN};
  const char* status = "waiting_for_observation";
};

inline void discard_pending(State& state) {
  for (auto& ticket : state.pending) {
    if (ticket.active) ++state.discarded_count;
    ticket.active = false;
  }
  state.previous = {};
  state.last_issue_ms = 0;
  state.latest_issue_epoch = 0;
  for (auto& value : state.latest) value = NAN;
}

inline double ticket_weather(const Ticket& t, double elapsed_s) {
  const double step = elapsed_s / kIssueIntervalS;
  const size_t i = static_cast<size_t>(step);
  if (i >= kForcingSteps) return t.weather[kForcingSteps];
  return t.weather[i] + (step - i) * (t.weather[i + 1] - t.weather[i]);
}

inline void observe(State& state, const Observation& observation) {
  if (!valid_observation(observation)) {
    discard_pending(state);
    state.status = "invalid_observation";
    return;
  }
  if (state.context != observation.context) {
    discard_pending(state);
    for (auto& errors : state.errors) errors = {};
    state.context = observation.context;
    state.last_model_samples = 0;
  }
  if (observation.model_samples < state.last_model_samples) {
    discard_pending(state);
    for (auto& errors : state.errors) errors = {};
  }
  state.last_model_samples = observation.model_samples;
  const auto& previous = state.previous;
  if (previous.monotonic_ms != 0) {
    // A duplicate adapter call is harmless, but a changed timestamp with an old
    // value cannot manufacture a continuous measurement interval.
    if (observation.monotonic_ms == previous.monotonic_ms) return;
    const int64_t dt = static_cast<int64_t>(observation.monotonic_ms) - previous.monotonic_ms;
    const int64_t clock_dt = (static_cast<int64_t>(observation.epoch) - previous.epoch) * 1000;
    if (dt <= 0 || dt > static_cast<int64_t>(kMaxSampleGapMs) || std::abs(clock_dt - dt) > 2000) {
      discard_pending(state);
      state.status = "measurement_gap_or_clock_jump";
      state.previous = observation;
      return;
    }
    for (auto& ticket : state.pending) {
      if (!ticket.active) continue;
      const double from_s = (previous.monotonic_ms - ticket.monotonic_ms) / 1000.0;
      const double to_s = (observation.monotonic_ms - ticket.monotonic_ms) / 1000.0;
      double cursor_s = from_s;
      while (ticket.active && cursor_s < to_s) {
        const double target_s = kHorizonS[ticket.next_horizon];
        const double end_s = std::fmin(to_s, target_s);
        const double midpoint_fraction = ((cursor_s + end_s) * 0.5 - from_s) / (to_s - from_s);
        const double heat = previous.heat_w + midpoint_fraction * (observation.heat_w - previous.heat_w);
        const double outside = previous.outside_c + midpoint_fraction * (observation.outside_c - previous.outside_c);
        const double hours = (end_s - cursor_s) / 3600.0;
        ticket.actual_inputs = advance(ticket.actual_inputs, outside, heat, ticket.h, ticket.c, hours);
        ticket.weather_actual_heat = advance(ticket.weather_actual_heat, ticket_weather(ticket, (cursor_s + end_s) / 2),
                                             heat, ticket.h, ticket.c, hours);
        cursor_s = end_s;
        if (end_s == target_s) {
          const double fraction = (end_s - from_s) / (to_s - from_s);
          const double measured = previous.room_c + fraction * (observation.room_c - previous.room_c);
          const size_t i = ticket.next_horizon;
          auto& errors = state.errors[i];
          errors.issued.add(ticket.issued[i] - measured);
          errors.persistence.add(ticket.persistence[i] - measured);
          errors.actual_inputs.add(ticket.actual_inputs - measured);
          errors.weather_contribution.add(ticket.weather_actual_heat - ticket.actual_inputs);
          errors.heat_assumption.add(ticket.issued[i] - ticket.weather_actual_heat);
          if (++ticket.next_horizon == kHorizons) ticket.active = false;
        }
      }
    }
  }
  state.previous = observation;
  state.status = valid_model(observation) ? "observing" : "waiting_for_model";
}

// Weather is sampled/frozen by the owner at issue time, and parameters stay
// frozen until scoring. Later fits or downloads cannot rewrite earlier forecasts.
inline bool issue(State& state, const float* weather, uint32_t generation) {
  const auto& o = state.previous;
  if (!valid_observation(o) || !valid_model(o) || !weather || generation == 0) return false;
  if (state.last_issue_ms != 0 && o.monotonic_ms - state.last_issue_ms < kIssueIntervalS * 1000ULL) return false;
  for (size_t i = 0; i <= kForcingSteps; ++i)
    if (!std::isfinite(weather[i]) || weather[i] < -40 || weather[i] > 55) return false;
  Ticket* ticket = nullptr;
  for (auto& slot : state.pending)
    if (!slot.active) {
      ticket = &slot;
      break;
    }
  if (!ticket) return false;
  *ticket = {};
  ticket->active = true;
  ticket->epoch = o.epoch;
  ticket->monotonic_ms = o.monotonic_ms;
  ticket->context = o.context;
  ticket->model_samples = o.model_samples;
  ticket->weather_generation = generation;
  ticket->h = o.h_w_per_k;
  ticket->c = o.c_wh_per_k;
  ticket->held_heat = o.heat_w;
  ticket->held_outside = o.outside_c;
  ticket->actual_inputs = ticket->weather_actual_heat = o.room_c;
  double room = o.room_c;
  size_t horizon = 0;
  for (size_t step = 0; step <= kForcingSteps; ++step) {
    ticket->weather[step] = weather[step];
    if (step == 0) continue;
    room = advance(room, (weather[step - 1] + weather[step]) * 0.5, o.heat_w, ticket->h, ticket->c, 0.25);
    if (step * kIssueIntervalS == kHorizonS[horizon]) {
      ticket->issued[horizon] = state.latest[horizon] = room;
      ticket->persistence[horizon] =
          advance(o.room_c, o.outside_c, o.heat_w, ticket->h, ticket->c, kHorizonS[horizon] / 3600.0);
      if (++horizon == kHorizons) break;
    }
  }
  state.last_issue_ms = o.monotonic_ms;
  state.latest_issue_epoch = o.epoch;
  ++state.issued_count;
  state.status = "observing";
  return true;
}

inline bool latest_valid(const State& state, size_t horizon, uint32_t now) {
  return horizon < kHorizons && state.latest_issue_epoch != 0 && now >= state.latest_issue_epoch &&
         now - state.latest_issue_epoch < kHorizonS[horizon] && std::isfinite(state.latest[horizon]);
}

}  // namespace oq_power_house::forecast
