#include <assert.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "../../openquatt/includes/performance/hp_perf_map_v2.h"

namespace {

bool near(float actual, float expected, float absolute_tolerance, float relative_tolerance = 0.000001f) {
  return std::fabs(actual - expected) <=
         std::max(absolute_tolerance, relative_tolerance * std::max(std::fabs(actual), std::fabs(expected)));
}

void test_axes_and_all_source_nodes() {
  using namespace oq_v2_model;
  static_assert(sizeof(data::PTH_W) + sizeof(data::COP) + sizeof(data::FREQUENCY_HZ) + sizeof(data::AMBIENT_C) +
                    sizeof(data::SUPPLY_C) ==
                11160U);
  for (size_t i = 1; i < 10; ++i) assert(data::FREQUENCY_HZ[i] > data::FREQUENCY_HZ[i - 1]);
  for (size_t i = 1; i < 17; ++i) assert(data::AMBIENT_C[i] > data::AMBIENT_C[i - 1]);
  for (size_t i = 1; i < 8; ++i) assert(data::SUPPLY_C[i] > data::SUPPLY_C[i - 1]);

  size_t valid_nodes = 0;
  size_t masked_nodes = 0;
  for (size_t supply = 0; supply < 8; ++supply) {
    for (size_t ambient = 0; ambient < 17; ++ambient) {
      for (size_t frequency = 0; frequency < 10; ++frequency) {
        const float expected_pth = data::PTH_W[supply][ambient][frequency];
        const float expected_cop = data::COP[supply][ambient][frequency];
        assert(std::isfinite(expected_pth) == std::isfinite(expected_cop));
        const auto prediction =
            predict_heating(data::FREQUENCY_HZ[frequency], data::AMBIENT_C[ambient], data::SUPPLY_C[supply]);
        if (!std::isfinite(expected_pth)) {
          assert(prediction.status == Status::MASKED);
          ++masked_nodes;
          continue;
        }
        assert(prediction.status == Status::VALID);
        assert(prediction.pth_w == expected_pth);
        assert(prediction.cop == expected_cop);
        assert(near(prediction.pel_w, expected_pth / expected_cop, 0.001f));
        ++valid_nodes;
      }
    }
  }
  assert(valid_nodes == 590U);
  assert(masked_nodes == 770U);
}

void test_reference_and_boundary_points() {
  using namespace oq_v2_model;
  const auto issue_point = predict_heating(20.0, 12.6, 22.5);
  assert(issue_point.status == Status::VALID);
  assert(near(issue_point.pth_w, 3072.6185f, 0.02f));
  assert(near(issue_point.cop, 7.064095f, 0.0002f));
  assert(near(issue_point.pel_w, 434.9601f, 0.02f));

  const auto hp1_slot_four = predict_heating(43.0, 12.6, 22.5);
  const auto hp2_slot_four = predict_heating(48.0, 12.6, 22.5);
  assert(hp1_slot_four.status == Status::VALID && hp2_slot_four.status == Status::VALID);
  assert(near(hp1_slot_four.pth_w, 5761.47f, 0.05f));
  assert(near(hp2_slot_four.pth_w, 6346.00f, 0.05f));

  assert(predict_heating(20.0, 12.6, 54.999999).status == Status::VALID);
  assert(predict_heating(20.0, 12.6, 55.0).status == Status::MASKED);
  assert(predict_heating(20.0, 12.6, 65.0).status == Status::MASKED);
  const auto high_supply = predict_heating(30.0, 12.6, 65.0);
  assert(high_supply.status == Status::VALID);
  assert(near(high_supply.pth_w, 2126.80f, 0.05f));

  assert(predict_heating(0.0, NAN, NAN).status == Status::OFF);
  assert(predict_heating(-1.0, 12.6, 22.5).status == Status::INVALID_INPUT);
  assert(predict_heating(INFINITY, 12.6, 22.5).status == Status::INVALID_INPUT);
  assert(predict_heating(19.99, 12.6, 22.5).status == Status::OUTSIDE_DOMAIN);
  assert(predict_heating(90.01, 12.6, 22.5).status == Status::OUTSIDE_DOMAIN);
  assert(predict_heating(20.0, -15.01, 22.5).status == Status::OUTSIDE_DOMAIN);
  assert(predict_heating(20.0, 12.6, 70.01).status == Status::OUTSIDE_DOMAIN);

  assert(predict_heating_for_control(20.0, 12.6, 16.0, false).status == Status::OUTSIDE_DOMAIN);
  const auto cold_boundary = predict_heating_for_control(20.0, 12.6, 16.0, true);
  const auto exact_boundary = predict_heating(20.0, 12.6, 18.0);
  assert(cold_boundary.status == Status::VALID);
  assert(cold_boundary.low_supply_boundary_estimate);
  assert(cold_boundary.pth_w == exact_boundary.pth_w);
}

}  // namespace

int main() {
  test_axes_and_all_source_nodes();
  test_reference_and_boundary_points();
  return 0;
}
