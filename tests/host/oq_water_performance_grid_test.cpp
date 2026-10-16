#include <cassert>
#include <cmath>
#include <string>

#include "../fixtures/v2_old_amh6_eeprom.h"
#include "../fixtures/v2_new_amh6_eeprom.h"
#include "includes/control/oq_compressor_frequency_runtime.h"

struct GenerationSelect {
  bool has_state() const { return true; }
  std::string current_option() const { return "V2"; }
} hp_generation;
#define id(value) value
#include "includes/performance/hp_perf_frequency.h"
#undef id
#include "includes/experimental/oq_water_performance_grid.h"

namespace {
using namespace oq_water_shadow;
Snapshot frame(bool reverse = false) {
  Snapshot s;
  s.now_ms = 100000;
  s.context_valid_until_ms = {129900, 129900};
  s.outside.observe(7, 99900, true);
  s.flow.observe(800, 99900, true);
  s.online = {true, true};
  s.heating = {true, true};
  s.order_confirmed = s.common_flow_confirmed = true;
  s.hp1_upstream = !reverse;
  for (unsigned hp = 0; hp < 2; ++hp) {
    const bool upstream = reverse ? hp == 1 : hp == 0;
    s.inlet[hp].observe(upstream ? 30 : 33, 99900, true);
    s.outlet[hp].observe(upstream ? 33 : 36, 99900, true);
    s.frequency[hp].observe(30, 99900, true);
  }
  return s;
}
GridAxes axes() { return {{{{0, 20, 30, 90}, {0, 26, 45, 90}}}, {4, 4}}; }
template <size_t N>
std::array<uint8_t, N * 2> response(const std::array<uint16_t, 512>& words, uint16_t address) {
  std::array<uint8_t, N * 2> result{};
  for (size_t i = 0; i < N; ++i) {
    const auto value = words[address - 2999 + i];
    result[2 * i] = value >> 8;
    result[2 * i + 1] = value & 255;
  }
  return result;
}
oq_odu::RuntimeFrequencySnapshot table(const std::array<uint16_t, 512>& words, oq_odu::Variant variant) {
  const auto base = response<oq_odu::BASE_FREQUENCY_TABLE_REGISTER_COUNT>(words, oq_odu::BASE_FREQUENCY_TABLE_REGISTER);
  auto result = oq_odu::parse_base_frequency_table_response(base.data(), base.size(), variant);
  if (variant == oq_odu::Variant::V2_NEW_MODEL) {
    const auto extension =
        response<oq_odu::EXTENDED_FREQUENCY_TABLE_REGISTER_COUNT>(words, oq_odu::EXTENDED_FREQUENCY_TABLE_REGISTER);
    assert(
        oq_odu::apply_v2_extension_frequency_table_response(result, extension.data(), extension.size()).heating_valid);
  }
  return result;
}
GridAxes hardware_axes() {
  const oq_frequency_runtime::Context context{false,
                                              table(oq_test::v2_old_amh6::EEPROM_WORDS, oq_odu::Variant::V2_OLD_MODEL),
                                              table(oq_test::v2_new_amh6::EEPROM_WORDS, oq_odu::Variant::V2_NEW_MODEL),
                                              90,
                                              {},
                                              {},
                                              oq_odu::Variant::V2_OLD_MODEL,
                                              oq_odu::Variant::V2_NEW_MODEL};
  GridAxes result;
  for (unsigned hp = 0; hp < 2; ++hp) {
    result.count[hp] = 1;
    for (int level = 1; level <= context.automatic_level_limit(hp == 0, 2); ++level)
      if (context.frequency_allowed(hp == 0, 2, level))
        result.hz[hp][result.count[hp]++] = context.automatic_frequency_hz(hp == 0, 2, level);
  }
  assert(result.count[0] * result.count[1] == 198);
  return result;
}
ModelPoint native(unsigned hp, float hz, float outside, float outlet) {
  const auto p = oq_perf::predict_heating_hz(hp == 0 ? oq_odu::Variant::V2_OLD_MODEL : oq_odu::Variant::V2_NEW_MODEL,
                                             hz, outside, outlet);
  return {p.pth_w, p.pel_w, p.available, p.low_supply_boundary_estimate};
}
void cached_grid_matches_reference() {
  for (bool reverse : {false}) {
    auto snapshot = frame(reverse);
    for (const auto& input_axes : {axes(), hardware_axes()}) {
      for (float outside : {7.0f, -7.0f}) {
        snapshot.outside.value = outside;
        GridJob job;
        job.begin(snapshot, input_axes);
        unsigned calls_cached = 0, calls_reference = 0, valid_pairs = 0;
        const auto cached = [&](unsigned hp, float hz, float ambient, float outlet) {
          ++calls_cached;
          return native(hp, hz, ambient, outlet);
        };
        const auto reference = [&](unsigned hp, float hz, float ambient, float outlet) {
          ++calls_reference;
          return native(hp, hz, ambient, outlet);
        };
        while (job.running) {
          const auto actual = job.step(snapshot.now_ms, cached);
          const auto expected = predict_series(snapshot, {float(job.last_hz[0]), float(job.last_hz[1])}, {}, reference);
          assert(actual.status == expected.status);
          valid_pairs += expected.valid();
          if (expected.valid()) {
            assert(std::fabs(actual.thermal_w - expected.thermal_w) < 0.001f);
            assert(std::fabs(actual.electrical_w - expected.electrical_w) < 0.001f);
            for (unsigned hp = 0; hp < 2; ++hp)
              assert(std::fabs(actual.unit[hp].outlet_c - expected.unit[hp].outlet_c) < 0.00001f);
          } else {
            assert(std::isnan(actual.thermal_w));
          }
        }
        const unsigned expected_pairs = input_axes.count[0] * input_axes.count[1];
        assert(job.complete && job.pairs == expected_pairs && job.valid_pairs == valid_pairs && valid_pairs > 1);
        assert(calls_cached < calls_reference);
        assert(job.unit_solves < 2 * expected_pairs);
      }
    }
  }
}
void lifecycle_and_expiration() {
  GridJob job;
  job.begin(frame(true), axes());
  assert(!job.running && job.status == Status::HYDRAULIC_MISMATCH);
  auto snapshot = frame();
  job.begin(snapshot, axes());
  job.step(snapshot.now_ms, native);
  const auto stale = job.step(130001, native);
  assert(stale.status == Status::STALE && !job.complete && !job.running && job.pairs == 1);
  snapshot.context_valid_until_ms[0] = 100010;
  job.begin(snapshot, axes());
  job.step(100010, native);
  assert(job.running);
  assert(job.step(100011, native).status == Status::STALE && !job.complete);
  snapshot = frame();
  job.begin(snapshot, axes());
  job.step(snapshot.now_ms, native);
  job.cancel();
  assert(!job.running && !job.complete && job.status == Status::DISABLED);
  snapshot.inlet[0].value = 31;
  job.begin(snapshot, axes());
  assert(job.pairs == 0 && job.unit_solves == 0);
  while (job.running) job.step(snapshot.now_ms, native);
  assert(job.complete && job.pairs == 16);
  snapshot.order_confirmed = false;
  job.begin(snapshot, axes());
  assert(!job.running && job.status == Status::UNKNOWN_ORDER);
  auto bad_axes = axes();
  bad_axes.hz[0][1] = 91;
  job.begin(frame(), bad_axes);
  assert(!job.running && job.status == Status::INVALID_INPUT);
  bad_axes = axes();
  bad_axes.count[1] = 22;
  job.begin(frame(), bad_axes);
  assert(!job.running && job.status == Status::INVALID_INPUT);
}
}  // namespace
int main() {
  cached_grid_matches_reference();
  lifecycle_and_expiration();
}
