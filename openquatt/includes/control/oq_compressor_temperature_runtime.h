#pragma once

#include "oq_compressor_temperature_policy.h"
#include <cstdio>
#include <cstring>
#include "../sources/oq_source_receipt_runtime.h"

#if defined(OQ_TOPOLOGY_DUO)
namespace oq_temperature_runtime {

#if OQ_TOPOLOGY_DUO
#define OQ_TEMPERATURE_SECONDARY_ID(suffix) id(hp2_##suffix)
#else
#define OQ_TEMPERATURE_SECONDARY_ID(suffix) id(hp1_##suffix)
#endif

// Owned by the ESPHome loop, like the raw receipts. No callbacks on another
// task may mutate these records. Every evaluation is idempotent for one input.
inline oq_temperature::Policy policies[2]{};
inline oq_temperature::Evaluation latest[2]{};
inline uint64_t maximum_age_ms{60000};

inline oq_temperature::Evaluation capture(bool hp1, const oq_odu::RuntimeFrequencySnapshot& snapshot) {
  const auto& receipts = hp1 ? oq_sources::hp1
#if OQ_TOPOLOGY_DUO
                             : oq_sources::hp2;
#else
                             : oq_sources::hp1;
#endif
  const uint64_t now = oq_sources::monotonic_ms();
  const bool online = hp1 ? id(hp1_is_online) : OQ_TEMPERATURE_SECONDARY_ID(is_online);
  const auto& offset = hp1 ? id(hp1_water_in_temp_offset) : OQ_TEMPERATURE_SECONDARY_ID(water_in_temp_offset);
  const bool identity_ready =
      hp1 ? id(hp1_odu_generation_detection_complete) : OQ_TEMPERATURE_SECONDARY_ID(odu_generation_detection_complete);
  const bool table_revalidation = hp1 ? id(hp1_runtime_frequency_revalidation_required)
                                      : OQ_TEMPERATURE_SECONDARY_ID(runtime_frequency_revalidation_required);
  const int variant_code = hp1 ? id(hp1_generation_variant_code) : OQ_TEMPERATURE_SECONDARY_ID(generation_variant_code);
  const bool profile_ready = identity_ready && !table_revalidation &&
                             oq_odu::confirmed_variant(identity_ready, variant_code) == snapshot.variant;
  const bool fresh = online && profile_ready && receipts.outside.fresh(now, maximum_age_ms) &&
                     receipts.water_in.fresh(now, maximum_age_ms) && offset.has_state() && std::isfinite(offset.state);
  // Canonicalization follows calibration. These are actual local measurements,
  // not the selected HA ambient source or a ten-second template publication.
  const oq_temperature::Inputs inputs{
      oq_temperature::centi_c(receipts.outside.value),
      oq_temperature::centi_c(static_cast<double>(receipts.water_in.value) + offset.state),
      fresh,
      id(oq_cold_start_session_active) && !id(oq_cold_start_hp_blocked),
  };
  auto result = policies[hp1 ? 0 : 1].evaluate(snapshot, inputs);
  latest[hp1 ? 0 : 1] = result;
  return result;
}

inline const char* profile_label(oq_odu::Variant variant) {
  if (oq_temperature::v2_profile(variant)) return "CiC 4.2.0 / V2 heating conservative";
  if (variant == oq_odu::Variant::V1 || variant == oq_odu::Variant::V1_5) return "CiC 4.2.0 / V1 heating conservative";
  return "Unknown / heating blocked";
}

inline void reason_text(uint16_t reasons, char* buffer, size_t size) {
  buffer[0] = '\0';
  const auto append = [&](uint16_t flag, const char* label) {
    if ((reasons & flag) == 0) return;
    const size_t used = std::strlen(buffer);
    if (used < size) std::snprintf(buffer + used, size - used, "%s%s", used ? ", " : "", label);
  };
  append(oq_temperature::PROFILE, "profile");
  append(oq_temperature::MEASUREMENT, "measurement");
  append(oq_temperature::OUTSIDE_DOMAIN, "outside domain");
  append(oq_temperature::TABLE, "table");
  append(oq_temperature::WATER_CAP, "water cap");
  append(oq_temperature::HYSTERESIS, "hysteresis");
  append(oq_temperature::NORMAL_CAP, "90 Hz cap");
  if (reasons == 0) std::snprintf(buffer, size, "none");
}

#undef OQ_TEMPERATURE_SECONDARY_ID
}  // namespace oq_temperature_runtime
#endif
