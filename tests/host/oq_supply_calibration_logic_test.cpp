#include <assert.h>
#include <limits.h>
#include <math.h>

#include "../../openquatt/includes/control/oq_supply_calibration_logic.h"

int main() {
  using namespace oq_supply_calibration;

  const auto pt1000 = source_identity("Local", true, "PT1000", true, "", false, "sensor.supply");
  const auto ds18b20 = source_identity("Local", true, "DS18B20", true, "", false, "sensor.supply");
  const auto non_q_local = source_identity("Local", false, "", false, "", false, "sensor.supply");
  const auto cic_a = source_identity("CIC", false, "", false, "http://cic-a/feed", true, "sensor.supply");
  const auto cic_b = source_identity("CIC", false, "", false, "http://cic-b/feed", true, "sensor.supply");
  const auto ha_a = source_identity("HA input", false, "", false, "", false, "sensor.supply-a");
  const auto ha_b = source_identity("HA input", false, "", false, "", false, "sensor.supply-b");

  assert(pt1000.ready && pt1000.code == SOURCE_LOCAL_PT1000);
  assert(ds18b20.ready && ds18b20.code == SOURCE_LOCAL_DS18B20);
  assert(non_q_local.ready && non_q_local.code == SOURCE_LOCAL_DS18B20);
  assert(pt1000.fingerprint != ds18b20.fingerprint);
  assert(cic_a.fingerprint != cic_b.fingerprint);
  assert(ha_a.fingerprint != ha_b.fingerprint);
  assert(!source_identity("CIC", false, "", false, "", false, "sensor.supply").ready);

  constexpr float offset = -0.37f;
  const uint32_t checksum = record_checksum(SOURCE_CIC, cic_a.fingerprint, offset);
  assert(record_matches(SOURCE_CIC, cic_a.fingerprint, checksum, offset, cic_a));
  assert(record_matches(SOURCE_CIC, cic_a.fingerprint, checksum, offset, cic_b));
  assert(!record_matches(SOURCE_CIC, cic_a.fingerprint, checksum, offset + 0.01f, cic_a));
  assert(!record_matches(SOURCE_CIC, cic_a.fingerprint, checksum ^ 1U, offset, cic_a));
  assert(!record_matches(-SOURCE_CIC, cic_a.fingerprint, checksum, offset, cic_a));
  assert(!record_matches(SOURCE_CIC, cic_a.fingerprint, checksum, 2.01f, cic_a));

  uint32_t cic_storage[kRecordStorageWords]{};
  uint32_t pt1000_storage[kRecordStorageWords]{};
  assert(!calibration_required(load_record(cic_storage), cic_a));
  assert(store_record(cic_storage, cic_a, offset));
  assert(record_matches(load_record(cic_storage), cic_a));
  assert(record_matches(load_record(cic_storage), cic_b));
  assert(!calibration_required(load_record(cic_storage), cic_b));

  const SourceIdentity restored_cic{SOURCE_CIC, fingerprint(SOURCE_CIC, "cic:type"), true};
  uint32_t restored_cic_storage[kRecordStorageWords]{};
  assert(store_record(restored_cic_storage, restored_cic, offset));
  assert(record_matches(load_record(restored_cic_storage), cic_a));
  assert(record_matches(load_record(restored_cic_storage), cic_b));

  assert(store_record(pt1000_storage, pt1000, 0.21f));
  assert(record_matches(load_record(pt1000_storage), pt1000));
  assert(record_matches(load_record(cic_storage), cic_a));
  assert(store_record(cic_storage, cic_b, -0.18f));
  assert(record_matches(load_record(cic_storage), cic_b));
  assert(record_matches(load_record(cic_storage), cic_a));
  assert(record_matches(load_record(pt1000_storage), pt1000));
  const CalibrationRecord valid_cic_record = load_record(cic_storage);
  assert(!store_record(cic_storage, cic_b, 2.01f));
  assert(record_matches(load_record(cic_storage), cic_b));
  assert(load_record(cic_storage).fingerprint == valid_cic_record.fingerprint &&
         load_record(cic_storage).checksum == valid_cic_record.checksum &&
         load_record(cic_storage).offset_c == valid_cic_record.offset_c);

  const CalibrationRecord interrupted_record{cic_b.fingerprint, 0U, -0.22f};
  assert(record_present(interrupted_record));
  assert(!record_matches(interrupted_record, cic_b));
  assert(calibration_required(interrupted_record, cic_b));

  CalibrationRecord corrupt_record{cic_a.fingerprint, checksum ^ 1U, offset};
  assert(record_present(corrupt_record));
  assert(!record_matches(corrupt_record, cic_a));
  assert(rounded_offset(0.126f) == 0.13f);
  assert(!signbit(rounded_offset(-0.004f)));
  assert(strcmp(source_label(INT32_MIN), "Unknown") == 0);
  return 0;
}
