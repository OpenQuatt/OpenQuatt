#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>

#include "../../openquatt/includes/learning/oq_ph_daily_gap_uncertainty.h"

namespace {
using oq_power_house::learning::daily_gap_uncertainty_ws;

void near(double actual, long double expected) {
  assert(std::isfinite(actual));
  const long double tolerance = 128.0L * std::numeric_limits<double>::epsilon() * std::fmax(1.0L, std::fabs(expected));
  if (std::fabs(static_cast<long double>(actual) - expected) > tolerance)
    std::fprintf(stderr, "near failed actual=%.17g expected=%.21Lg tolerance=%.21Lg\n", actual, expected, tolerance);
  assert(std::fabs(static_cast<long double>(actual) - expected) <= tolerance);
}

// Independent physical-units reference with an explicit zero-crossing time.
long double reference(double bound, double q0, double q1, uint64_t gap, uint64_t begin, uint64_t end) {
  const long double seconds = static_cast<long double>(gap) / 1000.0L;
  const long double a = static_cast<long double>(begin) / 1000.0L;
  const long double duration = static_cast<long double>(end - begin) / 1000.0L;
  const long double slope = (static_cast<long double>(q1) - q0) / seconds;
  if (slope == 0.0L) return (static_cast<long double>(bound) + std::fabs(q0)) * duration;
  const long double qa = q0 + slope * a;
  const long double qb = qa + slope * duration;
  long double area;
  if ((qa < 0 && qb > 0) || (qa > 0 && qb < 0)) {
    const long double until_zero = -qa / slope;
    area = (std::fabs(qa) * until_zero + std::fabs(qb) * (duration - until_zero)) / 2;
  } else {
    area = (std::fabs(qa) + std::fabs(qb)) * duration / 2;
  }
  return static_cast<long double>(bound) * duration + area;
}

void analytic_areas_and_crossings() {
  near(daily_gap_uncertainty_ws(10, 2, 6, 4000, 0, 4000), 56);
  near(daily_gap_uncertainty_ws(10, 2, 6, 4000, 1000, 3000), 28);
  near(daily_gap_uncertainty_ws(10, -2, -6, 4000, 0, 4000), 56);
  near(daily_gap_uncertainty_ws(10, 0, 10, 1000, 0, 1000), 15);
  near(daily_gap_uncertainty_ws(10, -10, 10, 4000, 0, 4000), 60);
  near(daily_gap_uncertainty_ws(10, -10, 10, 4000, 0, 2000), 30);
  near(daily_gap_uncertainty_ws(10, -10, 10, 4000, 2000, 4000), 30);
  near(daily_gap_uncertainty_ws(20, -4, 12, 4000, 0, 4000), 100);
  near(daily_gap_uncertainty_ws(20, -4, 12, 4000, 0, 500), 11.5);
  near(daily_gap_uncertainty_ws(20, -4, 12, 4000, 500, 2500), 45);
  // abs(mean) would incorrectly return96 for this sign-changing example.
  assert(daily_gap_uncertainty_ws(20, -4, 12, 4000, 0, 4000) > 96.0);
}

void constant_heat_keeps_old_arithmetic() {
  for (double heat : {-50000.0, -22000.0, -1.0, -0.0, 0.0, 1.0, 22000.0, 50000.0}) {
    for (uint64_t duration : {1ULL, 1000ULL, 20000ULL, 120000ULL}) {
      const double old = (50000.0 + std::fabs(heat)) * static_cast<double>(duration) / 1000.0;
      assert(daily_gap_uncertainty_ws(50000, heat, heat, 120000, 0, duration) == old);
      assert(daily_gap_uncertainty_ws(50000, heat, heat, 120000, 120000 - duration, 120000) == old);
    }
  }
  constexpr double budget = 100.0 * 86400.0;
  assert(daily_gap_uncertainty_ws(50000, 22000, 22000, 120000, 0, 120000) == budget);
  const double above = std::nextafter(22000.0, std::numeric_limits<double>::infinity());
  const double old_above = (50000.0 + above) * 120000.0 / 1000.0;
  assert(daily_gap_uncertainty_ws(50000, above, above, 120000, 0, 120000) == old_above);
}

void split_and_reversal_properties() {
  for (double q0 : {-50000.0, -12000.0, 0.0, 5000.0, 28000.0, 50000.0}) {
    for (double q1 : {-50000.0, -12000.0, 0.0, 5000.0, 28000.0, 50000.0}) {
      constexpr uint64_t total = 120000;
      for (uint64_t split : {1ULL, 20000ULL, 36000ULL, 60000ULL, 100000ULL, 119999ULL}) {
        const double first = daily_gap_uncertainty_ws(50000, q0, q1, total, 0, split);
        const double second = daily_gap_uncertainty_ws(50000, q0, q1, total, split, total);
        const double whole = daily_gap_uncertainty_ws(50000, q0, q1, total, 0, total);
        near(first, reference(50000, q0, q1, total, 0, split));
        near(second, reference(50000, q0, q1, total, split, total));
        near(first + second, whole);
        near(first, daily_gap_uncertainty_ws(50000, q1, q0, total, total - split, total));
        near(first, daily_gap_uncertainty_ws(50000, -q0, -q1, total, 0, split));
      }
    }
  }
}

void reference_and_old_max_comparison() {
  uint32_t random = 0x14649U;
  const auto next = [&random]() {
    random = random * 1664525U + 1013904223U;
    return random;
  };
  for (unsigned i = 0; i < 4000; ++i) {
    const double bound = i % 2 ? 50000.0 : 10.0;
    const double q0 = bound * (static_cast<int>(next() % 2001) - 1000) / 1000.0;
    const double q1 = bound * (static_cast<int>(next() % 2001) - 1000) / 1000.0;
    const uint64_t total = next() % 120000 + 1;
    const uint64_t begin = next() % total;
    const uint64_t end = begin + 1 + next() % (total - begin);
    const double actual = daily_gap_uncertainty_ws(bound, q0, q1, total, begin, end);
    const double old = (bound + std::fmax(std::fabs(q0), std::fabs(q1))) * (end - begin) / 1000.0;
    near(actual, reference(bound, q0, q1, total, begin, end));
    assert(actual <= old + 128.0 * std::numeric_limits<double>::epsilon() * std::fmax(1.0, old));
    assert(actual >= bound * ((end - begin) / 1000.0));
  }
  assert(daily_gap_uncertainty_ws(10, -10, 10, 4000, 0, 4000) < (10 + 10) * 4.0);
}

void invalid_and_zero_inputs() {
  const double infinity = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  for (double bad : {-1.0, infinity, -infinity, nan})
    assert(std::isnan(daily_gap_uncertainty_ws(bad, 0, 0, 1000, 0, 1000)));
  for (double bad : {11.0, -11.0, infinity, -infinity, nan}) {
    assert(std::isnan(daily_gap_uncertainty_ws(10, bad, 0, 1000, 0, 1000)));
    assert(std::isnan(daily_gap_uncertainty_ws(10, 0, bad, 1000, 0, 1000)));
  }
  assert(std::isnan(daily_gap_uncertainty_ws(10, 0, 0, 0, 0, 0)));
  assert(std::isnan(daily_gap_uncertainty_ws(10, 0, 0, 1000, 500, 400)));
  assert(std::isnan(daily_gap_uncertainty_ws(10, 0, 0, 1000, 0, 1001)));
  assert(std::isnan(daily_gap_uncertainty_ws(10, 0, 0, 1000, 1001, 1001)));
  assert(std::isnan(daily_gap_uncertainty_ws(0, 1, 0, 1000, 0, 1000)));
  assert(daily_gap_uncertainty_ws(0, 0, 0, 1000, 0, 1000) == 0);
  assert(daily_gap_uncertainty_ws(10, -10, 10, 1000, 500, 500) == 0);
  assert(std::isnan(daily_gap_uncertainty_ws(10, 11, 0, 1000, 500, 500)));
  const double largest = std::numeric_limits<double>::max();
  assert(daily_gap_uncertainty_ws(largest, largest, largest, 1000, 0, 0) == 0);
}

void finite_extremes_and_overflow_fail_closed() {
  const double largest = std::numeric_limits<double>::max();
  const double tiny = std::numeric_limits<double>::min();
  const uint64_t longest = std::numeric_limits<uint64_t>::max();
  assert(std::isfinite(daily_gap_uncertainty_ws(largest, -largest, largest, 1, 0, 1)));
  assert(std::isfinite(daily_gap_uncertainty_ws(largest / 2, 0, largest / 2, 1000, 0, 1000)));
  assert(daily_gap_uncertainty_ws(tiny, -tiny, tiny, 1000, 0, 1000) > 0);
  assert(std::isnan(daily_gap_uncertainty_ws(largest, -largest, largest, 1000, 0, 1000)));
  // Preserve the old constant-path arithmetic, including its intermediate overflow.
  assert(std::isnan(daily_gap_uncertainty_ws(largest, largest, largest, 1, 0, 1)));
  assert(std::isnan(daily_gap_uncertainty_ws(largest, 0, 0, 1000, 0, 1000)));
  assert(std::isnan(daily_gap_uncertainty_ws(std::numeric_limits<double>::denorm_min(), 0,
                                             std::numeric_limits<double>::denorm_min(), 1, 0, 1)));
  near(daily_gap_uncertainty_ws(1, -1, 1, longest, 0, longest), 1.5L * longest / 1000.0L);
  near(daily_gap_uncertainty_ws(1, -1, 1, longest, longest - 1, longest), 0.002L);
}
}  // namespace

int main() {
  analytic_areas_and_crossings();
  constant_heat_keeps_old_arithmetic();
  split_and_reversal_properties();
  reference_and_old_max_comparison();
  invalid_and_zero_inputs();
  finite_extremes_and_overflow_fail_closed();
  std::puts("daily gap uncertainty: analytic, split, reference, compatibility and invalid-input checks PASS");
}
