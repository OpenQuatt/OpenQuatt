#include <cassert>
#include <cstring>
#include <limits>
#include <string>

#include "components/openquatt_debug_recorder/debug_configuration_json.h"

using esphome::openquatt_debug_recorder::ConfigurationJsonWriter;

int main() {
  char buffer[1024]{};
  ConfigurationJsonWriter json(buffer, sizeof(buffer));
  assert(json.write_literal("{\"phRunExtension\":"));
  assert(json.write_bool(true));
  assert(json.write_literal(",\"phRunExtensionStopMargin\":"));
  assert(json.write_float(0.5f));
  assert(json.write_literal(",\"phRunExtensionRestartCooldown\":"));
  assert(json.write_float(0.2f));
  assert(json.write_char('}'));
  assert(std::string(buffer, json.size()) ==
         "{\"phRunExtension\":true,\"phRunExtensionStopMargin\":0.5,\"phRunExtensionRestartCooldown\":0.2}");

  char escaped[64]{};
  ConfigurationJsonWriter strings(escaped, sizeof(escaped));
  const char value[] = "HA\"\\\n\x01";
  assert(strings.write_string(value, sizeof(value) - 1));
  assert(std::string(escaped, strings.size()) == "\"HA\\\"\\\\\\u000A\\u0001\"");

  char numbers[64]{};
  ConfigurationJsonWriter invalid(numbers, sizeof(numbers));
  assert(invalid.write_float(std::numeric_limits<float>::quiet_NaN()));
  assert(invalid.write_char(','));
  assert(invalid.write_float(std::numeric_limits<float>::infinity()));
  assert(std::string(numbers, invalid.size()) == "null,null");

  // Exact fit needs no extra terminator; the recorder interns by length.
  char exact[4]{};
  ConfigurationJsonWriter fit(exact, sizeof(exact));
  assert(fit.write_bool(true));
  assert(fit.size() == sizeof(exact));
  assert(!fit.write_char('!'));
  assert(!fit.ok());
  assert(fit.size() == 0);
  assert(!fit.write_literal("null"));

  char guard[] = {'A', 'x', 'x', 'Z'};
  ConfigurationJsonWriter short_buffer(guard + 1, 2);
  assert(!short_buffer.write_string("\n", 1));
  assert(short_buffer.size() == 0);
  assert(guard[0] == 'A' && guard[3] == 'Z');
  ConfigurationJsonWriter no_storage(nullptr, 1024);
  assert(!no_storage.write_bool(false));
  assert(no_storage.size() == 0);
}
