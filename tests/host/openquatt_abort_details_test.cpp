#include <cassert>
#include <cstring>
#include <string>

#include "../../components/openquatt_crash_telemetry/OpenQuattAbortDetails.h"
#include "../../components/openquatt_crash_telemetry/OpenQuattCrashTelemetryAnsi.h"

using namespace esphome::openquatt_crash_telemetry::detail;

int main() {
  AbortReplayContext context{};
  assert(!context.is_abort && context.core == 0xFFU);
  context.observe("  Reason: Task watchdog\n");
  assert(!context.is_abort);
  context.observe("  Crashed core: 0\n");
  assert(context.core == 0U);
  context.observe("  Crashed core: 1\n");
  assert(context.core == 1U);
  std::string clean_line;
  AnsiSequenceFilter filter;
  for (char c : std::string("  Reason: Abort\033[0m")) {
    if (!filter.should_skip(static_cast<uint8_t>(c))) clean_line += c;
  }
  clean_line += '\n';
  context.observe(clean_line.c_str());
  assert(context.is_abort);
  context = {};
  context.observe("  Reason: Abort");  // Incomplete replay line.
  assert(!context.is_abort && context.core == 0xFFU);

  AbortDetails record{};
  constexpr uint32_t build = 123456U;
  const auto readable = [](uintptr_t) { return true; };
  const char* text = "assert failed: recv_tcp api_msg.c:322 (recv_tcp: recv for wrong pcb!)";
  capture_abort_details(record, text, build, 1U, readable);
  assert(valid_abort_details(record, build));
  assert(std::strcmp(record.text, text) == 0);
  assert(record.truncated == 0U);
  assert(!valid_abort_details(record, build + 1U));
  assert(!valid_abort_details(record, 0U));
  auto corrupt = record;
  corrupt.text[3] ^= 1;
  assert(!valid_abort_details(corrupt, build));
  corrupt = record;
  corrupt.version++;
  assert(!valid_abort_details(corrupt, build));
  corrupt = record;
  corrupt.length = UINT32_MAX;
  assert(!valid_abort_details(corrupt, build));
  corrupt = record;
  corrupt.magic = 0U;  // Interrupted write, otherwise complete.
  assert(!valid_abort_details(corrupt, build));
  assert(take_abort_details(record, build));
  assert(!take_abort_details(record, build));  // No stale details on later boots.

  for (size_t size : {254U, 255U, 256U, 600U}) {
    const std::string long_text(size, 'a');
    capture_abort_details(record, long_text.c_str(), build, 0U, readable);
    assert(valid_abort_details(record, build));
    assert(record.length == (size < 256U ? size : 255U));
    assert(record.truncated == (size >= 256U ? 1U : 0U));
    assert(record.text[record.length] == '\0');
  }

  // Every partial read must fail closed, including a missing terminator byte.
  for (size_t readable_bytes = 0; readable_bytes <= std::strlen(text); ++readable_bytes) {
    capture_abort_details(record, text, build, 0U, [&](uintptr_t address) {
      return address < reinterpret_cast<uintptr_t>(text) + readable_bytes;
    });
    assert(!valid_abort_details(record, build));
  }
  capture_abort_details(record, reinterpret_cast<const char*>(1U), build, 0U, [](uintptr_t) { return false; });
  assert(!valid_abort_details(record, build));
  capture_abort_details(record, nullptr, build, 0U, readable);
  assert(!valid_abort_details(record, build));
  capture_abort_details(record, "", build, 0U, readable);
  assert(!valid_abort_details(record, build));
  capture_abort_details(record, "test\n\033\t%%", build, 0U, readable);
  assert(valid_abort_details(record, build));
  assert(std::strcmp(record.text, "test???%%") == 0);
  capture_abort_details(record, text, build, 2U, readable);
  assert(!valid_abort_details(record, build));
  capture_abort_details(record, text, 0U, 0U, readable);
  assert(!valid_abort_details(record, build));

  AbortDetails cores[2]{};
  capture_abort_details(cores[0], "core zero", build, 0U, readable);
  capture_abort_details(cores[1], "core one", build, 1U, readable);
  assert(take_abort_details(cores[0], build));
  assert(take_abort_details(cores[1], build));
  assert(std::strcmp(cores[0].text, "core zero") == 0);
  assert(std::strcmp(cores[1].text, "core one") == 0);
  assert(cores[0].core == 0U && cores[1].core == 1U);

  AbortDetails snapshot{};
  capture_abort_details(record, "previous crash", build, 1U, readable);
  assert(snapshot_abort_details(record, build, snapshot));
  // New panic on the same core while normal runtime replays the old report.
  capture_abort_details(record, "new crash", build, 1U, readable);
  assert(std::strcmp(snapshot.text, "previous crash") == 0);
  assert(valid_abort_details(record, build));
  assert(take_abort_details(record, build));
  assert(!snapshot_abort_details(record, build, snapshot));
}
