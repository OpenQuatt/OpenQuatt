#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include "components/openquatt_weather/OpenQuattWeatherJson.h"

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) return 2;
  const std::string json((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
  alignas(std::max_align_t) uint8_t arena[oq_weather::kJsonArenaBytes];
  oq_weather::Series candidate;
  candidate.current = {1800000000, 10.5f, 85, 8, true};
  candidate.wind_direction_valid_mask = (uint64_t{1} << oq_weather::kHours) - 1;
  candidate.current_wind = {1800000000, 9, 90, true, true};
  const bool parsed =
      oq_weather::parse_json(json.data(), json.size(), arena, std::strtoul(argv[2], nullptr, 10), candidate, 52.09,
                             6.15, static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10)), 1000);
  if (argc == 4) {
    const auto& wind = candidate.current_wind;
    std::printf("%d %d %llu %d %d %u %.8g %.8g", parsed, candidate.current.valid,
                static_cast<unsigned long long>(candidate.wind_direction_valid_mask), wind.valid, wind.direction_valid,
                static_cast<unsigned>(wind.time_epoch), static_cast<double>(wind.speed_mps),
                static_cast<double>(wind.direction_deg));
    for (float degrees : candidate.wind_direction_deg) std::printf(" %.8g", static_cast<double>(degrees));
    std::puts("");
  } else {
    std::printf("%d %d %u %.8g\n", parsed, candidate.current.valid, static_cast<unsigned>(candidate.current.time_epoch),
                static_cast<double>(candidate.wind_mps[0]));
  }
}
