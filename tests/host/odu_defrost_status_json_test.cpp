#include <cassert>
#include <climits>
#include <limits>
#include <string>
#include "includes/odu/oq_odu_defrost_diagnostics.h"
#include "includes/odu/oq_odu_defrost_status_json.h"

// Exercises the production serializer, not a second implementation of its JSON.
struct Fixture {
  bool online{false}, fresh{false}, identity{false}, loaded{false}, automatic{false}, busy{false};
  bool active{false}, manual{false}, can_trigger{false};
  int mode{-1}, operation_mode{-1};
  float ambient{NAN}, coil{NAN}, evaporation{NAN}, hz{NAN};
  int elapsed_s{-1}, since_s{-1}, duration_s{-1};
  const char* state{"IDLE"};
  const char* guard{"OFFLINE"};
  bool profile_available{false}, auto_reapply{false};
  int desired_mode{-1};
  const char* profile_state{"NONE"};
  oq_defrost::Diagnostics diagnostics{};
};

int main() {
  Fixture s;
  std::string output;
  size_t max_chunk = 0;
  const auto send = [&](const char* value, size_t size) {
    output.append(value, size);
    if (size > max_chunk) max_chunk = size;
    return true;
  };
  assert(oq_defrost::write_status_json(s, 1, 2, false, "abcd", send));
  const std::string expected =
      R"({"hp":1,"online":false,"fresh":false,"identity_ready":false,"variant":2,"loaded":false,"auto_defrost_control_ok":false,"busy":false,"active":false,"manual":false,"can_trigger":false,"defrost_mode":-1,"operation_mode":-1,"state":"IDLE","guard":"OFFLINE","elapsed_s":-1,"since_last_s":-1,"last_duration_s":-1,"confidence":"limited","end_reason":"unknown","ambient_c":null,"coil_c":null,"evaporation_c":null,"compressor_hz":null,"delta_k":null,"start_threshold_c":null,"delta_required_k":null,"exit_threshold_c":null,"alternate_exit_c":null,"confirmation_s":-1,"confirmation_required_s":-1,"runtime_s":-1,"minimum_runtime_s":-1,"interval_s":-1,"max_duration_s":-1,"exit_confirmation_s":-1,"exit_required_s":-1,"inferred_end_reason":"UNKNOWN","profile_available":false,"auto_reapply":false,"desired_mode":-1,"profile_state":"NONE","csrf_token":"abcd"})";
  assert(output == expected && max_chunk < 192);

  s.online = s.fresh = s.identity = s.loaded = s.automatic = s.can_trigger = true;
  s.mode = s.operation_mode = s.elapsed_s = s.since_s = s.duration_s = INT_MIN;
  s.state = "PEER_DEFROST_ACTIVE";
  s.guard = "AUTO_CONTROL_UNAVAILABLE";
  s.profile_available = s.auto_reapply = true;
  s.desired_mode = INT_MAX;
  s.profile_state = "IDENTITY_MISMATCH";
  s.ambient = -std::numeric_limits<float>::max();
  s.evaporation = std::numeric_limits<float>::max();
  s.coil = INFINITY;
  s.hz = 49.0f;
  auto& d = s.diagnostics;
  d.confirm_s = d.confirm_required_s = d.runtime_s = d.minimum_runtime_s = INT_MIN;
  d.interval_s = d.max_duration_s = d.exit_confirm_s = d.exit_required_s = INT_MIN;
  d.end_reason = "MAX_DURATION";
  output.clear();
  assert(oq_defrost::write_status_json(s, UINT_MAX, UINT_MAX, true, "abcdef0123456789", send));
  assert(output.find("\"state\":\"QUEUED\"") != std::string::npos);
  assert(output.find("\"can_trigger\":false") != std::string::npos);
  assert(output.find("\"coil_c\":null") != std::string::npos);
  assert(output.find("\"delta_k\":null") != std::string::npos);
  assert(output.find("\"exit_required_s\":-2147483648") != std::string::npos);
  assert(output.find("\"desired_mode\":2147483647") != std::string::npos);
  assert(output.substr(output.size() - 18) == "abcdef0123456789\"}" && max_chunk < 192);

  // A failed synchronous send stops immediately; later chunks cannot claim success.
  unsigned calls = 0;
  assert(!oq_defrost::write_status_json(s, 1, 2, false, "abcd", [&](const char*, size_t) { return ++calls < 4; }));
  assert(calls == 4);
  const std::string oversized(300, 'A');
  s.guard = oversized.c_str();
  calls = 0;
  assert(!oq_defrost::write_status_json(s, 1, 2, false, "abcd", [&](const char*, size_t size) {
    assert(size < 192);
    ++calls;
    return true;
  }));
  assert(calls == 2);  // The truncated third chunk is never transmitted.
}
