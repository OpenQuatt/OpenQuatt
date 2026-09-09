#include <cassert>
#include <cstdint>

#include "openquatt/includes/odu/oq_odu_bottom_plate_settings.h"

int main() {
  using namespace oq_odu;

  assert(default_bottom_plate_settings(Variant::V1).mode == 1U);
  assert(default_bottom_plate_settings(Variant::V1_5).mode == 3U);
  assert(default_bottom_plate_settings(Variant::V2_OLD_MODEL).mode == 3U);
  assert(default_bottom_plate_settings(Variant::V2_NEW_MODEL).mode == 3U);

  const uint8_t dump_values[]{0x00, 0x03, 0x00, 0x22, 0x00, 0x03};
  BottomPlateSettings decoded;
  assert(decode_bottom_plate_settings(dump_values, sizeof(dump_values), decoded));
  assert(decoded.mode == 3U);
  assert(decoded.start_temperature_c == 4);
  assert(decoded.stop_delta_c == 3U);

  const auto writes = bottom_plate_write_targets(decoded);
  assert(writes[0].address == 3237U && writes[0].value == 34U);
  assert(writes[1].address == 3238U && writes[1].value == 3U);
  assert(writes[2].address == 3236U && writes[2].value == 3U);

  const auto disable_writes = bottom_plate_write_targets({0U, 2, 4U});
  assert(disable_writes[0].address == 3236U && disable_writes[0].value == 0U);
  assert(disable_writes[1].address == 3237U && disable_writes[1].value == 32U);
  assert(disable_writes[2].address == 3238U && disable_writes[2].value == 4U);

  const auto profile = make_bottom_plate_profile(decoded, Variant::V1_5, CONTROL_BOARD_ITEM_V1_5_OR_V2_OLD_MODEL, true);
  assert(valid_bottom_plate_profile(profile));
  assert(bottom_plate_profile_matches_identity(profile, CONTROL_BOARD_ITEM_V1_5_OR_V2_OLD_MODEL, Variant::V1_5));
  assert(
      !bottom_plate_profile_matches_identity(profile, CONTROL_BOARD_ITEM_V1_5_OR_V2_OLD_MODEL, Variant::V2_OLD_MODEL));

  auto corrupted = profile;
  corrupted.mode = 1U;
  assert(!valid_bottom_plate_profile(corrupted));

  assert(!valid_bottom_plate_settings({4U, 4, 3U}));
  assert(!valid_bottom_plate_settings({3U, 31, 3U}));
  assert(!valid_bottom_plate_settings({3U, 4, 31U}));
  return 0;
}
