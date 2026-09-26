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
  // All byte-valued modes: V1 has no combined mode; unknown variants fail closed.
  for (unsigned variant_code = 0U; variant_code <= 5U; ++variant_code) {
    const auto variant = static_cast<Variant>(variant_code);
    for (unsigned mode = 0U; mode <= 255U; ++mode) {
      const BottomPlateSettings candidate{static_cast<uint8_t>(mode), 4, 3U};
      const bool expected =
          variant == Variant::V1 ? mode <= 2U : variant_code >= 2U && variant_code <= 4U && mode <= 3U;
      assert(valid_bottom_plate_settings(candidate, variant) == expected);
    }
  }
  assert(!valid_bottom_plate_settings({1U, 4, 3U}, static_cast<Variant>(255U)));
  assert(!valid_bottom_plate_settings({1U, -31, 3U}, Variant::V1));
  assert(!valid_bottom_plate_settings({2U, 31, 3U}, Variant::V1));
  assert(!valid_bottom_plate_settings({2U, 4, 31U}, Variant::V1));

  // A value previously written by another client remains readable for repair.
  assert(decode_bottom_plate_settings(dump_values, sizeof(dump_values), decoded));
  assert(decoded.mode == 3U);
  assert(!valid_bottom_plate_settings(decoded, Variant::V1));
  assert(valid_bottom_plate_settings(decoded, Variant::V1_5));
  const uint8_t v1_values[]{0x00, 0x02, 0x00, 0x22, 0x00, 0x03};
  assert(decode_bottom_plate_settings(v1_values, sizeof(v1_values), decoded));
  assert(valid_bottom_plate_settings(decoded, Variant::V1));
  assert(!decode_bottom_plate_settings(nullptr, sizeof(v1_values), decoded));
  assert(!decode_bottom_plate_settings(v1_values, sizeof(v1_values) - 1U, decoded));

  // Restore/reapply uses this same validator, even for a checksum-valid legacy profile.
  for (bool auto_reapply : {false, true}) {
    for (uint8_t mode = 0U; mode <= 3U; ++mode) {
      const auto legacy = make_bottom_plate_profile({mode, 4, 3U}, Variant::V1, CONTROL_BOARD_ITEM_V1, auto_reapply);
      assert(legacy.checksum == bottom_plate_profile_checksum(legacy));
      assert(valid_bottom_plate_profile(legacy) == (mode <= 2U));
      assert(bottom_plate_profile_matches_identity(legacy, CONTROL_BOARD_ITEM_V1, Variant::V1) == (mode <= 2U));
    }
    for (auto variant : {Variant::V1_5, Variant::V2_OLD_MODEL, Variant::V2_NEW_MODEL}) {
      const uint16_t board =
          variant == Variant::V2_NEW_MODEL ? CONTROL_BOARD_ITEM_V2_NEW_MODEL : CONTROL_BOARD_ITEM_V1_5_OR_V2_OLD_MODEL;
      const auto combined = make_bottom_plate_profile({3U, 4, 3U}, variant, board, auto_reapply);
      assert(valid_bottom_plate_profile(combined));
      assert(bottom_plate_profile_matches_identity(combined, board, variant));
      assert(!bottom_plate_profile_matches_identity(combined, CONTROL_BOARD_ITEM_V1, Variant::V1));
    }
  }
  assert(sizeof(BottomPlateProfileStorage) == 16U);
  return 0;
}
