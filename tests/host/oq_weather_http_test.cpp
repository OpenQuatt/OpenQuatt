#include <array>
#include <cassert>
#include <cstring>
#include <string>

#include "components/openquatt_weather/OpenQuattWeatherHttp.h"

namespace {
using oq_weather::HttpResponse;

constexpr size_t kBodyCapacity = 4096;

struct Fixture {
  std::array<char, kBodyCapacity + 2> bytes;
  HttpResponse response;
  size_t capacity;

  explicit Fixture(size_t body_capacity = kBodyCapacity) : capacity(body_capacity) {
    assert(capacity <= kBodyCapacity);
    bytes.fill('#');
    response.reset(bytes.data() + 1, capacity);
  }
  void guards() const {
    assert(bytes.front() == '#');
    for (size_t i = capacity + 1; i < bytes.size(); ++i) assert(bytes[i] == '#');
    assert(response.size() < capacity || response.failed());
  }
  std::string body() const { return std::string(bytes.data() + 1, response.size()); }
};

void accepted(const std::string& wire, const std::string& body, size_t capacity = kBodyCapacity) {
  // Exercise every fixed split size for small messages and bytewise plus full
  // delivery for long boundary cases. Splitting can also divide CRLF and hex.
  const size_t maximum_split = wire.size() < 256 ? wire.size() : 1;
  for (size_t split = 1; split <= maximum_split; ++split) {
    Fixture fixture(capacity);
    for (size_t offset = 0; offset < wire.size(); offset += split) {
      const size_t count = split < wire.size() - offset ? split : wire.size() - offset;
      assert(fixture.response.feed(wire.data() + offset, count));
      fixture.guards();
    }
    assert(fixture.response.finish());
    assert(fixture.response.done() && !fixture.response.failed());
    assert(fixture.response.status() == 200);
    assert(fixture.body() == body);
    assert(fixture.bytes[body.size() + 1] == '\0');
    fixture.guards();
    assert(fixture.response.finish());  // A second EOF cannot append or corrupt.
  }
  Fixture whole(capacity);
  assert(whole.response.feed(wire.data(), wire.size()));
  assert(whole.response.finish());
  assert(whole.body() == body);
  whole.guards();
}

void rejected(const std::string& wire, size_t capacity = kBodyCapacity) {
  for (bool bytewise : {false, true}) {
    Fixture fixture(capacity);
    bool good = true;
    if (bytewise) {
      for (char byte : wire) {
        if (!fixture.response.feed(&byte, 1)) {
          good = false;
          break;
        }
        fixture.guards();
      }
    } else {
      good = fixture.response.feed(wire.data(), wire.size());
    }
    if (good) assert(!fixture.response.finish());
    assert(fixture.response.failed());
    assert(!fixture.response.done());
    assert(!fixture.response.finish());
    assert(!fixture.response.feed("x", 1));
    fixture.guards();
  }
}

std::string length_response(const std::string& body, const std::string& extra_headers = "") {
  return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + extra_headers + "\r\n" + body;
}

std::string chunked_headers() { return "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"; }

std::string metadata_padding(size_t bytes) {
  std::string padding;
  while (bytes != 0) {
    // Each valid header has at least X:\r\n and at most 511 content bytes.
    size_t length = bytes < HttpResponse::kLineBytes + 1 ? bytes : HttpResponse::kLineBytes + 1;
    if (bytes > length && bytes - length < 4) length -= 4 - (bytes - length);
    assert(length >= 4);
    padding += "X:" + std::string(length - 4, 'p') + "\r\n";
    bytes -= length;
  }
  return padding;
}

void valid_framing_and_fragmentation() {
  accepted(length_response("{\"temperature\":5.5}"), "{\"temperature\":5.5}");
  accepted("HTTP/1.0 200 OK\r\ncOnTeNt-LeNgTh:\t3 \t\r\nContent-Encoding: Identity\r\nX-Other: anything\r\n\r\nabc",
           "abc");
  accepted(chunked_headers() + "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n", "Wikipedia");
  accepted(chunked_headers() + "A;source=forecast\r\n0123456789\r\n0\r\nX-Checksum: ignored\r\n\r\n", "0123456789");
  accepted("HTTP/1.1 200 OK\r\nTransfer-Encoding: ChUnKeD\r\n\r\n1\r\na\r\n0\r\n\r\n", "a");
  accepted("HTTP/1.0 200 OK\r\nConnection: close\r\n\r\nabc", "abc");
  accepted("HTTP/1.1 200 OK\r\n\r\nabc", "abc");

  Fixture close_delimited;
  const std::string wire = "HTTP/1.0 200 OK\r\n\r\nx";
  assert(close_delimited.response.feed(wire.data(), wire.size()));
  assert(!close_delimited.response.done());
  assert(close_delimited.response.finish());
  assert(close_delimited.response.done());
}

void truncated_framing() {
  for (const auto& complete : {length_response("abc"), chunked_headers() + "3\r\nabc\r\n0\r\n\r\n"}) {
    for (size_t prefix = 0; prefix < complete.size(); ++prefix) rejected(complete.substr(0, prefix));
  }
  rejected("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n");
  rejected("HTTP/1.1 200 OK\r\n\r\n");
  rejected(chunked_headers() + "0\r\n\r\n");  // The weather payload must be nonempty.
  rejected(chunked_headers() + "1\r\nxX\n0\r\n\r\n");
  rejected(chunked_headers() + "1\r\nx\rX0\r\n\r\n");
  rejected(chunked_headers() + "1\r\nx\r\n0\r\nX-Trailer: missing-final-empty-line\r\n");
}

void invalid_status_headers_and_encoding() {
  for (const auto& status : {"HTTP/1.1 201 Created", "HTTP/1.1 204 No Content", "HTTP/1.1 301 Moved",
                             "HTTP/1.1 404 Missing", "HTTP/1.1 500 Error", "HTTP/2.0 200 OK", "HTTP/1.1 20 OK",
                             "HTTP/1.1 2000 OK", "HTTP/1.1 600 Invalid", "HTTP/1.1 +200 OK", "ICY 200 OK"})
    rejected(std::string(status) + "\r\nContent-Length: 1\r\n\r\nx");
  for (const auto& header :
       {"Content-Encoding: gzip", "Content-Encoding: br", "Content-Encoding:", "Transfer-Encoding: gzip",
        "Transfer-Encoding: identity", "Transfer-Encoding: gzip, chunked", "Transfer-Encoding: chunked, chunked",
        "Content Length: 1", "Content-Length : 1", "Missing-Colon", ": empty-key", " Folded-Header: value"})
    rejected("HTTP/1.1 200 OK\r\n" + std::string(header) + "\r\n\r\nx");
  rejected("HTTP/1.1 200 OK\nContent-Length: 1\n\nx");
  rejected("HTTP/1.1 200 OK\rXContent-Length: 1\r\n\r\nx");
  std::string embedded_nul = "HTTP/1.1 200 OK\r\nX-Test: a";
  embedded_nul.push_back('\0');
  embedded_nul += "b\r\n\r\nx";
  rejected(embedded_nul);
}

void ambiguous_lengths_and_chunk_sizes() {
  for (const auto& value : {"", "0", "-1", "+1", "1 0", "1,1", "0x10", "1;anything", "99999999999999999999999999999"})
    rejected("HTTP/1.1 200 OK\r\nContent-Length: " + std::string(value) + "\r\n\r\nx");
  for (const auto& second : {"Content-Length: 1", "Content-Length: 2", "Transfer-Encoding: chunked"})
    rejected("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n" + std::string(second) + "\r\n\r\nx");
  rejected("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 1\r\n\r\n1\r\nx\r\n0\r\n\r\n");
  rejected("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nTRANSFER-ENCODING: chunked\r\n\r\n1\r\nx\r\n0\r\n\r\n");
  for (const auto& value : {"", "-1", "+1", "0x1", "g", "1 0", "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"})
    rejected(chunked_headers() + value + "\r\nx\r\n0\r\n\r\n");
  rejected(chunked_headers() + "2\r\nab\r\n2\r\ncd\r\n0\r\n\r\n", 4);  // Cumulative overflow.
}

void body_capacity_and_reset() {
  for (size_t capacity : {0U, 1U}) {
    Fixture small(capacity);
    assert(small.response.failed());
    assert(!small.response.feed("x", 1));
    assert(!small.response.finish());
    small.guards();
  }
  HttpResponse missing;
  missing.reset(nullptr, 100);
  assert(missing.failed() && !missing.feed("x", 1) && !missing.finish());
  Fixture no_data;
  assert(no_data.response.feed(nullptr, 0));
  accepted(length_response("x"), "x", 2);
  accepted(length_response("abc"), "abc", 4);
  accepted(chunked_headers() + "3\r\nabc\r\n0\r\n\r\n", "abc", 4);
  accepted("HTTP/1.0 200 OK\r\n\r\nabc", "abc", 4);
  rejected(length_response("abcd"), 4);
  rejected(chunked_headers() + "4\r\nabcd\r\n0\r\n\r\n", 4);
  rejected("HTTP/1.0 200 OK\r\n\r\nabcd", 4);
  accepted(length_response(std::string(kBodyCapacity - 1, 'x')), std::string(kBodyCapacity - 1, 'x'));
  rejected(length_response(std::string(kBodyCapacity, 'x')));

  Fixture reused;
  assert(!reused.response.feed("bad\n", 4));
  reused.response.reset(reused.bytes.data() + 1, reused.capacity);
  assert(!reused.response.failed() && !reused.response.done() && reused.response.status() == 0 &&
         reused.response.size() == 0);
  const auto valid = length_response("reset");
  assert(reused.response.feed(valid.data(), valid.size()) && reused.response.finish());
  assert(reused.body() == "reset");
  reused.guards();
  reused.response.reset(reused.bytes.data() + 1, reused.capacity);
  assert(!reused.response.done() && reused.response.size() == 0);
}

void header_and_trailer_budgets() {
  const std::string prefix = "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n";
  accepted(prefix + "X:" + std::string(HttpResponse::kLineBytes - 3, 'p') + "\r\n\r\nx", "x");
  rejected(prefix + "X:" + std::string(HttpResponse::kLineBytes - 2, 'p') + "\r\n\r\nx");
  const size_t header_padding = HttpResponse::kHeaderBytes - prefix.size() - 2;
  accepted(prefix + metadata_padding(header_padding) + "\r\nx", "x");
  rejected(prefix + metadata_padding(header_padding + 1) + "\r\nx");

  const std::string chunks = chunked_headers() + "1\r\nx\r\n0\r\n";
  const size_t chunk_metadata = chunked_headers().size() + 6;  // Size lines; body and body-CRLF are separate.
  const size_t trailer_padding = HttpResponse::kHeaderBytes - chunk_metadata - 2;
  accepted(chunks + metadata_padding(trailer_padding) + "\r\n", "x");
  rejected(chunks + metadata_padding(trailer_padding + 1) + "\r\n");
  rejected(chunks + "X:" + std::string(HttpResponse::kLineBytes, 'p') + "\r\n\r\n");
  rejected(chunks + "not-a-trailer-header\r\n\r\n");
  rejected(chunked_headers() + "1;" + std::string(HttpResponse::kLineBytes, 'p') + "\r\nx\r\n0\r\n\r\n");

  std::string excessive_chunk_metadata = chunked_headers();
  for (size_t i = 0; i < 1800; ++i) excessive_chunk_metadata += "1\r\nx\r\n";
  excessive_chunk_metadata += "0\r\n\r\n";
  rejected(excessive_chunk_metadata);  // Body fits; aggregate chunk framing does not.
}

void unexpected_trailing_bytes() {
  for (const auto& complete : {length_response("x"), chunked_headers() + "1\r\nx\r\n0\r\n\r\n"}) {
    rejected(complete + "x");
    rejected(complete + "\r\n");
    rejected(complete + length_response("next"));
    Fixture fixture;
    assert(fixture.response.feed(complete.data(), complete.size()));
    assert(fixture.response.done());
    assert(!fixture.response.feed("x", 1));
    assert(fixture.response.failed());
    fixture.guards();
  }
}
}  // namespace

int main() {
  valid_framing_and_fragmentation();
  truncated_framing();
  invalid_status_headers_and_encoding();
  ambiguous_lengths_and_chunk_sizes();
  body_capacity_and_reset();
  header_and_trailer_budgets();
  unexpected_trailing_bytes();
}
