#pragma once

#include <atomic>
#include <new>
#include "oq_performance_observer_logic.h"
#include "../sources/oq_source_receipt_runtime.h"
#include "../control/oq_compressor_frequency_runtime.h"

#if defined(ESP_PLATFORM) && OQ_TOPOLOGY_DUO
#include "esp_heap_caps.h"

namespace oq_performance_observer {
struct RuntimeState {
  Job job;
  Stability stability;
  ContextKey key;
  uint64_t last_context_ms = 0;
  uint32_t completed = 0, rejected = 0, maximum_step_us = 0, stack_free = 0;
  bool context_received = false;
};
static_assert(sizeof(RuntimeState) <= 12000, "Observer must use a bounded PSRAM allocation");

class Runtime {
 public:
  // This is the only entry called by a switch action. Allocation, hooks, state
  // ownership and destruction all stay on the single ESPHome loop task.
  void request(bool enabled) { requested_.store(enabled, std::memory_order_release); }
  void capture(const oq_power_house_dispatch::DispatchInput& in, const oq_power_house_dispatch::DispatchTuning& tune,
               const oq_power_house_dispatch::DispatchState& state,
               const oq_power_house_dispatch::DispatchDecision& actual, Reference ref) {
    if (!state_) return;
    const auto next = key(in, tune, state, ref);
    if (!accept(next)) return;
    auto& ctx = state_->job.context;
    ctx.mode = Mode::POWER_HOUSE;
    ctx.ph = in;
    ctx.ph_tuning = tune;
    ctx.ph_state = state;
    ctx.ph_actual = actual;
    ctx.reference_outside_c = ref.outside_c;
    ctx.reference_water_c = ref.water_c;
    ctx.captured_ms = oq_sources::monotonic_ms();
  }
  void capture(const oq_curve_duo::Input& in, const oq_curve_duo::State& state, const Powers& powers,
               const oq_curve_duo::Decision& actual, Reference ref) {
    if (!state_) return;
    const auto next = key(in, state, powers, ref);
    if (!accept(next)) return;
    auto& ctx = state_->job.context;
    ctx.mode = Mode::CURVE;
    ctx.curve = in;
    ctx.curve_state = state;
    ctx.curve_power = powers;
    ctx.curve_actual = actual;
    ctx.reference_outside_c = ref.outside_c;
    ctx.reference_water_c = ref.water_c;
    ctx.captured_ms = oq_sources::monotonic_ms();
  }
  void tick(uint32_t minimum_off_ms) {
    const bool wanted = requested_.load(std::memory_order_acquire);
    if (wanted != enabled_) {
      enabled_ = wanted;
      if (!wanted) {
        ph_hook = nullptr;
        curve_hook = nullptr;
        if (state_) {
          state_->~RuntimeState();
          heap_caps_free(state_);
          state_ = nullptr;
        }
        status_ = Status::DISABLED;
      } else {
        // Never fall back to scarce internal DRAM for the pair matrix.
        void* storage = heap_caps_malloc(sizeof(RuntimeState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!storage) {
          status_ = Status::ALLOCATION_FAILED;
          return;
        }
        state_ = new (storage) RuntimeState{};
        status_ = Status::SETTLING;
        install_hooks();
      }
    }
    if (!state_) return;
    const uint64_t begin_us = esp_timer_get_time(), now_ms = oq_sources::monotonic_ms();
    const auto frequency = oq_frequency_runtime::capture();
    Policy policy;
    policy.cap_hz = frequency.cap_hz;
    policy.offsets = {id(hp1_water_in_temp_offset).state, id(hp1_water_out_temp_offset).state,
                      id(hp2_water_in_temp_offset).state, id(hp2_water_out_temp_offset).state};
    for (unsigned hp = 0; hp < 2; ++hp) {
      policy.variant[hp] = frequency.performance_variant(hp == 0);
      for (int level = 0; level <= 20; ++level) {
        policy.hz[hp][level] =
            std::max(0, oq_odu::frequency_for_physical_level(frequency.snapshot(hp == 0).heating, level));
        policy.allowed[hp][level] = level == 0 || (level <= frequency.automatic_level_limit(hp == 0, 2) &&
                                                   frequency.frequency_allowed(hp == 0, 2, level) &&
                                                   policy.hz[hp][level] > 0 && policy.hz[hp][level] <= 90);
      }
    }
    const auto frame = capture_frame(now_ms, policy);
    // Stable telemetry must also describe the applied physical level. Otherwise
    // an acknowledged command with held/wrong readback is not a nominal sample.
    const std::array<int, 2> applied{id(hp1_last_applied_level), id(hp2_last_applied_level)};
    for (unsigned hp = 0; hp < 2; ++hp) {
      if (applied[hp] < 0 || applied[hp] > 20 || !std::isfinite(frame.frequency[hp].value) ||
          std::fabs(frame.frequency[hp].value - policy.hz[hp][applied[hp]]) > .5f) {
        state_->stability.reset();
        revoke(Status::TRANSIENT);
        return;
      }
    }
    const uint64_t eligible_after =
        now_ms + std::max({remaining(id(hp1_last_real_heat_start_ms), 300000, millis(), frame.heating[0]),
                           remaining(id(hp2_last_real_heat_start_ms), 300000, millis(), frame.heating[1]),
                           remaining(id(hp1_last_level_change_ms), 60000, millis(), frame.frequency[0].value > 0),
                           remaining(id(hp2_last_level_change_ms), 60000, millis(), frame.frequency[1].value > 0)});
    const auto stable = state_->stability.observe(frame, policy, eligible_after);
    if (stable != Status::VALID) {
      revoke(stable);
      return;
    }
    if (policy.variant[0] == oq_odu::Variant::UNKNOWN || policy.variant[1] == oq_odu::Variant::UNKNOWN ||
        !state_->context_received || now_ms - state_->last_context_ms > 90000) {
      revoke(Status::INVALID_INPUT);
      return;
    }
    const auto& ctx = state_->job.context;
    const bool active_mode = id(oq_control_mode_code) == 2 || id(oq_control_mode_code) == 3;
    if (!active_mode || (ctx.mode == Mode::POWER_HOUSE ? id(oq_heat_mode_code) != 0 : id(oq_heat_mode_code) != 1)) {
      revoke(Status::DISABLED);
      return;
    }
    // The only changed model input is water temperature / series coupling.
    // A different ambient reference is a different experiment, not this pair.
    if (!std::isfinite(ctx.reference_water_c) || !std::isfinite(ctx.reference_outside_c) ||
        std::fabs(ctx.reference_outside_c - frame.outside.value) > .25f || !candidates_match(minimum_off_ms)) {
      revoke(Status::CONTEXT_CHANGED);
      return;
    }
    auto& job = state_->job;
    if ((job.running || job.result.valid) && (!(job.policy == policy) || state_->stability.changed(job.frame))) {
      revoke(Status::CONTEXT_CHANGED);
      return;
    }
    if (job.result.valid && now_ms > std::min(job.frame.context_valid_until_ms[0], job.frame.context_valid_until_ms[1]))
      revoke(Status::STALE);
    if (!job.running && (!job.result.valid || now_ms - job.frame.now_ms >= 20000)) job.begin(frame, policy);
    const bool was_running = job.running;
    job.step(now_ms);
    if (was_running && !job.running) {
      if (job.result.valid)
        ++state_->completed;
      else
        ++state_->rejected;
    }
    status_ = job.status;
    state_->maximum_step_us = std::max(state_->maximum_step_us, static_cast<uint32_t>(esp_timer_get_time() - begin_us));
    state_->stack_free = uxTaskGetStackHighWaterMark(nullptr);
  }
  std::string summary() const {
    return std::string("status=") + oq_water_shadow::status_name(status_) +
           " valid=" + (state_ && state_->job.result.valid ? "1" : "0");
  }
  std::string report() const {
    if (!state_) return summary();
    const auto& job = state_->job;
    const auto& result = job.result;
    const auto& a = result.old;
    const auto& b = result.series;
    char text[1024];
    const int size = snprintf(
        text, sizeof(text),
        "status=%s valid=%u seq=%u mode=%u map=existing water=own_outlet series=hp1_hp2 flow_source=hp1 "
        "variant1=%u variant2=%u old_f=%d/%d series_f=%d/%d old_hz=%u/%u series_hz=%u/%u "
        "old_pth=%.2f series_pth=%.2f old_pel=%.2f series_pel=%.2f old_target=%.2f series_target=%.2f "
        "old_cap=%.2f series_cap=%.2f old_reason=%d series_reason=%d own=%.2f/%.2f shared=%.2f/%.2f "
        "own_pel=%.2f/%.2f pred_out=%.3f/%.3f frame_ms=%llu context_ms=%llu samples=%u completed=%u rejected=%u "
        "unavailable=%u max_us=%u stack=%u "
        "bytes=%u",
        oq_water_shadow::status_name(status_), static_cast<unsigned>(result.valid),
        static_cast<unsigned>(result.sequence), static_cast<unsigned>(job.context.mode),
        static_cast<unsigned>(job.policy.variant[0]), static_cast<unsigned>(job.policy.variant[1]), a.first, a.second,
        b.first, b.second, static_cast<unsigned>(job.policy.hz[0][a.first]),
        static_cast<unsigned>(job.policy.hz[1][a.second]), static_cast<unsigned>(job.policy.hz[0][b.first]),
        static_cast<unsigned>(job.policy.hz[1][b.second]), a.thermal_w, b.thermal_w, a.electrical_w, b.electrical_w,
        a.target_w, b.target_w, a.capacity_w, b.capacity_w, a.reason, b.reason, result.own[0].thermal_w,
        result.own[1].thermal_w, result.shared_w[0], result.shared_w[1], result.own[0].electrical_w,
        result.own[1].electrical_w, result.predicted_outlet_c[0], result.predicted_outlet_c[1],
        static_cast<unsigned long long>(job.frame.now_ms),
        static_cast<unsigned long long>(result.valid ? result.context_ms : job.context.captured_ms),
        state_->stability.samples(), static_cast<unsigned>(state_->completed), static_cast<unsigned>(state_->rejected),
        job.unavailable, static_cast<unsigned>(state_->maximum_step_us), static_cast<unsigned>(state_->stack_free),
        static_cast<unsigned>(sizeof(RuntimeState)));
    return size > 0 && static_cast<size_t>(size) < sizeof(text) ? std::string(text, size)
                                                                : "status=report_overflow valid=0";
  }

 private:
  std::atomic<bool> requested_{false};
  bool enabled_ = false;
  RuntimeState* state_ = nullptr;
  Status status_ = Status::DISABLED;
  void install_hooks();
  bool accept(const ContextKey& next) {
    state_->last_context_ms = oq_sources::monotonic_ms();
    if (!next.valid) {
      state_->context_received = false;
      state_->stability.reset();
      revoke(Status::INVALID_INPUT);
      return false;
    }
    if (!state_->context_received || !(state_->key == next)) {
      state_->job.invalidate(Status::CONTEXT_CHANGED);
      state_->key = next;
      state_->context_received = true;
    }
    return !state_->job.running;
  }
  void revoke(Status why) {
    state_->job.invalidate(why);
    status_ = why;
  }
  static uint32_t remaining(uint32_t since, uint32_t window, uint32_t now, bool active) {
    if (!active) return 0;
    if (since == 0) return window;
    const uint32_t age = now - since;
    return age < window ? window - age : 0;
  }
  static Snapshot capture_frame(uint64_t now_ms, const Policy& policy) {
    Snapshot frame;
    frame.now_ms = now_ms;
    frame.order_confirmed = frame.hp1_upstream = true;
    frame.outside = oq_sources::hp1.outside;
    frame.flow = oq_sources::hp1.flow;
    frame.inlet = {oq_sources::hp1.water_in, oq_sources::hp2.water_in};
    frame.outlet = {oq_sources::hp1.water_out, oq_sources::hp2.water_out};
    frame.frequency = {oq_sources::hp1.compressor_frequency, oq_sources::hp2.compressor_frequency};
    frame.online = {id(hp1_is_online), id(hp2_is_online)};
    frame.common_flow_confirmed =
        oq_sources::hp2.flow.fresh(now_ms, 30000) &&
        std::fabs(frame.flow.value - oq_sources::hp2.flow.value) <= std::max(50.0f, .1f * frame.flow.value);
    const std::array<const oq_sources::HeatPumpReceipts*, 2> raw{&oq_sources::hp1, &oq_sources::hp2};
    for (unsigned hp = 0; hp < 2; ++hp) {
      frame.inlet[hp].value += policy.offsets[2 * hp];
      frame.outlet[hp].value += policy.offsets[2 * hp + 1];
      frame.heating[hp] = raw[hp]->working_mode.value == 2;
      frame.transient[hp] = !oq_water_shadow::stationary_context(raw[hp]->working_mode, raw[hp]->defrost,
                                                                 raw[hp]->status_2108, raw[hp]->status_2119, now_ms);
      const uint64_t oldest = std::min({raw[hp]->working_mode.received_ms, raw[hp]->defrost.received_ms,
                                        raw[hp]->status_2108.received_ms, raw[hp]->status_2119.received_ms});
      frame.context_valid_until_ms[hp] = oldest + 30000;
    }
    return frame;
  }
  bool candidates_match(uint32_t minimum_off_ms) const {
    const auto& ctx = state_->job.context;
    const std::array<int, 2> levels{id(hp1_last_applied_level), id(hp2_last_applied_level)};
    const std::array<uint32_t, 2> stops{id(hp1_last_stop_ms), id(hp2_last_stop_ms)};
    for (unsigned hp = 0; hp < 2; ++hp) {
      auto live = oq_hp_candidate::candidate_state(id(oq_incident_manager).get_outputs(hp + 1), levels[hp]);
      live.minimum_off_ready = oq_hp_candidate::minimum_off_ready(millis(), stops[hp], minimum_off_ms, levels[hp]);
      const auto& frozen = ctx.mode == Mode::POWER_HOUSE ? (hp == 0 ? ctx.ph.hp1.candidate : ctx.ph.hp2.candidate)
                                                         : (hp == 0 ? ctx.curve.hp1 : ctx.curve.hp2);
      if (live.previous_applied_level != frozen.previous_applied_level ||
          live.available_for_start != frozen.available_for_start || live.must_stop != frozen.must_stop ||
          live.link_suspect != frozen.link_suspect || live.minimum_off_ready != frozen.minimum_off_ready)
        return false;
    }
    return true;
  }
};
inline Runtime& runtime() {
  static Runtime instance;
  return instance;
}
inline void Runtime::install_hooks() {
  ph_hook = [](const auto& in, const auto& tune, const auto& state, const auto& actual, Reference ref) {
    runtime().capture(in, tune, state, actual, ref);
  };
  curve_hook = [](const auto& in, const auto& state, const auto& power, const auto& actual, Reference ref) {
    runtime().capture(in, state, power, actual, ref);
  };
}
}  // namespace oq_performance_observer
#endif
