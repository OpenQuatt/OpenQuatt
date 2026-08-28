#include <assert.h>

#include <array>

#include "../../openquatt/includes/control/oq_compressor_frequency_policy.h"

namespace {

oq_odu::FrequencyTable make_table(const std::array<uint8_t, 11>& frequencies) {
  oq_odu::FrequencyTable table;
  table.level_count = frequencies.size();
  table.valid = true;
  for (size_t level = 0; level < frequencies.size(); ++level) table.hz[level] = frequencies[level];
  return table;
}

oq_odu::FrequencyTable make_v2_heating_table() {
  oq_odu::FrequencyTable table;
  constexpr std::array<uint8_t, 21> frequencies = {
      0, 20, 26, 30, 36, 40, 45, 48, 52, 55, 60, 65, 68, 72, 76, 82, 85, 90, 95, 102, 110,
  };
  table.level_count = frequencies.size();
  table.valid = true;
  for (size_t level = 0; level < frequencies.size(); ++level) table.hz[level] = frequencies[level];
  return table;
}

oq_odu::FrequencyTable make_v2_cooling_table() {
  oq_odu::FrequencyTable table;
  constexpr std::array<uint8_t, 21> frequencies = {
      0, 20, 26, 30, 34, 36, 38, 40, 42, 44, 46, 48, 52, 54, 56, 58, 60, 64, 66, 68, 71,
  };
  table.level_count = frequencies.size();
  table.valid = true;
  for (size_t level = 0; level < frequencies.size(); ++level) table.hz[level] = frequencies[level];
  return table;
}

oq_odu::RuntimeFrequencySnapshot make_v1_snapshot() {
  oq_odu::RuntimeFrequencySnapshot snapshot;
  snapshot.variant = oq_odu::Variant::V1_5;
  snapshot.cooling = make_table({0, 30, 36, 42, 47, 52, 56, 61, 66, 71, 74});
  snapshot.heating = make_table({0, 30, 39, 49, 55, 61, 67, 72, 79, 85, 90});
  return snapshot;
}

}  // namespace

int main() {
  using namespace oq_frequency_policy;

  const auto v1 = make_v1_snapshot();
  assert(automatic_frequency_hz(false, v1, 2, 6) == 67);
  assert(automatic_frequency_hz(false, v1, 1, 6) == 56);
  assert(automatic_frequency_hz(false, v1, 2, 0) == 0);
  auto v1_original = v1;
  v1_original.variant = oq_odu::Variant::V1;
  assert(automatic_frequency_hz(false, v1_original, 2, 10) == 90);

  auto v2_old = v1;
  v2_old.variant = oq_odu::Variant::V2_OLD_MODEL;
  v2_old.cooling = make_table({0, 30, 36, 42, 46, 48, 52, 56, 61, 66, 71});
  v2_old.heating = make_table({0, 20, 26, 30, 48, 55, 61, 72, 80, 85, 90});
  assert(automatic_frequency_hz(true, v2_old, 2, 8) == 80);
  assert(automatic_frequency_hz(true, v2_old, 1, 10) == 71);

  auto v2 = v1;
  v2.variant = oq_odu::Variant::V2_NEW_MODEL;
  v2.cooling = make_v2_cooling_table();
  v2.heating = make_v2_heating_table();
  assert(automatic_frequency_hz(true, v2, 2, 4) == 48);
  assert(automatic_frequency_hz(true, v2, 2, 8) == 82);
  assert(automatic_frequency_hz(true, v2, 2, 10) == 90);
  assert(automatic_frequency_hz(true, v2, 1, 10) == 46);

  auto runtime_edited = v1;
  runtime_edited.heating.hz[6] = 65;
  runtime_edited.cooling.hz[6] = 54;
  assert(automatic_frequency_hz(false, runtime_edited, 2, 6) == 65);
  assert(automatic_frequency_hz(false, runtime_edited, 1, 6) == 54);
  assert(pick_allowed_level(false, runtime_edited, 2, 6, 1, 10, 64, {}) == 5);

  auto unknown = v1;
  unknown.heating.valid = false;
  assert(automatic_frequency_hz(false, unknown, 2, 6) == -1);

  const FrequencyRange none{};
  assert(frequency_allowed(67, 67, none));
  assert(!frequency_allowed(68, 67, none));
  assert(frequency_allowed(120, 120, none));
  assert(!frequency_allowed(-1, 120, none));
  assert(!frequency_allowed(30, 121, none));

  const FrequencyRange excluded{60, 68};
  assert(!frequency_allowed(67, 120, excluded));
  assert(frequency_allowed(85, 120, excluded));
  assert(frequency_allowed(72, 120, excluded));
  assert(valid_frequency_range({0, 0}));
  assert(valid_frequency_range({0, 60}));
  assert(frequency_allowed(30, 120, {0, 60}));
  assert(!valid_frequency_range({-1, 0}));
  assert(!valid_frequency_range({70, 60}));

  assert(pick_allowed_level(false, v1, 2, 10, 1, 10, 67, none) == 6);
  assert(pick_allowed_level(false, v1, 2, 6, 1, 10, 120, excluded) == 4);
  assert(pick_allowed_level(false, v1, 2, 1, 1, 10, 120, {30, 30}) == 2);
  assert(pick_allowed_level(true, v2, 2, 8, 1, 10, 80, none) == 7);
  assert(pick_allowed_level(false, unknown, 2, 6, 1, 10, 120, none) == 0);
  assert(pick_allowed_level(false, v1, 2, 6, 1, 10, 0, none) == 0);
  return 0;
}
