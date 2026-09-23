#include <cassert>
#include <cmath>
#include <cstring>
#include "includes/control/oq_defrost_logic.h"
#include "includes/control/oq_supervisory_state_logic.h"
#include "includes/odu/oq_odu_defrost_diagnostics.h"

using namespace oq_defrost;
static void observe(Cycle& c, uint32_t now, int mode, bool bit) {
  c.observe_mode(mode, now);
  c.observe_bit(bit, now);
  c.step(now);
}
int main() {
  Cycle c;
  assert(!c.fresh(0));
  observe(c, 100, 2, false);
  c.request(200);
  assert(c.owns());
  assert(oq_supervisory_state::hold_cm1_until_hp_idle(true, 0, c.owns() || c.observed_active()));
  observe(c, 500, 2, true);
  assert(c.phase == Phase::ACTIVE && std::strcmp(c.result, "ACCEPTED") != 0);
  observe(c, 1000, 4, true);
  assert(std::strcmp(c.result, "ACCEPTED") == 0);
  assert(oq_supervisory_state::hold_cm1_until_hp_idle(true, 0, c.owns() || c.observed_active()));
  observe(c, 2000, 2, true);  // A single clear signal cannot release ownership.
  assert(c.owns());
  observe(c, 3000, 2, false);
  c.step(6000);  // Reusing cached clear samples does not complete the cycle.
  assert(c.owns());
  observe(c, 6100, 2, false);
  assert(c.phase == Phase::RESYNC && c.history_known);
  c.resynced();
  assert(std::strcmp(c.result, "COMPLETE") == 0 && !c.manual);
  assert(!oq_supervisory_state::hold_cm1_until_hp_idle(true, 0, c.owns() || c.observed_active()));

  c.request(10000);
  observe(c, 220000, 2, false);
  assert(c.phase == Phase::RESYNC && std::strcmp(c.result, "TIMEOUT") == 0);
  c.resynced();
  assert(std::strcmp(c.result, "TIMEOUT") == 0);
  c.request(230000);
  c.offline();
  c.step(500000);
  assert(c.owns() && !c.fresh(500000));  // No blind timeout-resync after link loss.
  observe(c, 500100, 4, true);
  assert(c.owns() && !c.continuous);
  c.safety_stop();
  observe(c, 500200, 4, true);
  assert(std::strcmp(c.result, "SAFETY_STOP") == 0);
  observe(c, 501000, 0, false);
  observe(c, 503100, 0, false);
  c.resynced();
  assert(std::strcmp(c.result, "SAFETY_STOP") == 0);

  Cycle boot;
  observe(boot, 100, 4, true);
  assert(boot.owns() && !boot.continuous);  // Boot halfway through an ODU cycle.
  boot.offline();
  boot.observe_mode(0, 1000);
  assert(!boot.fresh(1000));
  boot.observe_bit(false, 1000);
  assert(boot.fresh(1000));
  assert(!boot.fresh(32000));
  assert(!boot.telemetry_fault(91000));
  assert(boot.telemetry_fault(91001));
  Cycle wrap;
  observe(wrap, UINT32_MAX - 5000U, 2, false);
  wrap.request(UINT32_MAX - 3000U);
  observe(wrap, 0, 4, true);
  observe(wrap, 1000, 2, false);
  observe(wrap, 4000, 2, false);
  assert(wrap.phase == Phase::RESYNC);

  Guard g{true, true, true, true, false, false, false, false, 2, 35, false};
  assert(std::strcmp(refusal(g), "READY") == 0);
  g.peer = true;
  assert(std::strcmp(refusal(g), "PEER_DEFROST_ACTIVE") == 0);
  g.peer = false;
  g.incident = true;
  assert(std::strcmp(refusal(g), "INCIDENT_BLOCK") == 0);
  g.incident = false;
  g.automatic = false;
  assert(std::strcmp(refusal(g), "AUTO_CONTROL_UNAVAILABLE") == 0);
  g.automatic = true;
  g.fresh = false;
  assert(std::strcmp(refusal(g), "OFFLINE") == 0);
  g.fresh = true;
  g.hz = NAN;
  assert(std::strcmp(refusal(g), "COMPRESSOR_NOT_RUNNING") == 0);
  g.hz = 35;
  g.mode = 1;
  assert(std::strcmp(refusal(g), "NOT_HEATING") == 0);

  Parameters p;
  p.loaded = true;
  p.variant = oq_odu::Variant::V1_5;
  p.base = {45, 0, 0, 0, 1, 0, 27, 47, 8, 30, 61};
  p.coil = {27, 25, 18, 10, 55, 1};
  p.timing = {5, 0, 0, 0, 10, 180, 5, 25, 40};
  p.delta = {18, 19, 19, 20, 21, 23, 24, 15, 55, 0, 0, 0, 3, 3};
  assert(start_threshold(p, 3) == -3);
  assert(start_threshold(p, -3) == -3);
  assert(start_threshold(p, -3.01f) == -5);
  assert(start_threshold(p, -10) == -5);
  assert(start_threshold(p, -10.01f) == -12);
  assert(temperature(47) == 17);
  assert(std::isnan(temperature(65535)));
  p.base[5] = 4;
  assert(delta_threshold(p, 1.2f) == 12);
  assert(delta_threshold(p, -5) == 11);
  assert(delta_threshold(p, -23) == 7);
  assert(delta_threshold(p, -24) == 6);
  assert(std::isnan(delta_threshold(p, 9)));
  Diagnostics d;
  d.sample(p, true, false, 1.2, -5, -11.1, 35, 1000);
  d.sample(p, true, false, 1.2, -5, -11.1, 35, 11000);
  assert(d.confirm_s == 10 && d.confirm_required_s == 60);
  assert(d.exit_c == 17 && d.max_duration_s == 480 && d.interval_s == 2700);
  d.sample(p, true, false, -1, -5, -13, 35, 12000);
  assert(d.confirm_s == 0);  // Changing the ambient band restarts qualification.
  d.sample(p, false, false, -1, -5, -13, 35, 13000);
  assert(d.confirm_s == -1 && d.runtime_s == -1);
  d.sample(p, true, true, -26, 26, -30, 61, 20000);
  assert(d.exit_c == 25 && d.exit_required_s == -1);  // Tick duration is not proven.
  p.base[5] = 0;
  d.sample(p, true, false, 2, -5, -11, 35, 21000);
  assert(d.interval_s == -1);  // Unknown ODU internal interval is not fabricated.
  p.base[5] = 3;
  d.sample(p, true, false, 2, -5, -11, 35, 22000);
  assert(d.confirm_required_s == -1 && std::isnan(d.start_c));

  assert(hp_active(false, false, false, false, 0, false, false) == false);
  assert(hp_active(true, false, false, false, 0, false, false));
  assert(hp_active(false, false, false, false, 1, false, false));
  assert(hp_active(false, false, false, false, 0, true, false));
  assert(hp_active(false, false, false, false, 0, false, true));
  // P1a: CM0 with an active HP (defrost hold/observed) must circulate, never stop PWM.
  assert(cm0_pump_target(false, true, 800, 1000) == 800);
  assert(cm0_pump_target(true, false, 800, 1000) == 800);
  assert(cm0_pump_target(false, false, 800, 1000) == 1000);
  // P1b: manual defrost needs actual valid minimum flow, not only the delayed fault.
  assert(minimum_flow_ready(800, 250));
  assert(!minimum_flow_ready(249, 250));
  assert(!minimum_flow_ready(NAN, 250));
  assert(!minimum_flow_ready(800, NAN));
  for (int mode = 0; mode <= 5; ++mode) {
    const bool common = mode == 0 || mode == 1 || mode == 3;
    assert(is_supported_defrost_mode(mode, oq_odu::Variant::V1) == common);
    assert(is_supported_defrost_mode(mode, oq_odu::Variant::V1_5) == (common || mode == 4));
    assert(is_supported_defrost_mode(mode, oq_odu::Variant::V2_OLD_MODEL) == (common || mode == 4));
    assert(is_supported_defrost_mode(mode, oq_odu::Variant::V2_NEW_MODEL) == (common || mode == 4));
    assert(!is_supported_defrost_mode(mode, oq_odu::Variant::UNKNOWN));
  }
  assert(!has_mode4_defrost(oq_odu::Variant::V1));
  assert(has_mode4_defrost(oq_odu::Variant::V1_5));
  assert(MODE_REGISTER == 3275U);
  Guard save{true, true, true, true, false, false, false, false, 0, 0, false};
  assert(std::strcmp(mode_save_guard_refusal(save), "READY") == 0);
  save.hz = 35;
  assert(std::strcmp(mode_save_guard_refusal(save), "COMPRESSOR_RUNNING") == 0);
  save.hz = NAN;
  assert(std::strcmp(mode_save_guard_refusal(save), "COMPRESSOR_RUNNING") == 0);
  save.hz = 0;
  save.active = true;
  assert(std::strcmp(mode_save_guard_refusal(save), "ALREADY_ACTIVE") == 0);
  save.active = false;
  assert(std::strcmp(mode_save_error(save, 4, 0, 0, true, true, oq_odu::Variant::V1_5), "READY") == 0);
  assert(std::strcmp(mode_save_error(save, 2, 0, 0, true, true, oq_odu::Variant::V1_5), "INVALID_MODE") == 0);
  assert(std::strcmp(mode_save_error(save, 4, 0, 0, true, true, oq_odu::Variant::V1_5), "READY") == 0);
  assert(std::strcmp(mode_save_error(save, 4, 1, 0, true, true, oq_odu::Variant::V1_5), "STALE") == 0);
  assert(std::strcmp(mode_save_error(save, 0, 0, 0, true, true, oq_odu::Variant::V1_5), "NO_CHANGE") == 0);
  // Stale snapshot wins over no-change: extern naar 4 verhuisd terwijl de UI 0 zag.
  assert(std::strcmp(mode_save_error(save, 4, 1, 4, true, true, oq_odu::Variant::V1_5), "STALE") == 0);
  assert(std::strcmp(mode_save_error(save, 4, 0, 0, false, true, oq_odu::Variant::V1_5), "LOAD_REQUIRED") == 0);
  const char* auto_control_error = mode_save_error(save, 4, 0, 0, true, false, oq_odu::Variant::V1_5);
  assert(std::strcmp(auto_control_error, "AUTO_CONTROL_UNAVAILABLE") == 0);
  assert(std::strcmp(mode_save_error(save, 4, 0, 0, true, true, oq_odu::Variant::V1), "INVALID_MODE") == 0);
  assert(std::strcmp(mode_save_error(save, 3, 0, 0, true, true, oq_odu::Variant::V1), "READY") == 0);

  // A raw mode-4 value on V1 is readable but must not produce V1.5 delta diagnostics.
  p.variant = oq_odu::Variant::V1;
  p.base[5] = 4;
  Diagnostics v1_mode4;
  v1_mode4.sample(p, true, false, 1, -5, -12, 35, 23000);
  assert(std::isnan(v1_mode4.delta_k));
  assert(v1_mode4.confirm_required_s == -1);
}
