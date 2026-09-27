#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>

namespace oq_defrost {
// Send must consume/copy each chunk synchronously; the bounded buffer is reused.
// Snapshot strings and token are trusted static enum labels / hexadecimal auth.
template <typename Snapshot, typename Send>
bool write_status_json(const Snapshot& s, unsigned hp, unsigned variant, bool pending, const char* token, Send&& send) {
  char out[192];
  const auto boolean = [](bool value) { return value ? "true" : "false"; };
  const auto emit = [&](int size) {
    return size >= 0 && static_cast<size_t>(size) < sizeof(out) && send(out, static_cast<size_t>(size));
  };
  const auto literal = [&](const char* value) { return send(value, strlen(value)); };
  const auto number = [&](float value) {
    return emit(std::isfinite(value) ? snprintf(out, sizeof(out), "%.2f", static_cast<double>(value))
                                     : snprintf(out, sizeof(out), "null"));
  };
  if (!emit(snprintf(
          out, sizeof(out),
          R"({"hp":%u,"online":%s,"fresh":%s,"identity_ready":%s,"variant":%u,"loaded":%s,"auto_defrost_control_ok":%s)",
          hp, boolean(s.online), boolean(s.fresh), boolean(s.identity), variant, boolean(s.loaded),
          boolean(s.automatic))) ||
      !emit(snprintf(out, sizeof(out),
                     R"(,"busy":%s,"active":%s,"manual":%s,"can_trigger":%s,"defrost_mode":%d,"operation_mode":%d)",
                     boolean(s.busy || pending), boolean(s.active), boolean(s.manual),
                     boolean(s.can_trigger && !pending), s.mode, s.operation_mode)) ||
      !emit(snprintf(out, sizeof(out),
                     R"(,"state":"%s","guard":"%s","elapsed_s":%d,"since_last_s":%d,"last_duration_s":%d)",
                     pending ? "QUEUED" : s.state, s.guard, s.elapsed_s, s.since_s, s.duration_s)) ||
      !literal(R"(,"confidence":"limited","end_reason":"unknown","ambient_c":)") || !number(s.ambient) ||
      !literal(",\"coil_c\":") || !number(s.coil) || !literal(",\"evaporation_c\":") || !number(s.evaporation) ||
      !literal(",\"compressor_hz\":") || !number(s.hz) || !literal(",\"delta_k\":") ||
      !number(s.ambient - s.evaporation) || !literal(",\"start_threshold_c\":") || !number(s.diagnostics.start_c) ||
      !literal(",\"delta_required_k\":") || !number(s.diagnostics.delta_k) || !literal(",\"exit_threshold_c\":") ||
      !number(s.diagnostics.exit_c) || !literal(",\"alternate_exit_c\":") || !number(s.diagnostics.alternate_exit_c) ||
      !emit(snprintf(out, sizeof(out),
                     R"(,"confirmation_s":%d,"confirmation_required_s":%d,"runtime_s":%d,"minimum_runtime_s":%d)",
                     s.diagnostics.confirm_s, s.diagnostics.confirm_required_s, s.diagnostics.runtime_s,
                     s.diagnostics.minimum_runtime_s)) ||
      !emit(snprintf(
          out, sizeof(out),
          R"(,"interval_s":%d,"max_duration_s":%d,"exit_confirmation_s":%d,"exit_required_s":%d,"inferred_end_reason":"%s")",
          s.diagnostics.interval_s, s.diagnostics.max_duration_s, s.diagnostics.exit_confirm_s,
          s.diagnostics.exit_required_s, s.diagnostics.end_reason)) ||
      !emit(snprintf(out, sizeof(out),
                     R"(,"profile_available":%s,"auto_reapply":%s,"desired_mode":%d,"profile_state":"%s")",
                     boolean(s.profile_available), boolean(s.auto_reapply), s.desired_mode, s.profile_state)) ||
      !literal(",\"csrf_token\":\"") || !literal(token) || !literal("\"}"))
    return false;
  return true;
}
}  // namespace oq_defrost
