#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace oq_odu {

inline constexpr uint16_t CORE_IDENTITY_REGISTER = 2114U;
inline constexpr uint16_t CORE_IDENTITY_REGISTER_COUNT = 14U;
inline constexpr size_t COMPRESSOR_CODE_WORD_OFFSET = 0U;
inline constexpr size_t PCB_PROGRAM_WORD_OFFSET = 8U;
inline constexpr size_t CONTROL_BOARD_ITEM_WORD_OFFSET = 13U;

inline constexpr uint16_t CUSTOMER_MODEL_REGISTER = 11160U;
inline constexpr uint16_t CUSTOMER_MODEL_REGISTER_COUNT = 2U;

inline constexpr uint16_t CONTROL_BOARD_ITEM_V1 = 0x0037U;
inline constexpr uint16_t CONTROL_BOARD_ITEM_V1_5_OR_V2_OLD_MODEL = 0x0E37U;
inline constexpr uint16_t CONTROL_BOARD_ITEM_V2_NEW_MODEL = 0x1037U;
inline constexpr uint16_t PCB_PROGRAM_V1_5 = 0x011EU;
inline constexpr uint16_t PCB_PROGRAM_V2_OLD_MODEL = 0x0122U;
inline constexpr uint16_t COMPRESSOR_CODE_V1_5 = 0U;
inline constexpr uint16_t COMPRESSOR_CODE_V2 = 2825U;
inline constexpr uint16_t CUSTOMER_MODEL_AM_WORD = 0x414DU;
inline constexpr uint16_t CUSTOMER_MODEL_H6_WORD = 0x4836U;

enum class Generation : uint8_t {
  UNKNOWN = 0,
  V1,
  V1_5,
  V2,
};

enum class Variant : uint8_t {
  UNKNOWN = 0,
  V1,
  V1_5,
  V2_OLD_MODEL,
  V2_NEW_MODEL,
};

struct CoreIdentity {
  bool available{false};
  uint16_t compressor_code{0};
  uint16_t pcb_program{0};
  uint16_t control_board_item{0};
};

struct CustomerModelPrefix {
  bool available{false};
  bool missing{false};
  bool printable{false};
  std::array<uint16_t, CUSTOMER_MODEL_REGISTER_COUNT> words{};
  std::array<char, CUSTOMER_MODEL_REGISTER_COUNT * 2U + 1U> code{};
};

struct Detection {
  Generation generation{Generation::UNKNOWN};
  Variant variant{Variant::UNKNOWN};
};

inline uint16_t read_word(const uint8_t* data, size_t index) {
  const size_t offset = index * 2U;
  return static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8U) | data[offset + 1U]);
}

inline CoreIdentity parse_core_identity_response(const uint8_t* data, size_t size) {
  if (data == nullptr || size < static_cast<size_t>(CORE_IDENTITY_REGISTER_COUNT) * 2U) {
    return {};
  }

  return {
      true,
      read_word(data, COMPRESSOR_CODE_WORD_OFFSET),
      read_word(data, PCB_PROGRAM_WORD_OFFSET),
      read_word(data, CONTROL_BOARD_ITEM_WORD_OFFSET),
  };
}

inline bool is_blank_customer_model_word(uint16_t word) { return word == 0x0000U || word == 0xFFFFU; }

inline bool is_printable_ascii(uint8_t value) { return value >= 0x20U && value <= 0x7EU; }

inline CustomerModelPrefix parse_customer_model_response(const uint8_t* data, size_t size) {
  if (data == nullptr || size < static_cast<size_t>(CUSTOMER_MODEL_REGISTER_COUNT) * 2U) {
    return {};
  }

  CustomerModelPrefix result;
  result.available = true;
  for (size_t index = 0; index < result.words.size(); ++index) result.words[index] = read_word(data, index);
  result.missing = is_blank_customer_model_word(result.words[0]) && is_blank_customer_model_word(result.words[1]);
  if (result.missing) return result;

  for (size_t index = 0; index < result.words.size(); ++index) {
    const uint8_t high = static_cast<uint8_t>((result.words[index] >> 8U) & 0xFFU);
    const uint8_t low = static_cast<uint8_t>(result.words[index] & 0xFFU);
    if (!is_printable_ascii(high) || !is_printable_ascii(low)) return result;
    result.code[index * 2U] = static_cast<char>(high);
    result.code[index * 2U + 1U] = static_cast<char>(low);
  }
  result.printable = true;
  return result;
}

inline bool is_customer_model_amh6(const CustomerModelPrefix& customer_model) {
  return customer_model.available && customer_model.words[0] == CUSTOMER_MODEL_AM_WORD &&
         customer_model.words[1] == CUSTOMER_MODEL_H6_WORD;
}

inline bool requires_customer_model(const CoreIdentity& core) {
  return core.available && core.control_board_item == CONTROL_BOARD_ITEM_V1_5_OR_V2_OLD_MODEL;
}

inline Detection detect_generation(const CoreIdentity& core, const CustomerModelPrefix& customer_model = {}) {
  if (!core.available) return {};

  if (core.control_board_item == CONTROL_BOARD_ITEM_V1) return {Generation::V1, Variant::V1};
  if (core.control_board_item == CONTROL_BOARD_ITEM_V2_NEW_MODEL) return {Generation::V2, Variant::V2_NEW_MODEL};
  if (core.control_board_item != CONTROL_BOARD_ITEM_V1_5_OR_V2_OLD_MODEL || !customer_model.available) return {};

  const bool v2_old_model_core =
      core.pcb_program == PCB_PROGRAM_V2_OLD_MODEL && core.compressor_code == COMPRESSOR_CODE_V2;
  if (v2_old_model_core && (customer_model.missing || is_customer_model_amh6(customer_model))) {
    return {Generation::V2, Variant::V2_OLD_MODEL};
  }

  const bool v1_5_core = core.pcb_program == PCB_PROGRAM_V1_5 && core.compressor_code == COMPRESSOR_CODE_V1_5;
  if (v1_5_core && customer_model.missing) return {Generation::V1_5, Variant::V1_5};

  return {};
}

inline uint32_t next_request_token(uint32_t current) {
  const uint32_t next = current + 1U;
  return next == 0U ? 1U : next;
}

inline const char* generation_label(Generation generation) {
  switch (generation) {
    case Generation::V1:
      return "V1";
    case Generation::V1_5:
      return "V1.5";
    case Generation::V2:
      return "V2";
    default:
      return "Unknown";
  }
}

inline const char* variant_label(Variant variant) {
  switch (variant) {
    case Variant::V1:
      return "V1";
    case Variant::V1_5:
      return "V1.5";
    case Variant::V2_OLD_MODEL:
      return "V2 old model";
    case Variant::V2_NEW_MODEL:
      return "V2 new model";
    default:
      return "Unknown";
  }
}

inline const char* customer_model_label(const CustomerModelPrefix& customer_model) {
  if (!customer_model.available) return "Unknown";
  if (customer_model.missing) return "Missing";
  if (customer_model.printable) return customer_model.code.data();
  return "Invalid";
}

}  // namespace oq_odu
