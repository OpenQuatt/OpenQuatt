#include <cassert>
#include <cstring>
#include <string>
#include "components/openquatt_weather/OpenQuattWeatherLocation.h"
#include "components/openquatt_weather/OpenQuattWeatherPolicy.h"

using namespace oq_weather;
static_assert(sizeof(LocationRecord) == 76, "persisted record must match the NVS budget and storage contract");

int main() {
  Location location;
  const auto parse = [&](const std::string& value) { return parse_location(value.data(), value.size(), location); };
  assert(parse("Brummen") == LocationKind::NAME);
  assert(parse(" Brummen ") == LocationKind::NAME && std::strcmp(location.query, "Brummen") == 0);
  assert(parse("52.09, 6.15556") == LocationKind::COORDINATES && location.latitude == 52.09);
  for (const auto* input :
       {"49, 6", "52, 9", "NaN, 6", "52, infinity", "52,", "52,6 extra", "https://example.org", "x", "Brum\nmen"})
    assert(parse(input) == LocationKind::INVALID);
  assert(parse(std::string(65, 'a')) == LocationKind::INVALID);
  assert(parse(std::string("Brum\0men", 8)) == LocationKind::INVALID);
  assert(parse("") == LocationKind::EMPTY);
  assert(parse("Brummen, Gelderland") == LocationKind::NAME);
  char encoded[128];
  assert(encode_query(location.query, encoded, sizeof(encoded)) &&
         std::strcmp(encoded, "Brummen%2C%20Gelderland") == 0);
  assert(!encode_query("Brummen", encoded, 4));
  for (const auto* input : {"6971", "6971AB", "6971 AB", "6971ab", "6971 ab", "6971aB"}) {
    assert(parse(input) == LocationKind::NAME);
    assert(encode_geocoding_query(location.query, encoded, sizeof(encoded)) && std::strcmp(encoded, "6971") == 0);
    assert(std::strcmp(location.query, input) == 0);
    const auto record = location_record(location);
    assert(valid_location_record(record) && std::strcmp(record.query, input) == 0);
    Location restored;
    assert(parse_location(record.query, std::strlen(record.query), restored) == LocationKind::NAME);
    assert(encode_geocoding_query(restored.query, encoded, sizeof(encoded)) && std::strcmp(encoded, "6971") == 0);
  }
  assert(parse(" 6971 ab ") == LocationKind::NAME && std::strcmp(location.query, "6971 ab") == 0);
  assert(encode_geocoding_query(location.query, encoded, 5) && std::strcmp(encoded, "6971") == 0);
  assert(!encode_geocoding_query(location.query, encoded, 4));
  for (const auto* input : {"Brummen", "6971ABC", "6971AB2", "0697AB", "69710AB", "6971A1", ""}) {
    assert(encode_geocoding_query(input, encoded, sizeof(encoded)) && std::strcmp(encoded, input) == 0);
  }
  assert(encode_geocoding_query("6971 AB extra", encoded, sizeof(encoded)) &&
         std::strcmp(encoded, "6971%20AB%20extra") == 0);
  assert(parse("Brummen") == LocationKind::NAME);
  const auto wanted = location_record(location);
  assert(valid_location_record(wanted));
  auto corrupted = wanted;
  corrupted.query[0] = 'X';
  assert(!valid_location_record(corrupted));
  corrupted = wanted;
  corrupted.version = 2;
  assert(!valid_location_record(corrupted));
  corrupted = wanted;
  corrupted.query[64] = 'x';
  assert(!valid_location_record(corrupted));
  int writes = 0, commits = 0, reads = 0;
  for (int failure = 0; failure <= 4; ++failure) {
    writes = commits = reads = 0;
    const bool saved = persist_location_verified(
        wanted,
        [&](const LocationRecord&) {
          ++writes;
          return failure != 1;
        },
        [&]() {
          ++commits;
          return failure != 2;
        },
        [&](LocationRecord& value) {
          ++reads;
          value = failure == 4 ? corrupted : wanted;
          return failure != 3;
        });
    assert(saved == (failure == 0));
    assert(writes == 1 && commits == (failure == 1 ? 0 : 1) && reads == (failure == 1 || failure == 2 ? 0 : 1));
  }

  // These are the two possible linearized OTA/save interleavings. A queued
  // operation can be canceled; one that owns the flash gate must finish safely.
  std::atomic<WorkGate> gate{WorkGate::QUEUED};
  cancel_queued_work(gate);
  assert(!begin_flash_work(gate));
  gate.store(WorkGate::QUEUED);
  assert(begin_flash_work(gate));
  cancel_queued_work(gate);
  assert(gate.load() == WorkGate::FLASH_STARTED);

  OtaPause pause;
  int first = 1, second = 2;
  pause.progress(&first, 100);
  pause.progress(&second, 200);
  pause.failed(&first);
  assert(pause.active(300, false));
  pause.failed(&second);
  assert(!pause.active(300, false));
  pause.progress(&first, 1000);
  assert(pause.active(1000 + kOtaQuietTimeoutMs, true));
  assert(!pause.active(1000 + kOtaQuietTimeoutMs, false));
  pause.progress(&first, 500000);
  assert(pause.active(1, false));  // A clock rollback must not release the hold.
  assert(!request_expired(100, 100 + kRequestDeadlineMs - 1));
  assert(request_expired(100, 100 + kRequestDeadlineMs));
  assert(request_expired(100, 99));
  // Deadlines use the 64-bit monotonic clock, including long disabled periods.
  assert(!request_expired(0x100000000ULL + 42, 0x100000000ULL + 43));
}
