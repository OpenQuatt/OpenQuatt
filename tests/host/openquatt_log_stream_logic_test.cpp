#include <assert.h>
#include <initializer_list>
#include <stdint.h>

#include "components/openquatt_log_history/OpenQuattLogStreamLogic.h"

int main() {
  using namespace esphome::openquatt_log_history::log_stream_logic;

  // --- seq_is_newer: plain ordering -------------------------------------
  assert(seq_is_newer(101, 100));
  assert(!seq_is_newer(100, 100));
  assert(!seq_is_newer(100, 101));

  // --- seq_is_newer: uint16 wrap -----------------------------------------
  assert(seq_is_newer(0, 65535));
  assert(seq_is_newer(1, 65535));
  assert(!seq_is_newer(65535, 0));
  assert(!seq_is_newer(65535, 1));
  // Half-range boundary: distances < 32768 count as newer.
  assert(seq_is_newer(32767, 0));
  assert(!seq_is_newer(32768, 0));

  // --- cursor_in_window ----------------------------------------------------
  // Window [90, 100]: oldest-1 and every buffered seq are in window.
  assert(cursor_in_window(89, 90, 100));
  assert(cursor_in_window(90, 90, 100));
  assert(cursor_in_window(95, 90, 100));
  assert(cursor_in_window(100, 90, 100));
  // Too old (overwritten) and future cursors are out of window.
  assert(!cursor_in_window(88, 90, 100));
  assert(!cursor_in_window(101, 90, 100));
  assert(!cursor_in_window(0, 90, 100));
  // Wrapped window [65530, 5]: members on both sides of the wrap are inside.
  assert(cursor_in_window(65529, 65530, 5));
  assert(cursor_in_window(65530, 65530, 5));
  assert(cursor_in_window(0, 65530, 5));
  assert(cursor_in_window(5, 65530, 5));
  assert(!cursor_in_window(65528, 65530, 5));
  assert(!cursor_in_window(6, 65530, 5));

  // --- resolve_initial_seq ---------------------------------------------------
  // Empty history: start at next_seq-1, never a gap.
  {
    const CursorResolution r = resolve_initial_seq(false, 0, true, 0, 0, 42);
    assert(r.initial_seq == 42);
    assert(!r.need_gap);
  }
  {
    const CursorResolution r = resolve_initial_seq(true, 41, true, 0, 0, 42);
    assert(r.initial_seq == 42);
    assert(!r.need_gap);
  }
  // Live connect without cursor: start at newest, no gap.
  {
    const CursorResolution r = resolve_initial_seq(false, 0, false, 90, 100, 100);
    assert(r.initial_seq == 100);
    assert(!r.need_gap);
  }
  // Valid cursors resume without a gap.
  for (uint16_t cursor : {89, 90, 95, 100}) {
    const CursorResolution r = resolve_initial_seq(true, cursor, false, 90, 100, 100);
    assert(r.initial_seq == cursor);
    assert(!r.need_gap);
  }
  // Stale cursor (overwritten) and future cursor: start live with a gap flag
  // so the client backfills via GET /openquatt/logs/recent.
  for (uint16_t cursor : {88, 0, 101, 500}) {
    const CursorResolution r = resolve_initial_seq(true, cursor, false, 90, 100, 100);
    assert(r.initial_seq == 100);
    assert(r.need_gap);
  }

  // --- parse_seq_strict -------------------------------------------------------
  uint16_t seq = 0xFFFF;
  assert(parse_seq_strict("0", &seq) && seq == 0);
  assert(parse_seq_strict("1", &seq) && seq == 1);
  assert(parse_seq_strict("65535", &seq) && seq == 65535);
  assert(parse_seq_strict("007", &seq) && seq == 7);
  // Invalid: empty, non-numeric, trailing garbage, out of range, signs, space.
  assert(!parse_seq_strict(nullptr, &seq));
  assert(!parse_seq_strict("", &seq));
  assert(!parse_seq_strict("abc", &seq));
  assert(!parse_seq_strict("123abc", &seq));
  assert(!parse_seq_strict("12 3", &seq));
  assert(!parse_seq_strict("70000", &seq));
  assert(!parse_seq_strict("65536", &seq));
  assert(!parse_seq_strict("-1", &seq));
  assert(!parse_seq_strict("+1", &seq));
  assert(!parse_seq_strict(" 1", &seq));
  assert(!parse_seq_strict("1 ", &seq));
  assert(!parse_seq_strict("0x10", &seq));
  assert(!parse_seq_strict("3.5", &seq));

  // State-machine contracts that socket-level HIL must additionally cover
  // (needs real httpd sockets, see PR #674 HIL plan):
  // - EAGAIN/partial send keeps exactly one pending frame; last_seq is already
  //   committed at queue time so the frame is never queued twice.
  // - gap frames clear need_gap at queue time for the same reason.
  // - closing slots are only recycled after free_ctx confirms the real close;
  //   a late free_ctx must never wipe a replacement connection's fd.
}
