#include <assert.h>
#include <stdio.h>

#include <cmath>
#include <string>

struct GenerationSelect {
  bool has_state() const { return true; }
  std::string current_option() const { return "V1.5"; }
} hp_generation;

#define id(value) value
#include "../../openquatt/includes/protocol/oq_ot_heating_display.h"
#undef id

namespace {

constexpr uint64_t NOW = 100000;
constexpr uint64_t STALE = 60000;

oq_ot_display::HeatPump hp(oq_odu::Variant variant, float mode, float frequency) {
  oq_ot_display::HeatPump result;
  result.variant = variant;
  result.online = true;
  result.working_mode.observe(mode, NOW, true);
  result.compressor_frequency.observe(frequency, NOW, true);
  result.defrost.observe(0.0f, NOW, true);
  return result;
}

oq_ot_display::Display display(const oq_ot_display::HeatPump& first, const oq_ot_display::HeatPump& second = {},
                               bool duo = false, uint64_t now = NOW, float outside = 7.0f, float supply = 35.0f,
                               float defrost_factor = 0.764f) {
  return oq_ot_display::calculate(first, second, duo, now, STALE, outside, supply, defrost_factor, false);
}

void near(float actual, float expected) {
  if (!(std::fabs(actual - expected) < 0.01f)) fprintf(stderr, "Expected %.5f, got %.5f\n", expected, actual);
  assert(std::fabs(actual - expected) < 0.01f);
}

}  // namespace

int main() {
  using oq_odu::Variant;
  auto first = hp(Variant::V1_5, 2.0f, 61.0f);
  auto second = hp(Variant::V1_5, 0.0f, 0.0f);
  const auto single = display(first);
  assert(single.heating_active && !single.cooling_active);
  near(single.max_capacity_kw, 6.05091f);
  near(single.min_modulation_percent, 2071.03f * 100.0f / 6050.91f);
  near(single.modulation_percent, 4064.54f * 100.0f / 6050.91f);
  assert(std::fabs(single.modulation_percent - 50.0f) > 10.0f);
  near(display(hp(Variant::V1_5, 2.0f, 85.0f)).modulation_percent, 5700.05f * 100.0f / 6050.91f);
  near(display(hp(Variant::V1, 2.0f, 90.0f)).modulation_percent, 100.0f);
  // A nonzero-weight missing cell still fails closed; no extrapolated fill.
  assert(std::isnan(display(hp(Variant::V1_5, 2.0f, 90.0f), {}, false, NOW, 7.01f).modulation_percent));

  // The OT boiler selection never changes the HP dataset or hides HP heating.
  assert(oq_ot_display::flame_on(single, false, false, false));
  assert(oq_ot_display::flame_on(single, true, false, false));
  const auto duo = display(first, second, true);
  near(duo.max_capacity_kw, single.max_capacity_kw * 2.0f);
  near(duo.modulation_percent, single.modulation_percent / 2.0f);
  near(duo.min_modulation_percent, single.min_modulation_percent / 2.0f);
  second = first;
  near(display(first, second, true).modulation_percent, single.modulation_percent);

  // Mixed confirmed variants use each unit's own performance map.
  second = hp(Variant::V2_NEW_MODEL, 2.0f, 26.0f);
  const auto mixed = display(first, second, true);
  // Higher V2 frequencies are masked at A7/W35; select the maximum valid point.
  const auto v2_max = oq_perf::predict_heating_hz(Variant::V2_NEW_MODEL, 72.0f, 7.0f, 35.0f);
  const auto v2_now = oq_perf::predict_heating_hz(Variant::V2_NEW_MODEL, 26.0f, 7.0f, 35.0f);
  near(mixed.max_capacity_kw, (6050.91f + v2_max.pth_w) / 1000.0f);
  near(mixed.modulation_percent, (4064.54f + v2_now.pth_w) * 100.0f / (6050.91f + v2_max.pth_w));

  // Idle, pre/postflow, anti-freeze circulation and boiler-only operation:
  // a stopped compressor never becomes a heat source from residual demand.
  const auto idle = display(hp(Variant::V1_5, 2.0f, 0.0f), {}, false, NOW, NAN, NAN);
  near(idle.modulation_percent, 0.0f);
  near(idle.max_capacity_kw, single.max_capacity_kw);
  assert(!idle.heating_active);
  assert(!oq_ot_display::flame_on(idle, false, false, false));
  assert(oq_ot_display::flame_on(idle, false, false, true));
  assert(!oq_ot_display::flame_on(idle, true, false, true));
  assert(oq_ot_display::flame_on(idle, true, true, false));  // includes a real DHW flame
  const auto cooling = display(hp(Variant::V1_5, 1.0f, 61.0f));
  assert(cooling.cooling_active && !cooling.heating_active);
  near(cooling.modulation_percent, 0.0f);
  assert(!oq_ot_display::flame_on(cooling, false, false, false));

  // Receipts expire independently, at the exact boundary, without a new sample.
  assert(display(first, {}, false, NOW + STALE).heating_active);
  const auto expired = display(first, {}, false, NOW + STALE + 1);
  assert(!expired.heating_active);
  assert(std::isnan(expired.modulation_percent));
  near(expired.max_capacity_kw, single.max_capacity_kw);  // installed reference survives link loss
  auto missing = first;
  missing.working_mode = {};
  assert(!display(missing).heating_active && std::isnan(display(missing).modulation_percent));
  missing = first;
  missing.compressor_frequency.invalidate();
  assert(!display(missing).heating_active && std::isnan(display(missing).modulation_percent));
  missing.compressor_frequency.observe(NAN, NOW, true);
  assert(!display(missing).heating_active);
  missing = first;
  missing.online = false;
  assert(!display(missing).heating_active && std::isnan(display(missing).modulation_percent));
  assert(display(first).heating_active);  // fresh recovery

  // An unknown/offline second installed unit cannot silently turn Duo into Single.
  second = hp(Variant::UNKNOWN, 0.0f, 0.0f);
  assert(std::isnan(display(first, second, true).max_capacity_kw));
  assert(std::isnan(display(first, second, true).modulation_percent));
  assert(display(first, second, true).heating_active);
  second = hp(Variant::V1_5, 0.0f, 0.0f);
  second.online = false;
  assert(std::isnan(display(first, second, true).modulation_percent));
  near(display(first, second, true).max_capacity_kw, duo.max_capacity_kw);

  // Unknown model inputs invalidate only the percentage, not the activity icon.
  assert(std::isnan(display(first, {}, false, NOW, NAN).modulation_percent));
  assert(display(first, {}, false, NOW, NAN).heating_active);
  assert(std::isnan(display(first, {}, false, NOW, 7.0f, NAN).modulation_percent));
  missing = first;
  missing.compressor_frequency.observe(121.0f, NOW, true);
  assert(!display(missing).heating_active);
  missing.compressor_frequency.observe(95.0f, NOW, true);
  assert(display(missing).heating_active && std::isnan(display(missing).modulation_percent));
  missing.working_mode.observe(3.0f, NOW, true);
  assert(!display(missing).heating_active && std::isnan(display(missing).modulation_percent));

  // Defrost keeps the active heating cycle visible, but derates the estimate.
  first.defrost.observe(1.0f, NOW, true);
  near(display(first).modulation_percent, single.modulation_percent * 0.764f);
  assert(display(first).heating_active);
  assert(std::isnan(display(first, {}, false, NOW, 7.0f, 35.0f, NAN).modulation_percent));
  first.defrost.invalidate();
  assert(std::isnan(display(first).modulation_percent) && display(first).heating_active);
  // Native defrost mode 4 is sufficient confirmation, even when the bit lags.
  first = hp(Variant::V1_5, 2.0f, 61.0f);
  first.working_mode.observe(4.0f, NOW, true);
  assert(display(first).heating_active && !display(first).cooling_active);
  near(display(first).modulation_percent, single.modulation_percent * 0.764f);
  first.defrost.invalidate();
  near(display(first).modulation_percent, single.modulation_percent * 0.764f);
  first.working_mode.observe(2.0f, NOW, true);
  assert(display(first).heating_active && std::isnan(display(first).modulation_percent));
  first.defrost.observe(0.0f, NOW, true);
  near(display(first).modulation_percent, single.modulation_percent);
  first.working_mode.observe(4.0f, NOW, true);
  first.compressor_frequency.observe(0.0f, NOW, true);
  assert(!display(first).heating_active);
  near(display(first).modulation_percent, 0.0f);

  // Nominal capacity is fixed across weather/control transitions; percent is bounded.
  first = hp(Variant::V2_OLD_MODEL, 2.0f, 72.0f);
  const auto warmer = display(first, {}, false, NOW, 10.0f, 35.0f);
  const auto reference = display(first);
  near(warmer.max_capacity_kw, reference.max_capacity_kw);
  assert(warmer.modulation_percent >= 0.0f && warmer.modulation_percent <= 100.0f);
  near(reference.modulation_percent, 100.0f);
  return 0;
}
