#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

// Host-testable format and storage policy for the settings change log. The flash window is a
// ring of three erase sectors with fixed 80-byte records. Appends only ever write erased bytes;
// when the head sector is full, the oldest sector is erased and becomes the new head.
namespace esphome::openquatt_change_log {

static constexpr size_t SECTOR_SIZE = 4096U;
static constexpr size_t SECTOR_COUNT = 3U;
static constexpr size_t CAPACITY = 100U;
// label + from + to share this many UTF-8 bytes; the web UI shortens text to fit.
static constexpr size_t TEXT_LIMIT = 65U;
static constexpr size_t TEXT_FIELD_SIZE = TEXT_LIMIT + 3U;
static constexpr uint32_t SECTOR_MAGIC = 0x4C43514FU;  // "OQCL"
static constexpr uint16_t SECTOR_FORMAT_VERSION = 1U;
static constexpr uint8_t RECORD_FORMAT_VERSION = 1U;
// Timestamps come from the browser clock; anything outside 2020..2100 is rejected.
static constexpr uint32_t MIN_TIMESTAMP_S = 1577836800U;
static constexpr uint32_t MAX_TIMESTAMP_S = 4102444800U;

struct ChangeLogRecord {
  uint32_t seq;
  uint32_t at;
  uint16_t checksum;
  uint8_t version;
  uint8_t text_length;  // label, from and to including their three terminators
  char text[TEXT_FIELD_SIZE];
};

struct ChangeLogSectorHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t sequence;
  uint32_t checksum;
};

static_assert(sizeof(ChangeLogRecord) == 80U, "Change log records are fixed 80-byte slots");
static_assert(sizeof(ChangeLogSectorHeader) == 16U, "Change log sector headers are 16 bytes");

static constexpr size_t RECORDS_PER_SECTOR = (SECTOR_SIZE - sizeof(ChangeLogSectorHeader)) / sizeof(ChangeLogRecord);
// Rotation erases one sector, so the other sectors alone must still hold a complete log.
static_assert(RECORDS_PER_SECTOR * (SECTOR_COUNT - 1U) >= CAPACITY,
              "Erasing the oldest sector must keep at least CAPACITY records");

// Fixed JSON keys and two ten-digit numbers per entry, plus text in which quotes and backslashes
// double. Control characters never reach the writer because records reject them.
static constexpr size_t JSON_ENTRY_MAX_BYTES = 64U + (2U * TEXT_LIMIT);
static constexpr size_t JSON_BUFFER_SIZE = 96U + (CAPACITY * JSON_ENTRY_MAX_BYTES);

struct ChangeLogText {
  const char* data;
  size_t length;
};

struct ChangeLogEntry {
  uint32_t seq;
  uint32_t at;
  const char* label;
  const char* from;
  const char* to;
};

inline bool sequence_is_newer(uint32_t candidate, uint32_t reference) {
  return static_cast<int32_t>(candidate - reference) > 0;
}

inline uint16_t crc16_ccitt(uint16_t crc, const uint8_t* data, size_t length) {
  for (size_t index = 0U; index < length; ++index) {
    crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[index]) << 8U));
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) != 0U ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U) : static_cast<uint16_t>(crc << 1U);
    }
  }
  return crc;
}

inline uint16_t record_checksum(const ChangeLogRecord& record) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
  const size_t text_length = record.text_length <= TEXT_FIELD_SIZE ? record.text_length : TEXT_FIELD_SIZE;
  const uint16_t head = crc16_ccitt(0xFFFFU, bytes, offsetof(ChangeLogRecord, checksum));
  return crc16_ccitt(head, bytes + offsetof(ChangeLogRecord, version),
                     offsetof(ChangeLogRecord, text) - offsetof(ChangeLogRecord, version) + text_length);
}

inline uint32_t sector_header_checksum(const ChangeLogSectorHeader& header) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&header);
  uint32_t hash = 2166136261U;
  for (size_t index = 0U; index < offsetof(ChangeLogSectorHeader, checksum); ++index) {
    hash ^= bytes[index];
    hash *= 16777619U;
  }
  return hash;
}

inline ChangeLogSectorHeader make_sector_header(uint32_t sequence) {
  ChangeLogSectorHeader header{SECTOR_MAGIC, SECTOR_FORMAT_VERSION, 0U, sequence, 0U};
  header.checksum = sector_header_checksum(header);
  return header;
}

inline bool valid_sector_header(const ChangeLogSectorHeader& header) {
  return header.magic == SECTOR_MAGIC && header.version == SECTOR_FORMAT_VERSION && header.reserved == 0U &&
         header.checksum == sector_header_checksum(header);
}

inline bool bytes_erased(const void* data, size_t length) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t index = 0U; index < length; ++index) {
    if (bytes[index] != 0xFFU) return false;
  }
  return true;
}

// Well-formed UTF-8 without control characters, so stored text is always valid inside a JSON string.
inline bool valid_text_part(const char* text, size_t length) {
  if (length > 0U && text == nullptr) return false;
  const auto* bytes = reinterpret_cast<const uint8_t*>(text);
  size_t index = 0U;
  while (index < length) {
    const uint8_t lead = bytes[index];
    if (lead < 0x80U) {
      if (lead < 0x20U || lead == 0x7FU) return false;
      ++index;
      continue;
    }
    size_t extra = 0U;
    uint8_t min_next = 0x80U;
    uint8_t max_next = 0xBFU;
    if (lead >= 0xC2U && lead <= 0xDFU) {
      extra = 1U;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
      extra = 2U;
      if (lead == 0xE0U) min_next = 0xA0U;
      if (lead == 0xEDU) max_next = 0x9FU;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
      extra = 3U;
      if (lead == 0xF0U) min_next = 0x90U;
      if (lead == 0xF4U) max_next = 0x8FU;
    } else {
      return false;
    }
    if (length - index <= extra || bytes[index + 1U] < min_next || bytes[index + 1U] > max_next) return false;
    for (size_t offset = 2U; offset <= extra; ++offset) {
      if ((bytes[index + offset] & 0xC0U) != 0x80U) return false;
    }
    // C1 controls (U+0080-009F) and bidi overrides (U+202A-202E, U+2066-2069) could make one
    // entry in the shared log read differently from what it stores.
    const uint8_t next = bytes[index + 1U];
    if ((lead == 0xC2U && next < 0xA0U) ||
        (lead == 0xE2U && ((next == 0x80U && bytes[index + 2U] >= 0xAAU && bytes[index + 2U] <= 0xAEU) ||
                           (next == 0x81U && bytes[index + 2U] >= 0xA6U && bytes[index + 2U] <= 0xA9U)))) {
      return false;
    }
    index += extra + 1U;
  }
  return true;
}

inline bool valid_timestamp(uint32_t at) { return at >= MIN_TIMESTAMP_S && at < MAX_TIMESTAMP_S; }

inline bool parse_timestamp(const char* text, size_t length, uint32_t* at) {
  if (text == nullptr || at == nullptr || length == 0U || length > 10U) return false;
  uint64_t value = 0U;
  for (size_t index = 0U; index < length; ++index) {
    if (text[index] < '0' || text[index] > '9') return false;
    value = (value * 10U) + static_cast<uint64_t>(text[index] - '0');
  }
  if (value > UINT32_MAX || !valid_timestamp(static_cast<uint32_t>(value))) return false;
  *at = static_cast<uint32_t>(value);
  return true;
}

inline bool encode_record(uint32_t seq, uint32_t at, ChangeLogText label, ChangeLogText from, ChangeLogText to,
                          ChangeLogRecord* record) {
  if (record == nullptr || seq == 0U || seq == UINT32_MAX || !valid_timestamp(at) || label.length == 0U ||
      label.length > TEXT_LIMIT || from.length > TEXT_LIMIT || to.length > TEXT_LIMIT ||
      label.length + from.length + to.length > TEXT_LIMIT || !valid_text_part(label.data, label.length) ||
      !valid_text_part(from.data, from.length) || !valid_text_part(to.data, to.length)) {
    return false;
  }
  std::memset(record, 0, sizeof(*record));
  record->seq = seq;
  record->at = at;
  record->version = RECORD_FORMAT_VERSION;
  size_t position = 0U;
  for (const ChangeLogText& part : {label, from, to}) {
    if (part.length > 0U) std::memcpy(record->text + position, part.data, part.length);
    position += part.length;
    record->text[position++] = '\0';
  }
  record->text_length = static_cast<uint8_t>(position);
  record->checksum = record_checksum(*record);
  return true;
}

inline bool decode_record(const ChangeLogRecord& record, ChangeLogEntry* entry) {
  if (record.version != RECORD_FORMAT_VERSION || record.text_length < 4U || record.text_length > TEXT_FIELD_SIZE ||
      record.seq == 0U || record.seq == UINT32_MAX || !valid_timestamp(record.at) ||
      record.checksum != record_checksum(record)) {
    return false;
  }
  const char* parts[3] = {nullptr, nullptr, nullptr};
  size_t start = 0U;
  for (size_t part = 0U; part < 3U; ++part) {
    const void* end = std::memchr(record.text + start, '\0', record.text_length - start);
    if (end == nullptr) return false;
    const size_t length = static_cast<size_t>(static_cast<const char*>(end) - (record.text + start));
    if ((part == 0U && length == 0U) || !valid_text_part(record.text + start, length)) return false;
    parts[part] = record.text + start;
    start += length + 1U;
  }
  if (start != record.text_length) return false;
  if (entry != nullptr) *entry = ChangeLogEntry{record.seq, record.at, parts[0], parts[1], parts[2]};
  return true;
}

// Fixed-capacity ring over caller-owned storage that keeps the newest records.
class ChangeLogRing {
 public:
  void bind(ChangeLogRecord* storage, size_t capacity) {
    this->storage_ = storage;
    this->capacity_ = storage == nullptr ? 0U : capacity;
    this->clear();
  }
  void clear() {
    this->head_ = 0U;
    this->size_ = 0U;
  }
  void push(const ChangeLogRecord& record) {
    if (this->capacity_ == 0U) return;
    this->storage_[this->head_] = record;
    this->head_ = (this->head_ + 1U) % this->capacity_;
    if (this->size_ < this->capacity_) ++this->size_;
  }
  size_t size() const { return this->size_; }
  // Index 0 is the newest record; callers keep index below size().
  const ChangeLogRecord& newest(size_t index) const {
    return this->storage_[(this->head_ + this->capacity_ - 1U - index) % this->capacity_];
  }

 private:
  ChangeLogRecord* storage_{nullptr};
  size_t capacity_{0U};
  size_t head_{0U};
  size_t size_{0U};
};

// FIFO over caller-owned storage for records that still have to reach flash.
class ChangeLogQueue {
 public:
  void bind(ChangeLogRecord* storage, size_t capacity) {
    this->storage_ = storage;
    this->capacity_ = storage == nullptr ? 0U : capacity;
    this->clear();
  }
  void clear() {
    this->tail_ = 0U;
    this->size_ = 0U;
  }
  bool full() const { return this->size_ >= this->capacity_; }
  size_t size() const { return this->size_; }
  bool push(const ChangeLogRecord& record) {
    if (this->full()) return false;
    this->storage_[(this->tail_ + this->size_) % this->capacity_] = record;
    ++this->size_;
    return true;
  }
  bool pop(ChangeLogRecord* record) {
    if (this->size_ == 0U || record == nullptr) return false;
    *record = this->storage_[this->tail_];
    this->tail_ = (this->tail_ + 1U) % this->capacity_;
    --this->size_;
    return true;
  }

 private:
  ChangeLogRecord* storage_{nullptr};
  size_t capacity_{0U};
  size_t tail_{0U};
  size_t size_{0U};
};

// Bounds flash wear from a misbehaving client: a burst for a settings session, then a slow refill.
class ChangeLogRateLimiter {
 public:
  static constexpr uint32_t BURST = 20U;
  static constexpr uint32_t REFILL_MS = 30000U;

  bool try_take(uint32_t now_ms) {
    if (!this->started_) {
      this->started_ = true;
      this->tokens_ = BURST;
      this->last_refill_ms_ = now_ms;
    }
    const uint32_t refill = (now_ms - this->last_refill_ms_) / REFILL_MS;
    if (refill > 0U) {
      this->tokens_ = this->tokens_ + refill >= BURST ? BURST : this->tokens_ + refill;
      this->last_refill_ms_ = this->tokens_ == BURST ? now_ms : this->last_refill_ms_ + (refill * REFILL_MS);
    }
    if (this->tokens_ == 0U) return false;
    if (this->tokens_ == BURST) this->last_refill_ms_ = now_ms;
    --this->tokens_;
    return true;
  }

 private:
  bool started_{false};
  uint32_t tokens_{0U};
  uint32_t last_refill_ms_{0U};
};

// Io offsets are relative to the change-log window:
//   bool read(size_t offset, void* data, size_t size);
//   bool erase_sector(size_t sector);
//   bool write(size_t offset, const void* data, size_t size);
template <typename Io>
class ChangeLogFlashStore {
 public:
  explicit ChangeLogFlashStore(Io& io) : io_(io) {}

  static constexpr size_t sector_offset(size_t sector) { return sector * SECTOR_SIZE; }
  static constexpr size_t record_offset(size_t sector, size_t slot) {
    return sector_offset(sector) + sizeof(ChangeLogSectorHeader) + (slot * sizeof(ChangeLogRecord));
  }

  // Replays valid records oldest first. Returns false, and leaves the store disabled, when the
  // window cannot be read.
  template <typename Sink>
  bool load(Sink&& sink) {
    this->healthy_ = false;
    this->head_sector_ = SECTOR_COUNT;
    this->head_sequence_ = 0U;
    this->next_slot_ = RECORDS_PER_SECTOR;
    this->last_seq_ = 0U;
    this->skipped_ = 0U;

    uint32_t sequences[SECTOR_COUNT] = {};
    size_t order[SECTOR_COUNT] = {};
    size_t valid_count = 0U;
    for (size_t sector = 0U; sector < SECTOR_COUNT; ++sector) {
      ChangeLogSectorHeader header{};
      if (!this->io_.read(sector_offset(sector), &header, sizeof(header))) return false;
      if (!valid_sector_header(header)) continue;
      sequences[sector] = header.sequence;
      size_t position = valid_count++;
      while (position > 0U && sequence_is_newer(sequences[order[position - 1U]], header.sequence)) {
        order[position] = order[position - 1U];
        --position;
      }
      order[position] = sector;
    }

    bool have_record = false;
    for (size_t rank = 0U; rank < valid_count; ++rank) {
      const size_t sector = order[rank];
      size_t used = 0U;
      for (size_t slot = 0U; slot < RECORDS_PER_SECTOR; ++slot) {
        ChangeLogRecord record{};
        if (!this->io_.read(record_offset(sector, slot), &record, sizeof(record))) return false;
        if (bytes_erased(&record, sizeof(record))) continue;
        used = slot + 1U;
        if (!decode_record(record, nullptr) || (have_record && !sequence_is_newer(record.seq, this->last_seq_))) {
          ++this->skipped_;
          continue;
        }
        have_record = true;
        this->last_seq_ = record.seq;
        sink(record);
      }
      if (rank + 1U == valid_count) {
        this->head_sector_ = sector;
        this->head_sequence_ = sequences[sector];
        this->next_slot_ = used;
      }
    }
    this->healthy_ = true;
    return true;
  }

  // A failed or unverified write disables the store for this boot: a slot that may hold partial
  // bytes can only be reused after its sector is erased again.
  bool append(const ChangeLogRecord& record) {
    if (!this->healthy_) return false;
    if (this->next_slot_ >= RECORDS_PER_SECTOR && !this->rotate_()) {
      this->healthy_ = false;
      return false;
    }
    const size_t offset = record_offset(this->head_sector_, this->next_slot_);
    ++this->next_slot_;
    if (!this->write_verified_(offset, &record, sizeof(record))) {
      this->healthy_ = false;
      return false;
    }
    return true;
  }

  bool healthy() const { return this->healthy_; }
  uint32_t last_sequence() const { return this->last_seq_; }
  size_t skipped_records() const { return this->skipped_; }

 private:
  bool rotate_() {
    const bool fresh = this->head_sector_ >= SECTOR_COUNT;
    const size_t sector = fresh ? 0U : (this->head_sector_ + 1U) % SECTOR_COUNT;
    const ChangeLogSectorHeader header = make_sector_header(fresh ? 1U : this->head_sequence_ + 1U);
    if (!this->io_.erase_sector(sector) || !this->write_verified_(sector_offset(sector), &header, sizeof(header))) {
      return false;
    }
    this->head_sector_ = sector;
    this->head_sequence_ = header.sequence;
    this->next_slot_ = 0U;
    return true;
  }

  bool write_verified_(size_t offset, const void* data, size_t size) {
    uint8_t readback[sizeof(ChangeLogRecord)] = {};
    return size <= sizeof(readback) && this->io_.write(offset, data, size) && this->io_.read(offset, readback, size) &&
           std::memcmp(readback, data, size) == 0;
  }

  Io& io_;
  bool healthy_{false};
  size_t head_sector_{SECTOR_COUNT};
  uint32_t head_sequence_{0U};
  size_t next_slot_{RECORDS_PER_SECTOR};
  uint32_t last_seq_{0U};
  size_t skipped_{0U};
};

// Bounded JSON writer; any overflow makes the whole document invalid.
class ChangeLogJsonWriter {
 public:
  ChangeLogJsonWriter(char* buffer, size_t capacity) : buffer_(buffer), capacity_(buffer == nullptr ? 0U : capacity) {}

  void raw(const char* text) { this->append_(text, std::strlen(text)); }
  void number(uint32_t value) {
    char digits[10];
    size_t count = 0U;
    do {
      digits[count++] = static_cast<char>('0' + (value % 10U));
      value /= 10U;
    } while (value != 0U);
    while (count > 0U) this->append_(&digits[--count], 1U);
  }
  void string(const char* text) {
    static constexpr char HEX[] = "0123456789abcdef";
    this->append_("\"", 1U);
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
      const auto byte = static_cast<uint8_t>(*cursor);
      if (*cursor == '"' || *cursor == '\\') {
        const char escaped[2] = {'\\', *cursor};
        this->append_(escaped, sizeof(escaped));
      } else if (byte < 0x20U) {
        const char escaped[6] = {'\\', 'u', '0', '0', HEX[byte >> 4U], HEX[byte & 0x0FU]};
        this->append_(escaped, sizeof(escaped));
      } else {
        this->append_(cursor, 1U);
      }
    }
    this->append_("\"", 1U);
  }
  // Length of a complete, NUL-terminated document, or 0 after an overflow.
  size_t finish() {
    if (!this->ok_ || this->length_ >= this->capacity_) return 0U;
    this->buffer_[this->length_] = '\0';
    return this->length_;
  }

 private:
  void append_(const char* data, size_t length) {
    if (!this->ok_ || length >= this->capacity_ - this->length_) {
      this->ok_ = false;
      return;
    }
    std::memcpy(this->buffer_ + this->length_, data, length);
    this->length_ += length;
  }

  char* buffer_;
  size_t capacity_;
  size_t length_{0U};
  bool ok_{true};
};

// Newest first, matching what the settings page shows.
inline size_t write_change_log_json(char* buffer, size_t capacity, const ChangeLogRing& ring, bool persistent) {
  ChangeLogJsonWriter json(buffer, capacity);
  json.raw("{\"ok\":true,\"capacity\":");
  json.number(static_cast<uint32_t>(CAPACITY));
  json.raw(persistent ? ",\"persistent\":true,\"entries\":[" : ",\"persistent\":false,\"entries\":[");
  bool first = true;
  for (size_t index = 0U; index < ring.size(); ++index) {
    ChangeLogEntry entry{};
    if (!decode_record(ring.newest(index), &entry)) continue;
    json.raw(first ? "{\"seq\":" : ",{\"seq\":");
    first = false;
    json.number(entry.seq);
    json.raw(",\"at\":");
    json.number(entry.at);
    json.raw(",\"label\":");
    json.string(entry.label);
    json.raw(",\"from\":");
    json.string(entry.from);
    json.raw(",\"to\":");
    json.string(entry.to);
    json.raw("}");
  }
  json.raw("]}");
  return json.finish();
}

}  // namespace esphome::openquatt_change_log
