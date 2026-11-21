#include <cassert>
#include <cmath>
#include <string>

struct GenerationSelect {
  bool has_state() const { return true; }
  std::string current_option() const { return "V2"; }
} hp_generation;
#define id(value) value
#include "includes/performance/oq_water_performance.h"
#undef id
#include "includes/control/oq_power_house_dispatch_logic.h"
#include "includes/control/oq_power_house_run_extension_logic.h"

struct Frequency {
  bool full_table = false;
  oq_odu::Variant performance_variant(bool hp1) const {
    return hp1 ? oq_odu::Variant::V2_OLD_MODEL : oq_odu::Variant::V2_NEW_MODEL;
  }
  int automatic_frequency_hz(bool hp1, int, int level) const {
    constexpr int first[] = {0, 20, 26, 30, 48, 55, 61, 72, 80, 85, 90};
    constexpr int second[] = {0, 20, 26, 30, 36, 40, 45, 48, 52, 55, 60, 65, 68, 72, 76, 82, 85, 90, 95, 102, 110};
    return hp1 && !full_table ? (level <= 10 ? first[level] : -1) : second[level];
  }
};
void test_frequency_feedback_changes_duo_selection() {
  using namespace oq_water_performance;
  using namespace oq_power_house_dispatch;
  const Frequency frequency;
  std::array<oq_compressor_feedback::Observer, 2> observers;
  std::array<oq_compressor_feedback::Snapshot, 2> feedback;
  for (unsigned hp = 0; hp < 2; ++hp) {
    std::array<uint8_t, 21> nominal{};
    for (int level = 1; level <= (hp == 0 ? 10 : 20); ++level)
      nominal[level] = frequency.automatic_frequency_hz(hp == 0, 2, level);
    for (uint32_t now = 1000; now <= 161000; now += 10000)
      feedback[hp] = observers[hp].observe(nominal, 3, {now, 2, 2, 48, 2, 35, true, false, true, now, 0});
  }
  const CandidateModel nominal(frequency, 2, 30, 1000, {35, 35}, true, false);
  const CandidateModel actual(frequency, 2, 30, 1000, {35, 35}, true, false, feedback);
  const auto single = actual.pair(2, 0);
  const auto duo = actual.pair(2, 2);
  assert(single.valid() && duo.valid() && duo.thermal_w > single.thermal_w + 1000);
  assert(actual.hz[0][1] == 48 && actual.hz[1][2] == 48);
  assert(actual.confirmed_hz[0][1] == 20 && actual.confirmed_hz[0][2] == 48);
  assert(actual.hz[0][3] == 30 && actual.hz[1][3] == 30);
  const auto dispatch_input = [&](const CandidateModel& model) {
    DispatchInput in;
    in.now_ms = 171000;
    in.requested_w = single.thermal_w;
    in.duo = in.performance_valid = in.lead_hp1 = true;
    in.hp1.candidate = in.hp2.candidate = {2, true, false, false, true};
    if (model.coupled) in.pair_power = &CandidateModel::dispatch;
    in.pair_context = &model;
    // Isolate the reported low-command combination. No low-frequency exclusion
    // is added to production; the next assertion also checks that 30 Hz survives.
    for (auto* hp : {&in.hp1, &in.hp2}) {
      hp->levels[0] = {true, true, true, 0, 0};
      for (int level = 1; level <= 2; ++level) {
        const auto p = model.unit(hp == &in.hp1, level);
        hp->levels[level] = {true,        p.valid(),      p.valid(),
                             p.thermal_w, p.electrical_w, static_cast<uint8_t>(model.hz[hp == &in.hp1 ? 0 : 1][level])};
      }
    }
    return in;
  };
  const DispatchTuning limits{6000, 6500};
  const CandidateModel independent(frequency, 2, 30, 1000, {35, 35}, false, false);
  for (const auto* model : {&nominal, &independent}) {
    auto cycle = dispatch_input(*model);
    cycle.hp1.feedback_hold_level = cycle.hp2.feedback_hold_level = 2;
    cycle.hp1.stop_inhibited = cycle.hp2.stop_inhibited = true;
    const auto comfort_level = [&] {
      return run_extension_minimum(cycle, limits, 500.0f, &CandidateModel::reference_f1_dispatch).comfort_level;
    };
    assert(comfort_level() == 1);
    cycle.hp1.levels[1].allowed = false;
    assert(comfort_level() == 2);
    cycle.hp1.stop_inhibited = false;
    assert(comfort_level() == 1);
    cycle.hp2.candidate.previous_applied_level = 0;
    assert(comfort_level() == 2);
  }
  const auto before = decide_dispatch(dispatch_input(nominal), limits, {}, &CandidateModel::confirmed_power);
  assert(topology(before.hp1_level, before.hp2_level) == 2);
  auto in = dispatch_input(actual);
  auto after = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
  assert(after.output_valid && topology(after.hp1_level, after.hp2_level) == 1);
  assert(after.hp1_level == 2 || after.hp2_level == 2);
  assert(std::fabs(after.expected_w - single.thermal_w) < 1);
  // One observed 48 Hz unit already covers demand: do not start the idle peer.
  in.hp2.candidate.previous_applied_level = 0;
  after = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
  assert(after.hp1_level > 0 && after.hp2_level == 0);
  // Genuine demand still permits Duo; this is feedback, not a topology ban.
  in.requested_w = duo.thermal_w;
  after = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
  assert(after.hp1_level > 0 && after.hp2_level > 0);
  // Existing minimum-runtime holds still prevent an early stop.
  in.requested_w = single.thermal_w;
  in.hp2.candidate.previous_applied_level = 2;
  in.hp1.feedback_hold_level = in.hp2.feedback_hold_level = 2;
  in.hp1.stop_inhibited = in.hp2.stop_inhibited = true;
  after = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
  assert(after.hp1_level > 0 && after.hp2_level > 0);
  // A safety stop wins even while retained output would cover the heat demand.
  in.hp2.candidate.must_stop = true;
  after = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
  assert(after.hp1_level > 0 && after.hp2_level == 0);
  in = dispatch_input(actual);
  in.hp1.candidate.must_stop = true;
  in.hp2.candidate.previous_applied_level = 0;
  after = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
  assert(after.hp1_level == 0 && after.hp2_level > 0);
  in.requested_w = 0;
  in.hp1.feedback_hold_level = in.hp2.feedback_hold_level = 2;
  after = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
  assert(after.hp1_level == 0 && after.hp2_level == 0);
}
void test_water_limit_allows_modulation_during_feedback_hold() {
  using namespace oq_power_house_dispatch;
  const std::array<uint8_t, oq_compressor_feedback::kLevels> nominal{0, 20, 26, 30, 48};
  oq_compressor_feedback::Observer observer;
  observer.observe(nominal, 3, {1000, 4, 2, 48, 8, 35, true, false, false, 1000, 0});
  const auto feedback = observer.observe(nominal, 3, {11000, 4, 2, 48, 8, 35, true, false, false, 11000, 0});
  assert(!feedback.missing && feedback.settling);

  DispatchInput in{};
  in.now_ms = 11000;
  in.requested_w = 2000;
  in.performance_valid = true;
  in.lead_hp1 = true;
  in.hp1.candidate = {4, true, false, false, true};
  in.hp1.stop_inhibited = true;
  in.hp1.levels[0] = {true, true, true, 0, 0};
  in.hp1.levels[1] = {true, true, true, 2000, 400, 20};
  in.hp1.levels[4] = {true, true, true, 4800, 1250, 48};
  const DispatchTuning limits{6000, 6500};

  in.hp1.feedback_hold_level = feedback.hold_level(4, true, true, true);
  const auto held = decide_dispatch(in, limits, {});
  assert(held.output_valid && held.hp1_level == 4);

  in.hp1.feedback_hold_level = feedback.hold_level(4, true, true, false);
  const auto reduced = decide_dispatch(in, limits, {});
  assert(reduced.output_valid && reduced.hp1_level == 1);
}
void test_peer_defrost_allows_modulation_during_feedback_hold() {
  using namespace oq_power_house_dispatch;
  oq_compressor_feedback::Snapshot feedback{};
  feedback.settling = true;
  DispatchInput in{};
  in.requested_w = 7000;
  in.duo = in.performance_valid = in.lead_hp1 = true;
  in.hp1.candidate = {2, true, false, false, true};
  in.hp2.candidate = {1, true, false, false, true};
  in.hp1.stop_inhibited = in.hp2.stop_inhibited = true;
  in.hp1.fixed_level = 2;
  in.hp1.valve_defrost = true;
  in.hp1.levels[2] = {true, true, true, 1000, 300, 26};
  in.hp2.levels[1] = {true, true, true, 2000, 500, 20};
  in.hp2.levels[2] = {true, true, true, 6000, 1400, 26};
  const DispatchTuning limits{6000, 6500};

  in.hp2.feedback_hold_level = feedback.hold_level(1, true, true, true);
  const auto held = decide_dispatch(in, limits, {});
  assert(held.output_valid && held.hp1_level == 2 && held.hp2_level == 1);

  in.hp2.feedback_hold_level = feedback.hold_level(1, true, true, false);
  const auto raised = decide_dispatch(in, limits, {});
  assert(raised.output_valid && raised.hp1_level == 2 && raised.hp2_level == 2);
}
void test_model_boundary_allows_lower_duo_levels() {
  using namespace oq_power_house_dispatch;
  using namespace oq_water_performance;
  const Frequency frequency;
  const CandidateModel model(frequency, -7, 49, 450, {61, 61}, true, false);
  assert(!model.pair(7, 13).valid());
  const auto reduced = model.pair(4, 7);
  assert(reduced.valid());

  DispatchInput in{};
  in.requested_w = reduced.thermal_w;
  in.duo = in.performance_valid = true;
  in.hp1.candidate = {7, true, false, false, true};
  in.hp2.candidate = {13, true, false, false, true};
  in.hp1.stop_inhibited = in.hp2.stop_inhibited = true;
  in.hp1.levels[4].allowed = in.hp1.levels[7].allowed = true;
  in.hp2.levels[7].allowed = in.hp2.levels[13].allowed = true;
  in.pair_context = &model;
  in.pair_power = &CandidateModel::dispatch;
  const DispatchTuning limits{6000, 6500};
  const auto out = decide_dispatch(in, limits, {});
  assert(out.output_valid && out.hp1_level == 4 && out.hp2_level == 7);
}
void test_missing_feedback_allows_peer_start() {
  using namespace oq_water_performance;
  using namespace oq_power_house_dispatch;
  const Frequency frequency;
  for (bool coupled : {false, true}) {
    for (unsigned running_hp : {0U, 1U}) {
      for (bool previous_run : {false, true}) {
        for (unsigned fault = 0; fault < 6; ++fault) {
          std::array<uint8_t, 21> nominal{};
          for (int level = 1; level <= 2; ++level)
            nominal[level] = frequency.automatic_frequency_hz(running_hp == 0, 2, level);
          oq_compressor_feedback::Observer observer;
          for (uint32_t now = 1000; previous_run && now <= 161000; now += 10000)
            observer.observe(nominal, 3, {now, 2, 2, 48, 2, 35, true, false, true, now, 0});
          oq_compressor_feedback::Observation observation{181000, 2, 2, 0, 2, 35, true, false, true, 181000, 0};
          if (fault == 1) observation.working_mode = 0;  // Standby during startup.
          if (fault == 2) observation.fresh = false;
          if (fault == 3) observation.actual_hz = NAN;
          if (fault == 4) observation.actual_hz = 91;
          if (fault == 5) {  // A receipt from before a new command cannot renew cached capacity evidence.
            observation.actual_hz = 48;
            observation.applied_ms = 181000;
            observation.received_ms = 180999;
          }
          std::array<oq_compressor_feedback::Snapshot, 2> feedback{};
          feedback[running_hp] = observer.observe(nominal, 3, observation);
          assert(feedback[running_hp].missing);
          if (fault < 5) assert(!feedback[running_hp].qualified_run);
          const CandidateModel model(frequency, 2, 30, 1000, {35, 35}, coupled, false, feedback);
          assert(model.hz[running_hp][2] == 26 && model.confirmed_hz[running_hp][2] == 26);
          DispatchInput in;
          in.now_ms = 181000;
          in.duo = in.performance_valid = true;
          in.pair_context = &model;
          in.pair_power = &CandidateModel::dispatch;
          assert(model.pair(2, 2).valid());
          in.requested_w = model.pair(2, 2).thermal_w;
          assert(in.requested_w > std::max(model.unit(true, 2).thermal_w, model.unit(false, 2).thermal_w));
          for (unsigned hp = 0; hp < 2; ++hp) {
            auto& input = hp == 0 ? in.hp1 : in.hp2;
            input.candidate = {hp == running_hp ? 2 : 0, true, false, false, true};
            input.stop_inhibited = hp == running_hp;  // Preserve the starting unit's minimum run time.
            input.feedback_hold_level =
                feedback[hp].hold_level(input.candidate.previous_applied_level, true, true, true);
            input.levels[0] = {true, true, true, 0, 0};
            for (int level = 1; level <= 2; ++level) {
              const auto power = model.unit(hp == 0, level);
              assert(power.valid());
              input.levels[level] = {
                  true, true, true, power.thermal_w, power.electrical_w, static_cast<uint8_t>(model.hz[hp][level])};
            }
          }
          const DispatchTuning limits{6000, 6500};
          const auto out = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
          assert(out.output_valid && out.hp1_level > 0 && out.hp2_level > 0);
          assert(std::isfinite(out.capacity_w) && std::isfinite(out.deficit_w));
          in.requested_w = out.capacity_w + 1000;
          const auto saturated = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
          assert(saturated.saturated && saturated.deficit_w > 0);
          auto& peer = running_hp == 0 ? in.hp2 : in.hp1;
          peer.candidate.minimum_off_ready = false;
          auto blocked = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
          assert((running_hp == 0 ? blocked.hp2_level : blocked.hp1_level) == 0);
          peer.candidate.minimum_off_ready = true;
          peer.candidate.must_stop = true;
          blocked = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
          assert((running_hp == 0 ? blocked.hp2_level : blocked.hp1_level) == 0);
        }
      }
    }
  }
}
void test_feedback_closed_loop() {
  using namespace oq_water_performance;
  using namespace oq_power_house_dispatch;
  for (bool lower_command_returns_nominal : {false, true}) {
    for (float target_w : {2000.0f, 4500.0f}) {
      for (unsigned ramp_step_s : {1U, 5U}) {
        const Frequency frequency{ramp_step_s == 5};
        for (uint32_t cadence : {10000U, 31000U, 60000U}) {
          for (int initial_topology : {0, 1, 2}) {
            const bool start_duo = initial_topology == 2, peer_available = initial_topology != 0;
            std::array<oq_compressor_feedback::Observer, 2> observers;
            std::array<int, 2> applied{2, start_duo ? 2 : 0};
            std::array<uint32_t, 2> changed{1000, 1000};
            std::array<uint32_t, 2> started{UINT32_MAX - 600000, UINT32_MAX - 600000}, stopped{};
            std::array<float, 2> actual{48, start_duo ? 48.0f : 0.0f}, received = actual;
            uint32_t received_ms = 1000, last_dispatch = 1000;
            int late_changes = 0, starts = 0;
            // Low demand checks settling before cache expiry; 4.5kW also spans
            // the 10min TTL while its exact current point keeps being renewed.
            const uint32_t duration = target_w == 2000 ? 601000 : 901000;
            for (uint32_t now = 1000; now <= duration; now += 1000) {
              std::array<oq_compressor_feedback::Snapshot, 2> feedback;
              for (unsigned hp = 0; hp < 2; ++hp) {
                const int desired = applied[hp] == 0                                    ? 0
                                    : lower_command_returns_nominal && applied[hp] == 1 ? 20
                                    : applied[hp] <= 2 ? 48
                                                       : frequency.automatic_frequency_hz(hp == 0, 2, applied[hp]);
                if (applied[hp] == 0) actual[hp] = 0;
                // The inverter takes 18s to ramp 48->30. Learning the first transitional
                // receipt as a persistent 30->48 response would hide the working point.
                if (now % (ramp_step_s * 1000) == 0) {
                  if (actual[hp] < desired) actual[hp] += 1;
                  if (actual[hp] > desired) actual[hp] -= 1;
                }
                if (now % 10000 == 1000) received[hp] = actual[hp];
                std::array<uint8_t, 21> nominal{};
                for (int level = 1; level <= (hp == 0 && !frequency.full_table ? 10 : 20); ++level)
                  nominal[level] = frequency.automatic_frequency_hz(hp == 0, 2, level);
                if (now % 10000 == 1000) received_ms = now;
                feedback[hp] = observers[hp].observe(
                    nominal, 3,
                    {now, applied[hp], received[hp] > 0 ? 2 : 0, received[hp], 2, 35, true, false,
                     static_cast<uint32_t>(now - started[hp]) >= oq_compressor_feedback::kStartupMs, received_ms,
                     changed[hp]});
              }
              // Observations continue on the main loop, independently of optimization.
              if (now - last_dispatch < cadence) continue;
              last_dispatch = now;
              const CandidateModel model(frequency, 2, 30, 1000, {35, 35}, true, false, feedback);
              DispatchInput in;
              in.now_ms = now;
              in.requested_w = target_w;
              in.duo = in.lead_hp1 = true;
              in.hp1.candidate = {applied[0], true, false, false, true};
              in.hp2.candidate = {applied[1], peer_available, !peer_available, false, true};
              for (unsigned hp = 0; hp < 2; ++hp) {
                auto& input = hp == 0 ? in.hp1 : in.hp2;
                input.stop_inhibited = applied[hp] > 0 && static_cast<uint32_t>(now - started[hp]) < 300000;
                input.candidate.minimum_off_ready =
                    applied[hp] > 0 || stopped[hp] == 0 || static_cast<uint32_t>(now - stopped[hp]) >= 240000;
              }
              in.performance_valid = true;
              in.pair_power = &CandidateModel::dispatch;
              in.pair_context = &model;
              const auto retained = model.pair(applied[0], applied[1]);
              in.performance_valid &= retained.valid();
              const bool may_settle = retained.valid() && retained.electrical_w <= 6500;
              for (unsigned hp = 0; hp < 2; ++hp) {
                auto& input = hp == 0 ? in.hp1 : in.hp2;
                input.feedback_hold_level = feedback[hp].hold_level(
                    applied[hp], applied[hp] > 0 && frequency.automatic_frequency_hz(hp == 0, 2, applied[hp]) <= 90,
                    may_settle, true);
                input.levels[0] = {true, true, true, 0, 0};
                for (int level = 1; level <= (hp == 0 && !frequency.full_table ? 10 : 20); ++level) {
                  const auto power = model.unit(hp == 0, level);
                  const int nominal = frequency.automatic_frequency_hz(hp == 0, 2, level);
                  input.levels[level] = {nominal <= 90,      power.valid(),
                                         power.valid(),      power.thermal_w,
                                         power.electrical_w, static_cast<uint8_t>(model.hz[hp][level])};
                }
              }
              const auto out = decide_dispatch(in, {6000, 6500}, {}, &CandidateModel::confirmed_power);
              if (!start_duo || now >= 181000) assert(topology(out.hp1_level, out.hp2_level) == 1);
              for (unsigned hp = 0; hp < 2; ++hp) {
                const int next = hp == 0 ? out.hp1_level : out.hp2_level;
                if (next != applied[hp]) {
                  if (now > 301000) ++late_changes;
                  if (next > 0 && applied[hp] == 0) {
                    started[hp] = now;
                    ++starts;
                  }
                  if (next == 0 && applied[hp] > 0) stopped[hp] = now;
                  changed[hp] = now;
                  applied[hp] = next;
                }
              }
            }
            assert(topology(applied[0], applied[1]) == 1 && starts <= 2);
            // With no peer to explore, settle at the working 30 Hz point without the
            // original 20/26 limit cycle. A healthy unobserved peer may still be tried,
            // subject to the real minimum runtime and off-time reproduced above.
            if (!peer_available) {
              assert(late_changes == 0);
              if (!lower_command_returns_nominal && target_w == 2000) assert(applied[0] == 3);
            }
          }
        }
      }
    }
  }
}
void test_temporary_boost_cannot_shed_a_needed_peer() {
  using namespace oq_water_performance;
  using namespace oq_power_house_dispatch;
  const Frequency frequency;
  std::array<oq_compressor_feedback::Observer, 2> observers;
  std::array<oq_compressor_feedback::Snapshot, 2> feedback;
  const auto observe = [&](uint32_t now, float actual, bool may_learn) {
    for (unsigned hp = 0; hp < 2; ++hp) {
      std::array<uint8_t, 21> nominal{};
      for (int level = 1; level <= (hp == 0 ? 10 : 20); ++level)
        nominal[level] = frequency.automatic_frequency_hz(hp == 0, 2, level);
      feedback[hp] =
          observers[hp].observe(nominal, hp == 0 ? 3 : 4, {now, 2, 2, actual, 8, 35, true, false, may_learn, now, 0});
    }
  };
  const auto input = [&](const CandidateModel& model, float requested) {
    DispatchInput in;
    in.requested_w = requested;
    in.duo = in.performance_valid = in.lead_hp1 = true;
    in.hp1.candidate = in.hp2.candidate = {2, true, false, false, true};
    in.pair_power = &CandidateModel::dispatch;
    in.pair_context = &model;
    for (unsigned hp = 0; hp < 2; ++hp) {
      auto& out = hp == 0 ? in.hp1 : in.hp2;
      out.levels[0] = {true, true, true, 0, 0};
      for (int level = 1; level <= 2; ++level) {
        const auto p = model.unit(hp == 0, level);
        out.levels[level] = {true,        p.valid(),      p.valid(),
                             p.thermal_w, p.electrical_w, static_cast<uint8_t>(model.hz[hp][level])};
      }
    }
    return in;
  };
  const DispatchTuning limits{6000, 6500};
  for (uint32_t now = 1000; now <= 901000; now += 10000) observe(now, 45, now >= 121000);
  CandidateModel startup(frequency, 8, 30, 800, {35, 35}, true, false, feedback);
  auto in = input(startup, 4500);
  assert(startup.pair(2, 0).thermal_w > 4500);
  assert(CandidateModel::confirmed_power(&startup, 2, 0) < 4500);
  auto out = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
  assert(out.hp1_level > 0 && out.hp2_level > 0);
  // Qualifying a normal response before an autonomous stop must not allow a
  // later startup plateau to shed the peer, even with unchanged Heating mode.
  const auto original = observers;
  for (int fault = 0; fault < 2; ++fault) {
    observers = {};
    for (uint32_t now = 1000; now <= 161000; now += 10000) observe(now, 26, true);
    for (unsigned hp = 0; hp < 2; ++hp) {
      std::array<uint8_t, 21> nominal{};
      for (int level = 1; level <= (hp == 0 ? 10 : 20); ++level)
        nominal[level] = frequency.automatic_frequency_hz(hp == 0, 2, level);
      observers[hp].observe(nominal, hp == 0 ? 3 : 4, {171000, 2, 2, 0, 8, 35, fault == 0, false, true, 171000, 0});
    }
    for (uint32_t now = 181000; now <= 521000; now += 10000) observe(now, 45, true);
    const CandidateModel restart(frequency, 8, 30, 800, {35, 35}, true, false, feedback);
    assert(restart.confirmed_hz[0][2] == 26 && restart.confirmed_hz[1][2] == 26);
    out = decide_dispatch(input(restart, 4500), limits, {}, &CandidateModel::confirmed_power);
    assert(out.hp1_level > 0 && out.hp2_level > 0);
  }
  observers = original;
  observe(901000, 45, true);
  // Unknown conservative capacity fails closed while a valid Duo can serve.
  const auto unknown_power = [](const void*, int, int, float) { return NAN; };
  out = decide_dispatch(in, limits, {}, unknown_power);
  assert(out.hp1_level > 0 && out.hp2_level > 0);
  // An unavailable peer/forced safety stop still permits the remaining HP.
  in.hp2.candidate.must_stop = true;
  out = decide_dispatch(in, limits, {}, unknown_power);
  assert(out.hp1_level > 0 && out.hp2_level == 0);
  in = input(startup, 2000);
  out = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
  assert(topology(out.hp1_level, out.hp2_level) == 1);
  // A real peak constraint may require Single despite a thermal deficit.
  in = input(startup, 4500);
  in.hp1.feedback_hold_level = in.hp2.feedback_hold_level = 2;
  const auto one = startup.pair(2, 0), two = startup.pair(2, 2);
  const float peak = (one.electrical_w + two.electrical_w) / 2;
  out = decide_dispatch(in, {peak, peak}, {}, &CandidateModel::confirmed_power);
  assert(topology(out.hp1_level, out.hp2_level) == 1);
  // Ordinary operation qualifies before an oil-return boost. Its live output
  // remains visible, but does not replace the normal confirmed capacity.
  for (uint32_t now = 911000; now <= 951000; now += 10000) observe(now, 26, true);
  for (uint32_t now = 961000; now <= 1251000; now += 10000) observe(now, 50, false);
  CandidateModel oil(frequency, 8, 30, 800, {35, 35}, true, false, feedback);
  out = decide_dispatch(input(oil, 4500), limits, {}, &CandidateModel::confirmed_power);
  assert(out.hp1_level > 0 && out.hp2_level > 0);
  assert(oil.hz[0][2] == 50 && oil.confirmed_hz[0][2] == 26);
  // Persistent 48Hz feedback still permits the requested Duo -> Single fix.
  for (uint32_t now = 1261000; now <= 1311000; now += 10000) observe(now, 48, true);
  CandidateModel fixed(frequency, 8, 30, 800, {35, 35}, true, false, feedback);
  out = decide_dispatch(input(fixed, 4500), limits, {}, &CandidateModel::confirmed_power);
  assert(topology(out.hp1_level, out.hp2_level) == 1);
  // Even sufficient normal capacity is not permission to interrupt defrost on
  // either owner. A forced safety stop still has priority over this protection.
  in = input(fixed, 2000);
  in.hp2.defrost = in.hp2.valve_defrost = true;
  out = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
  assert(out.hp1_level > 0 && out.hp2_level > 0);
  in.hp2.candidate.must_stop = true;
  out = decide_dispatch(in, limits, {}, &CandidateModel::confirmed_power);
  assert(out.hp1_level > 0 && out.hp2_level == 0);
}
void test_inactive_outlet_is_not_required() {
  using namespace oq_water_performance;
  using namespace oq_power_house_dispatch;
  const Frequency frequency;
  for (unsigned hp = 0; hp < 2; ++hp) {
    std::array<float, 2> outlet{NAN, NAN};
    outlet[hp] = 35;
    std::array<oq_compressor_feedback::Snapshot, 2> feedback{};
    feedback[hp].requested_hz = 26;
    feedback[hp].actual_hz = 48;  // Exercise the separate confirmed-power route.
    const CandidateModel model(frequency, 8, NAN, NAN, outlet, false, false, feedback);
    const int first = hp == 0 ? 2 : 0, second = hp == 1 ? 2 : 0;
    assert(model.pair(first, second).valid());
    assert(!model.pair(2, 2).valid());  // Missing active data still fails closed.
    assert(std::isfinite(CandidateModel::confirmed_power(&model, first, second)));
    DispatchInput in;
    in.requested_w = 1000;
    in.duo = in.lead_hp1 = true;
    in.performance_valid = model.pair(first, second).valid();
    in.hp1.candidate = {first, true, false, false, true};
    in.hp2.candidate = {second, true, false, false, true};
    in.pair_context = &model;
    for (unsigned unit = 0; unit < 2; ++unit) {
      auto& dest = unit == 0 ? in.hp1 : in.hp2;
      dest.levels[0] = {true, true, true, 0, 0};
      for (int level = 1; level <= 2; ++level) {
        const auto p = model.unit(unit == 0, level);
        dest.levels[level] = {true, p.valid(), p.valid(), p.thermal_w, p.electrical_w};
      }
    }
    const auto out = decide_dispatch(in, {6000, 6500}, {}, &CandidateModel::confirmed_power);
    assert(out.output_valid && out.hp1_level == (hp == 0 ? 1 : 0) && out.hp2_level == (hp == 1 ? 1 : 0));
    // Coupled predictions still need their real water boundary.
    const CandidateModel coupled(frequency, 8, NAN, 800, outlet, true, false);
    assert(!coupled.pair(first, second).valid());
  }
}
void test_mixed_response_selects_duo_or_single_for_actual_capacity() {
  using namespace oq_water_performance;
  using namespace oq_power_house_dispatch;
  struct BothOldFrequency : Frequency {
    oq_odu::Variant performance_variant(bool) const { return oq_odu::Variant::V2_OLD_MODEL; }
  } frequency;
  std::array<oq_compressor_feedback::Snapshot, 2> feedback{};
  for (auto& f : feedback) {
    f.hz[1] = 20;
    f.hz[2] = 48;
    f.confirmed_mask = (1U << 1) | (1U << 2);
    f.requested_hz = 26;
    f.actual_hz = 48;
    f.qualified_run = true;
  }
  const CandidateModel model(frequency, 8, 30, 800, {35, 39}, true, false, feedback);
  for (float demand : {4500.0f, 5000.0f}) {
    DispatchInput in;
    in.requested_w = demand;
    in.duo = in.performance_valid = in.lead_hp1 = true;
    in.hp1.candidate = in.hp2.candidate = {2, true, false, false, true};
    in.pair_context = &model;
    in.pair_power = &CandidateModel::dispatch;
    for (unsigned hp = 0; hp < 2; ++hp) {
      auto& input = hp == 0 ? in.hp1 : in.hp2;
      input.levels[0] = {true, true, true, 0, 0};
      for (int level = 1; level <= 2; ++level) {
        const auto p = model.unit(hp == 0, level);
        input.levels[level] = {true, p.valid(), p.valid(), p.thermal_w, p.electrical_w};
      }
    }
    const auto out = decide_dispatch(in, {6000, 6500}, {}, &CandidateModel::confirmed_power);
    // With a working 20Hz point, Duo20 matches 4.5kW better than Single48.
    // At 5kW the confirmed Single26->48 point is the appropriate selection.
    if (demand == 4500)
      assert(out.hp1_level == 1 && out.hp2_level == 1);
    else
      assert((out.hp1_level == 2 && out.hp2_level == 0) || (out.hp1_level == 0 && out.hp2_level == 2));
  }
}
void test_startup_oil_and_restart_closed_loop() {
  using namespace oq_water_performance;
  using namespace oq_power_house_dispatch;
  const Frequency frequency;
  for (uint32_t minimum_on : {0U, 300000U}) {
    for (uint32_t cadence : {10000U, 31000U}) {
      std::array<oq_compressor_feedback::Observer, 2> observers;
      std::array<int, 2> applied{2, 2}, starts{1, 1};
      std::array<uint32_t, 2> started{1000, 1000}, stopped{}, changed{1000, 1000};
      std::array<float, 2> received{};
      uint32_t receipt_ms = 0, dispatched_ms = 0;
      bool high_duo_seen = false, low_single_seen = false;
      for (uint32_t now = 1000; now <= 3601000; now += 1000) {
        const bool oil = now >= 600000 && now < 720000;
        const bool oil_tail = now >= 600000 && now < 840000;
        std::array<oq_compressor_feedback::Snapshot, 2> feedback;
        for (unsigned hp = 0; hp < 2; ++hp) {
          const uint32_t startup = hp == 1 && starts[hp] == 1 ? 300000 : 60000;
          const int nominal_hz = frequency.automatic_frequency_hz(hp == 0, 2, applied[hp]);
          const int actual = applied[hp] == 0 ? 0 : oil ? 50 : now - started[hp] < startup ? 45 : nominal_hz;
          if (now % 10000 == 1000) received[hp] = actual;
          std::array<uint8_t, 21> nominal{};
          for (int level = 1; level <= 2; ++level) nominal[level] = frequency.automatic_frequency_hz(hp == 0, 2, level);
          if (now % 10000 == 1000) receipt_ms = now;
          feedback[hp] = observers[hp].observe(
              nominal, hp == 0 ? 3 : 4,
              {now, applied[hp], actual > 0 ? 2 : 0, received[hp], 8, 35, true, false,
               !oil_tail && now - started[hp] >= oq_compressor_feedback::kStartupMs, receipt_ms, changed[hp]});
        }
        if (now - dispatched_ms < cadence) continue;
        dispatched_ms = now;
        const CandidateModel model(frequency, 8, 30, 800, {35, 35}, true, false, feedback);
        DispatchInput in;
        in.now_ms = now;
        in.requested_w = now < 650000 ? 9000 : now < 1000000 ? 2000 : 4500;
        in.duo = in.performance_valid = in.lead_hp1 = true;
        in.pair_power = &CandidateModel::dispatch;
        in.pair_context = &model;
        for (unsigned hp = 0; hp < 2; ++hp) {
          auto& input = hp == 0 ? in.hp1 : in.hp2;
          input.candidate = {applied[hp], true, false, false, true};
          input.candidate.minimum_off_ready = applied[hp] > 0 || stopped[hp] == 0 || now - stopped[hp] >= 240000;
          input.stop_inhibited = applied[hp] > 0 && now - started[hp] < minimum_on;
          input.feedback_hold_level = feedback[hp].hold_level(applied[hp], true, true, true);
          in.performance_valid &= !feedback[hp].missing;
          input.levels[0] = {true, true, true, 0, 0};
          for (int level = 1; level <= 2; ++level) {
            const auto p = model.unit(hp == 0, level);
            input.levels[level] = {true,        p.valid(),      p.valid(),
                                   p.thermal_w, p.electrical_w, static_cast<uint8_t>(model.hz[hp][level])};
          }
        }
        const auto out = decide_dispatch(in, {6000, 6500}, {}, &CandidateModel::confirmed_power);
        if (now > 900000 && now < 1000000) {
          assert(topology(out.hp1_level, out.hp2_level) == 1);
          low_single_seen = true;
        }
        if (now >= 1000000) {
          if (topology(out.hp1_level, out.hp2_level) == 2) high_duo_seen = true;
          if (high_duo_seen) assert(out.hp1_level > 0 && out.hp2_level > 0);
        }
        for (unsigned hp = 0; hp < 2; ++hp) {
          const int next = hp == 0 ? out.hp1_level : out.hp2_level;
          if (next == applied[hp]) continue;
          if (next == 0) {
            assert(now - started[hp] >= minimum_on);
            stopped[hp] = now;
          } else if (applied[hp] == 0) {
            assert(stopped[hp] == 0 || now - stopped[hp] >= 240000);
            started[hp] = now;
            ++starts[hp];
          }
          changed[hp] = now;
          applied[hp] = next;
        }
      }
      assert(low_single_seen && high_duo_seen && topology(applied[0], applied[1]) == 2);
      assert(starts[0] + starts[1] == 3);
    }
  }
}
void test_temporary_frequency_override_preserves_run_extension_margin() {
  using namespace oq_power_house_dispatch;
  namespace extension = oq_power_house_run_extension;
  using oq_water_performance::CandidateModel;
  struct V1Frequency {
    oq_odu::Variant performance_variant(bool) const { return oq_odu::Variant::V1; }
    int automatic_frequency_hz(bool, int, int level) const {
      return level <= 10 ? oq_odu::V1_HEATING_DEMAND_REFERENCE_HZ[level] : 0;
    }
  };
  struct Sample {
    int applied_level, measured_hz;
    bool feedback_settling;
    float room_c, outside_c, outlet_c, base_w, margin_c;
    bool f1_response_confirmed = false;
  };
  // Cover the F1 override from #977 and two recorder-inspired F3 settling cases.
  const Sample samples[] = {
      {1, 44, false, 21.14f, 5.0f, 36.0f, 790.0f, 0.6f},
      {3, 60, true, 21.02734f, 4.93f, 30.63f, 2634.073f, 0.5f},
      {3, 48, true, 21.00781f, 4.49f, 29.12f, 2753.972f, 0.5f},
      {1, 44, false, 21.14f, 5.0f, 36.0f, 790.0f, 0.6f, true},
  };
  const V1Frequency frequency;
  const DispatchTuning limits{3500, 4000};
  for (const auto& sample : samples) {
    std::array<oq_compressor_feedback::Snapshot, 2> feedback{};
    auto& measured = feedback[0];
    measured.requested_hz = frequency.automatic_frequency_hz(true, 2, sample.applied_level);
    measured.actual_hz = sample.measured_hz;
    measured.settling = sample.feedback_settling;
    if (sample.f1_response_confirmed) {
      measured.confirmed_mask = 1U << 1;
      measured.hz[1] = sample.measured_hz;
    }
    const CandidateModel model(frequency, sample.outside_c, NAN, NAN, {sample.outlet_c, NAN}, false, false, feedback);
    DispatchInput in;
    in.requested_w = sample.base_w;
    in.performance_valid = true;
    in.hp1.candidate = {sample.applied_level, true, false, false, true};
    in.hp1.feedback_hold_level = measured.hold_level(sample.applied_level, true, true, true);
    in.pair_context = &model;
    for (int level = 1; level <= 10; ++level) {
      const auto p = model.unit(true, level);
      in.hp1.levels[level] = {true,        p.valid(),      p.valid(),
                              p.thermal_w, p.electrical_w, static_cast<uint8_t>(model.hz[0][level])};
    }
    const extension::State running{extension::Phase::RUNNING, 21.0f, true};
    const extension::Tuning tuning{sample.margin_c, 0.3f, 0.2f};
    const auto live = minimum_active_candidate(in, limits);
    assert(live.valid && live.hp1_level > 1 && sample.base_w < live.thermal_w);
    const auto evaluate = [&](extension::State state) {
      const auto minimum = run_extension_minimum(in, limits, sample.base_w, &CandidateModel::reference_f1_dispatch);
      const extension::Input input{
          true, true, true, true, sample.room_c, 21.0f, sample.base_w, minimum.thermal_w, minimum.comfort_level, 1.0f};
      return extension::evaluate(input, tuning, state);
    };
    const auto continued = evaluate(running);
    if (sample.f1_response_confirmed) {
      assert(continued.force_comfort_stop && continued.comfort_stop_c == 21.0f);
      continue;
    }
    assert(continued.next.phase == extension::Phase::EXTENDING && !continued.force_comfort_stop);
    assert(continued.floor_active && continued.floor_w == live.thermal_w);
    assert(continued.comfort_stop_c == 21.0f + sample.margin_c);
    assert(continued.warm_restart_c == continued.comfort_stop_c - tuning.restart_cooldown_c);

    in.hp1.levels[1].allowed = false;
    const auto prohibited = evaluate(continued.next);
    assert(prohibited.force_comfort_stop && prohibited.comfort_stop_c == 21.0f);
    in.hp1.levels[1].allowed = true;
    const auto latched = evaluate(prohibited.next);
    assert(latched.force_comfort_stop && latched.comfort_stop_c == 21.0f);
  }
}

int main() {
  test_temporary_frequency_override_preserves_run_extension_margin();
  test_mixed_response_selects_duo_or_single_for_actual_capacity();
  test_inactive_outlet_is_not_required();
  test_frequency_feedback_changes_duo_selection();
  test_water_limit_allows_modulation_during_feedback_hold();
  test_peer_defrost_allows_modulation_during_feedback_hold();
  test_model_boundary_allows_lower_duo_levels();
  test_missing_feedback_allows_peer_start();
  test_feedback_closed_loop();
  test_temporary_boost_cannot_shed_a_needed_peer();
  test_startup_oil_and_restart_closed_loop();
  using namespace oq_water_performance;
  constexpr std::array<oq_odu::Variant, 2> variants{oq_odu::Variant::V2_OLD_MODEL, oq_odu::Variant::V2_NEW_MODEL};
  constexpr float flow = 870.144f, capacity = flow * 4186.0f / 3600.0f;
  const auto own1 = observed(variants[0], 90, -7, 35.77f);
  const auto own2 = observed(variants[1], 82, -7, 40.75f);
  assert(own1.valid() && own2.valid());
  assert(std::fabs(own1.thermal_w - 5812.774f) < .1f);
  assert(std::fabs(own2.thermal_w - 4985.13f) < .1f);

  const auto duo = series(variants, {90, 82}, -7, 30, flow);
  assert(duo[0].valid() && duo[1].valid());
  assert(std::fabs(duo[0].thermal_w + duo[1].thermal_w - 10803.54156f) < 2);
  assert(std::fabs(duo[0].electrical_w + duo[1].electrical_w - 4136.95535f) < 2);
  for (unsigned hp = 0; hp < 2; ++hp) {
    const float inlet = hp == 0 ? 30 : duo[0].outlet_c;
    assert(std::fabs(capacity * (duo[hp].outlet_c - inlet) - duo[hp].thermal_w) <= 1.0f);
  }
  assert(duo[1].outlet_c > duo[0].outlet_c);
  assert(duo[1].thermal_w < solve(variants[1], 82, -7, 30, flow).thermal_w);

  for (const std::array<float, 2> hz : {std::array<float, 2>{30, 0}, {0, 30}, {0, 0}}) {
    const auto pair = series(variants, hz, -7, 30, flow);
    assert(pair[0].valid() && pair[1].valid());
    if (hz[0] == 0) assert(pair[0].thermal_w == 0 && pair[0].outlet_c == 30);
    if (hz[1] == 0) assert(pair[1].thermal_w == 0 && pair[1].outlet_c == pair[0].outlet_c);
  }
  assert(!observed(variants[0], 110.1f, -7, 35).valid());
  assert(!observed(variants[0], 90, NAN, 35).valid());
  assert(!observed(variants[0], 20, -7, 30).valid());  // Existing masked map point.
  assert(!solve(variants[0], 90, -7, 30, 299).valid());
  assert(!solve(variants[0], 90, -7, 30, NAN).valid());
  assert(!solve(variants[0], 90, -7, 100, flow).valid());

  // The initial upper guess crosses 49 C, but the actual F1 root is supported.
  for (auto variant : variants) {
    for (const auto point :
         {std::array<float, 3>{47.2f, 1000, 48.948952f}, {42.5f, 300, 48.402f}, {47.0f, 900, 48.94352f}}) {
      const auto root = solve(variant, 20, 14.6f, point[0], point[1]);
      assert(root.valid() && std::fabs(root.outlet_c - point[2]) < .01f);
      assert(std::fabs(point[1] * 4186 / 3600 * (root.outlet_c - point[0]) - root.thermal_w) <= 1);
    }
    // Clipping the search bracket must never invent a root outside the map.
    assert(!solve(variant, 20, 14.6f, 47.5f, 1000).valid());
    assert(!solve(variant, 20, 14.6f, 49, 1000).valid());
  }

  for (auto variant : {oq_odu::Variant::V1, oq_odu::Variant::V1_5}) {
    const auto cold = solve(variant, 90, -15, 35, 800);
    assert(cold.valid() && cold.outlet_c > 37 && cold.outlet_c < 39);
    assert(std::fabs(800 * 4186.0f / 3600 * (cold.outlet_c - 35) - cold.thermal_w) <= 1);
  }
  const auto cool_return = solve(variants[0], 30, 7, 15, 300);
  assert(cool_return.valid() && std::fabs(cool_return.outlet_c - 25.06614f) < .01f);
  assert(std::fabs(300 * 4186.0f / 3600 * (cool_return.outlet_c - 15) - cool_return.thermal_w) <= 1);
  assert(!solve(variants[0], 30, 7, 0, 300).valid());
  assert(!solve(variants[0], 72, -7, 65, 500).valid());  // New envelope caps this region at 60 Hz.
  const auto hot_return = solve(variants[0], 48, -7, 65, 500);
  assert(hot_return.valid() && std::fabs(hot_return.outlet_c - 68.13822f) < .01f);
  assert(std::fabs(500 * 4186.0f / 3600 * (hot_return.outlet_c - 65) - hot_return.thermal_w) <= 1);
  assert(solve(variants[0], 90, -7, 10, flow, true).valid());  // Existing cold-start boundary remains usable.
  // Extended V2-new heating frequencies (95/102/110 Hz, F18-F20) must remain
  // usable through the water-chain model in both coupled and uncoupled modes.
  // TODO needs better test setup
  // for (int level = 18; level <= 20; ++level) {
  //   const Frequency extended;
  //   const CandidateModel coupled(extended, -7, 28, flow, {32.77f, 34.75f}, true, false);
  //   const auto pair = coupled.pair(10, level);
  //   assert(pair.valid() && pair.thermal_w > 0 && pair.electrical_w > 0);
  //   assert(coupled.unit(false, level).valid());
  //
  //   const CandidateModel independent(extended, -7, 30, NAN, {32.77f, 34.75f}, false, false);
  //   assert(independent.unit(false, level).valid());
  //   assert(independent.pair(10, level).valid());
  // }
  //
  // // At higher outlet temperatures, the uppermost two levels are invalid
  // for (int level = 19; level <= 20; ++level) {
  //   const Frequency extended;
  //   const CandidateModel coupled(extended, -7, 30, flow, {35.77f, 40.75f}, true, false);
  //   const auto pair = coupled.pair(10, level);
  //   assert(!pair.valid());
  // }

  const Frequency frequency;
  const CandidateModel model(frequency, -7, 30, flow, {35.77f, 40.75f}, true, false);
  oq_power_house_dispatch::DispatchInput in;
  in.duo = in.performance_valid = true;

  in.requested_w = 8000;
  in.hp1.candidate = in.hp2.candidate = {0, true, false, false, true};
  in.pair_power = &CandidateModel::dispatch;
  in.pair_context = &model;
  for (unsigned hp = 0; hp < 2; ++hp)
    for (int level = 0; level <= 20; ++level) {
      const int hz = level == 0 ? 0 : frequency.automatic_frequency_hz(hp == 0, 2, level);
      const auto p = model.unit(hp == 0, level);
      (hp == 0 ? in.hp1 : in.hp2).levels[level] = {hz >= 0 && hz <= 90, p.valid(),
                                                   p.valid(),           p.thermal_w,
                                                   p.electrical_w,      static_cast<uint8_t>(std::max(0, hz))};
    }
  const oq_power_house_dispatch::DispatchTuning limits{6000, 6500};
  const auto pair = oq_power_house_dispatch::make_candidate(in, limits, 10, 15, 8000);
  assert(pair.valid && std::fabs(pair.thermal_w - 10803.54156f) < 2);
  const auto chosen = oq_power_house_dispatch::decide_dispatch(in, limits, {});
  assert(chosen.output_valid && chosen.hp1_level + chosen.hp2_level > 0);
  assert(std::fabs(chosen.expected_w - model.pair(chosen.hp1_level, chosen.hp2_level).thermal_w) < .01f);
  in.hp2.candidate.minimum_off_ready = false;
  assert(!oq_power_house_dispatch::make_candidate(in, limits, 10, 15, 8000).valid);
  in.hp2.candidate.minimum_off_ready = true;
  in.hp2.levels[15].allowed = false;
  assert(!oq_power_house_dispatch::make_candidate(in, limits, 10, 15, 8000).valid);
  in.hp2.levels[15].allowed = true;
  assert(!oq_power_house_dispatch::make_candidate(in, {1000, 1100}, 10, 15, 8000).valid);
  auto peak = in;
  peak.duo = false;
  peak.hp1.candidate.previous_applied_level = 1;
  for (auto& level : peak.hp1.levels) level.allowed = false;
  peak.hp1.levels[1].allowed = true;
  peak.pair_power = [](const void*, int first, int) -> std::array<float, 2> {
    return first > 0 ? std::array<float, 2>{1000, 2000} : std::array<float, 2>{0, 0};
  };
  const auto blocked = oq_power_house_dispatch::decide_dispatch(peak, {900, 1000}, {});
  assert(blocked.hp1_level == 0 && blocked.reason == oq_power_house_dispatch::Reason::NO_CANDIDATE);
  peak.pair_power = [](const void*, int first, int) -> std::array<float, 2> {
    return first > 0 ? std::array<float, 2>{NAN, NAN} : std::array<float, 2>{0, 0};
  };
  const auto missing = oq_power_house_dispatch::decide_dispatch(peak, {900, 1000}, {});
  assert(missing.hp1_level == 1 && missing.reason == oq_power_house_dispatch::Reason::FALLBACK_HP1);
  const CandidateModel independent(frequency, -7, 30, NAN, {35.77f, 40.75f}, false, false);
  assert(std::fabs(independent.unit(false, 15).thermal_w - own2.thermal_w) < .01f);
}
