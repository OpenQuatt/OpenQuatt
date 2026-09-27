#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "../control/oq_defrost_logic.h"

namespace oq_defrost {
// Pxxx -> PDU xxx + 2999. Parameters come from the connected ODU, not defaults.
struct Parameters {
  std::array<uint16_t, 11> base{};   // P271..P281
  std::array<uint16_t, 9> timing{};  // P308..P316
  std::array<uint16_t, 6> coil{};    // P337..P342
  std::array<uint16_t, 14> delta{};  // P415..P428; V1.5/V2 mode-4 extension, not V1
  oq_odu::Variant variant{oq_odu::Variant::UNKNOWN};
  bool loaded{false};
  int mode() const { return base[5]; }
  bool automatic() const { return loaded && base[4] == 1; }
};
inline float temperature(uint16_t raw) { return raw <= 255 ? static_cast<float>(raw) - 30.0f : NAN; }
inline float start_threshold(const Parameters& p, float ambient) {
  if (!p.automatic() || !std::isfinite(ambient)) return NAN;
  if (p.mode() == 1) return temperature(p.base[6]);
  if (p.mode() != 0) return NAN;
  return temperature(ambient >= 3 ? p.base[6] : ambient >= -3 ? p.coil[0] : ambient >= -10 ? p.coil[1] : p.coil[2]);
}
inline float delta_threshold(const Parameters& p, float ambient) {
  if (!p.automatic() || p.mode() != 4 || !has_mode4_defrost(p.variant) || !std::isfinite(ambient) || ambient >= 9)
    return NAN;
  const unsigned band = ambient >= 0     ? 0
                        : ambient >= -5  ? 1
                        : ambient >= -10 ? 2
                        : ambient >= -15 ? 3
                        : ambient >= -20 ? 4
                        : ambient >= -23 ? 5
                                         : 6;
  return p.delta[band] <= 255 ? 30.0f - p.delta[band] : NAN;
}
struct Confirmation {
  uint32_t started{0};
  bool running{false};
  unsigned sample(bool condition, uint32_t now) {
    if (!condition) {
      running = false;
      return 0;
    }
    if (!running) {
      started = now;
      running = true;
    }
    return (now - started) / 1000U;
  }
};
struct Diagnostics {
  float start_c{NAN}, delta_k{NAN}, exit_c{NAN}, alternate_exit_c{NAN};
  int confirm_s{-1}, confirm_required_s{-1}, runtime_s{-1};
  int interval_s{-1}, minimum_runtime_s{-1}, max_duration_s{-1};
  int exit_confirm_s{-1}, exit_required_s{-1};
  const char* end_reason{"UNKNOWN"};
  Confirmation start_timer{}, exit_timer{}, compressor_timer{};
  uint32_t last_ms{0};
  float previous_threshold{NAN};

  void sample(const Parameters& p, bool fresh, bool active, float ambient, float coil_c, float evap, float hz,
              uint32_t now) {
    const bool gap = !fresh || now - last_ms > 30000U;
    last_ms = now;
    if (gap) {
      start_timer = {};
      exit_timer = {};
      compressor_timer = {};
    }
    if (!std::isfinite(hz)) compressor_timer = {};
    runtime_s = fresh && std::isfinite(hz) ? static_cast<int>(compressor_timer.sample(hz > 0, now)) : -1;
    if (!p.loaded || !is_supported_defrost_mode(p.mode(), p.variant)) {
      start_c = delta_k = exit_c = alternate_exit_c = NAN;
      confirm_s = confirm_required_s = interval_s = minimum_runtime_s = max_duration_s = -1;
      exit_confirm_s = exit_required_s = -1;
      start_timer = {};
      exit_timer = {};
      previous_threshold = NAN;
      return;
    }
    start_c = fresh ? start_threshold(p, ambient) : NAN;
    delta_k = fresh ? delta_threshold(p, ambient) : NAN;
    const float threshold = p.mode() == 4 ? delta_k : start_c;
    if (!std::isfinite(threshold) || threshold != previous_threshold) start_timer = {};
    previous_threshold = threshold;
    confirm_required_s = std::isfinite(threshold) ? (p.mode() == 4 ? 60 : 180) : -1;
    const bool criterion =
        p.mode() == 4 ? std::isfinite(evap) && ambient - evap > delta_k : std::isfinite(coil_c) && coil_c < start_c;
    confirm_s = confirm_required_s < 0 || !fresh
                    ? -1
                    : static_cast<int>(start_timer.sample(!active && hz > 0 && criterion, now));
    // The mode-0 interval is internal, and P342 can override it in an incompletely
    // known ambient band. Do not fabricate a countdown by replaying a partial model.
    interval_s = p.automatic() && p.mode() == 4 ? static_cast<int>(p.base[0]) * 60 : -1;
    minimum_runtime_s = !p.automatic()  ? -1
                        : p.mode() == 4 ? static_cast<int>(p.timing[0]) * 60
                        : p.mode() <= 1 ? 300
                                        : -1;
    max_duration_s = p.loaded ? static_cast<int>(p.base[8]) * 60 : -1;
    const bool cold_path = p.mode() == 4 && std::isfinite(ambient) && ambient < temperature(p.timing[6]);
    exit_c =
        p.loaded && (p.mode() != 4 || std::isfinite(ambient)) ? temperature(cold_path ? p.delta[8] : p.base[7]) : NAN;
    alternate_exit_c = p.loaded ? temperature(p.coil[4]) : NAN;
    // P340/P427/P428 are task ticks in the register book. Their physical time
    // unit is not proven; do not publish them as seconds or infer an exit from them.
    exit_required_s = -1;
    exit_confirm_s =
        !fresh || !std::isfinite(exit_c) ? -1 : static_cast<int>(exit_timer.sample(active && coil_c >= exit_c, now));
  }
  void ended(uint32_t duration) {
    // Inferences only: multiple ODU criteria can become true between polls.
    end_reason = "UNKNOWN";
    if (max_duration_s > 0 && duration >= static_cast<unsigned>(max_duration_s)) end_reason = "INFERRED_MAX_TIME";
  }
};
}  // namespace oq_defrost
