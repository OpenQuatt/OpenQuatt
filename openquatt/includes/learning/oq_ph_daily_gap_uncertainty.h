#pragma once

#include <math.h>
#include <stdint.h>

namespace oq_power_house::learning {

// Conditional heat-error bound only: |actual_heat(t)| <= max_abs_heat_w.
// For the linearly interpolated heat q(t), |actual_heat(t) - q(t)| <= B + |q(t)|.
// Integrate that pointwise bound over the requested part of the original gap;
// using the original offsets also preserves the zero crossing when a day splits.
inline double daily_gap_uncertainty_ws(double max_abs_heat_w, double heat_start_w, double heat_end_w,
                                       uint64_t total_gap_ms, uint64_t begin_ms, uint64_t end_ms) {
  if (!isfinite(max_abs_heat_w) || max_abs_heat_w < 0.0 || !isfinite(heat_start_w) || !isfinite(heat_end_w) ||
      fabs(heat_start_w) > max_abs_heat_w || fabs(heat_end_w) > max_abs_heat_w || total_gap_ms == 0 ||
      begin_ms > end_ms || end_ms > total_gap_ms)
    return NAN;

  const uint64_t duration_ms = end_ms - begin_ms;
  if (duration_ms == 0) return 0.0;

  if (heat_start_w == heat_end_w) {
    // Keep the old multiplication/division order for inclusive budget edges.
    const double result = (max_abs_heat_w + fabs(heat_start_w)) * static_cast<double>(duration_ms) / 1000.0;
    return isfinite(result) && (result > 0.0 || max_abs_heat_w == 0.0) ? result : NAN;
  }

  // Different valid endpoints imply B > 0. Normalize first: opposite signed
  // endpoints cannot overflow a subtraction, nor can the area square huge W.
  const double start = heat_start_w / max_abs_heat_w;
  const double end = heat_end_w / max_abs_heat_w;
  const double begin_fraction = static_cast<double>(begin_ms) / static_cast<double>(total_gap_ms);
  const double end_fraction = static_cast<double>(end_ms) / static_cast<double>(total_gap_ms);
  const double begin_heat = start * (1.0 - begin_fraction) + end * begin_fraction;
  const double end_heat = start * (1.0 - end_fraction) + end * end_fraction;
  const double begin_abs = fabs(begin_heat);
  const double end_abs = fabs(end_heat);
  double mean_abs;
  if ((begin_heat < 0.0 && end_heat > 0.0) || (begin_heat > 0.0 && end_heat < 0.0)) {
    // Two triangles, not abs(mean(q)): (a*a + b*b) / (2*(a+b)).
    const double larger = fmax(begin_abs, end_abs);
    const double ratio = fmin(begin_abs, end_abs) / larger;
    mean_abs = (0.5 * larger) * ((1.0 + ratio * ratio) / (1.0 + ratio));
  } else {
    mean_abs = 0.5 * begin_abs + 0.5 * end_abs;
  }
  const double background_ws = max_abs_heat_w * (static_cast<double>(duration_ms) / 1000.0);
  const double result = background_ws * (1.0 + mean_abs);
  // A positive bound rounded all the way to zero is unavailable, never free.
  return isfinite(result) && result > 0.0 ? result : NAN;
}

}  // namespace oq_power_house::learning
