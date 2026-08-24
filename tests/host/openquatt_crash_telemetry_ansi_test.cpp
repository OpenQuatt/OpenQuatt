#include <cassert>
#include <string>

#include "components/openquatt_crash_telemetry/OpenQuattCrashTelemetryAnsi.h"

using esphome::openquatt_crash_telemetry::detail::AnsiSequenceFilter;

namespace {

std::string strip_ansi(const char* value) {
  AnsiSequenceFilter filter;
  std::string result;
  for (const unsigned char c : std::string(value)) {
    if (!filter.should_skip(c)) result.push_back(static_cast<char>(c));
  }
  return result;
}

}  // namespace

int main() {
  assert(strip_ansi("\x1b[31merror\x1b[0m") == "error");
  assert(strip_ansi("\x1b[1;33mwarning\x1b[0m") == "warning");
  assert(strip_ansi("plain text") == "plain text");
  return 0;
}
