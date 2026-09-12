#pragma once

// Pure, host-testable decision logic for the dedicated SSE log stream
// (GET /openquatt/logs/stream, issue #673).
//
// This header deliberately depends only on standard C++ headers so it can be
// covered by host regression tests. It performs no I/O and owns no state:
// the firmware component in OpenQuattLogHistory.{h,cpp} remains the only place
// that touches sockets, PSRAM buffers and the history mutex.
//
// Conventions:
// - Log sequence numbers are uint16_t and wrap. All ordering comparisons are
//   wrap-aware and only meaningful for distances well below 32768. The stream
//   closes a client long before that (history holds 250 entries), so wrap
//   ambiguity cannot occur in practice.
// - A cursor is "in window" when it equals oldest-1 (ask everything buffered)
//   or lies in [oldest, newest] (catch up from cursor+1). Anything else means
//   the client must backfill via GET /openquatt/logs/recent.

#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace esphome {
namespace openquatt_log_history {
namespace log_stream_logic {

inline bool seq_is_newer(uint16_t seq, uint16_t base) {
  const auto diff = static_cast<uint16_t>(seq - base);
  return diff != 0 && diff < 0x8000U;
}

inline bool cursor_in_window(uint16_t cursor, uint16_t oldest, uint16_t newest) {
  if (cursor == static_cast<uint16_t>(oldest - 1U)) {
    return true;
  }
  const auto dist_to_cursor = static_cast<uint16_t>(cursor - oldest);
  const auto dist_to_newest = static_cast<uint16_t>(newest - oldest);
  return dist_to_cursor <= dist_to_newest;
}

struct CursorResolution {
  // Initial last_seq for the new session: entries newer than this are sent.
  uint16_t initial_seq{0};
  // True when the client must backfill via /recent before relying on live data.
  bool need_gap{false};
};

inline CursorResolution resolve_initial_seq(bool has_since, uint16_t since, bool history_empty, uint16_t oldest,
                                            uint16_t newest, uint16_t next_seq_minus_one) {
  CursorResolution out{};
  if (history_empty) {
    out.initial_seq = next_seq_minus_one;
    out.need_gap = false;
    return out;
  }
  if (!has_since) {
    out.initial_seq = newest;
    out.need_gap = false;
    return out;
  }
  if (cursor_in_window(since, oldest, newest)) {
    out.initial_seq = since;
    out.need_gap = false;
    return out;
  }
  // Stale or future cursor: start live and tell the client to backfill.
  out.initial_seq = newest;
  out.need_gap = true;
  return out;
}

// Strict decimal uint16 cursor parsing for ?since= / Last-Event-ID.
// Accepts only [0-9]+ without sign, whitespace or trailing garbage and only
// values <= 65535. Returns false for absent (null/empty) and invalid input;
// callers distinguish "parameter absent" (key/header not present) from
// "parameter present but invalid" before calling this helper.
inline bool parse_seq_strict(const char* text, uint16_t* out) {
  if (out != nullptr) {
    *out = 0;
  }
  if (text == nullptr || out == nullptr || text[0] == '\0') {
    return false;
  }
  unsigned long value = 0;
  for (size_t i = 0; text[i] != '\0'; ++i) {
    const char c = text[i];
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10UL + static_cast<unsigned long>(c - '0');
    if (value > 0xFFFFUL) {
      return false;
    }
  }
  *out = static_cast<uint16_t>(value);
  return true;
}

}  // namespace log_stream_logic
}  // namespace openquatt_log_history
}  // namespace esphome
