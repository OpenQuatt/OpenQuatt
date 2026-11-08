#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace oq_weather {

// No HTTP parser allocations: header lines, total metadata and body all have
// independent limits. Feed bounded TLS reads, checking cancellation in between.
class HttpResponse {
 public:
  static constexpr size_t kHeaderBytes = 4096;
  static constexpr size_t kLineBytes = 512;
  enum class Phase : uint8_t {
    STATUS,
    HEADERS,
    LENGTH_BODY,
    CLOSE_BODY,
    CHUNK_SIZE,
    CHUNK_BODY,
    CHUNK_CRLF,
    TRAILERS,
    DONE,
    ERROR
  };

  void reset(char* body, size_t capacity) {
    body_ = body;
    capacity_ = capacity;
    size_ = line_size_ = metadata_size_ = remaining_ = crlf_size_ = 0;
    status_ = 0;
    content_length_ = chunked_ = line_cr_ = false;
    phase_ = body && capacity > 1 ? Phase::STATUS : Phase::ERROR;
  }
  bool feed(const char* data, size_t size) {
    if (!data && size != 0) return fail_();
    for (size_t i = 0; i < size; ++i) {
      const char ch = data[i];
      switch (phase_) {
        case Phase::STATUS:
        case Phase::HEADERS:
        case Phase::CHUNK_SIZE:
        case Phase::TRAILERS:
          if (++metadata_size_ > kHeaderBytes) return fail_();
          if (line_cr_) {
            if (ch != '\n') return fail_();
            line_[line_size_] = '\0';
            if (!process_line_()) return fail_();
            line_size_ = 0;
            line_cr_ = false;
          } else if (ch == '\r') {
            line_cr_ = true;
          } else {
            if (ch == '\n' || (static_cast<unsigned char>(ch) < 0x20 && ch != '\t') || line_size_ >= kLineBytes - 1)
              return fail_();
            line_[line_size_++] = ch;
          }
          break;
        case Phase::LENGTH_BODY:
        case Phase::CLOSE_BODY:
        case Phase::CHUNK_BODY:
          if (!body_ || size_ >= capacity_ - 1) return fail_();
          body_[size_++] = ch;
          if (phase_ != Phase::CLOSE_BODY && --remaining_ == 0)
            phase_ = phase_ == Phase::CHUNK_BODY ? Phase::CHUNK_CRLF : Phase::DONE;
          break;
        case Phase::CHUNK_CRLF:
          if (ch != (crlf_size_ == 0 ? '\r' : '\n')) return fail_();
          if (++crlf_size_ == 2) {
            crlf_size_ = 0;
            phase_ = Phase::CHUNK_SIZE;
          }
          break;
        default:
          return fail_();
      }
    }
    return phase_ != Phase::ERROR;
  }
  bool finish() {
    if (phase_ == Phase::CLOSE_BODY) phase_ = Phase::DONE;
    if (phase_ != Phase::DONE || size_ == 0 || !body_) return fail_();
    body_[size_] = '\0';
    return true;
  }
  bool done() const { return phase_ == Phase::DONE; }
  bool failed() const { return phase_ == Phase::ERROR; }
  size_t size() const { return size_; }
  int status() const { return status_; }

 private:
  // Keep this scratch in the owner's PSRAM storage, never on a task stack.
  char line_[kLineBytes]{};
  char* body_ = nullptr;
  size_t capacity_ = 0, size_ = 0, line_size_ = 0, metadata_size_ = 0, remaining_ = 0, crlf_size_ = 0;
  int status_ = 0;
  bool content_length_ = false, chunked_ = false, line_cr_ = false;
  Phase phase_ = Phase::ERROR;

  bool fail_() {
    phase_ = Phase::ERROR;
    return false;
  }
  static char lower_(char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
  static bool equal_(const char* a, const char* b) {
    while (*a && *b)
      if (lower_(*a++) != *b++) return false;
    return *a == *b;
  }
  bool number_(const char* value, unsigned base, size_t& result) {
    result = 0;
    size_t count = 0;
    for (; *value && *value != ';'; ++value) {
      const char ch = lower_(*value);
      const unsigned digit = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : base;
      if (digit >= base || result > (capacity_ - 1) / base) return false;
      result = result * base + digit;
      if (result >= capacity_) return false;
      ++count;
    }
    return count > 0 && (base == 16 || *value == '\0');
  }
  bool process_line_() {
    if (phase_ == Phase::STATUS) {
      if (line_size_ < 12 || (std::memcmp(line_, "HTTP/1.1 ", 9) && std::memcmp(line_, "HTTP/1.0 ", 9)) ||
          line_[9] < '1' || line_[9] > '5' || line_[10] < '0' || line_[10] > '9' || line_[11] < '0' ||
          line_[11] > '9' || (line_size_ > 12 && line_[12] != ' '))
        return false;
      status_ = (line_[9] - '0') * 100 + (line_[10] - '0') * 10 + line_[11] - '0';
      if (status_ != 200) return false;
      phase_ = Phase::HEADERS;
      return true;
    }
    if (phase_ == Phase::CHUNK_SIZE) {
      if (!number_(line_, 16, remaining_) || remaining_ >= capacity_ - size_) return false;
      phase_ = remaining_ == 0 ? Phase::TRAILERS : Phase::CHUNK_BODY;
      return true;
    }
    if (line_size_ == 0) {
      if (phase_ == Phase::TRAILERS) {
        phase_ = Phase::DONE;
        return true;
      }
      if (content_length_ && chunked_) return false;
      phase_ = chunked_ ? Phase::CHUNK_SIZE : content_length_ ? Phase::LENGTH_BODY : Phase::CLOSE_BODY;
      return !content_length_ || remaining_ > 0;
    }
    char* colon = std::strchr(line_, ':');
    if (!colon || colon == line_ || line_[0] == ' ') return false;
    for (char* key = line_; key < colon; ++key)
      if (static_cast<unsigned char>(*key) <= 0x20 || static_cast<unsigned char>(*key) >= 0x7f) return false;
    *colon = '\0';
    char* value = colon + 1;
    while (*value == ' ' || *value == '\t') ++value;
    char* end = line_ + line_size_;
    while (end > value && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
    if (phase_ == Phase::TRAILERS) return true;
    if (equal_(line_, "content-length")) {
      if (content_length_ || !number_(value, 10, remaining_)) return false;
      content_length_ = true;
    } else if (equal_(line_, "transfer-encoding")) {
      if (chunked_ || !equal_(value, "chunked")) return false;
      chunked_ = true;
    } else if (equal_(line_, "content-encoding") && !equal_(value, "identity")) {
      return false;
    }
    return true;
  }
};

}  // namespace oq_weather
