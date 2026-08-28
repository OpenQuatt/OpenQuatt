#include <assert.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../openquatt/includes/experimental/oq_odu_runtime_frequency_table_logic.h"

namespace {

template <size_t N>
std::array<uint8_t, N * 2U> encode_words(const std::array<uint16_t, N>& words) {
  std::array<uint8_t, N * 2U> data{};
  for (size_t index = 0; index < words.size(); ++index) {
    data[index * 2U] = static_cast<uint8_t>(words[index] >> 8U);
    data[index * 2U + 1U] = static_cast<uint8_t>(words[index] & 0xFFU);
  }
  return data;
}

}  // namespace

int main() {
  using namespace oq_odu_runtime_frequency;

  constexpr std::array<uint16_t, 22> base = {
      0, 20, 26, 30, 34, 36, 38, 40, 42, 44, 46, 0, 20, 26, 30, 36, 40, 45, 48, 52, 55, 60,
  };
  constexpr std::array<uint16_t, 20> extension = {
      65, 68, 72, 76, 82, 85, 90, 95, 102, 110, 48, 52, 54, 56, 58, 60, 64, 66, 68, 71,
  };
  const auto base_data = encode_words(base);
  const auto extension_data = encode_words(extension);

  RuntimeFrequencyTables tables;
  size_t loaded = 0U;
  assert(parse_base_runtime_table(base_data.data(), base_data.size(), tables, loaded));
  assert(loaded == BASE_TABLE_REGISTER_COUNT);
  assert(tables.level_count == BASE_LEVEL_COUNT);
  assert(validate_monotonic_table(tables.cooling, tables.level_count));
  assert(validate_monotonic_table(tables.heating, tables.level_count));

  size_t extension_loaded = 0U;
  assert(parse_extended_runtime_table(extension_data.data(), extension_data.size(), tables, extension_loaded));
  assert(extension_loaded == EXTENDED_TABLE_REGISTER_COUNT);
  assert(tables.level_count == EXTENDED_LEVEL_COUNT);
  assert(tables.heating[11] == 65.0f && tables.heating[20] == 110.0f);
  assert(tables.cooling[11] == 48.0f && tables.cooling[20] == 71.0f);
  assert(validate_monotonic_table(tables.cooling, tables.level_count));
  assert(validate_monotonic_table(tables.heating, tables.level_count));

  const auto first_base = runtime_write_register(tables, 0U);
  const auto last_base = runtime_write_register(tables, 21U);
  const auto first_extension = runtime_write_register(tables, 22U);
  const auto last_heating_extension = runtime_write_register(tables, 31U);
  const auto first_cooling_extension = runtime_write_register(tables, 32U);
  const auto last_extension = runtime_write_register(tables, 41U);
  assert(first_base.valid && first_base.address == 3000U && first_base.value == 0U);
  assert(last_base.valid && last_base.address == 3021U && last_base.value == 60U);
  assert(first_extension.valid && first_extension.address == 3050U && first_extension.value == 65U);
  assert(last_heating_extension.valid && last_heating_extension.address == 3059U &&
         last_heating_extension.value == 110U);
  assert(first_cooling_extension.valid && first_cooling_extension.address == 3060U &&
         first_cooling_extension.value == 48U);
  assert(last_extension.valid && last_extension.address == 3069U && last_extension.value == 71U);
  assert(!runtime_write_register(tables, 42U).valid);

  RuntimeFrequencyTables base_only = tables;
  base_only.level_count = BASE_LEVEL_COUNT;
  assert(!runtime_write_register(base_only, 22U).valid);
  assert(runtime_register_count(base_only.level_count) == 22U);
  assert(runtime_register_count(tables.level_count) == 42U);
  assert(runtime_register_count(0U) == 0U);

  auto mismatch = tables;
  mismatch.heating[20] = 109.0f;
  assert(!tables_match(mismatch, tables));
  assert(tables_match(tables, tables));

  auto invalid = tables.heating;
  invalid[12] = 64.0f;
  assert(!validate_monotonic_table(invalid, EXTENDED_LEVEL_COUNT));
  invalid = tables.heating;
  invalid[0] = 1.0f;
  assert(!validate_monotonic_table(invalid, EXTENDED_LEVEL_COUNT));
  invalid = tables.heating;
  invalid[1] = 0.0f;
  assert(!validate_monotonic_table(invalid, EXTENDED_LEVEL_COUNT));
  assert(!parse_extended_runtime_table(extension_data.data(), extension_data.size() - 2U, mismatch, extension_loaded));
  return 0;
}
