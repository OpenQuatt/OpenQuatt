#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "components/openquatt_measurement_archive/MeasurementArchive.h"
#include "components/openquatt_measurement_archive/MeasurementArchiveExport.h"

using namespace oq_measurement_archive;

struct MemoryFlash : Flash {
  std::vector<uint8_t> data = std::vector<uint8_t>(kEndOffset, 0xFF);
  bool fail_read = false, fail_erase = false, fail_write = false, lose_ack = false;
  size_t cut_after = SIZE_MAX;
  bool read(uint32_t at, void* out, size_t n) override {
    if (fail_read || at < kOffset || at + n > data.size()) return false;
    std::memcpy(out, data.data() + at, n);
    return true;
  }
  bool write(uint32_t at, const void* bytes, size_t n) override {
    assert(at >= kOffset && at + n <= data.size());
    const size_t size = fail_write ? std::min(n, cut_after == SIZE_MAX ? n / 2 : cut_after) : n;
    auto* source = static_cast<const uint8_t*>(bytes);
    for (size_t i = 0; i < size; ++i) {
      assert((data[at + i] & source[i]) == source[i]);
      data[at + i] &= source[i];
    }
    return !fail_write && !lose_ack;
  }
  bool erase(uint32_t at) override {
    assert(at >= kOffset && at + kSectorBytes <= data.size() && at % kSectorBytes == 0);
    if (fail_erase) return false;
    std::fill_n(data.data() + at, kSectorBytes, 0xFF);
    return true;
  }
};

template <typename RingType, size_t Count>
struct Open {
  SectorIndex index[Count]{};
  uint8_t scratch[kSectorBytes];
  RingType ring;
  explicit Open(MemoryFlash& flash, uint32_t boot = 1) { assert(ring.open(flash, index, scratch, boot)); }
};
using Small = Ring<Measurement, 3, kMeasurementOffset, 0x314D514F>;

Measurement make_row(unsigned i) {
  Measurement row{};
  row.epoch = 1800000000U + i * 300U;
  row.heat_tenth_wh = static_cast<int32_t>(i % 10000) - 5000;
  row.room_start_centi = 2000 + i % 5;
  row.room_end_centi = 2000 + (i + 1) % 5;
  row.duration_s = row.heat_valid_s = row.temperature_valid_s = 300;
  row.flags = i % 7;
  return row;
}
void collector() {
  Collector collector;
  unsigned count = 0;
  auto save = [&](const Measurement& row, uint32_t context) {
    ++count;
    assert(context == 1 && row.duration_s == 300 && row.heat_valid_s == 300 && row.temperature_valid_s == 300);
    assert(row.heat_tenth_wh == 1500 && row.outside_mean_centi == 500 && row.room_end_centi == 2010);
    return true;
  };
  for (unsigned sec = 0; sec <= 300; sec += 10)
    assert(collector.observe(
        {1000ULL + sec * 1000ULL, 1800000000U + sec, 1, 20.0f + sec / 3000.0f, 5, sec * 12.0f, 20, 0}, save));
  assert(count == 1);
  collector.reset();
  Sample s{1000, 1800000000, 1, 20, 5, NAN, 20, DEFROST};
  auto missing = [&](const Measurement& row, uint32_t) {
    assert(row.heat_tenth_wh == kMissingHeat && row.heat_valid_s == 0 && (row.flags & MISSING_HEAT));
    ++count;
    return true;
  };
  for (unsigned sec = 0; sec <= 300; sec += 10) {
    s.monotonic_ms = 1000 + sec * 1000;
    s.epoch = 1800000000 + sec;
    assert(collector.observe(s, missing));
  }
  assert(count == 2);
  s.monotonic_ms += 36000000;
  s.epoch += 36000;
  assert(collector.observe(s, [](auto&, auto) {
    assert(false);
    return false;
  }));
}
void failures() {
  Small unopened;
  assert(unopened.count() == 0 && !unopened.healthy());
  MemoryFlash flash;
  {
    Open<Small, 3> first(flash);
    assert(first.ring.append(make_row(1), 1));
    flash.fail_write = true;
    assert(!first.ring.append(make_row(2), 1));
    assert(!first.ring.healthy());
  }
  flash.fail_write = false;
  {
    Open<Small, 3> resumed(flash, 2);
    assert(resumed.ring.count() == 1);
    assert(resumed.ring.append(make_row(3), 1));
    std::vector<unsigned> seen;
    assert(resumed.ring.visit(0, [&](auto& row, auto&) {
      seen.push_back(row.epoch);
      return true;
    }));
    assert(seen.size() == 2 && seen[1] == make_row(3).epoch);
    flash.lose_ack = true;
    assert(!resumed.ring.append(make_row(4), 1));
  }
  flash.lose_ack = false;
  {
    Open<Small, 3> reboot(flash, 3);
    assert(reboot.ring.count() == 3);
    assert(!reboot.ring.append(make_row(4), 1));
    assert(reboot.ring.append(make_row(5), 1));
    flash.fail_read = true;
    assert(!reboot.ring.visit(0, [](auto&, auto&) { return true; }));
  }
  flash.fail_read = false;
  flash.fail_erase = true;
  Open<Small, 3> erased(flash);
  assert(!erased.ring.append(make_row(6), 2));
  flash.fail_erase = false;
  flash.fail_read = true;
  SectorIndex index[3]{};
  uint8_t scratch[kSectorBytes];
  Small blocked;
  assert(!blocked.open(flash, index, scratch, 4));
}
void every_powercut_boundary() {
  for (size_t cut = 0; cut < sizeof(Header); ++cut) {
    MemoryFlash flash;
    Open<Small, 3> first(flash);
    flash.fail_write = true;
    flash.cut_after = cut;
    assert(!first.ring.append(make_row(1), 1));
    flash.fail_write = false;
    Open<Small, 3> restart(flash, 2);
    assert(restart.ring.count() == 0);
    assert(restart.ring.append(make_row(2), 1));
  }
  for (size_t cut = 0; cut < sizeof(Measurement); ++cut) {
    MemoryFlash flash;
    Open<Small, 3> first(flash);
    assert(first.ring.append(make_row(1), 1));
    flash.fail_write = true;
    flash.cut_after = cut;
    assert(!first.ring.append(make_row(2), 1));
    flash.fail_write = false;
    Open<Small, 3> restart(flash, 2);
    assert(restart.ring.count() == 1);
    assert(restart.ring.append(make_row(3), 1));
    assert(restart.ring.count() == 2);
  }
  MemoryFlash future;
  Header header{0x314D514F, 2, sizeof(Measurement), 1, 1, 1, 1800000000, 0, 0};
  seal(header);
  assert(future.write(kMeasurementOffset, &header, sizeof(header)));
  SectorIndex ix[3]{};
  uint8_t scratch[kSectorBytes];
  Small old_firmware;
  assert(!old_firmware.open(future, ix, scratch, 1));
  std::puts("PASS all 60 header/record powercut boundaries and future-schema rejection");
}

void registry() {
  MemoryFlash flash;
  Contexts registry;
  uint8_t scratch[kSectorBytes];
  registry.open(flash, scratch);
  const char key[] = "physical-profile-a";
  constexpr uint64_t all_live = ~uint64_t{0};
  assert(registry.bind(key, sizeof(key), 0) == 1);
  assert(registry.bind(key, sizeof(key), all_live) == 1);
  flash.fail_write = true;
  const char second[] = "physical-profile-b";
  assert(registry.bind(second, sizeof(second), context_bit(1)) == 0);
  flash.fail_write = false;
  assert(registry.bind(second, sizeof(second), context_bit(1)) == 3);
  for (size_t i = 3; i < kContextCapacity; ++i) assert(registry.bind(&i, sizeof(i), 0) == i + 1);
  const char full[] = "full";
  assert(registry.bind(full, sizeof(full), all_live) == kUnregisteredContext);
  assert(registry.bind(key, sizeof(key), all_live) == 1);
  assert(registry.revision() == 0);
  const auto first_sector_free = all_live & ~uint64_t{7};
  flash.fail_read = true;
  assert(registry.bind(full, sizeof(full), first_sector_free) == 0);
  assert(registry.revision() == 0);
  flash.fail_read = false;
  flash.fail_erase = true;
  assert(registry.bind(full, sizeof(full), first_sector_free) == 0);
  flash.fail_erase = false;
  assert(registry.bind(full, sizeof(full), first_sector_free) == 1);
  assert(registry.revision() == 2);
  Context value;
  HeatRoute route;
  assert(Contexts::read_heat_route(flash, kUnregisteredContext, value, route) && !route.magic);
  Open<Small, 3> overflow(flash);
  assert(overflow.ring.append(make_row(0), kUnregisteredContext));
  Open<Small, 3> after_restart(flash, 2);
  assert(after_restart.ring.count() == 1 && !after_restart.ring.live_contexts());
}

void recycle_contexts() {
  MemoryFlash flash;
  uint8_t scratch[kSectorBytes];
  std::fill(flash.data.begin() + kOffset, flash.data.begin() + kMeasurementOffset, 0xA5);
  Contexts registry;
  assert(registry.open(flash, scratch));
  const char weather_key[] = "weather-only context";
  const auto weather_context = registry.bind(weather_key, sizeof(weather_key), 0);
  assert(weather_context == 1);
  assert(registry.revision() == 1);
  assert(flash.data[kOffset + kSectorBytes] == 0xA5);

  Open<Small, 3> history(flash);
  Open<WeatherHistory, kWeatherSectors> weather(flash);
  Weather forecast{};
  forecast.epoch = 1800000000U;
  assert(weather.ring.append(forecast, weather_context));
  for (unsigned i = 0; i < 120; ++i) {
    const auto live = history.ring.live_contexts() | weather.ring.live_contexts();
    HeatRoute route;
    route.flow_endpoint = i;
    const auto slot = registry.bind(&route, sizeof(route), live);
    assert(slot && slot <= kContextCapacity && !(live & context_bit(slot)));
    assert(history.ring.append(make_row(i), slot));
  }
  Open<Small, 3> restarted(flash, 2);
  Open<WeatherHistory, kWeatherSectors> weather_restarted(flash, 2);
  assert(weather_restarted.ring.live_contexts() == context_bit(weather_context));
  assert(registry.bind(weather_key, sizeof(weather_key), ~uint64_t{0}) == weather_context);
  assert(restarted.ring.live_contexts() == history.ring.live_contexts());
  assert(restarted.ring.count() == 3);
  assert(restarted.ring.visit(0, [&](const auto& row, const auto& header) {
    Context stored;
    HeatRoute route;
    assert(Contexts::read_heat_route(flash, header.context, stored, route));
    assert(route.flow_endpoint == (row.epoch - 1800000000U) / 300);
    return true;
  }));
}

void source_switch_with_legacy_record() {
  MemoryFlash flash;
  uint8_t scratch[kSectorBytes];
  Contexts contexts;
  assert(contexts.open(flash, scratch));
  const uint8_t legacy_key[] = {3, 0, 0, 0};
  const auto legacy = contexts.bind(legacy_key, sizeof(legacy_key), 0);
  HeatRoute pumps{};
  pumps.flow_source = 19;  // HP1 flow meter
  HeatRoute system = pumps;
  system.boundary = HeatBoundary::SYSTEM;
  system.supply_source = 1;  // Local PT1000
  system.supply_endpoint = 123;
  system.supply_offset_c = -0.125f;
  const auto pump_context = contexts.bind(&pumps, sizeof(pumps), 0);
  const auto system_context = contexts.bind(&system, sizeof(system), 0);
  assert(legacy == 1 && pump_context == 2 && system_context == 3);
  Open<Small, 3> recording(flash);
  assert(recording.ring.append(make_row(0), legacy));
  Collector collector;
  const auto save = [&](Measurement row, uint32_t id) { return recording.ring.append(row, id); };
  for (unsigned sec = 10; sec <= 130; sec += 10)
    assert(collector.observe({sec * 1000ULL, 1800000000U + sec, pump_context, 20, 5, sec == 120 ? NAN : 3600.0f, 21, 0},
                             save));
  assert(collector.finish_partial(save));
  assert(collector.finish_partial(save));  // No duplicate or empty block.
  for (unsigned sec = 140; sec <= 440; sec += 10)
    assert(collector.observe({sec * 1000ULL, 1800000000U + sec, system_context, 20, 5, 7200, 21, 0}, save));

  Open<Small, 3> restarted(flash, 2);
  assert(contexts.bind(legacy_key, sizeof(legacy_key), recording.ring.live_contexts()) == legacy);
  assert(contexts.bind(&pumps, sizeof(pumps), recording.ring.live_contexts()) == pump_context);
  assert(contexts.bind(&system, sizeof(system), recording.ring.live_contexts()) == system_context);
  unsigned count = 0;
  assert(restarted.ring.visit(0, [&](const Measurement& row, const Header& header) {
    assert(verified(row));
    switch (++count) {
      case 1:
        assert(header.context == legacy && row.epoch == 1800000000U);
        break;
      case 2:
        assert(header.context == pump_context && row.epoch == 1800000130U);
        assert(row.duration_s == 120 && row.heat_valid_s == 100 && row.heat_tenth_wh == 1000);
        assert((row.flags & (PARTIAL | MISSING_HEAT)) == (PARTIAL | MISSING_HEAT));
        break;
      case 3:
        assert(header.context == system_context && row.epoch == 1800000440U);
        assert(row.duration_s == 300 && row.heat_valid_s == 300 && row.heat_tenth_wh == 6000);
        assert(!(row.flags & PARTIAL));
        break;
      default:
        assert(false);
    }
    return true;
  }));
  assert(count == 3);
  Context context_buffer;
  HeatRoute readback;
  assert(Contexts::read_heat_route(flash, legacy, context_buffer, readback) && readback.magic == 0);
  assert(Contexts::read_heat_route(flash, system_context, context_buffer, readback));
  assert(readback.boundary == HeatBoundary::SYSTEM && readback.supply_offset_c == -0.125f);
  std::puts("PASS separate heat routes and valid seconds after reopening flash with a legacy block");
}

void read_only_day_snapshot() {
  uint32_t start = 0;
  assert(utc_day("2024-02-29", start) && start == 1709164800U);
  assert(!utc_day("2026-02-29", start) && !utc_day("2026-11-18&", start));
  assert(!utc_day("2106-02-07", start) && !utc_day("2026-11-00", start));

  MemoryFlash flash;
  Open<Small, 3> writer(flash);
  Measurement row{};
  for (uint32_t epoch : {start - 300, start, start + 300}) {
    row.epoch = epoch;
    assert(writer.ring.append(row, 1));
  }
  SectorIndex snapshot[3]{};
  const auto first = writer.ring.copy_index(snapshot);
  row.epoch = start + 600;
  assert(writer.ring.append(row, 1));
  uint8_t scratch[kSectorBytes];
  std::vector<uint32_t> exported;
  const auto collect = [&](const Measurement& sample, const Header&) {
    exported.push_back(sample.epoch);
    return true;
  };
  assert(Small::visit_snapshot(flash, snapshot, first, start, start + kDaySeconds, scratch, collect));
  assert((exported == std::vector<uint32_t>{start, start + 300}));
  assert(writer.ring.period().first_epoch == start - 300 && writer.ring.period().last_epoch == start + 600);
  for (uint32_t next_context : {2, 3, 4}) {
    row.epoch += 300;
    assert(writer.ring.append(row, next_context));
  }
  exported.clear();
  assert(!Small::visit_snapshot(flash, snapshot, first, start, start + kDaySeconds, scratch, collect));
  assert(exported.empty());
}

void solar_after_upgrade_and_interrupted_write() {
  MemoryFlash flash;
  Open<WeatherHistory, kWeatherSectors> weather(flash);
  Weather legacy{};
  legacy.epoch = 1800000000U;
  legacy.current_direction_centi = 9000;
  assert(weather.ring.append(legacy, 2));
  Open<SolarHistory, kSolarSectors> solar(flash);
  assert(solar.ring.count() == 0);
  const SolarRadiation entry{legacy.epoch + 900, {0, kMissingSolar, 1, 500, 1200}, 0, 0};
  assert(solar.ring.append(entry, kUnregisteredContext));
  auto incomplete = entry;
  incomplete.epoch += 900;
  flash.fail_write = true;
  flash.cut_after = 7;
  assert(!solar.ring.append(incomplete, kUnregisteredContext));
  flash.fail_write = false;
  Open<WeatherHistory, kWeatherSectors> weather_reopened(flash, 2);
  assert(weather_reopened.ring.count() == 1);
  assert(weather_reopened.ring.visit(0, [&](auto& row, auto&) { return row.current_direction_centi == 9000; }));
  Open<SolarHistory, kSolarSectors> reopened(flash, 2);
  assert(reopened.ring.count() == 1 && reopened.ring.last_epoch() == entry.epoch);
  std::string json;
  const auto append = [&](auto format, auto... args) {
    char buf[64];
    const int size = std::snprintf(buf, sizeof(buf), format, args...);
    assert(size > 0 && static_cast<size_t>(size) < sizeof(buf));
    json.append(buf, static_cast<size_t>(size));
    return true;
  };
  assert(reopened.ring.visit(0, [&](auto& row, auto&) { return export_solar_forecast(row, true, append); }));
  assert(json == "[1800000900,1800000000,0,null,1,500,1200]");
  incomplete.epoch += 900;
  assert(reopened.ring.append(incomplete, kUnregisteredContext));
  assert(reopened.ring.count() == 2);
}

void full_retention() {
  MemoryFlash flash;
  Open<Measurements, kMeasurementSectors> recorded(flash);
  Open<WeatherHistory, kWeatherSectors> weather(flash);
  Open<SolarHistory, kSolarSectors> solar(flash);
  constexpr unsigned days = 194;
  for (unsigned i = 0; i < days * 288; ++i) {
    assert(recorded.ring.append(make_row(i), 1));
    if (i % 3 == 0) {
      Weather row{};
      row.epoch = make_row(i).epoch;
      row.first_hour_epoch = row.epoch / 3600 * 3600;
      row.current_speed_centi = 1234;
      row.current_direction_centi = 35950;
      assert(weather.ring.append(row, 2));
      const SolarRadiation radiation{row.epoch, {0, 100, kMissingSolar, 700, 900}, 0, 0};
      assert(solar.ring.append(radiation, kUnregisteredContext));
    }
  }
  Open<Measurements, kMeasurementSectors> rebooted(flash, 2);
  Open<WeatherHistory, kWeatherSectors> weather_reboot(flash, 2);
  const uint32_t end = make_row(days * 288 - 1).epoch;
  unsigned count = 0;
  uint32_t prev = 0;
  const uint32_t cutoff = end - kRetentionS + 300;
  assert(rebooted.ring.visit(cutoff, [&](auto& row, auto& header) {
    assert(row.epoch > prev && header.context == 1 && verified(row));
    const unsigned i = (row.epoch - 1800000000U) / 300;
    const auto expected = make_row(i);
    assert(row.heat_tenth_wh == expected.heat_tenth_wh && row.flags == expected.flags);
    prev = row.epoch;
    ++count;
    return true;
  }));
  assert(count == 180 * 288);
  unsigned n_weather = 0;
  assert(weather_reboot.ring.visit(cutoff, [&](auto& row, auto& header) {
    assert(header.context == 2 && row.current_speed_centi == 1234 && row.current_direction_centi == 35950);
    ++n_weather;
    return true;
  }));
  assert(n_weather == 180 * 96);
  Open<SolarHistory, kSolarSectors> solar_reboot(flash, 2);
  unsigned n_solar = 0;
  assert(solar_reboot.ring.visit(cutoff, [&](auto& row, auto& header) {
    assert(header.context == kUnregisteredContext && row.wm2[0] == 0 && row.wm2[2] == kMissingSolar);
    ++n_solar;
    return true;
  }));
  assert(n_solar == n_weather);
  for (size_t i = 0; i < kOffset; ++i) assert(flash.data[i] == 0xFF);
  std::printf("PASS retention: 194 days, %u measurements, %u forecasts, %zu bytes reserved\n", count, n_weather,
              static_cast<size_t>(kEndOffset - kOffset));
}
int main() {
  collector();
  failures();
  every_powercut_boundary();
  registry();
  recycle_contexts();
  source_switch_with_legacy_record();
  read_only_day_snapshot();
  solar_after_upgrade_and_interrupted_write();
  full_retention();
}
