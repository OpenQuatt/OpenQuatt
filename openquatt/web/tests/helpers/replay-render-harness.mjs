import { readFile } from "node:fs/promises";
import { createHash } from "node:crypto";

const sourceUrl = new URL("../../js/src/features/control-replay-view.js", import.meta.url);

// Expose and count private render helpers in the test copy, not the production API.
export async function loadReplayHarness(source) {
  source ??= await readFile(sourceUrl, "utf8");
  const counters = ["getControlWorkingCurrent", "getControlWorkingDecisionLogItems", "getDecisionEventCopy"];
  for (const name of counters) {
    source = source.replace(new RegExp(`(function ${name}\\([^)]*\\) \\{)`), `$1\n    renderCalls.${name}++;`);
  }
  source = source.replace(/(from\s+["'])(\.[^"']+)(["'])/g, (_, before, path, after) => `${before}${new URL(path, sourceUrl).href}${after}`);
  source += `\nexport const renderCalls = ${JSON.stringify(Object.fromEntries(counters.map(name => [name, 0])))};\nexport { getDecisionEventCopy, patchControlReplayDom };`;
  return import(`data:text/javascript;base64,${Buffer.from(source).toString("base64")}`);
}

const eventTypes = [
  "source_start", "source_stop", "topology_change", "decision_hold", "decision_blocked", "candidate_blocked",
  "flow_hold_start", "flow_hold_clear", "startup_inhibit_start", "startup_inhibit_clear", "startup_inhibit_refresh",
  "defrost_seen_start", "defrost_seen_clear", "cooling_limited", "cooling_released", "sticky_pump_run",
  "frost_protection_start", "frost_protection_clear", "boiler_assist_start", "boiler_assist_stop", "attention_pattern",
  "incident_start", "incident_clear", "hp_availability_change", "control_mode_change", "boiler_fallback_start",
  "boiler_fallback_stop", "unknown_event",
];
const reasons = [
  "unknown", "keep_current", "less_power", "demand_decreased", "cooling_request_cleared", "heating_request_cleared",
  "dew_stop", "restart_wait", "buffer_stop", "capacity_cap", "room_cap", "cooling_limiter", "simmer",
  "falling_gap", "level1_hold", "projected_floor", "soft_guard", "sensor_fallback", "no_candidate",
  "flow_preflow", "flow_postflow", "flow_too_low", "defrost_hold", "candidate_in_rest", "start_stop_rate_high",
  "hp_fault", "hp_link_loss", "hp_start_failed", "hp_stop_unconfirmed", "hp_persistence_failure", "hp_recovered", "boiler_fallback",
];

export function decisionCopyDigests(getCopy) {
  return Object.fromEntries(eventTypes.map(event_type => {
    const hash = createHash("sha256");
    for (const reason of reasons) {
      for (const subject of ["HP1", "HP2", "boiler"]) {
        for (let variant = 0; variant < 8; variant++) {
          const event = {
            event_type, reason, subject, cm: variant % 2 ? 5 : 2,
            to: ["idle", "single", "duo", "available", "suspect", "offline", "recovering", "standby"][variant],
            from: "available", value_a: [0, 1, 22, 1001, 1002, 1003, 1004, 350][variant], value_b: variant,
            flags: variant, _oq_cooling_runtime_hold: Boolean(variant & 2), _oq_heating_runtime_hold: Boolean(variant & 4),
            _oq_active_cooling_source: "Warmtepomp 2", _oq_active_heating_source: "Warmtepomp 1",
          };
          if (variant >= 4) event._oq_context_cm = variant % 2 ? 2 : 5;
          if (variant === 6) event._oq_cooling_stop_reason = "dew_stop";
          hash.update(JSON.stringify(getCopy(event)) + "\n");
        }
      }
    }
    return [event_type, hash.digest("hex")];
  }));
}
