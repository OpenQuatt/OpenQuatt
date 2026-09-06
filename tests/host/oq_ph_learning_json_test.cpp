#include <assert.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "../../openquatt/includes/control/oq_ph_learning_json.h"

namespace {
using namespace oq_ph_learning;

void add_record(JsonWriter& json, bool comma) {
  static constexpr char kFloat[] = "-3.40282e+38";
  static constexpr char kUint[] = "4294967295";
  json.add("%s[%s,%s,%s,%s,%s,%s,%s,%s]", comma ? "," : "", kUint, kUint, kFloat, kFloat, kFloat, kFloat, kFloat,
           kUint);
}

void add_diagnostic(JsonWriter& json, bool comma) {
  static constexpr char kFloat[] = "-3.40282e+38";
  static constexpr char kUint[] = "4294967295";
  static constexpr char kInt[] = "-2147483648";
  static constexpr char kBool[] = "false";
  json.add("%s[%s,%s,%s", comma ? "," : "", kUint, kUint, kInt);
  for (size_t index = 0; index < 4U; ++index) json.add(",%s", kFloat);
  json.add(",%s,%s,%s,%s,%s,%s,%s,%s", kBool, kBool, kFloat, kFloat, kBool, kFloat, kFloat, kUint);
  for (size_t index = 0; index < 10U; ++index) json.add(",%s", kBool);
  json.add(",%s]", kUint);
}

void test_worst_case_export_fits_fixed_psram_cache() {
  char buffer[kExportJsonBufferSize]{};
  JsonWriter json(buffer, sizeof(buffer));
  json.add("%s", kExportRecordsPrefix);
  for (size_t index = 0; index < kMaxExportRecordRows; ++index) add_record(json, index != 0U);
  json.add("%s", kExportDiagnosticsPrefix);
  for (size_t index = 0; index < kMaxExportDiagnosticRows; ++index) add_diagnostic(json, index != 0U);
  json.add("%s", kExportSuffix);

  assert(json.ok());
  assert(json.size() == kMaxExportJsonBytes);
  assert(json.size() < sizeof(buffer));
  assert(buffer[json.size()] == '\0');
}

void test_writer_bounds_numbers_and_fails_closed_on_overflow() {
  char number_buffer[32]{};
  JsonWriter number(number_buffer, sizeof(number_buffer));
  number.number(-std::numeric_limits<float>::max());
  assert(number.ok());
  assert(number.size() <= kMaxSerializedFloatChars);

  char unavailable_buffer[8]{};
  JsonWriter unavailable(unavailable_buffer, sizeof(unavailable_buffer));
  unavailable.number(std::numeric_limits<float>::quiet_NaN());
  assert(std::strcmp(unavailable_buffer, "null") == 0);

  char short_buffer[4]{};
  JsonWriter overflow(short_buffer, sizeof(short_buffer));
  overflow.add("1234");
  assert(!overflow.ok());
  assert(overflow.size() == 0U);
}
}  // namespace

int main() {
  test_worst_case_export_fits_fixed_psram_cache();
  test_writer_bounds_numbers_and_fails_closed_on_overflow();
  return 0;
}
