#include <cassert>
#include <limits>

#include "components/openquatt_compressor_limits/limits_storage.h"

using namespace esphome::openquatt_compressor_limits;

class FakeStorage : public LimitsStorage {
 public:
  ReadResult read_bundle(LimitsRecord& out) override {
    ++reads;
    if (read_error || (readback_error && writes > 0U)) return ReadResult::ERROR;
    if (!exists) return ReadResult::MISSING;
    out = record;
    if (stale_readback && writes > 0U) out.limit_2h = 1.0f;
    return ReadResult::FOUND;
  }
  ReadResult read_legacy(uint8_t index, float& out) override {
    if (legacy_error[index]) return ReadResult::ERROR;
    if (!legacy_exists[index]) return ReadResult::MISSING;
    out = legacy[index];
    return ReadResult::FOUND;
  }
  bool write_bundle(const LimitsRecord& in) override {
    ++writes;
    if (write_error) return false;
    record = in;
    exists = true;
    return !commit_error;  // A failed commit can leave a readable new record.
  }
  bool erase_legacy() override {
    ++erasures;
    legacy_exists[0] = false;
    if (erase_error) return false;  // Interrupted/partial cleanup.
    legacy_exists[1] = false;
    return true;
  }
  LimitsRecord record{};
  float legacy[2]{9.0f, 75.0f};
  bool legacy_exists[2]{true, true};
  bool legacy_error[2]{};
  bool exists{false};
  bool read_error{false};
  bool readback_error{false};
  bool stale_readback{false};
  bool write_error{false};
  bool commit_error{false};
  bool erase_error{false};
  unsigned reads{0U};
  unsigned writes{0U};
  unsigned erasures{0U};
};

int main() {
  // Fresh install and missing individual preferences preserve exact defaults.
  for (unsigned mask = 0U; mask < 4U; ++mask) {
    FakeStorage storage;
    storage.legacy_exists[0] = (mask & 1U) != 0U;
    storage.legacy_exists[1] = (mask & 2U) != 0U;
    LimitsState state;
    assert(state.restore(storage));
    assert(state.value(0U) == ((mask & 1U) ? 9.0f : 6.0f));
    assert(state.value(1U) == ((mask & 2U) ? 75.0f : 40.0f));
    assert(storage.writes == 0U && storage.erasures == 0U);
    assert(state.flush(storage));
    assert(valid_record(storage.record));
    assert(!state.pending());
    assert(!storage.legacy_exists[0] && !storage.legacy_exists[1]);
    assert(state.flush(storage));
    assert(storage.writes == 1U && storage.erasures == 1U);
  }

  // Boot read faults and unsupported records cannot become a fresh install.
  for (unsigned fault = 0U; fault < 7U; ++fault) {
    FakeStorage storage;
    storage.exists = true;
    switch (fault) {
      case 0U:
        storage.read_error = true;
        break;
      case 1U:
        storage.record.magic = 0U;
        break;
      case 2U:
        storage.record.version = 2U;
        break;
      case 3U:
        storage.record.bytes = 20U;
        break;
      case 4U:
        storage.record.limit_2h = std::numeric_limits<float>::quiet_NaN();
        break;
      case 5U:
        storage.record.limit_72h = 121.0f;
        break;
      case 6U:
        storage.record.limit_2h = 0.0f;
        break;
    }
    LimitsState state;
    assert(!state.restore(storage));
    assert(!state.ready());
    assert(!state.set(0U, 8.0f));
    assert(!state.flush(storage));
    assert(storage.writes == 0U && storage.erasures == 0U);
    assert(storage.legacy_exists[0] && storage.legacy_exists[1]);
  }
  for (uint8_t index = 0U; index < 2U; ++index) {
    FakeStorage storage;
    storage.legacy_error[index] = true;
    LimitsState state;
    assert(!state.restore(storage));
    assert(!state.flush(storage));
    assert(storage.erasures == 0U);
    storage.legacy_error[index] = false;
    storage.legacy[index] = std::numeric_limits<float>::infinity();
    assert(!state.restore(storage));
    assert(storage.writes == 0U);
    storage.legacy[index] = 10.0f;
    assert(state.restore(storage));  // Transient read failure can recover.
    assert(state.flush(storage));
  }

  // Full NVS, commit failure, unavailable/stale readback all retain old keys.
  for (unsigned fault = 0U; fault < 4U; ++fault) {
    FakeStorage storage;
    LimitsState state;
    assert(state.restore(storage));
    storage.write_error = fault == 0U;
    storage.commit_error = fault == 1U;
    storage.readback_error = fault == 2U;
    storage.stale_readback = fault == 3U;
    assert(!state.flush(storage));
    assert(state.pending());
    assert(storage.erasures == 0U);
    assert(storage.legacy_exists[0] && storage.legacy_exists[1]);
    storage.write_error = storage.commit_error = storage.readback_error = storage.stale_readback = false;
    assert(state.flush(storage));
    assert(state.value(0U) == 9.0f && state.value(1U) == 75.0f);
  }

  // Reset before write: redo import. Reset after commit/before cleanup:
  // the new record wins over stale legacy values without another write.
  {
    FakeStorage storage;
    LimitsState before_write;
    assert(before_write.restore(storage));
    LimitsState after_reset;
    assert(after_reset.restore(storage));
    assert(after_reset.value(0U) == 9.0f);
    assert(after_reset.set(0U, 12.0f));
    storage.readback_error = true;
    assert(!after_reset.flush(storage));
    storage.readback_error = false;
    LimitsState after_commit;
    assert(after_commit.restore(storage));
    assert(after_commit.value(0U) == 12.0f);
    assert(after_commit.flush(storage));
    assert(storage.writes == 1U);
  }

  // Partial deletion is retryable across a reset; do not restore stale keys.
  {
    FakeStorage storage;
    LimitsState state;
    assert(state.restore(storage));
    assert(state.set(1U, 80.0f));
    storage.erase_error = true;
    assert(!state.flush(storage));
    assert(state.pending());
    assert(storage.writes == 1U);
    assert(!storage.legacy_exists[0] && storage.legacy_exists[1]);
    LimitsState after_reset;
    assert(after_reset.restore(storage));
    assert(after_reset.value(1U) == 80.0f);
    storage.erase_error = false;
    assert(after_reset.flush(storage));
    assert(storage.writes == 1U);
  }

  // Rapid changes are coalesced; both fields survive boot and clean flushes
  // do not rewrite flash. Existing fractional values also remain unchanged.
  {
    FakeStorage storage;
    storage.legacy[0] = 9.5f;
    LimitsState state;
    assert(state.restore(storage));
    assert(state.value(0U) == 9.5f);
    assert(state.flush(storage));
    assert(state.set(0U, 10.0f));
    assert(state.set(1U, 80.0f));
    assert(state.set(0U, 11.0f));
    assert(!state.set(2U, 11.0f));
    assert(!state.set(0U, 21.0f));
    assert(!state.set(1U, std::numeric_limits<float>::quiet_NaN()));
    assert(state.flush(storage));
    assert(storage.writes == 2U);
    LimitsState after_reset;
    assert(after_reset.restore(storage));
    assert(after_reset.value(0U) == 11.0f && after_reset.value(1U) == 80.0f);
    assert(after_reset.flush(storage));
    assert(after_reset.set(0U, 11.0f));
    assert(after_reset.flush(storage));
    assert(storage.writes == 2U);
  }
}
