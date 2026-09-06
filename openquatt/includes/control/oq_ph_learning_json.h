#pragma once

#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace oq_ph_learning {

constexpr size_t kStatusJsonBufferSize = 4U * 1024U;
constexpr size_t kExportJsonBufferSize = 24U * 1024U;
constexpr size_t kMaxExportRecordRows = 64U;
constexpr size_t kMaxExportDiagnosticRows = 60U;

// %.6g needs at most 12 characters for any finite IEEE-754 float, including
// its sign and exponent. JSON null is shorter and is used for non-finite data.
constexpr size_t kMaxSerializedFloatChars = 12U;
constexpr size_t kMaxSerializedUint32Chars = 10U;
constexpr size_t kMaxSerializedInt32Chars = 11U;
constexpr size_t kMaxSerializedBoolChars = 5U;

static constexpr char kExportRecordsPrefix[] =
    "{\"schema\":1,\"mode\":\"passive\",\"auto_apply_allowed\":false,\"record_columns\":[\"start_epoch_s\",\"end_"
    "epoch_s\",\"mean_room_c\",\"mean_setpoint_c\",\"mean_outside_c\",\"mean_heat_w\",\"heat_uncertainty_w\","
    "\"room_trend_k_per_h\",\"context_revision\"],\"records\":[";
static constexpr char kExportDiagnosticsPrefix[] =
    "],\"diagnostic_columns\":[\"epoch_s\",\"invalid_reasons\",\"control_mode\",\"room_c\",\"setpoint_c\","
    "\"outside_c\",\"actual_signed_heat_w\",\"heat_valid\",\"training_qualified\",\"base_w\",\"request_w\","
    "\"request_known\",\"heat_minus_base_w\",\"request_minus_heat_w\",\"coverage_s\",\"hp1_active\","
    "\"hp1_active_known\",\"hp2_active\",\"hp2_active_known\",\"active_limit\",\"active_limit_known\","
    "\"boiler_active\",\"boiler_known\",\"protection_active\",\"protection_known\",\"context_revision\"],"
    "\"diagnostics\":[";
static constexpr char kExportSuffix[] = "]}";

constexpr size_t delimited_rows_max(size_t row_size, size_t count) {
  return count == 0U ? 0U : row_size * count + count - 1U;
}

constexpr size_t kExportRecordRowMaxBytes = 2U + 8U + 3U * kMaxSerializedUint32Chars + 6U * kMaxSerializedFloatChars;
constexpr size_t kExportDiagnosticRowMaxBytes = 2U + 25U + 4U * kMaxSerializedUint32Chars + kMaxSerializedInt32Chars +
                                                8U * kMaxSerializedFloatChars + 13U * kMaxSerializedBoolChars;
constexpr size_t kMaxExportJsonBytes =
    sizeof(kExportRecordsPrefix) - 1U + delimited_rows_max(kExportRecordRowMaxBytes, kMaxExportRecordRows) +
    sizeof(kExportDiagnosticsPrefix) - 1U + delimited_rows_max(kExportDiagnosticRowMaxBytes, kMaxExportDiagnosticRows) +
    sizeof(kExportSuffix) - 1U;

static_assert(kMaxExportJsonBytes < kExportJsonBufferSize,
              "Worst-case house-learning export must leave room for its terminating NUL");

class JsonWriter {
 public:
  JsonWriter(char* buffer, size_t size) : buffer_(buffer), capacity_(size) { buffer_[0] = '\0'; }
  void add(const char* format, ...) {
    if (!ok_) return;
    va_list args;
    va_start(args, format);
    const int count = vsnprintf(buffer_ + used_, capacity_ - used_, format, args);
    va_end(args);
    if (count < 0 || static_cast<size_t>(count) >= capacity_ - used_) {
      ok_ = false;
      return;
    }
    used_ += static_cast<size_t>(count);
  }
  void number(float value) {
    if (std::isfinite(value))
      add("%.6g", static_cast<double>(value));
    else
      add("null");
  }
  bool ok() const { return ok_; }
  size_t size() const { return ok_ ? used_ : 0U; }

 private:
  char* buffer_;
  size_t capacity_;
  size_t used_{0U};
  bool ok_{true};
};

}  // namespace oq_ph_learning
