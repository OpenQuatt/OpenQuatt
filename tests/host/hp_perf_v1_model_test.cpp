#include <assert.h>

#include <cmath>
#include <limits>
#include <string>

struct FakeGenerationSelect {
  bool has_state() const { return true; }
  std::string current_option() const { return "V1.5"; }
} hp_generation;

#define id(value) value
#include "../../openquatt/includes/performance/hp_perf_frequency.h"
#undef id

namespace {

void assert_value(float actual, float expected) {
  if (std::isnan(expected)) {
    assert(std::isnan(actual));
  } else {
    assert(std::isfinite(actual));
    assert(std::fabs(actual - expected) < 0.001f);
  }
}

void assert_raw_nodes() {
  // Every exact node must depend only on that node, including missing nodes.
  for (int ambient = 0; ambient < oq_perf_v1::N_AMB; ++ambient) {
    for (int supply = 0; supply < oq_perf_v1::N_SUP; ++supply) {
      for (int index = 0; index < oq_perf_v1::N_LVL; ++index) {
        const float power = oq_perf_v1::P_th_W[ambient][supply][index];
        const float cop = oq_perf_v1::COP[ambient][supply][index];
        const float outside_c = oq_perf_v1::T_amb_bp[ambient];
        const float supply_c = oq_perf_v1::T_sup_bp[supply];
        assert_value(oq_perf_v1::interp_power_th_w(index + 1, outside_c, supply_c), power);
        assert_value(oq_perf_v1::interp_cop(index + 1, outside_c, supply_c), cop);
        if (std::isfinite(power) && std::isfinite(cop))
          assert_value(oq_perf_v1::interp_power_el_w(index + 1, outside_c, supply_c), power / cop);
      }
    }
  }
}

void assert_missing_corner_dependencies() {
  struct Sample {
    float outside_c;
    float supply_c;
    float expected;
    unsigned required_corners;
  };
  // Plane 100 + 200*x + 100*y in the first temperature cell. The masks
  // independently specify which corner measurements each point requires.
  const Sample samples[] = {
      {-15.0f, 35.0f, 100.0f, 0x1}, {-7.0f, 35.0f, 300.0f, 0x2},  {-15.0f, 45.0f, 200.0f, 0x4},
      {-7.0f, 45.0f, 400.0f, 0x8},  {-11.0f, 35.0f, 200.0f, 0x3}, {-11.0f, 45.0f, 300.0f, 0xc},
      {-15.0f, 40.0f, 150.0f, 0x5}, {-7.0f, 40.0f, 350.0f, 0xa},  {-11.0f, 40.0f, 250.0f, 0xf},
      {-30.0f, 10.0f, 100.0f, 0x1}, {-30.0f, 40.0f, 150.0f, 0x5}, {-11.0f, 10.0f, 200.0f, 0x3},
  };
  float data[oq_perf_v1::N_AMB][oq_perf_v1::N_SUP][oq_perf_v1::N_LVL]{};
  for (unsigned missing = 0; missing < 16; ++missing) {
    data[0][0][0] = (missing & 0x1) ? NAN : 100.0f;
    data[1][0][0] = (missing & 0x2) ? NAN : 300.0f;
    data[0][1][0] = (missing & 0x4) ? NAN : 200.0f;
    data[1][1][0] = (missing & 0x8) ? NAN : 400.0f;
    for (const auto& sample : samples) {
      const float expected = (missing & sample.required_corners) ? NAN : sample.expected;
      assert_value(oq_perf_v1::interp_3d(data, 1, sample.outside_c, sample.supply_c), expected);
    }
  }
}

void assert_clamped_boundaries() {
  // Existing out-of-range behaviour remains clamping, with the same missing
  // data requirement as the exact boundary point.
  for (int level = 1; level <= oq_perf_v1::N_LVL; ++level) {
    for (const float outside : {-30.0f, -15.0f, 12.0f, 30.0f}) {
      for (const float supply : {10.0f, 35.0f, 55.0f, 70.0f}) {
        const int ambient_index = outside <= -15.0f ? 0 : oq_perf_v1::N_AMB - 1;
        const int supply_index = supply <= 35.0f ? 0 : oq_perf_v1::N_SUP - 1;
        assert_value(oq_perf_v1::interp_power_th_w(level, outside, supply),
                     oq_perf_v1::P_th_W[ambient_index][supply_index][level - 1]);
        assert_value(oq_perf_v1::interp_cop(level, outside, supply),
                     oq_perf_v1::COP[ambient_index][supply_index][level - 1]);
      }
    }
  }
}

void assert_frequency_dispatch() {
  for (const auto variant : {oq_odu::Variant::V1, oq_odu::Variant::V1_5}) {
    const auto node = oq_perf::predict_heating_hz(variant, 85.0f, -15.0f, 55.0f);
    assert(node.available);
    assert_value(node.pth_w, 2200.24f);
    assert_value(node.cop, 1.69f);
    assert_value(node.pel_w, 2200.24f / 1.69f);

    // Any positive contribution from the missing -7/W55/85-Hz node must
    // still make the interpolation unavailable, even just inside the cell.
    const float inside_ambient = std::nextafter(-15.0f, 0.0f);
    assert(!oq_perf::predict_heating_hz(variant, 85.0f, inside_ambient, 55.0f).available);
    const float inside_supply = std::nextafter(45.0f, 55.0f);
    assert(!oq_perf::predict_heating_hz(variant, 85.0f, -7.0f, inside_supply).available);
    assert(!oq_perf::predict_heating_hz(variant, 87.0f, -15.0f, 55.0f).available);
    assert(!oq_perf::predict_heating_hz(variant, 85.0f, 12.0f, 35.0f).available);
  }
  assert_value(oq_perf_v1::interp_power_th_w(0, -15.0f, 55.0f), 0.0f);
  assert_value(oq_perf_v1::interp_power_th_w(11, -15.0f, 35.0f), oq_perf_v1::P_th_W[0][0][9]);
  const float invalid = std::numeric_limits<float>::quiet_NaN();
  assert(std::isnan(oq_perf_v1::interp_power_th_w(1, invalid, 35.0f)));
  assert(std::isnan(oq_perf_v1::interp_power_th_w(1, -15.0f, invalid)));
}

}  // namespace

int main() {
  assert_raw_nodes();
  assert_missing_corner_dependencies();
  assert_clamped_boundaries();
  assert_frequency_dispatch();
  return 0;
}
