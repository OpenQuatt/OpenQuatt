#include <cassert>
#include <cmath>
#include "../../components/openquatt_ble/ble_registry.cpp"

using namespace esphome::openquatt_ble;

static void test_partial_dew_point_coverage() {
  const uint32_t start = UINT32_MAX - 25;
  BleConfig config{};
  config.enabled = true;
  config.count = 4;
  for (size_t i = 0; i < 3; ++i) config.paired[i].use_dew_point = true;
  Measurement paired[CAPACITY]{};
  assert(highest_dew_point(config, paired, start).required);
  assert(std::isnan(highest_dew_point(config, paired, start).value));

  paired[0].ingest({Protocol::BTHOME, 20.0f, 50.0f}, -60, start + 200);
  paired[1].ingest({Protocol::BTHOME, 24.0f, 50.0f}, -60, start);
  paired[2].ingest({Protocol::BTHOME, 22.0f, 50.0f}, -60, start + 100);
  paired[3].ingest({Protocol::BTHOME, 30.0f, 50.0f}, -60, start + 300);

  for (const uint32_t now : {start + 300, start + FRESH_MS}) {
    const auto full = highest_dew_point(config, paired, now);
    assert(full.required && full.value == paired[1].read(now).dew_point);
  }
  const auto two = highest_dew_point(config, paired, start + FRESH_MS + 1);
  assert(two.required && two.value == paired[2].read(start + FRESH_MS + 1).dew_point);
  const auto one = highest_dew_point(config, paired, start + FRESH_MS + 101);
  assert(one.required && one.value == paired[0].read(start + FRESH_MS + 101).dew_point);

  const uint32_t all_expired = start + FRESH_MS + 201;
  const auto none = highest_dew_point(config, paired, all_expired);
  assert(none.required && std::isnan(none.value));
  paired[1].ingest({Protocol::BTHOME, 23.0f, 50.0f}, -60, all_expired);
  assert(highest_dew_point(config, paired, all_expired).value == paired[1].read(all_expired).dew_point);
  config.enabled = false;
  const auto disabled = highest_dew_point(config, paired, all_expired);
  assert(disabled.required && std::isnan(disabled.value));
  config.count = 0;
  assert(!highest_dew_point(config, paired, all_expired).required);
}

int main() {
  test_partial_dew_point_coverage();
  const uint64_t mac = 0xA4C138123456;
  Frame f{};
  uint8_t both[]{12, 0x16, 0xD2, 0xFC, 0x40, 0, 255, 2, 0xC4, 9, 3, 0x88, 0x13};
  for (size_t n = 0; n < sizeof(both); ++n) assert(!decode_advertisement(mac, both, n, f));
  assert(decode_advertisement(mac, both, sizeof(both), f));
  assert(f.counter == 255 && f.temperature == 25 && f.humidity == 50);
  const uint32_t start = UINT32_MAX - 50;
  Measurement m{};
  m.ingest(f, -60, start);
  const auto pair = m.read(start);
  assert(std::abs(pair.dew_point - 13.85f) < 0.05f);
  m.ingest(f, -55, start + 100);
  assert(m.read(start + 100).temperature_age_ms == 100);
  assert(m.read(start + 100).rssi_dbm == -55);
  uint8_t room[]{9, 0x16, 0xD2, 0xFC, 0x44, 0, 0, 2, 0x7C, 0xFC};
  assert(decode_advertisement(mac, room, sizeof(room), f));
  assert(f.temperature == -9.0f && std::isnan(f.humidity) && f.counter == 0);
  m.ingest(f, -55, start + 150);
  auto current = m.read(start + 151);
  assert(current.temperature == -9 && current.temperature_age_ms == 1 && current.pair_age_ms == 151);
  assert(current.dew_point == pair.dew_point);
  m.expire(start + FRESH_MS + 1);
  current = m.read(start + FRESH_MS + 1);
  assert(std::isnan(current.dew_point) && current.temperature == -9 && current.pair_age_ms == FRESH_MS + 1);
  m.expire(start + FRESH_MS + 151);
  m.ingest(f, -52, start + FRESH_MS + 152);
  assert(std::isnan(m.read(start + FRESH_MS + 152).temperature));
  assert(std::isnan(m.read(start + 200).temperature));  // Long uptime cannot resurrect expired data.

  both[4] = 0x41;
  assert(!decode_advertisement(mac, both, sizeof(both), f));
  both[4] = 0x40;
  both[12] = 0xFF;
  assert(!decode_advertisement(mac, both, sizeof(both), f));
  uint8_t unknown[]{8, 0x16, 0xD2, 0xFC, 0x40, 2, 0, 0, 0xFF};
  assert(!decode_advertisement(mac, unknown, sizeof(unknown), f));
  uint8_t duplicate[]{10, 0x16, 0xD2, 0xFC, 0x40, 2, 0, 0, 2, 0, 0};
  assert(!decode_advertisement(mac, duplicate, sizeof(duplicate), f));
  uint8_t atc[]{16, 0x16, 0x1A, 0x18, 0xA4, 0xC1, 0x38, 0x12, 0x34, 0x56, 0xFF, 0xCE, 60, 85, 0x0B, 0xAF, 17};
  assert(decode_advertisement(mac, atc, sizeof(atc), f));
  assert(f.protocol == Protocol::ATC1441 && f.temperature == -5 && f.humidity == 60 && f.counter == 17);
  atc[4] = 0;
  assert(!decode_advertisement(mac, atc, sizeof(atc), f));
  uint8_t pvvx[]{18, 0x16, 0x1A, 0x18, 0x56, 0x34, 0x12, 0x38, 0xC1, 0xA4, 0xF4, 1, 0x70, 0x17, 0xB0, 0x0B, 75, 7, 0};
  assert(decode_advertisement(mac, pvvx, sizeof(pvvx), f));
  assert(f.protocol == Protocol::PVVX && f.temperature == 5 && f.humidity == 60 && f.counter == 7);
  assert(!decode_advertisement(mac, pvvx, sizeof(pvvx) - 1, f));

  const uint8_t named[]{9,    0x16, 0xD2, 0xFC, 0x44, 0,    0,   2,   0x7C, 0xFC, 4,
                        0x08, 'A',  'T',  'C',  6,    0x09, 'K', 'a', 'm',  'e',  'r'};
  assert(decode_advertisement(mac, named, sizeof(named), f));
  assert(f.advertised_name.complete && std::strcmp(f.advertised_name.text, "Kamer") == 0);
  Measurement named_reading{};
  named_reading.ingest(f, -60, 10);
  assert(decode_advertisement(mac, named, sizeof(named) - 7, f));
  assert(!f.advertised_name.complete && std::strcmp(f.advertised_name.text, "ATC") == 0);
  named_reading.ingest(f, -60, 25);
  assert(std::strcmp(named_reading.read(25).advertised_name, "Kamer") == 0);
  assert(named_reading.read(25).temperature_age_ms == 15);
  const uint8_t invalid_name[]{3, 0x09, 0xC0, 0xAF, 9, 0x16, 0xD2, 0xFC, 0x44, 0, 0, 2, 0x7C, 0xFC};
  assert(decode_advertisement(mac, invalid_name, sizeof(invalid_name), f));
  assert(!f.advertised_name.text[0] && f.temperature == -9.0f);

  BleConfig c{};
  assert(valid_config(c));
  c.room_mac = mac;
  assert(!valid_config(c));
  c.count = 1;
  c.paired[0].mac = mac;
  std::strcpy(c.paired[0].name, "Woonkamer");
  c.paired[0].protocol = Protocol::PVVX;
  assert(valid_config(c));
  c.count = 2;
  c.paired[1] = c.paired[0];
  assert(!valid_config(c));
  c.count = 1;
  std::memset(c.paired[0].name, 'A', sizeof(c.paired[0].name));
  assert(!valid_config(c));

  BleConfig old{}, candidate{};
  old.revision = 9;
  candidate.revision = 10;
  auto disk_revision = old.revision;
  for (bool candidate_ack : {false, true}) {
    for (bool recovery : {false, true}) {
      for (bool rollback_ack : {false, true}) {
        int writes = 0;
        auto write = [&](const BleConfig& value) {
          ++writes;
          disk_revision = value.revision;  // Model a lost ack after an NVS write.
          return writes == 1 ? candidate_ack : rollback_ack;
        };
        const auto result = persist_config(old, candidate, write, [&] { return !recovery; });
        if (candidate_ack && !recovery) {
          assert(result == Persist::APPLIED && disk_revision == 10 && writes == 1);
        } else {
          assert(result == (rollback_ack ? Persist::ROLLED_BACK : Persist::UNKNOWN));
          assert(disk_revision == 9 && writes == 2);
        }
      }
    }
  }
}
